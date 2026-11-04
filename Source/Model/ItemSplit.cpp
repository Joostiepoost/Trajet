// ItemSplit.cpp — see ItemSplit.h. The same file is used in Trajet and Effet.

#include "ItemSplit.h"
#include "Identifiers.h"
#include "ItemTime.h"

namespace ItemSplit
{
namespace
{
    using Automation::Shape;

    struct Pt
    {
        juce::ValueTree tree;
        double time;
        float value;
        Shape shape;
        float tension;
    };

    std::vector<Pt> readPoints (const juce::ValueTree& item)
    {
        std::vector<Pt> pts;
        for (const auto& c : item)
            if (c.hasType (IDs::POINT))
                pts.push_back ({ c, (double) c[IDs::time], (float) (double) c[IDs::value],
                                 (Shape) juce::jlimit (0, 3, (int) c[IDs::shape]), (float) (double) c[IDs::tension] });
        std::stable_sort (pts.begin(), pts.end(), [] (const Pt& a, const Pt& b) { return a.time < b.time; });
        return pts;
    }

    juce::ValueTree makePoint (double time, float value, Shape shape, float tension)
    {
        juce::ValueTree p (IDs::POINT);
        p.setProperty (IDs::time, time, nullptr);
        p.setProperty (IDs::value, value, nullptr);
        p.setProperty (IDs::shape, (int) shape, nullptr);
        p.setProperty (IDs::tension, tension, nullptr);
        return p;
    }

    float along (const Pt& a, const Pt& b, double x)
    {
        return a.value + (b.value - a.value) * Automation::shapeFraction (a.shape, a.tension, (float) x);
    }

    /** Splits the points at local time c: changes the item's own points (left) and
        fills `right` with the points of the right piece (times from its start). */
    void splitPoints (juce::ValueTree item, juce::ValueTree right, double c, juce::UndoManager* um)
    {
        auto pts = readPoints (item);
        if (pts.empty())
            return;

        // a = last point before the cut, b = first point at or after it.
        int ia = -1, ib = -1;
        for (int i = 0; i < (int) pts.size(); ++i)
        {
            if (pts[(size_t) i].time < c) ia = i;
            else { ib = i; break; }
        }

        float leftEnd, rightStart;
        Shape rightShape = Shape::linear;
        float rightTension = 0.0f;
        std::vector<juce::ValueTree> leftExtra, rightExtra;   // traced smooth segments

        if (ia < 0)
        {
            // The cut is before the first point: the curve is flat there.
            leftEnd = rightStart = pts[(size_t) ib].value;
        }
        else if (ib < 0)
        {
            // After the last point: flat as well.
            leftEnd = rightStart = pts[(size_t) ia].value;
        }
        else
        {
            auto a = pts[(size_t) ia];         // copies share the ValueTree
            const auto b = pts[(size_t) ib];
            const double span = b.time - a.time;
            const double s = span > 0.0 ? (c - a.time) / span : 1.0;
            leftEnd = along (a, b, s);
            rightStart = b.time > c ? leftEnd : b.value;

            if (b.time > c)
            {
                switch (a.shape)
                {
                    case Shape::curve:
                        // An exponential bend restricted to a part of its span is the same
                        // bend scaled: tension * s on the left, tension * (1 - s) on the right.
                        a.tree.setProperty (IDs::tension, a.tension * (float) s, um);
                        rightShape = Shape::curve;
                        rightTension = a.tension * (float) (1.0 - s);
                        break;

                    case Shape::hold:
                        rightShape = Shape::hold;
                        break;

                    case Shape::smooth:
                    {
                        // Not self-similar: trace both parts with straight segments.
                        constexpr int n = 8;
                        a.tree.setProperty (IDs::shape, (int) Shape::linear, um);
                        for (int k = 1; k < n; ++k)
                        {
                            const double xl = s * k / n;
                            leftExtra.push_back (makePoint (a.time + xl * span, along (a, b, xl), Shape::linear, 0.0f));
                            const double xr = s + (1.0 - s) * k / n;
                            rightExtra.push_back (makePoint (a.time + xr * span - c, along (a, b, xr), Shape::linear, 0.0f));
                        }
                        break;
                    }

                    case Shape::linear:
                    default:
                        break;
                }
            }
        }

        // Left: drop everything from the cut on, add the traced points and the end point.
        for (auto& p : pts)
            if (p.time >= c)
                item.removeChild (p.tree, um);
        for (auto& p : leftExtra)
            item.appendChild (p, um);
        if (ia >= 0 || ib >= 0)
            item.appendChild (makePoint (ia >= 0 ? c : 0.0, leftEnd, Shape::linear, 0.0f), um);

        // Right: the start value, the traced points, then the rest shifted.
        const bool startsWithPoint = ib >= 0 && pts[(size_t) ib].time <= c;
        if (! startsWithPoint)
            right.appendChild (makePoint (0.0, rightStart, rightShape, rightTension), nullptr);
        for (auto& p : rightExtra)
            right.appendChild (p, nullptr);
        for (auto& p : pts)
            if (p.time >= c)
                right.appendChild (makePoint (juce::jmax (0.0, p.time - c), p.value, p.shape, p.tension), nullptr);
    }

    void splitTempo (juce::ValueTree item, juce::ValueTree right, double c, juce::UndoManager* um)
    {
        const auto map = ItemTime::readTempo (item);
        double bpm = map.points.front().bpm;
        bool ramp = false;
        for (size_t i = 0; i < map.points.size(); ++i)
        {
            const auto& a = map.points[i];
            if (a.beat > c) break;
            const auto* b = i + 1 < map.points.size() ? &map.points[i + 1] : nullptr;
            bpm = a.bpm;
            ramp = a.ramp && b != nullptr;
            if (ramp && b->beat > c && b->beat > a.beat)
                bpm = a.bpm + (b->bpm - a.bpm) * (c - a.beat) / (b->beat - a.beat);
        }

        // Left: tempo points after the cut go; a ramp across the cut ends at the cut.
        bool pointAtCut = false;
        for (int i = item.getNumChildren(); --i >= 0;)
        {
            auto t = item.getChild (i);
            if (! t.hasType (IDs::TEMPO)) continue;
            const double beat = t[IDs::beat];
            if (beat > c) item.removeChild (i, um);
            else if (std::abs (beat - c) < 1.0e-9) pointAtCut = true;
        }
        if (ramp && ! pointAtCut && c > 0.0)
        {
            juce::ValueTree t (IDs::TEMPO);
            t.setProperty (IDs::beat, c, nullptr);
            t.setProperty (IDs::bpm, bpm, nullptr);
            t.setProperty (IDs::ramp, false, nullptr);
            item.appendChild (t, um);
        }

        // Right: the tempo at the cut, then the later points shifted.
        juce::ValueTree first (IDs::TEMPO);
        first.setProperty (IDs::beat, 0.0, nullptr);
        first.setProperty (IDs::bpm, bpm, nullptr);
        first.setProperty (IDs::ramp, ramp, nullptr);
        right.appendChild (first, nullptr);
        for (auto& p : map.points)
            if (p.beat > c + 1.0e-9)
            {
                juce::ValueTree t (IDs::TEMPO);
                t.setProperty (IDs::beat, p.beat - c, nullptr);
                t.setProperty (IDs::bpm, p.bpm, nullptr);
                t.setProperty (IDs::ramp, p.ramp, nullptr);
                right.appendChild (t, nullptr);
            }
    }

    void splitNotes (juce::ValueTree item, juce::ValueTree right, double c, juce::UndoManager* um)
    {
        for (int i = item.getNumChildren(); --i >= 0;)
        {
            auto n = item.getChild (i);
            if (! n.hasType (IDs::NOTE)) continue;
            const double t = n[IDs::time], len = n[IDs::length];

            if (t >= c)
            {
                auto moved = n.createCopy();
                moved.setProperty (IDs::time, t - c, nullptr);
                right.addChild (moved, 0, nullptr);
                item.removeChild (i, um);
            }
            else if (t + len > c)
            {
                auto rest = n.createCopy();
                rest.setProperty (IDs::time, 0.0, nullptr);
                rest.setProperty (IDs::length, t + len - c, nullptr);
                right.addChild (rest, 0, nullptr);
                n.setProperty (IDs::length, c - t, um);
            }
        }
    }
}

//==============================================================================
bool canSplit (const juce::ValueTree& item, double absoluteSeconds)
{
    const double cut = absoluteSeconds - (double) item[IDs::start];
    return item.isValid() && cut >= minPiece && (double) item[IDs::length] - cut >= minPiece;
}

juce::ValueTree split (juce::ValueTree item, double absoluteSeconds, juce::UndoManager* um)
{
    auto lane = item.getParent();
    if (! lane.isValid() || ! canSplit (item, absoluteSeconds))
        return {};

    const ItemTime it (item);
    const double cutSeconds = absoluteSeconds - it.getStart();
    const double lengthSeconds = item[IDs::length];
    const double c = it.toLocal (cutSeconds);
    const double localLength = it.localLength();

    // The right piece: the same settings, its own children.
    auto right = item.createCopy();
    right.removeAllChildren (nullptr);
    right.setProperty (IDs::start, absoluteSeconds, nullptr);
    right.setProperty (IDs::length, lengthSeconds - cutSeconds, nullptr);

    if (it.isMusical())
    {
        right.setProperty (IDs::lengthBeats, localLength - c, nullptr);
        splitTempo (item, right, c, um);
        item.setProperty (IDs::lengthBeats, c, um);
    }

    if (item[IDs::generator].toString().isNotEmpty())
    {
        // Both pieces keep the original's span; the right one starts where the cut was.
        const double span = item.hasProperty (IDs::genSpan) ? (double) item[IDs::genSpan] : localLength;
        const double offset = item.getProperty (IDs::genOffset, 0.0);
        const double offsetSeconds = item.getProperty (IDs::genOffsetSeconds, 0.0);
        item.setProperty (IDs::genSpan, span, um);
        right.setProperty (IDs::genSpan, span, nullptr);
        right.setProperty (IDs::genOffset, offset + c, nullptr);
        right.setProperty (IDs::genOffsetSeconds, offsetSeconds + cutSeconds, nullptr);
    }

    splitPoints (item, right, c, um);
    splitNotes (item, right, c, um);

    item.setProperty (IDs::length, cutSeconds, um);
    lane.addChild (right, lane.indexOf (item) + 1, um);
    return right;
}

juce::Array<juce::ValueTree> splitAt (juce::ValueTree item, std::vector<double> times, juce::UndoManager* um)
{
    juce::Array<juce::ValueTree> pieces;
    if (! item.isValid())
        return pieces;

    // From right to left, so the original item is always the one being cut.
    std::sort (times.begin(), times.end(), std::greater<double>());
    double lastCut = (double) item[IDs::start] + (double) item[IDs::length];
    for (double t : times)
    {
        if (lastCut - t < minPiece)
            continue;
        auto right = split (item, t, um);
        if (right.isValid())
        {
            pieces.insert (0, right);
            lastCut = t;
        }
    }
    pieces.insert (0, item);
    return pieces;
}

juce::Array<juce::ValueTree> splitEqually (juce::ValueTree item, int n, juce::UndoManager* um)
{
    const ItemTime it (item);
    std::vector<double> times;
    for (int k = 1; k < n; ++k)
        times.push_back (it.absolute (it.localLength() * k / n));
    return splitAt (item, times, um);
}
}
