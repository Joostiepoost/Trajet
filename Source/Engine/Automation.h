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
    // What the audio thread works from: a read-only copy of all lanes,
    // with times converted to absolute seconds.

    struct Item
    {
        double start = 0.0, end = 0.0;
        std::vector<Point> points;   // absolute times, sorted
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
                return valueAt (item.points, t);

            v = valueAt (item.points, item.end);
        }

        return v;
    }
}
