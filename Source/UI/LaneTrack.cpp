#include "LaneTrack.h"

namespace
{
    const juce::Colour trackBackground (0xff22262c);
    const juce::Colour accent          (0xffe8a33d);
    constexpr float edgeZone = 6.0f;
    constexpr float pointHitRadius = 7.0f;

    /** Grid step (seconds) for drawing: the snap grid if it is wide enough, else a coarser one. */
    double drawGridStep (double pixelsPerSecond, double snapGrid)
    {
        if (snapGrid * pixelsPerSecond >= 12.0)
            return snapGrid;
        for (double step : { 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0 })
            if (step * pixelsPerSecond >= 24.0)
                return step;
        return 120.0;
    }

    /** Ramer-Douglas-Peucker: which of the points to keep so the line stays within eps. */
    void simplify (const std::vector<juce::Point<float>>& pts, size_t first, size_t last, float eps, std::vector<bool>& keep)
    {
        if (last <= first + 1)
            return;

        const juce::Line<float> line (pts[first], pts[last]);
        float maxDist = 0.0f;
        size_t index = first;

        for (size_t i = first + 1; i < last; ++i)
        {
            juce::Point<float> nearest;
            const auto d = line.getDistanceFromPoint (pts[i], nearest);
            if (d > maxDist) { maxDist = d; index = i; }
        }

        if (maxDist > eps)
        {
            keep[index] = true;
            simplify (pts, first, index, eps, keep);
            simplify (pts, index, last, eps, keep);
        }
    }
}

LaneTrack::LaneTrack (Context& c, juce::ValueTree laneTree)
    : ctx (c), lane (laneTree)
{
    setRepaintsOnMouseActivity (false);
}

//==============================================================================
// Geometry

juce::Rectangle<float> LaneTrack::itemBounds (const juce::ValueTree& item) const
{
    const double start = item[IDs::start];
    const double length = item[IDs::length];
    const float x0 = ctx.timeView.toX (start), x1 = ctx.timeView.toX (start + length);
    return { x0, 2.0f, juce::jmax (2.0f, x1 - x0), (float) getHeight() - 4.0f };
}

juce::Rectangle<float> LaneTrack::curveArea() const
{
    return getLocalBounds().toFloat().withTrimmedTop (2.0f + (float) stripHeight + 4.0f).withTrimmedBottom (6.0f);
}

float LaneTrack::valueToY (float v) const
{
    auto r = curveArea();
    return r.getBottom() - v * r.getHeight();
}

float LaneTrack::yToValue (float y) const
{
    auto r = curveArea();
    return juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / juce::jmax (1.0f, r.getHeight()));
}

double LaneTrack::relativeTime (const juce::ValueTree& item, float x, bool bypassSnap) const
{
    const double start = item[IDs::start];
    const double length = item[IDs::length];
    const double t = ctx.snap.snap (ctx.timeView.toTime (x), bypassSnap) - start;
    return juce::jlimit (0.0, length, t);
}

juce::ValueTree LaneTrack::itemAt (float x) const
{
    const double t = ctx.timeView.toTime (x);
    const double tolerance = edgeZone / ctx.timeView.getPixelsPerSecond();

    for (const auto& item : lane)
        if (item.hasType (IDs::ITEM))
        {
            const double s = item[IDs::start], e = s + (double) item[IDs::length];
            if (t >= s - tolerance && t <= e + tolerance)
                return item;
        }
    return {};
}

LaneTrack::Zone LaneTrack::zoneFor (const juce::ValueTree& item, juce::Point<float> pos) const
{
    if (! item.isValid())
        return Zone::none;

    auto r = itemBounds (item);
    if (std::abs (pos.x - r.getX()) <= edgeZone)      return Zone::leftEdge;
    if (std::abs (pos.x - r.getRight()) <= edgeZone)  return Zone::rightEdge;
    if (pos.y < r.getY() + (float) stripHeight)       return Zone::strip;
    return Zone::curve;
}

juce::ValueTree LaneTrack::pointAt (const juce::ValueTree& item, juce::Point<float> pos) const
{
    if (! item.isValid())
        return {};

    const double start = item[IDs::start];
    juce::ValueTree best;
    float bestDistance = pointHitRadius;

    for (const auto& p : item)
    {
        if (! p.hasType (IDs::POINT))
            continue;

        const juce::Point<float> pp (ctx.timeView.toX (start + (double) p[IDs::time]),
                                     valueToY ((float) (double) p[IDs::value]));
        const float d = pos.getDistanceFrom (pp);
        if (d <= bestDistance) { bestDistance = d; best = p; }
    }
    return best;
}

juce::AudioProcessorParameter* LaneTrack::getParameter() const
{
    auto* plugin = ctx.host.getPlugin();
    const int index = ctx.controller.resolveParameterIndex (lane);
    return (plugin != nullptr && index >= 0) ? plugin->getParameters()[index] : nullptr;
}

juce::String LaneTrack::valueText (float v) const
{
    if (auto* p = getParameter())
    {
        auto text = p->getText (v, 32);
        auto label = p->getLabel();
        if (label.isNotEmpty() && ! text.endsWith (label))
            text << " " << label;
        return text;
    }
    return juce::String (v, 3);
}

//==============================================================================
void LaneTrack::paint (juce::Graphics& g)
{
    const auto colour = Project::laneColour (lane);
    auto& tv = ctx.timeView;
    const float w = (float) getWidth(), h = (float) getHeight();

    g.fillAll (trackBackground);

    // Time grid: second lines a bit stronger.
    const double step = drawGridStep (tv.getPixelsPerSecond(), ctx.snap.getGrid());
    const double firstLine = std::floor (tv.getOffset() / step) * step;
    for (double t = firstLine; tv.toX (t) < w; t += step)
    {
        const bool major = std::abs (t - std::round (t)) < 1.0e-6 && step < 1.0;
        g.setColour (juce::Colours::white.withAlpha (major ? 0.08f : 0.035f));
        g.drawVerticalLine ((int) tv.toX (t), 0.0f, h);
    }

    // Project end.
    const float endX = tv.toX (ctx.project.getLengthSeconds());
    if (endX < w)
    {
        g.setColour (juce::Colours::black.withAlpha (0.35f));
        g.fillRect (endX, 0.0f, w - endX, h);
    }

    auto* param = getParameter();
    const bool muted = (bool) lane[IDs::mute];

    for (const auto& item : lane)
    {
        if (! item.hasType (IDs::ITEM))
            continue;

        auto r = itemBounds (item);
        if (r.getRight() < 0.0f || r.getX() > w)
            continue;

        const bool selected = ctx.selection.getItems().contains (item);
        const double start = item[IDs::start];

        g.setColour (colour.withAlpha (muted ? 0.04f : 0.10f));
        g.fillRoundedRectangle (r, 4.0f);

        // Title strip (move handle).
        auto strip = r.withHeight ((float) stripHeight);
        g.setColour (colour.withAlpha (muted ? 0.25f : 0.55f));
        g.fillRoundedRectangle (strip, 4.0f);
        g.fillRect (strip.withTrimmedTop (6.0f));

        g.setColour (selected ? juce::Colours::white : colour.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 4.0f, selected ? 2.0f : 1.0f);

        // Curve: evaluated at every pixel, the same maths the engine uses.
        auto points = Project::getPoints (item);
        if (! points.empty())
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());

            juce::Path curve;
            const float xa = juce::jmax (r.getX(), 0.0f), xb = juce::jmin (r.getRight(), w);
            bool first = true;
            for (float x = xa; x <= xb; x += 1.0f)
            {
                const float y = valueToY (Automation::valueAt (points, tv.toTime (x) - start));
                if (first) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                first = false;
            }
            g.setColour (muted ? colour.withAlpha (0.4f) : colour);
            g.strokePath (curve, juce::PathStrokeType (1.6f));

            for (const auto& p : item)
            {
                if (! p.hasType (IDs::POINT))
                    continue;
                const float px = tv.toX (start + (double) p[IDs::time]);
                const float py = valueToY ((float) (double) p[IDs::value]);
                const bool pointSelected = ctx.selection.getPoints().contains (p);
                g.setColour (pointSelected ? accent : juce::Colours::white);
                g.fillEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f);
                g.setColour (colour.darker (0.4f));
                g.drawEllipse (px - 3.5f, py - 3.5f, 7.0f, 7.0f, 1.0f);
            }
        }
    }

    // Value readout while dragging points.
    if ((mode == Mode::movePoints || mode == Mode::ramp) && dragAnchor.isValid() && dragAnchor.getParent().isValid())
    {
        const double start = dragAnchor.getParent()[IDs::start];
        const float v = (float) (double) dragAnchor[IDs::value];
        const double t = start + (double) dragAnchor[IDs::time];
        const float px = tv.toX (t), py = valueToY (v);
        const auto text = valueText (v) + "   " + TimeFormat::format (t);
        g.setFont (juce::FontOptions (11.5f));
        const float tw = (float) juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), text) + 10.0f;
        auto box = juce::Rectangle<float> (px + 10.0f, py - 24.0f, tw, 18.0f);
        if (box.getRight() > w) box.setX (px - 10.0f - tw);
        if (box.getY() < 0.0f)  box.setY (py + 8.0f);
        g.setColour (juce::Colours::black.withAlpha (0.75f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour (juce::Colours::white);
        g.drawText (text, box.reduced (5.0f, 0.0f), juce::Justification::centredLeft);
    }

    // Playhead.
    g.setColour (accent.withAlpha (0.9f));
    g.drawVerticalLine ((int) tv.toX (ctx.host.getEngine().getPositionSeconds()), 0.0f, h);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, w);

    juce::ignoreUnused (param);
}

//==============================================================================
void LaneTrack::mouseMove (const juce::MouseEvent& e)
{
    auto item = itemAt (e.position.x);
    switch (zoneFor (item, e.position))
    {
        case Zone::leftEdge:
        case Zone::rightEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Zone::strip:     setMouseCursor (e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor
                                                                 : juce::MouseCursor::DraggingHandCursor); break;
        case Zone::curve:     setMouseCursor (e.mods.isAltDown() ? juce::MouseCursor::CrosshairCursor
                                                                 : juce::MouseCursor::NormalCursor); break;
        case Zone::none:      setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void LaneTrack::mouseDown (const juce::MouseEvent& e)
{
    mode = Mode::none;
    auto& um = ctx.project.getUndoManager();
    auto item = itemAt (e.position.x);
    const auto zone = zoneFor (item, e.position);
    const bool add = e.mods.isCommandDown();

    if (! item.isValid())
    {
        if (! e.mods.isPopupMenu())
            ctx.selection.clear();
        return;
    }

    if (e.mods.isPopupMenu())
    {
        auto hit = pointAt (item, e.position);
        if (hit.isValid()) showPointMenu (hit);
        else               showItemMenu (item);
        return;
    }

    dragItemTree = item;
    origStart = item[IDs::start];
    origLength = item[IDs::length];

    if (zone == Zone::leftEdge || zone == Zone::rightEdge)
    {
        um.beginNewTransaction ("Trim item");
        ctx.selection.selectItem (item);
        freeRange = ctx.project.getFreeRange (lane, item, origStart);
        origPoints.clear();
        for (auto p : item)
            if (p.hasType (IDs::POINT))
                origPoints.push_back ({ p, (double) p[IDs::time], (float) (double) p[IDs::value] });
        mode = (zone == Zone::leftEdge) ? Mode::trimLeft : Mode::trimRight;
        return;
    }

    if (zone == Zone::strip)
    {
        um.beginNewTransaction (e.mods.isAltDown() ? "Copy item" : "Move item");
        if (add) { if (ctx.selection.getItems().contains (item)) ctx.selection.deselect (item); else ctx.selection.selectItem (item, true); }
        else ctx.selection.selectItem (item);
        mode = e.mods.isAltDown() ? Mode::copyItem : Mode::moveItem;
        copyCreated = false;
        return;
    }

    // Curve area.
    auto hit = pointAt (item, e.position);

    if (e.mods.isAltDown() && e.mods.isShiftDown())
    {
        um.beginNewTransaction ("Draw ramp");
        const double t = relativeTime (item, e.position.x, false);
        const float v = yToValue (e.position.y);
        auto a = ctx.project.addPoint (item, t, v);
        rampEnd = ctx.project.addPoint (item, t, v);
        dragAnchor = rampEnd;
        drawnPoints = { a, rampEnd };
        ctx.selection.selectItem (item);
        mode = Mode::ramp;
    }
    else if (e.mods.isAltDown())
    {
        um.beginNewTransaction ("Draw");
        const double t = relativeTime (item, e.position.x, ! ctx.snap.isOn());
        drawnPoints = { ctx.project.addPoint (item, t, yToValue (e.position.y)) };
        lastDrawX = e.position.x;
        lastDrawTime = t;
        ctx.selection.selectItem (item);
        mode = Mode::draw;
    }
    else if (e.mods.isCtrlDown())
    {
        // Bend the segment under the mouse.
        auto points = Project::getPoints (item);
        const double t = ctx.timeView.toTime (e.position.x) - origStart;
        juce::ValueTree a;
        float aValue = 0.0f, bValue = 0.0f;
        double aTime = -1.0, bTime = 1.0e12;

        for (auto p : item)
        {
            if (! p.hasType (IDs::POINT)) continue;
            const double pt = p[IDs::time];
            if (pt <= t && pt > aTime) { aTime = pt; a = p; aValue = (float) (double) p[IDs::value]; }
            if (pt > t && pt < bTime)  { bTime = pt; bValue = (float) (double) p[IDs::value]; }
        }

        if (a.isValid() && bTime < 1.0e12)
        {
            um.beginNewTransaction ("Bend curve");
            bendPoint = a;
            bendRising = bValue >= aValue;
            origTension = ((int) a[IDs::shape] == (int) Automation::Shape::curve) ? (double) a[IDs::tension] : 0.0;
            mode = Mode::bend;
        }
    }
    else if (hit.isValid())
    {
        if (add)
        {
            if (ctx.selection.getPoints().contains (hit)) ctx.selection.deselect (hit);
            else ctx.selection.selectPoint (hit, true);
            return;
        }
        if (! ctx.selection.getPoints().contains (hit))
            ctx.selection.selectPoint (hit);
        um.beginNewTransaction ("Move points");
        beginPointDrag (e, item, hit);
    }
    else
    {
        if (add) ctx.selection.selectItem (item, true);
        else     ctx.selection.selectItem (item);
    }

    repaint();
}

void LaneTrack::beginPointDrag (const juce::MouseEvent&, const juce::ValueTree& item, const juce::ValueTree& hit)
{
    dragAnchor = hit;
    constrainAxis = 0;
    origPoints.clear();

    // All selected points in this item move together.
    for (auto& p : ctx.selection.getPoints())
        if (p.getParent() == item)
            origPoints.push_back ({ p, (double) p[IDs::time], (float) (double) p[IDs::value] });

    mode = Mode::movePoints;
}

void LaneTrack::mouseDrag (const juce::MouseEvent& e)
{
    switch (mode)
    {
        case Mode::movePoints:  dragPoints (e); break;
        case Mode::moveItem:
        case Mode::copyItem:
        case Mode::trimLeft:
        case Mode::trimRight:   dragItem (e); break;
        case Mode::draw:        drawFreehand (e); break;

        case Mode::ramp:
        {
            auto item = rampEnd.getParent();
            auto& um = ctx.project.getUndoManager();
            rampEnd.setProperty (IDs::time, relativeTime (item, e.position.x, e.mods.isCommandDown()), &um);
            rampEnd.setProperty (IDs::value, yToValue (e.position.y), &um);
            break;
        }

        case Mode::bend:
        {
            const double dy = e.position.y - e.mouseDownPosition.y;
            double tension = origTension + (bendRising ? dy : -dy) / 120.0;
            tension = juce::jlimit (-1.0, 1.0, tension);
            auto& um = ctx.project.getUndoManager();
            const bool straight = std::abs (tension) < 0.02;
            bendPoint.setProperty (IDs::shape, (int) (straight ? Automation::Shape::linear : Automation::Shape::curve), &um);
            bendPoint.setProperty (IDs::tension, straight ? 0.0 : tension, &um);
            break;
        }

        case Mode::none:
        default: break;
    }
    repaint();
}

void LaneTrack::dragPoints (const juce::MouseEvent& e)
{
    if (! dragAnchor.isValid() || origPoints.empty())
        return;

    auto item = dragAnchor.getParent();
    const double start = item[IDs::start], length = item[IDs::length];
    const bool fine = e.mods.isCommandDown();
    const float scale = fine ? 0.1f : 1.0f;

    auto delta = (e.position - e.mouseDownPosition) * scale;

    // Shift: lock to the axis of the first clear movement.
    if (e.mods.isShiftDown())
    {
        if (constrainAxis == 0 && delta.getDistanceFromOrigin() > 3.0f)
            constrainAxis = std::abs (delta.x) >= std::abs (delta.y) ? 1 : 2;
        if (constrainAxis == 1) delta.y = 0.0f;
        if (constrainAxis == 2) delta.x = 0.0f;
    }
    else
    {
        constrainAxis = 0;
    }

    // Snap the anchor's time; the others follow by the same amount.
    const auto anchorOrig = std::find_if (origPoints.begin(), origPoints.end(),
                                          [this] (const Orig& o) { return o.tree == dragAnchor; });
    if (anchorOrig == origPoints.end())
        return;

    const double rawAnchor = start + anchorOrig->time + (double) delta.x / ctx.timeView.getPixelsPerSecond();
    const double snappedAnchor = std::abs (delta.x) < 1.0e-6f ? start + anchorOrig->time : ctx.snap.snap (rawAnchor, fine);
    const double dt = snappedAnchor - (start + anchorOrig->time);
    const float dv = -delta.y / juce::jmax (1.0f, curveArea().getHeight());

    auto& um = ctx.project.getUndoManager();
    for (auto& o : origPoints)
    {
        o.tree.setProperty (IDs::time, juce::jlimit (0.0, length, o.time + dt), &um);
        o.tree.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, o.value + dv), &um);
    }
}

void LaneTrack::dragItem (const juce::MouseEvent& e)
{
    auto& um = ctx.project.getUndoManager();
    const bool bypass = e.mods.isCommandDown();
    const double dt = (double) (e.position.x - e.mouseDownPosition.x) / ctx.timeView.getPixelsPerSecond();

    if (mode == Mode::trimLeft)
    {
        const double end = origStart + origLength;
        const double newStart = juce::jlimit (freeRange.getStart(), end - Project::minItemLength,
                                              ctx.snap.snap (origStart + dt, bypass));
        dragItemTree.setProperty (IDs::start, newStart, &um);
        dragItemTree.setProperty (IDs::length, end - newStart, &um);
        // Points keep their place in time.
        for (auto& o : origPoints)
            o.tree.setProperty (IDs::time, o.time + (origStart - newStart), &um);
        return;
    }

    if (mode == Mode::trimRight)
    {
        const double newEnd = juce::jlimit (origStart + Project::minItemLength, freeRange.getEnd(),
                                            ctx.snap.snap (origStart + origLength + dt, bypass));
        dragItemTree.setProperty (IDs::length, newEnd - origStart, &um);
        return;
    }

    // Move or copy: only to places where the item fits without overlapping.
    const double wanted = juce::jmax (0.0, ctx.snap.snap (origStart + dt, bypass));

    if (mode == Mode::copyItem && ! copyCreated)
    {
        // The copy appears once it can be placed somewhere free (not on top of the original).
        if (! ctx.project.isFree (lane, wanted, origLength))
            return;
        auto copy = dragItemTree.createCopy();
        copy.setProperty (IDs::start, wanted, nullptr);
        lane.appendChild (copy, &um);
        dragItemTree = copy;
        copyCreated = true;
        ctx.selection.selectItem (copy);
        return;
    }

    if (ctx.project.isFree (lane, wanted, origLength, dragItemTree))
        dragItemTree.setProperty (IDs::start, wanted, &um);
}

void LaneTrack::drawFreehand (const juce::MouseEvent& e)
{
    if (drawnPoints.empty())
        return;

    auto item = drawnPoints.front().getParent();
    auto& um = ctx.project.getUndoManager();
    const double t = relativeTime (item, e.position.x, ! ctx.snap.isOn() || e.mods.isCommandDown());
    const float v = yToValue (e.position.y);

    // Existing points in the stretch we just drew over are replaced.
    const double lo = juce::jmin (t, lastDrawTime), hi = juce::jmax (t, lastDrawTime);
    for (int i = item.getNumChildren(); --i >= 0;)
    {
        auto p = item.getChild (i);
        if (! p.hasType (IDs::POINT) || std::find (drawnPoints.begin(), drawnPoints.end(), p) != drawnPoints.end())
            continue;
        const double pt = p[IDs::time];
        if (pt >= lo - 1.0e-9 && pt <= hi + 1.0e-9)
            item.removeChild (i, &um);
    }

    if (std::abs (e.position.x - lastDrawX) >= 3.0f && std::abs (t - lastDrawTime) > 1.0e-6)
    {
        drawnPoints.push_back (ctx.project.addPoint (item, t, v));
        lastDrawX = e.position.x;
        lastDrawTime = t;
    }
    else
    {
        drawnPoints.back().setProperty (IDs::value, v, &um);
    }
}

void LaneTrack::finishFreehand()
{
    if (drawnPoints.size() < 3)
        return;

    auto item = drawnPoints.front().getParent();
    const double start = item[IDs::start];

    std::sort (drawnPoints.begin(), drawnPoints.end(),
               [] (const auto& a, const auto& b) { return (double) a[IDs::time] < (double) b[IDs::time]; });

    std::vector<juce::Point<float>> screen;
    for (auto& p : drawnPoints)
        screen.push_back ({ ctx.timeView.toX (start + (double) p[IDs::time]), valueToY ((float) (double) p[IDs::value]) });

    std::vector<bool> keep (screen.size(), false);
    keep.front() = keep.back() = true;
    simplify (screen, 0, screen.size() - 1, 1.5f, keep);

    auto& um = ctx.project.getUndoManager();
    for (size_t i = 0; i < drawnPoints.size(); ++i)
        if (! keep[i])
            item.removeChild (drawnPoints[i], &um);
}

void LaneTrack::finishRamp()
{
    if (drawnPoints.size() != 2)
        return;

    auto a = drawnPoints[0], b = drawnPoints[1];
    auto item = a.getParent();
    auto& um = ctx.project.getUndoManager();
    const double ta = a[IDs::time], tb = b[IDs::time];

    if (std::abs (ta - tb) < 1.0e-6)
    {
        item.removeChild (b, &um);    // no drag: just one point
        return;
    }

    const double lo = juce::jmin (ta, tb), hi = juce::jmax (ta, tb);
    for (int i = item.getNumChildren(); --i >= 0;)
    {
        auto p = item.getChild (i);
        if (p == a || p == b || ! p.hasType (IDs::POINT))
            continue;
        const double pt = p[IDs::time];
        if (pt > lo - 1.0e-9 && pt < hi + 1.0e-9)
            item.removeChild (i, &um);
    }
}

void LaneTrack::mouseUp (const juce::MouseEvent&)
{
    if (mode == Mode::draw)  finishFreehand();
    if (mode == Mode::ramp)  finishRamp();

    mode = Mode::none;
    dragAnchor = {};
    rampEnd = {};
    bendPoint = {};
    drawnPoints.clear();
    origPoints.clear();
    ctx.selection.prune();
    repaint();
}

void LaneTrack::mouseDoubleClick (const juce::MouseEvent& e)
{
    auto item = itemAt (e.position.x);

    if (! item.isValid())
    {
        // New item, up to 4 s long, in the free space under the mouse.
        const double t = juce::jmax (0.0, ctx.snap.snap (ctx.timeView.toTime (e.position.x)));
        auto free = ctx.project.getFreeRange (lane, {}, t);
        const double length = juce::jmin (4.0, free.getEnd() - t);

        if (t >= ctx.project.getLengthSeconds() || length < Project::minItemLength)
        {
            ctx.showStatus ("No room for a new item here.");
            return;
        }

        ctx.project.getUndoManager().beginNewTransaction ("New item");
        auto newItem = ctx.project.addItem (lane, t, length);
        ctx.selection.selectItem (newItem);
        return;
    }

    auto hit = pointAt (item, e.position);
    if (hit.isValid())
        editPointNumerically (hit);
    else if (zoneFor (item, e.position) == Zone::curve)
        ctx.showStatus ("The item editor (double-click an item) arrives in step 5.");
}

void LaneTrack::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (ctx.handleWheel)
        ctx.handleWheel (e, wheel, *this);
}

//==============================================================================
void LaneTrack::showPointMenu (juce::ValueTree point)
{
    const int shape = point[IDs::shape];
    const float tension = (float) (double) point[IDs::tension];

    juce::PopupMenu m;
    m.addSectionHeader ("Segment from this point");
    m.addItem (1, "Linear",              true, shape == 0);
    m.addItem (2, "Curve: slow start",   true, shape == 1 && tension > 0.0f);
    m.addItem (3, "Curve: fast start",   true, shape == 1 && tension < 0.0f);
    m.addItem (4, "Smooth (S-curve)",    true, shape == 2);
    m.addItem (5, "Hold, then jump",     true, shape == 3);
    m.addSeparator();
    m.addItem (8, "Type time and value...");
    m.addItem (9, "Delete point");

    juce::Component::SafePointer<LaneTrack> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safeThis, point] (int result) mutable
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto& um = safeThis->ctx.project.getUndoManager();
        if (result == 8) { safeThis->editPointNumerically (point); return; }

        um.beginNewTransaction (result == 9 ? "Delete point" : "Change segment shape");
        auto set = [&] (Automation::Shape s, float t)
        {
            point.setProperty (IDs::shape, (int) s, &um);
            point.setProperty (IDs::tension, t, &um);
        };

        switch (result)
        {
            case 1: set (Automation::Shape::linear, 0.0f);  break;
            case 2: set (Automation::Shape::curve,  0.6f);  break;
            case 3: set (Automation::Shape::curve, -0.6f);  break;
            case 4: set (Automation::Shape::smooth, 0.0f);  break;
            case 5: set (Automation::Shape::hold,   0.0f);  break;
            case 9: safeThis->ctx.selection.deselect (point);
                    point.getParent().removeChild (point, &um); break;
            default: break;
        }
        safeThis->repaint();
    });
}

void LaneTrack::showItemMenu (juce::ValueTree item)
{
    juce::PopupMenu m;
    m.addItem (1, "Duplicate after itself");
    m.addItem (2, "Delete item");

    juce::Component::SafePointer<LaneTrack> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safeThis, item] (int result) mutable
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto& project = safeThis->ctx.project;
        auto& um = project.getUndoManager();
        auto laneTree = item.getParent();

        if (result == 1)
        {
            const double start = (double) item[IDs::start] + (double) item[IDs::length];
            const double length = item[IDs::length];
            if (! project.isFree (laneTree, start, length))
            {
                safeThis->ctx.showStatus ("No room after this item for a copy.");
                return;
            }
            um.beginNewTransaction ("Duplicate item");
            auto copy = item.createCopy();
            copy.setProperty (IDs::start, start, nullptr);
            laneTree.appendChild (copy, &um);
            safeThis->ctx.selection.selectItem (copy);
        }
        else if (result == 2)
        {
            um.beginNewTransaction ("Delete item");
            safeThis->ctx.selection.deselect (item);
            laneTree.removeChild (item, &um);
        }
    });
}

void LaneTrack::editPointNumerically (juce::ValueTree point)
{
    auto item = point.getParent();
    const double start = item[IDs::start];
    const double absTime = start + (double) point[IDs::time];
    const float value = (float) (double) point[IDs::value];

    auto* window = new juce::AlertWindow ("Point", "Time is absolute (m:ss.mmm or seconds). "
                                                   "Value in the plugin's own units.",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("time", TimeFormat::format (absTime), "Time");
    window->addTextEditor ("value", getParameter() != nullptr ? getParameter()->getText (value, 32)
                                                              : juce::String (value, 4), "Value");
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LaneTrack> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window, point] (int result) mutable
    {
        std::unique_ptr<juce::AlertWindow> owner (window);
        if (safeThis == nullptr || result != 1 || ! point.getParent().isValid())
            return;

        auto parentItem = point.getParent();
        const double itemStart = parentItem[IDs::start], length = parentItem[IDs::length];
        const double t = juce::jlimit (0.0, length, TimeFormat::parse (window->getTextEditorContents ("time")) - itemStart);

        const auto text = window->getTextEditorContents ("value");
        float v = (float) (double) point[IDs::value];
        if (auto* p = safeThis->getParameter())
            v = p->getValueForText (text);
        else
            v = (float) text.getDoubleValue();

        auto& um = safeThis->ctx.project.getUndoManager();
        um.beginNewTransaction ("Edit point");
        point.setProperty (IDs::time, t, &um);
        point.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, v), &um);
    }), false);
}
