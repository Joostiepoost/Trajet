#include "LaneTrack.h"
#include "../Model/ItemTime.h"

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
    return snapLocal (ItemTime (item), ctx.timeView.toTime (x), bypassSnap);
}

double LaneTrack::snapLocal (const ItemTime& it, double absSeconds, bool bypassSnap) const
{
    // Position in the item's own units (seconds or beats), on its grid.
    double local;
    if (it.isMusical())
    {
        local = it.localAt (absSeconds);
        if (ctx.snap.isOn() && ! bypassSnap) local = std::round (local / it.gridBeats) * it.gridBeats;
        else                                 local = std::round (local * 1000.0) / 1000.0;
    }
    else
    {
        local = ctx.snap.snap (absSeconds, bypassSnap) - it.getStart();
    }
    return juce::jlimit (0.0, it.localLength(), local);
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
    if (! item.isValid() || ItemTime::isGenerator (item))
        return {};

    const ItemTime it (item);
    juce::ValueTree best;
    float bestDistance = pointHitRadius;

    for (const auto& p : item)
    {
        if (! p.hasType (IDs::POINT))
            continue;

        const juce::Point<float> pp (ctx.timeView.toX (it.absolute ((double) p[IDs::time])),
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

void LaneTrack::collectPointsIn (juce::Rectangle<float> area, juce::Array<juce::ValueTree>& result) const
{
    for (const auto& item : lane)
    {
        if (! item.hasType (IDs::ITEM) || ItemTime::isGenerator (item)) continue;
        const ItemTime it (item);
        for (const auto& p : item)
            if (p.hasType (IDs::POINT)
                && area.contains (ctx.timeView.toX (it.absolute ((double) p[IDs::time])), valueToY ((float) (double) p[IDs::value])))
                result.addIfNotAlreadyThere (p);
    }
}

juce::Array<juce::ValueTree> LaneTrack::selectedPointsHere() const
{
    juce::Array<juce::ValueTree> pts;
    for (auto& p : ctx.selection.getPoints())
        if (p.getParent().getParent() == lane)
            pts.add (p);
    return pts;
}

juce::Rectangle<float> LaneTrack::selectionBox() const
{
    auto pts = selectedPointsHere();
    if (pts.size() < 2)
        return {};

    float x0 = 1.0e9f, x1 = -1.0e9f, y0 = 1.0e9f, y1 = -1.0e9f;
    for (auto& p : pts)
    {
        const float x = ctx.timeView.toX (ItemTime (p.getParent()).absolute ((double) p[IDs::time]));
        const float y = valueToY ((float) (double) p[IDs::value]);
        x0 = juce::jmin (x0, x); x1 = juce::jmax (x1, x);
        y0 = juce::jmin (y0, y); y1 = juce::jmax (y1, y);
    }
    return juce::Rectangle<float>::leftTopRightBottom (x0, y0, x1, y1).expanded (6.0f);
}

LaneTrack::Handle LaneTrack::handleAt (juce::Point<float> pos) const
{
    auto box = selectionBox();
    if (box.isEmpty())
        return Handle::none;

    auto near = [pos] (float x, float y) { return pos.getDistanceFrom ({ x, y }) <= 7.0f; };
    if (near (box.getX(), box.getCentreY()))      return Handle::left;
    if (near (box.getRight(), box.getCentreY()))  return Handle::right;
    if (near (box.getCentreX(), box.getY()))      return Handle::top;
    if (near (box.getCentreX(), box.getBottom())) return Handle::bottom;
    return Handle::none;
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

    // Note lanes: the pitch range shown fits all notes of the lane.
    const bool noteLane = Project::isNoteLane (lane);
    juce::Range<int> pitches (60, 72);
    if (noteLane)
    {
        int lo = 128, hi = -1;
        for (const auto& item : lane)
            for (const auto& n : item)
                if (n.hasType (IDs::NOTE)) { lo = juce::jmin (lo, (int) n[IDs::pitch]); hi = juce::jmax (hi, (int) n[IDs::pitch]); }
        if (hi >= lo)
        {
            const int centre = (lo + hi) / 2, span = juce::jmax (12, hi - lo + 4);
            pitches = { centre - span / 2, centre - span / 2 + span };
        }
    }

    for (const auto& item : lane)
    {
        if (! item.hasType (IDs::ITEM))
            continue;

        auto r = itemBounds (item);
        if (r.getRight() < 0.0f || r.getX() > w)
            continue;

        const bool selected = ctx.selection.getItems().contains (item);
        const ItemTime it (item);

        g.setColour (colour.withAlpha (muted ? 0.04f : 0.10f));
        g.fillRoundedRectangle (r, 4.0f);

        // Title strip (move handle).
        auto strip = r.withHeight ((float) stripHeight);
        g.setColour (colour.withAlpha (muted ? 0.25f : 0.55f));
        g.fillRoundedRectangle (strip, 4.0f);
        g.fillRect (strip.withTrimmedTop (6.0f));

        // Musical items: bar and beat lines, and the tempo in the title strip.
        if (it.isMusical())
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());
            const double pxPerBeat = tv.getPixelsPerSecond() * 60.0 / juce::jmax (10.0, it.tempo.firstBpm());
            const bool showBeats = pxPerBeat >= 8.0;
            const bool showBars = pxPerBeat * it.beatsPerBar >= 6.0;
            const int lastBeat = (int) std::ceil (it.lengthBeats);
            for (int b = 0; b <= lastBeat && showBars; ++b)
            {
                const bool isBar = b % it.beatsPerBar == 0;
                if (! isBar && ! showBeats) continue;
                const float x = tv.toX (it.absolute ((double) b));
                if (x < -2.0f) continue;
                if (x > w) break;
                g.setColour (juce::Colours::white.withAlpha (isBar ? 0.16f : 0.06f));
                g.drawVerticalLine ((int) x, r.getY() + (float) stripHeight, r.getBottom());
            }

            const auto first = it.tempo.points.front().bpm, last = it.tempo.points.back().bpm;
            juce::String label = juce::String (it.beatsPerBar) + "/" + juce::String (it.beatUnit) + "  "
                                 + juce::String (juce::CharPointer_UTF8 ("\xe2\x99\xa9")) + "=" + juce::String (first, 0);
            if (it.tempo.points.size() > 1 && std::abs (last - first) > 0.01)
                label << juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) << juce::String (last, 0);
            g.setColour (juce::Colours::black.withAlpha (0.7f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            if (r.getWidth() > 90.0f)
                g.drawText (label, r.withHeight ((float) stripHeight).reduced (6.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
        }

        g.setColour (selected ? juce::Colours::white : colour.withAlpha (0.6f));
        g.drawRoundedRectangle (r, 4.0f, selected ? 2.0f : 1.0f);

        // Generator items: their curve, and what they are, in the title strip.
        const auto generator = ItemTime::readGenerator (item);
        if (generator.type != Automation::GeneratorType::none)
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());

            const double length = it.localLength();
            juce::Path curve;
            const float xa = juce::jmax (r.getX(), 0.0f), xb = juce::jmin (r.getRight(), w);
            bool first = true;
            for (float x = xa; x <= xb; x += 1.0f)
            {
                const double local = juce::jlimit (0.0, length, it.localAt (tv.toTime (x)));
                const float y = valueToY (Automation::generatorValue (generator, local, length, it.toSeconds (local)));
                if (first) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                first = false;
            }
            g.setColour (muted ? colour.withAlpha (0.4f) : colour);
            g.strokePath (curve, juce::PathStrokeType (1.4f));

            // Dashed lines at the generator's min and max.
            for (float v : { generator.minValue, generator.maxValue })
            {
                juce::Path line;
                line.startNewSubPath (xa, valueToY (v));
                line.lineTo (xb, valueToY (v));
                juce::Path dashed;
                const float dashes[] = { 3.0f, 4.0f };
                juce::PathStrokeType (1.0f).createDashedStroke (dashed, line, dashes, 2);
                g.setColour (colour.withAlpha (0.35f));
                g.fillPath (dashed);
            }

            static const char* lfoNames[] = { "sine", "triangle", "square", "saw up", "saw down" };
            static const char* randomNames[] = { "stepped", "smooth", "drift" };
            const juce::String unit = it.isMusical() ? "/beat" : " Hz";
            juce::String label;
            if (generator.type == Automation::GeneratorType::lfo)
                label = "LFO " + juce::String (lfoNames[juce::jlimit (0, 4, generator.shape)]) + "  " + juce::String (generator.rate, 2) + unit;
            else if (generator.type == Automation::GeneratorType::random)
                label = "Random " + juce::String (randomNames[juce::jlimit (0, 2, generator.shape)]) + "  " + juce::String (generator.rate, 2) + unit;
            else
                label = generator.formula != nullptr ? "f = " + item[IDs::formula].toString() : "formula error";

            g.setColour (juce::Colours::black.withAlpha (0.75f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            auto stripText = r.withHeight ((float) stripHeight).reduced (6.0f, 0.0f);
            if (it.isMusical()) stripText = stripText.withTrimmedLeft (juce::jmin (stripText.getWidth() * 0.5f, 120.0f));
            if (r.getWidth() > 60.0f)
                g.drawText (label, stripText.toNearestInt(), juce::Justification::centredLeft, true);
        }

        if (noteLane)
        {
            paintNotes (g, item, r, pitches, colour, muted);
            continue;
        }

        // Curve: evaluated at every pixel, the same maths the engine uses.
        auto points = generator.type == Automation::GeneratorType::none ? Project::getPoints (item) : std::vector<Automation::Point>();
        if (! points.empty())
        {
            juce::Graphics::ScopedSaveState save (g);
            g.reduceClipRegion (r.toNearestInt());

            juce::Path curve;
            const float xa = juce::jmax (r.getX(), 0.0f), xb = juce::jmin (r.getRight(), w);
            bool first = true;
            for (float x = xa; x <= xb; x += 1.0f)
            {
                const float y = valueToY (Automation::valueAt (points, it.localAt (tv.toTime (x))));
                if (first) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                first = false;
            }
            g.setColour (muted ? colour.withAlpha (0.4f) : colour);
            g.strokePath (curve, juce::PathStrokeType (1.6f));

            for (const auto& p : item)
            {
                if (! p.hasType (IDs::POINT))
                    continue;
                const float px = tv.toX (it.absolute ((double) p[IDs::time]));
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
        const ItemTime it (dragAnchor.getParent());
        const float v = (float) (double) dragAnchor[IDs::value];
        const double local = dragAnchor[IDs::time];
        const double t = it.absolute (local);
        const float px = tv.toX (t), py = valueToY (v);
        const auto text = valueText (v) + "   " + (it.isMusical() ? it.barBeat (local) + "  " : juce::String())
                          + TimeFormat::format (t);
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

    // Selection box with scale handles (two or more selected points).
    auto box = selectionBox();
    if (! box.isEmpty())
    {
        juce::Path outline;
        outline.addRectangle (box);
        juce::Path dashed;
        const float dashes[] = { 4.0f, 3.0f };
        juce::PathStrokeType (1.0f).createDashedStroke (dashed, outline, dashes, 2);
        g.setColour (accent.withAlpha (0.8f));
        g.fillPath (dashed);
        for (auto c : { juce::Point<float> (box.getX(), box.getCentreY()), juce::Point<float> (box.getRight(), box.getCentreY()),
                        juce::Point<float> (box.getCentreX(), box.getY()), juce::Point<float> (box.getCentreX(), box.getBottom()) })
            g.fillRect (juce::Rectangle<float> (7.0f, 7.0f).withCentre (c));
    }

    // Rubber band (when this track draws its own).
    if (mode == Mode::rubberBand && ! ctx.rubberBand)
    {
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (rubberRect);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRect (rubberRect, 1.0f);
    }

    // Playhead.
    g.setColour (accent.withAlpha (0.9f));
    g.drawVerticalLine ((int) tv.toX (ctx.host.getEngine().getPositionSeconds()), 0.0f, h);

    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawHorizontalLine (getHeight() - 1, 0.0f, w);

    juce::ignoreUnused (param);
}

void LaneTrack::paintNotes (juce::Graphics& g, const juce::ValueTree& item, juce::Rectangle<float> r,
                            juce::Range<int> pitches, juce::Colour colour, bool muted)
{
    juce::Graphics::ScopedSaveState save (g);
    g.reduceClipRegion (r.toNearestInt());

    auto area = r.withTrimmedTop ((float) stripHeight + 3.0f).withTrimmedBottom (3.0f);
    const float rowH = area.getHeight() / (float) juce::jmax (1, pitches.getLength() + 1);
    const ItemTime it (item);
    int count = 0;

    // Faint lines at every C.
    for (int p = pitches.getStart(); p <= pitches.getEnd(); ++p)
        if (p % 12 == 0)
        {
            const float y = area.getBottom() - (float) (p - pitches.getStart() + 1) * rowH;
            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawHorizontalLine ((int) (y + rowH), area.getX(), area.getRight());
        }

    for (const auto& n : item)
    {
        if (! n.hasType (IDs::NOTE)) continue;
        ++count;
        const double t0 = n[IDs::time], t1 = t0 + (double) n[IDs::length];
        const float x0 = ctx.timeView.toX (it.absolute (t0)), x1 = ctx.timeView.toX (it.absolute (t1));
        const float y = area.getBottom() - (float) ((int) n[IDs::pitch] - pitches.getStart() + 1) * rowH;
        const float vel = (float) (int) n[IDs::velocity] / 127.0f;
        g.setColour (colour.withAlpha ((muted ? 0.35f : 1.0f) * (0.35f + 0.65f * vel)));
        g.fillRect (juce::Rectangle<float> (x0, y, juce::jmax (2.0f, x1 - x0 - 1.0f), juce::jmax (2.0f, rowH - 1.0f)));
    }

    g.setColour (juce::Colours::black.withAlpha (0.75f));
    g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
    auto strip = r.withHeight ((float) stripHeight).reduced (6.0f, 0.0f);
    if (it.isMusical()) strip = strip.withTrimmedLeft (juce::jmin (strip.getWidth() * 0.5f, 120.0f));
    if (r.getWidth() > 60.0f)
        g.drawText (juce::String (count) + (count == 1 ? " note" : " notes"), strip.toNearestInt(), juce::Justification::centredLeft, true);
}

//==============================================================================
void LaneTrack::mouseMove (const juce::MouseEvent& e)
{
    if (auto hnd = handleAt (e.position); hnd != Handle::none)
    {
        setMouseCursor ((hnd == Handle::left || hnd == Handle::right) ? juce::MouseCursor::LeftRightResizeCursor
                                                                      : juce::MouseCursor::UpDownResizeCursor);
        return;
    }

    auto item = itemAt (e.position.x);
    switch (zoneFor (item, e.position))
    {
        case Zone::leftEdge:
        case Zone::rightEdge: setMouseCursor (juce::MouseCursor::LeftRightResizeCursor); break;
        case Zone::strip:     setMouseCursor (e.mods.isAltDown() ? juce::MouseCursor::CopyingCursor
                                                                 : juce::MouseCursor::DraggingHandCursor); break;
        case Zone::curve:     setMouseCursor (e.mods.isAltDown() && e.mods.isCommandDown() ? juce::MouseCursor::UpDownResizeCursor
                                              : e.mods.isAltDown() ? juce::MouseCursor::CrosshairCursor
                                                                   : juce::MouseCursor::NormalCursor); break;
        case Zone::none:      setMouseCursor (juce::MouseCursor::NormalCursor); break;
    }
}

void LaneTrack::mouseDown (const juce::MouseEvent& e)
{
    mode = Mode::none;
    auto& um = ctx.project.getUndoManager();

    // Scale handles of the selection box come first.
    if (! e.mods.isPopupMenu())
        if (auto hnd = handleAt (e.position); hnd != Handle::none)
        {
            um.beginNewTransaction ("Scale points");
            scaleHandle = hnd;
            origPoints.clear();
            boxT0 = 1.0e12; boxT1 = -1.0e12; boxV0 = 1.0f; boxV1 = 0.0f;
            for (auto& p : selectedPointsHere())
            {
                const double t = ItemTime (p.getParent()).absolute ((double) p[IDs::time]);
                const float v = (float) (double) p[IDs::value];
                origPoints.push_back ({ p, t, v });   // absolute time here
                boxT0 = juce::jmin (boxT0, t); boxT1 = juce::jmax (boxT1, t);
                boxV0 = juce::jmin (boxV0, v); boxV1 = juce::jmax (boxV1, v);
            }
            mode = Mode::scale;
            return;
        }

    auto item = itemAt (e.position.x);
    const auto zone = zoneFor (item, e.position);
    const bool add = e.mods.isCommandDown();

    // In the item editor, tools stand in for the modifier keys.
    const auto tool = ctx.tool != nullptr ? *ctx.tool : Tool::select;
    const bool wantPencil = e.mods.isAltDown() || tool == Tool::pencil;
    const bool wantLine   = (e.mods.isAltDown() && e.mods.isShiftDown()) || tool == Tool::line;
    const bool wantBend   = (e.mods.isAltDown() && e.mods.isCommandDown()) || tool == Tool::bend;

    if (! item.isValid())
    {
        if (! e.mods.isPopupMenu())
        {
            // Empty space: this lane becomes the paste target, the playhead moves here,
            // and dragging draws a rubber band.
            if (! add) ctx.selection.clear();
            ctx.selection.setActiveLane (lane);
            auto t = ctx.timeView.toTime (e.position.x);
            if (! e.mods.isCommandDown())
                t = ctx.snap.snap (t);
            ctx.controller.locate (t);
            mode = Mode::rubberBand;
            rubberRect = {};
        }
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
        {
            ItemTime it (item);
            origTempo = it.tempo.points;
            origLengthBeats = it.lengthBeats;
        }
        origPoints.clear();
        for (auto p : item)
            if (p.hasType (IDs::POINT) || p.hasType (IDs::NOTE))
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

    // Generator and note items have no points to edit here: clicking selects the item.
    if (ItemTime::isGenerator (item) || Project::isNoteItem (item))
    {
        if (add) ctx.selection.selectItem (item, true); else ctx.selection.selectItem (item);
        mode = Mode::rubberBand;
        rubberRect = {};
        repaint();
        return;
    }

    // Clicking right on a point always grabs it (unless Option is held to draw).
    const bool onPoint = hit.isValid() && ! e.mods.isAltDown();

    if (wantBend && ! onPoint)
    {
        if (startBend (item, e.position.x))
            um.beginNewTransaction ("Bend curve");
    }
    else if (wantLine && ! onPoint)
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
    else if (wantPencil && ! onPoint)
    {
        um.beginNewTransaction ("Draw");
        const double t = relativeTime (item, e.position.x, ! ctx.snap.isOn());
        drawnPoints = { ctx.project.addPoint (item, t, yToValue (e.position.y)) };
        lastDrawX = e.position.x;
        lastDrawTime = t;
        ctx.selection.selectItem (item);
        mode = Mode::draw;
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
        // Empty curve area: select the item, and dragging draws a rubber band.
        if (add) ctx.selection.selectItem (item, true);
        else     ctx.selection.selectItem (item);
        mode = Mode::rubberBand;
        rubberRect = {};
    }

    repaint();
}

bool LaneTrack::startBend (const juce::ValueTree& item, float x)
{
    const double t = ItemTime (item).localAt (ctx.timeView.toTime (x));
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

    if (! a.isValid() || bTime >= 1.0e12)
        return false;

    bendPoint = a;
    bendRising = bValue >= aValue;
    origTension = ((int) a[IDs::shape] == (int) Automation::Shape::curve) ? (double) a[IDs::tension] : 0.0;
    ctx.selection.selectItem (item);
    mode = Mode::bend;
    return true;
}

void LaneTrack::dragScale (const juce::MouseEvent& e)
{
    const bool aroundCentre = e.mods.isAltDown();
    const bool bypass = e.mods.isCommandDown();
    auto& um = ctx.project.getUndoManager();

    double t0 = boxT0, t1 = boxT1;
    float v0 = boxV0, v1 = boxV1;
    const double tm = 0.5 * (boxT0 + boxT1);
    const float vm = 0.5f * (boxV0 + boxV1);

    const double mouseT = ctx.snap.snap (ctx.timeView.toTime (e.position.x), bypass);
    const float mouseV = yToValue (e.position.y);

    switch (scaleHandle)
    {
        case Handle::left:   t0 = juce::jmin (mouseT, t1 - 0.001); if (aroundCentre) t1 = 2.0 * tm - t0; break;
        case Handle::right:  t1 = juce::jmax (mouseT, t0 + 0.001); if (aroundCentre) t0 = 2.0 * tm - t1; break;
        case Handle::top:    v1 = mouseV; if (aroundCentre) v0 = 2.0f * vm - v1; break;
        case Handle::bottom: v0 = mouseV; if (aroundCentre) v1 = 2.0f * vm - v0; break;
        case Handle::none:
        default: return;
    }

    const double tSpan = boxT1 - boxT0;
    const float vSpan = boxV1 - boxV0;

    for (auto& o : origPoints)
    {
        auto item = o.tree.getParent();
        if (! item.isValid()) continue;
        const ItemTime it (item);

        const double t = tSpan > 1.0e-9 ? t0 + (o.time - boxT0) / tSpan * (t1 - t0) : o.time;
        const float v = vSpan > 1.0e-6f ? v0 + (o.value - boxV0) / vSpan * (v1 - v0) : o.value;

        o.tree.setProperty (IDs::time, juce::jlimit (0.0, it.localLength(), it.localAt (t)), &um);
        o.tree.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, v), &um);
    }
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

        case Mode::scale: dragScale (e); break;

        case Mode::rubberBand:
        {
            rubberRect = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            if (ctx.rubberBand)
            {
                auto screenArea = juce::Rectangle<int> (e.getMouseDownScreenPosition(), e.getScreenPosition());
                ctx.rubberBand (screenArea, e.mods.isCommandDown(), false);
            }
            else
            {
                juce::Array<juce::ValueTree> found;
                collectPointsIn (rubberRect, found);
                if (! e.mods.isCommandDown()) ctx.selection.clear();
                for (auto& p : found) ctx.selection.selectPoint (p, true);
            }
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
    const ItemTime it (item);
    const double length = it.localLength();
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

    // Work in the item's own units (seconds or beats), snapped to its grid.
    const double rawAnchor = it.absolute (anchorOrig->time) + (double) delta.x / ctx.timeView.getPixelsPerSecond();
    const double snappedAnchor = std::abs (delta.x) < 1.0e-6f ? anchorOrig->time : snapLocal (it, rawAnchor, fine);
    const double dt = snappedAnchor - anchorOrig->time;
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

    if (mode == Mode::trimLeft || mode == Mode::trimRight)
    {
        // The item as it was when the drag started (musical items convert through it).
        ItemTime orig (dragItemTree);
        orig.tempo.points = origTempo;
        orig.lengthBeats = origLengthBeats;
        const double end = origStart + origLength;

        auto snapEdge = [&] (double t)
        {
            if (! orig.isMusical()) return ctx.snap.snap (t, bypass);
            if (! ctx.snap.isOn() || bypass) return t;
            const double beats = std::round (orig.toLocal (t - origStart) / orig.gridBeats) * orig.gridBeats;
            return origStart + orig.toSeconds (beats);
        };

        if (mode == Mode::trimRight)
        {
            const double newEnd = juce::jlimit (origStart + Project::minItemLength, freeRange.getEnd(),
                                                snapEdge (origStart + origLength + dt));
            dragItemTree.setProperty (IDs::length, newEnd - origStart, &um);
            if (orig.isMusical())
                dragItemTree.setProperty (IDs::lengthBeats, orig.toLocal (newEnd - origStart), &um);
            return;
        }

        const double newStart = juce::jlimit (freeRange.getStart(), end - Project::minItemLength, snapEdge (origStart + dt));
        dragItemTree.setProperty (IDs::start, newStart, &um);
        dragItemTree.setProperty (IDs::length, end - newStart, &um);

        // Points keep their place in time.
        const double shift = orig.toLocal (newStart - origStart);   // seconds or beats (negative when extending)
        for (auto& o : origPoints)
            o.tree.setProperty (IDs::time, o.time - shift, &um);

        if (orig.isMusical())
        {
            // Rebuild the tempo map so it starts at the new item start.
            std::vector<Automation::TempoPoint> tempo;
            if (shift >= 0.0)
            {
                bool rampHere = false;
                for (auto& tp : origTempo) if (tp.beat <= shift) rampHere = tp.ramp;
                tempo.push_back ({ 0.0, orig.tempo.bpmAt (shift), rampHere });
                for (auto& tp : origTempo)
                    if (tp.beat > shift)
                        tempo.push_back ({ tp.beat - shift, tp.bpm, tp.ramp });
            }
            else
            {
                tempo.push_back ({ 0.0, origTempo.front().bpm, false });
                for (auto& tp : origTempo)
                    tempo.push_back ({ tp.beat - shift, tp.bpm, tp.ramp });
            }

            for (int i = dragItemTree.getNumChildren(); --i >= 0;)
                if (dragItemTree.getChild (i).hasType (IDs::TEMPO))
                    dragItemTree.removeChild (i, &um);
            for (auto& tp : tempo)
            {
                juce::ValueTree t (IDs::TEMPO);
                t.setProperty (IDs::beat, tp.beat, nullptr);
                t.setProperty (IDs::bpm, tp.bpm, nullptr);
                t.setProperty (IDs::ramp, tp.ramp, nullptr);
                dragItemTree.appendChild (t, &um);
            }
            dragItemTree.setProperty (IDs::lengthBeats, origLengthBeats - shift, &um);
        }
        return;
    }

    // Move or copy: only to places where the item fits without overlapping.
    // Dragging up or down moves (or copies) the item to the lane under the mouse.
    const double wanted = juce::jmax (0.0, ctx.snap.snap (origStart + dt, bypass));
    auto currentLane = dragItemTree.getParent();
    auto targetLane = ctx.laneAt ? ctx.laneAt (e.getScreenPosition()) : currentLane;
    if (! targetLane.isValid() || Project::isNoteLane (targetLane) != Project::isNoteLane (currentLane))
        targetLane = currentLane;   // notes stay in note lanes, automation in automation lanes

    if (mode == Mode::copyItem && ! copyCreated)
    {
        // The copy appears once it can be placed somewhere free (not on top of the original).
        if (! ctx.project.isFree (targetLane, wanted, origLength))
            return;
        auto copy = dragItemTree.createCopy();
        copy.setProperty (IDs::start, wanted, nullptr);
        targetLane.appendChild (copy, &um);
        dragItemTree = copy;
        copyCreated = true;
        ctx.selection.selectItem (copy);
        return;
    }

    if (targetLane != currentLane)
    {
        if (ctx.project.isFree (targetLane, wanted, origLength))
        {
            auto item = dragItemTree;
            currentLane.removeChild (item, &um);
            item.setProperty (IDs::start, wanted, &um);
            targetLane.appendChild (item, &um);
            ctx.selection.selectItem (item);
        }
        return;
    }

    if (ctx.project.isFree (currentLane, wanted, origLength, dragItemTree))
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
    const ItemTime it (item);

    std::sort (drawnPoints.begin(), drawnPoints.end(),
               [] (const auto& a, const auto& b) { return (double) a[IDs::time] < (double) b[IDs::time]; });

    std::vector<juce::Point<float>> screen;
    for (auto& p : drawnPoints)
        screen.push_back ({ ctx.timeView.toX (it.absolute ((double) p[IDs::time])), valueToY ((float) (double) p[IDs::value]) });

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

void LaneTrack::mouseUp (const juce::MouseEvent& e)
{
    if (mode == Mode::draw)  finishFreehand();
    if (mode == Mode::ramp)  finishRamp();
    if (mode == Mode::rubberBand && ctx.rubberBand && e.getDistanceFromDragStart() > 2)
        ctx.rubberBand (juce::Rectangle<int> (e.getMouseDownScreenPosition(), e.getScreenPosition()), e.mods.isCommandDown(), true);
    scaleHandle = Handle::none;
    rubberRect = {};

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
    else if (ctx.openItemEditor)
        ctx.openItemEditor (item);
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
    const bool musical = ItemTime (item).isMusical();
    const auto genType = item[IDs::generator].toString();
    juce::PopupMenu generators;
    generators.addItem (10, "Points (draw by hand)", true, ! ItemTime::isGenerator (item));
    generators.addItem (11, "LFO", true, genType == "lfo");
    generators.addItem (12, "Random", true, genType == "random");
    generators.addItem (13, "Formula", true, genType == "formula");
    m.addItem (1, "Duplicate after itself");
    m.addItem (3, musical ? "Make linear (seconds)" : "Make musical (bars and beats)");
    if (! Project::isNoteItem (item))
    {
        m.addSubMenu ("Generator", generators);
        m.addItem (14, "Bake generator to points", ItemTime::isGenerator (item));
    }
    m.addItem (4, Project::isNoteItem (item) ? "Open in piano roll" : "Open in item editor");
    m.addSeparator();
    m.addItem (2, "Delete item");

    juce::Component::SafePointer<LaneTrack> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safeThis, item] (int result) mutable
    {
        if (safeThis == nullptr || result == 0)
            return;

        auto& project = safeThis->ctx.project;
        auto& um = project.getUndoManager();
        auto laneTree = item.getParent();

        if (result >= 10 && result <= 13)
        {
            um.beginNewTransaction ("Generator");
            const char* types[] = { "", "lfo", "random", "formula" };
            project.setGenerator (item, types[result - 10]);
            safeThis->ctx.selection.selectItem (item);
            return;
        }
        if (result == 14)
        {
            um.beginNewTransaction ("Bake generator");
            const int n = project.bakeGenerator (item);
            safeThis->ctx.showStatus ("Baked to " + juce::String (n) + " points.");
            return;
        }
        if (result == 3)
        {
            um.beginNewTransaction ("Change time base");
            project.setTimebase (item, ! ItemTime (item).isMusical(), 120.0);
            return;
        }
        if (result == 4)
        {
            if (safeThis->ctx.openItemEditor) safeThis->ctx.openItemEditor (item);
            return;
        }

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
    const ItemTime it (item);
    const double absTime = it.absolute ((double) point[IDs::time]);
    const float value = (float) (double) point[IDs::value];

    auto* window = new juce::AlertWindow ("Point", it.isMusical() ? "Time: absolute (m:ss.mmm or seconds) or bar|beat, e.g. 3|2.5. "
                                                                    "Value in the plugin's own units."
                                                                  : "Time is absolute (m:ss.mmm or seconds). "
                                                                    "Value in the plugin's own units.",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("time", it.isMusical() ? it.barBeat ((double) point[IDs::time]) : TimeFormat::format (absTime), "Time");
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

        const ItemTime pt (point.getParent());
        const auto timeText = window->getTextEditorContents ("time");
        const double barBeat = pt.isMusical() ? pt.parseBarBeat (timeText) : -1.0;
        const double local = barBeat >= 0.0 ? barBeat : pt.localAt (TimeFormat::parse (timeText));
        const double t = juce::jlimit (0.0, pt.localLength(), local);

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
