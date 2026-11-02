// Automation.h — how a value is computed from points.
//
// The same code is used by the audio engine (to drive the plugin) and by the
// lane views (to draw the curves), so what you see is exactly what you hear.

#pragma once

#include <JuceHeader.h>

namespace Automation
{
    /** The shape of the segment that starts at a point and runs to the next one. */
    enum class Shape { linear = 0, curve = 1, smooth = 2, hold = 3 };

    struct Point
    {
        double time = 0.0;           // seconds (relative or absolute, depending on context)
        float value = 0.0f;          // 0..1, the plugin's normalised parameter value
        Shape shape = Shape::linear;
        float tension = 0.0f;        // for Shape::curve: -1..+1, 0 = straight
    };

    /** Where along a segment we are (x = 0..1) -> how far the value has moved (0..1). */
    inline float shapeFraction (Shape shape, float tension, float x) noexcept
    {
        switch (shape)
        {
            case Shape::hold:   return 0.0f;
            case Shape::smooth: return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::pi * x);
            case Shape::curve:
            {
                // Exponential bend: positive tension starts slowly, negative starts fast.
                const float c = tension * 8.0f;
                if (std::abs (c) < 1.0e-3f)
                    return x;
                return (std::exp (c * x) - 1.0f) / (std::exp (c) - 1.0f);
            }
            case Shape::linear:
            default:            return x;
        }
    }

    /** Value at time t for points sorted by time. Before the first point: its value;
        after the last: the last value. */
    inline float valueAt (const std::vector<Point>& points, double t) noexcept
    {
        if (points.empty())                 return 0.0f;
        if (t <= points.front().time)       return points.front().value;
        if (t >= points.back().time)        return points.back().value;

        auto next = std::upper_bound (points.begin(), points.end(), t,
                                      [] (double time, const Point& p) { return time < p.time; });
        const auto& b = *next;
        const auto& a = *(next - 1);

        const double span = b.time - a.time;
        if (span <= 0.0)
            return b.value;

        const auto x = (float) ((t - a.time) / span);
        return a.value + (b.value - a.value) * shapeFraction (a.shape, a.tension, x);
    }

    //==============================================================================
    // Tempo maps for musical items.
    //
    // A tempo map is a list of tempo points (beat, bpm). From each point the tempo
    // either stays constant up to the next point, or changes linearly (per beat)
    // towards it: an accelerando or ritardando. Converting between beats and seconds
    // uses exact formulas, so a ramped item lands precisely where predicted:
    //
    //   constant:  seconds = beats * 60 / bpm
    //   ramp:      with k = (bpm1 - bpm0) / (beat1 - beat0) and T(b) = bpm0 + k (b - beat0),
    //              seconds = 60 / k * ln (T(b) / bpm0),   beats = bpm0 / k * (exp (k * seconds / 60) - 1)

    struct TempoPoint
    {
        double beat = 0.0;
        double bpm = 120.0;
        bool ramp = false;      // ramp towards the next tempo point
    };

    struct TempoMap
    {
        std::vector<TempoPoint> points;   // sorted by beat, the first at beat 0

        static double segmentSeconds (const TempoPoint& a, const TempoPoint* b, double beat) noexcept
        {
            const double db = beat - a.beat;
            if (b == nullptr || ! a.ramp || std::abs (b->bpm - a.bpm) < 1.0e-9 || b->beat <= a.beat)
                return db * 60.0 / a.bpm;
            const double k = (b->bpm - a.bpm) / (b->beat - a.beat);
            return 60.0 / k * std::log ((a.bpm + k * db) / a.bpm);
        }

        static double segmentBeats (const TempoPoint& a, const TempoPoint* b, double seconds) noexcept
        {
            if (b == nullptr || ! a.ramp || std::abs (b->bpm - a.bpm) < 1.0e-9 || b->beat <= a.beat)
                return seconds * a.bpm / 60.0;
            const double k = (b->bpm - a.bpm) / (b->beat - a.beat);
            return a.bpm / k * (std::exp (k * seconds / 60.0) - 1.0);
        }

        double firstBpm() const noexcept     { return points.empty() ? 120.0 : points.front().bpm; }

        /** Seconds from the item start to a beat position. */
        double secondsAt (double beat) const noexcept
        {
            if (points.empty() || beat <= 0.0)
                return beat * 60.0 / firstBpm();

            double t = 0.0;
            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto* next = i + 1 < points.size() ? &points[i + 1] : nullptr;
                if (next == nullptr || beat <= next->beat)
                    return t + segmentSeconds (points[i], next, beat);
                t += segmentSeconds (points[i], next, next->beat);
            }
            return t;
        }

        /** Beat position at a number of seconds from the item start. */
        double beatAt (double seconds) const noexcept
        {
            if (points.empty() || seconds <= 0.0)
                return seconds * firstBpm() / 60.0;

            double t = 0.0;
            for (size_t i = 0; i < points.size(); ++i)
            {
                const auto* next = i + 1 < points.size() ? &points[i + 1] : nullptr;
                const double segment = next != nullptr ? segmentSeconds (points[i], next, next->beat) : 1.0e300;
                if (seconds <= t + segment)
                    return points[i].beat + segmentBeats (points[i], next, seconds - t);
                t += segment;
            }
            return points.back().beat;
        }

        /** Tempo at a beat position. */
        double bpmAt (double beat) const noexcept
        {
            if (points.empty()) return 120.0;
            for (size_t i = points.size(); i-- > 0;)
                if (beat >= points[i].beat)
                {
                    const auto* next = i + 1 < points.size() ? &points[i + 1] : nullptr;
                    if (next == nullptr || ! points[i].ramp || next->beat <= points[i].beat)
                        return points[i].bpm;
                    return points[i].bpm + (next->bpm - points[i].bpm) * (beat - points[i].beat) / (next->beat - points[i].beat);
                }
            return points.front().bpm;
        }
    };

    //==============================================================================
    // What the audio thread works from: a read-only copy of all lanes.

    struct Item
    {
        double start = 0.0, end = 0.0;       // absolute seconds
        bool musical = false;
        TempoMap tempo;                      // musical items only
        double localEnd = 0.0;               // length in the item's own units
        std::vector<Point> points;           // times in the item's own units (seconds or beats), sorted

        double toLocal (double absoluteSeconds) const noexcept
        {
            return musical ? tempo.beatAt (absoluteSeconds - start) : absoluteSeconds - start;
        }
    };

    struct Lane
    {
        int paramIndex = -1;
        float baseValue = 0.0f;      // the patch value, used before the first item
        std::vector<Item> items;     // sorted by start, never overlapping
    };

    struct Snapshot
    {
        std::vector<Lane> lanes;
    };

    /** Value of a lane at time t: the patch value before its first item,
        the item's curve inside an item, and the last value held between items. */
    inline float laneValueAt (const Lane& lane, double t) noexcept
    {
        float v = lane.baseValue;

        for (const auto& item : lane.items)
        {
            if (t < item.start)
                break;

            if (item.points.empty())
                continue;

            if (t <= item.end)
                return valueAt (item.points, item.toLocal (t));

            v = valueAt (item.points, item.localEnd);
        }

        return v;
    }
}
