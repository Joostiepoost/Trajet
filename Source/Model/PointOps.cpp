#include "PointOps.h"
#include "ItemTime.h"

namespace PointOps
{
    namespace
    {
        juce::Array<juce::ValueTree> clipboard;   // copies with relative times

        double timeOf (const juce::ValueTree& p)   { return p[IDs::time]; }
        float valueOf (const juce::ValueTree& p)   { return (float) (double) p[IDs::value]; }

        void sortByTime (juce::Array<juce::ValueTree>& pts)
        {
            std::sort (pts.begin(), pts.end(), [] (const auto& a, const auto& b) { return timeOf (a) < timeOf (b); });
        }

        void rdp (const std::vector<juce::Point<float>>& pts, size_t first, size_t last, float eps, std::vector<bool>& keep)
        {
            if (last <= first + 1)
                return;

            const juce::Line<float> line (pts[first], pts[last]);
            float maxDist = 0.0f;
            size_t index = first;
            for (size_t i = first + 1; i < last; ++i)
            {
                juce::Point<float> nearest;
                const float d = line.getDistanceFromPoint (pts[i], nearest);
                if (d > maxDist) { maxDist = d; index = i; }
            }

            if (maxDist > eps)
            {
                keep[index] = true;
                rdp (pts, first, index, eps, keep);
                rdp (pts, index, last, eps, keep);
            }
        }
    }

    std::vector<std::pair<juce::ValueTree, juce::Array<juce::ValueTree>>> byItem (const juce::Array<juce::ValueTree>& points)
    {
        std::vector<std::pair<juce::ValueTree, juce::Array<juce::ValueTree>>> groups;

        for (auto& p : points)
        {
            auto item = p.getParent();
            if (! item.isValid()) continue;
            auto it = std::find_if (groups.begin(), groups.end(), [&item] (const auto& g) { return g.first == item; });
            if (it == groups.end()) groups.push_back ({ item, { p } });
            else it->second.add (p);
        }

        for (auto& g : groups)
            sortByTime (g.second);

        return groups;
    }

    void nudge (const juce::Array<juce::ValueTree>& points, int steps, double gridSeconds, float dv, juce::UndoManager& um)
    {
        for (auto p : points)
        {
            auto item = p.getParent();
            if (! item.isValid()) continue;
            const ItemTime it (item);
            const double dt = steps * (it.isMusical() ? it.gridBeats : gridSeconds);
            p.setProperty (IDs::time, juce::jlimit (0.0, it.localLength(), timeOf (p) + dt), &um);
            p.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, valueOf (p) + dv), &um);
        }
    }

    void smooth (const juce::Array<juce::ValueTree>& points, juce::UndoManager& um)
    {
        for (auto& [item, pts] : byItem (points))
        {
            if (pts.size() < 3) continue;

            std::vector<float> values;
            for (auto& p : pts) values.push_back (valueOf (p));

            for (int i = 1; i < pts.size() - 1; ++i)
            {
                const float v = 0.25f * values[(size_t) i - 1] + 0.5f * values[(size_t) i] + 0.25f * values[(size_t) i + 1];
                pts.getReference (i).setProperty (IDs::value, v, &um);
            }
        }
    }

    int thin (const juce::Array<juce::ValueTree>& points, juce::UndoManager& um, float tolerance)
    {
        int removed = 0;

        for (auto& [item, pts] : byItem (points))
        {
            if (pts.size() < 3) continue;

            // Compare in a square space: time as a share of the selection's span, value 0..1.
            const double t0 = timeOf (pts.getFirst()), span = juce::jmax (1.0e-9, timeOf (pts.getLast()) - t0);
            std::vector<juce::Point<float>> xy;
            for (auto& p : pts)
                xy.push_back ({ (float) ((timeOf (p) - t0) / span), valueOf (p) });

            std::vector<bool> keep (xy.size(), false);
            keep.front() = keep.back() = true;
            rdp (xy, 0, xy.size() - 1, tolerance, keep);

            for (size_t i = 0; i < keep.size(); ++i)
                if (! keep[i])
                {
                    auto p = pts[(int) i];
                    item.removeChild (p, &um);
                    ++removed;
                }
        }
        return removed;
    }

    void reverse (const juce::Array<juce::ValueTree>& points, juce::UndoManager& um)
    {
        for (auto& [item, pts] : byItem (points))
        {
            if (pts.size() < 2) continue;
            const double lo = timeOf (pts.getFirst()), hi = timeOf (pts.getLast());

            // Shapes belong to the segment after a point; when reversed they move to the point before.
            std::vector<std::pair<int, double>> shapes;
            for (auto& p : pts) shapes.push_back ({ (int) p[IDs::shape], (double) p[IDs::tension] });

            for (int i = 0; i < pts.size(); ++i)
            {
                auto p = pts[i];
                p.setProperty (IDs::time, lo + hi - timeOf (p), &um);
                const int from = i - 1;   // the segment that ended here now starts here
                if (from >= 0)
                {
                    p.setProperty (IDs::shape, shapes[(size_t) from].first, &um);
                    p.setProperty (IDs::tension, -shapes[(size_t) from].second, &um);
                }
            }
        }
    }

    void invert (const juce::Array<juce::ValueTree>& points, juce::UndoManager& um)
    {
        if (points.isEmpty()) return;
        float lo = 1.0f, hi = 0.0f;
        for (auto& p : points) { lo = juce::jmin (lo, valueOf (p)); hi = juce::jmax (hi, valueOf (p)); }
        if (hi <= lo) { lo = 0.0f; hi = 1.0f; }   // a single value: mirror around the middle

        for (auto p : points)
        {
            p.setProperty (IDs::value, lo + hi - valueOf (p), &um);
            if ((int) p[IDs::shape] == 1)
                p.setProperty (IDs::tension, -(double) p[IDs::tension], &um);
        }
    }

    void copy (const juce::Array<juce::ValueTree>& points)
    {
        clipboard.clearQuick();
        // Stored in seconds relative to the earliest point, so points can move
        // between linear and musical items.
        double earliest = 1.0e12;
        for (auto& p : points)
            if (p.getParent().isValid())
                earliest = juce::jmin (earliest, ItemTime (p.getParent()).absolute (timeOf (p)));

        for (auto& p : points)
        {
            if (! p.getParent().isValid()) continue;
            auto c = p.createCopy();
            c.setProperty (IDs::time, ItemTime (p.getParent()).absolute (timeOf (p)) - earliest, nullptr);
            clipboard.add (c);
        }
    }

    bool hasCopiedPoints()   { return ! clipboard.isEmpty(); }

    juce::Array<juce::ValueTree> paste (juce::ValueTree item, double at, juce::UndoManager& um)
    {
        juce::Array<juce::ValueTree> added;
        if (! item.isValid()) return added;

        const ItemTime it (item);
        const double length = it.localLength();
        for (auto& c : clipboard)
        {
            const double t = it.localAt (at + timeOf (c));
            if (t < -1.0e-6 || t > length + 1.0e-6) continue;
            auto p = c.createCopy();
            p.setProperty (IDs::time, juce::jlimit (0.0, length, t), nullptr);
            item.appendChild (p, &um);
            added.add (p);
        }
        return added;
    }
}
