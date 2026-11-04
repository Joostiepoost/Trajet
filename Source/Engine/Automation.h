// Automation.h — how a value is computed from points.
//
// The same code is used by the audio engine (to drive the plugin) and by the
// lane views (to draw the curves), so what you see is exactly what you hear.

#pragma once

#include <JuceHeader.h>
#include "Formula.h"

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
    // Generators: items that move a parameter by themselves.
    //
    // Rates are in cycles (or random steps) per second in linear items and per beat
    // in musical items, so in a musical item with an accelerando an LFO speeds up too.

    enum class GeneratorType { none = 0, lfo, random, formula };
    enum class LfoShape { sine = 0, triangle, square, sawUp, sawDown };
    enum class RandomMode { stepped = 0, smooth, drift };

    struct Generator
    {
        GeneratorType type = GeneratorType::none;
        int shape = 0;               // LfoShape or RandomMode
        double rate = 1.0;           // per second (linear) or per beat (musical)
        float minValue = 0.0f, maxValue = 1.0f;
        double phase = 0.0;          // 0..1 (LFO)
        int seed = 1;                // random
        Formula::Expression formula;

        // Set on pieces of a split item, so the generator carries on where the cut was.
        double offset = 0.0;         // item units added to the position
        double offsetSeconds = 0.0;  // seconds added to the formula's s
        double span = 0.0;           // the original item's length in its units (0 = this item's)
    };

    /** 0..1 shape value of a generator at a position (local units), before mapping to min..max. */
    inline double generatorShape (const Generator& g, double local, double localLength, double seconds) noexcept
    {
        local += g.offset;
        seconds += g.offsetSeconds;
        if (g.span > 0.0) localLength = g.span;

        switch (g.type)
        {
            case GeneratorType::lfo:
            {
                const double x = local * g.rate + g.phase;
                const double p = x - std::floor (x);
                switch ((LfoShape) g.shape)
                {
                    case LfoShape::sine:     return 0.5 - 0.5 * std::cos (juce::MathConstants<double>::twoPi * p);   // starts at the bottom
                    case LfoShape::triangle: return p < 0.5 ? 2.0 * p : 2.0 - 2.0 * p;
                    case LfoShape::square:   return p < 0.5 ? 1.0 : 0.0;
                    case LfoShape::sawUp:    return p;
                    case LfoShape::sawDown:  return 1.0 - p;
                    default:                 return 0.0;
                }
            }

            case GeneratorType::random:
            {
                const double x = local * g.rate;
                switch ((RandomMode) g.shape)
                {
                    case RandomMode::stepped: return Formula::hash01 (g.seed, (juce::int64) std::floor (x));
                    case RandomMode::smooth:
                    {
                        const double fl = std::floor (x);
                        const double f = x - fl;
                        const double u = 0.5 - 0.5 * std::cos (juce::MathConstants<double>::pi * f);
                        const auto i = (juce::int64) fl;
                        return Formula::hash01 (g.seed, i) * (1.0 - u) + Formula::hash01 (g.seed, i + 1) * u;
                    }
                    case RandomMode::drift:
                    {
                        // Slow wandering: three layers of smooth noise at different speeds.
                        const double v = 0.57 * Formula::valueNoise (g.seed, x)
                                       + 0.29 * Formula::valueNoise (g.seed + 101, x * 2.31)
                                       + 0.14 * Formula::valueNoise (g.seed + 211, x * 5.17);
                        return juce::jlimit (0.0, 1.0, (v - 0.5) * 1.35 + 0.5);
                    }
                    default: return 0.0;
                }
            }

            case GeneratorType::formula:
            {
                if (g.formula == nullptr) return 0.0;
                Formula::Variables vars;
                vars.t = localLength > 0.0 ? local / localLength : 0.0;
                vars.s = seconds;
                vars.b = local;
                const double v = g.formula->eval (vars);
                return std::isfinite (v) ? juce::jlimit (0.0, 1.0, v) : 0.0;
            }

            case GeneratorType::none:
            default: return 0.0;
        }
    }

    inline float generatorValue (const Generator& g, double local, double localLength, double seconds) noexcept
    {
        const double shape = generatorShape (g, local, localLength, seconds);
        return (float) (g.minValue + (g.maxValue - g.minValue) * shape);
    }

    //==============================================================================
    // What the audio thread works from: a read-only copy of all lanes.

    struct Item
    {
        double start = 0.0, end = 0.0;       // absolute seconds
        bool musical = false;
        TempoMap tempo;                      // musical items only
        double localEnd = 0.0;               // length in the item's own units
        std::vector<Point> points;           // times in the item's own units (seconds or beats), sorted
        Generator generator;                 // if set, used instead of the points

        float valueAtLocal (double local) const noexcept
        {
            if (generator.type != GeneratorType::none)
                return generatorValue (generator, local, localEnd, musical ? tempo.secondsAt (local) : local);
            return valueAt (points, local);
        }

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

    /** A note of a note lane, in absolute seconds. */
    struct Note
    {
        double start = 0.0, end = 0.0;
        int pitch = 60, velocity = 100;
    };

    struct Snapshot
    {
        std::vector<Lane> lanes;
        std::vector<Note> notes;     // all note lanes together, sorted by start
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

            if (item.points.empty() && item.generator.type == GeneratorType::none)
                continue;

            if (t <= item.end)
                return item.valueAtLocal (item.toLocal (t));

            v = item.valueAtLocal (item.localEnd);
        }

        return v;
    }
}
