#include "LanesView.h"

namespace
{
    const juce::Colour laneBackground (0xff22262c);
    const juce::Colour headerColour   (0xff2a2f37);
    const juce::Colour gridColour     (0x14ffffff);
    const juce::Colour curveColour    (0xff5b9bd5);
    const juce::Colour playheadColour (0xffe8a33d);

    juce::String formatTime (double seconds)
    {
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (minutes) + ":" + juce::String (rest, 3).paddedLeft ('0', 6);
    }

    /** A good grid step (in seconds) so lines are at least ~60 px apart. */
    double gridStep (double pixelsPerSecond)
    {
        for (double step : { 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
            if (step * pixelsPerSecond >= 60.0)
                return step;
        return 600.0;
    }
}

//==============================================================================
// Shared time <-> x mapping: the timeline fills the width right of the header.
struct TimeMapping
{
    double length;
    float x0, width;

    float toX (double seconds) const     { return x0 + (float) (seconds / length) * width; }
    double toSeconds (float x) const     { return juce::jlimit (0.0, length, (double) ((x - x0) / width) * length); }
};

static TimeMapping makeMapping (const juce::Component& c, double length)
{
    const float x0 = (float) LanesView::headerWidth;
    return { juce::jmax (1.0, length), x0, juce::jmax (10.0f, (float) c.getWidth() - x0 - 12.0f) };
}

//==============================================================================
class LanesView::Ruler : public juce::Component
{
public:
    Ruler (Project& p, PluginHost& h, AutomationController& c) : project (p), host (h), controller (c) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (headerColour);
        auto map = makeMapping (*this, project.getLengthSeconds());
        const double step = gridStep (map.width / map.length);

        g.setFont (juce::FontOptions (11.0f));
        for (double t = 0.0; t <= map.length + 1.0e-6; t += step)
        {
            const float x = map.toX (t);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, (float) getHeight() - 8.0f, (float) getHeight());
            if (x + 40.0f > (float) getWidth())
                continue;
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.drawText (formatTime (t).dropLastCharacters (step >= 1.0 ? 4 : 2), (int) x + 3, 2, 70, getHeight() - 6,
                        juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.drawText (controller.isPatchMode() ? "PATCH" : "SCORE", 12, 0, headerWidth - 24, getHeight(),
                    juce::Justification::centredLeft);

        const float px = map.toX (host.getEngine().getPositionSeconds());
        g.setColour (playheadColour);
        juce::Path tri;
        tri.addTriangle (px - 6.0f, 2.0f, px + 6.0f, 2.0f, px, 12.0f);
        g.fillPath (tri);
        g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override  { locateTo (e); }
    void mouseDrag (const juce::MouseEvent& e) override  { locateTo (e); }

private:
    void locateTo (const juce::MouseEvent& e)
    {
        if (e.x < headerWidth)
            return;
        controller.locate (makeMapping (*this, project.getLengthSeconds()).toSeconds ((float) e.x));
        if (auto* parent = getParentComponent())
            parent->repaint();
    }

    Project& project;
    PluginHost& host;
    AutomationController& controller;
};

//==============================================================================
class LanesView::LaneStrip : public juce::Component
{
public:
    LaneStrip (Project& p, PluginHost& h, AutomationController& c, juce::ValueTree laneTree)
        : project (p), host (h), controller (c), lane (laneTree)
    {
        removeButton.setButtonText ("x");
        removeButton.setTooltip ("Remove this lane");
        removeButton.onClick = [this]
        {
            project.getUndoManager().beginNewTransaction ("Remove lane");
            project.removeLane (lane);   // this strip is deleted afterwards
        };
        addAndMakeVisible (removeButton);
    }

    juce::ValueTree getLane() const  { return lane; }

    void resized() override
    {
        removeButton.setBounds (headerWidth - 30, 8, 22, 22);
    }

    void paint (juce::Graphics& g) override
    {
        auto map = makeMapping (*this, project.getLengthSeconds());
        auto bounds = getLocalBounds();
        auto curveArea = curveBounds();

        // Header: lane name and current value.
        g.setColour (headerColour);
        g.fillRect (bounds.removeFromLeft (headerWidth));
        g.setColour (juce::Colours::white);
        g.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        g.drawText (lane[IDs::name].toString(), 12, 6, headerWidth - 46, 20, juce::Justification::centredLeft);

        if (auto* param = getParameter())
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (12.0f));
            g.drawText (param->getCurrentValueAsText(), 12, 28, headerWidth - 24, 18, juce::Justification::centredLeft);
        }
        else
        {
            g.setColour (juce::Colours::orange.withAlpha (0.8f));
            g.setFont (juce::FontOptions (11.5f));
            g.drawText ("parameter not found", 12, 28, headerWidth - 24, 18, juce::Justification::centredLeft);
        }

        // Lane area with a light time grid.
        g.setColour (laneBackground);
        g.fillRect (bounds);
        const double step = gridStep (map.width / map.length);
        g.setColour (gridColour);
        for (double t = 0.0; t <= map.length + 1.0e-6; t += step)
            g.drawVerticalLine ((int) map.toX (t), (float) curveArea.getY(), (float) curveArea.getBottom());

        // Items and their curves.
        for (const auto& item : lane)
        {
            if (! item.hasType (IDs::ITEM))
                continue;

            const double start = item[IDs::start];
            const double length = item[IDs::length];
            auto itemRect = juce::Rectangle<float> (map.toX (start), (float) curveArea.getY() - 3.0f,
                                                    map.toX (start + length) - map.toX (start),
                                                    (float) curveArea.getHeight() + 6.0f);
            g.setColour (curveColour.withAlpha (0.08f));
            g.fillRoundedRectangle (itemRect, 4.0f);
            g.setColour (curveColour.withAlpha (0.4f));
            g.drawRoundedRectangle (itemRect, 4.0f, 1.0f);

            auto points = Project::getPoints (item);
            if (points.empty())
                continue;

            // Draw the curve by evaluating it at every pixel: exactly what the engine plays.
            juce::Path curve;
            const float xa = itemRect.getX(), xb = itemRect.getRight();
            bool first = true;
            for (float x = xa; x <= xb; x += 1.0f)
            {
                const double t = map.toSeconds (x) - start;
                const float y = valueToY (Automation::valueAt (points, t));
                if (first) curve.startNewSubPath (x, y); else curve.lineTo (x, y);
                first = false;
            }
            g.setColour (curveColour);
            g.strokePath (curve, juce::PathStrokeType (1.6f));

            for (const auto& pt : points)
            {
                const float px = map.toX (start + pt.time), py = valueToY (pt.value);
                g.setColour (juce::Colours::white);
                g.fillEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f);
                g.setColour (curveColour);
                g.drawEllipse (px - 4.0f, py - 4.0f, 8.0f, 8.0f, 1.5f);
            }
        }

        // While dragging: show the value as the plugin names it.
        if (dragPoint.isValid())
            if (auto* param = getParameter())
            {
                const auto text = param->getText ((float) (double) dragPoint[IDs::value], 32);
                const float px = map.toX (itemStart() + (double) dragPoint[IDs::time]);
                const float py = valueToY ((float) (double) dragPoint[IDs::value]);
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.fillRoundedRectangle (px + 8.0f, py - 22.0f, 90.0f, 18.0f, 3.0f);
                g.setColour (juce::Colours::white);
                g.setFont (juce::FontOptions (11.5f));
                g.drawText (text, (int) px + 12, (int) py - 22, 84, 18, juce::Justification::centredLeft);
            }

        // Playhead.
        g.setColour (playheadColour.withAlpha (0.9f));
        g.drawVerticalLine ((int) map.toX (host.getEngine().getPositionSeconds()), 0.0f, (float) getHeight());

        g.setColour (juce::Colours::black.withAlpha (0.4f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
    }

    //==============================================================================
    void mouseDown (const juce::MouseEvent& e) override
    {
        if (e.x < headerWidth)
            return;

        auto item = firstItem();
        if (! item.isValid())
            return;

        auto hit = pointAt (e.position);

        if (e.mods.isPopupMenu())
        {
            if (hit.isValid())
                showShapeMenu (hit);
            return;
        }

        if (hit.isValid())
        {
            project.getUndoManager().beginNewTransaction ("Move point");
            dragPoint = hit;
        }
        else
        {
            project.getUndoManager().beginNewTransaction ("Add point");
            dragPoint = project.addPoint (item, timeAt (e.position.x), yToValue (e.position.y));
        }
        repaint();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (! dragPoint.isValid())
            return;

        auto& um = project.getUndoManager();
        dragPoint.setProperty (IDs::time, timeAt (e.position.x), &um);
        dragPoint.setProperty (IDs::value, yToValue (e.position.y), &um);
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragPoint = {};
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto hit = pointAt (e.position);
        if (hit.isValid())
        {
            project.getUndoManager().beginNewTransaction ("Delete point");
            hit.getParent().removeChild (hit, &project.getUndoManager());
            dragPoint = {};
            repaint();
        }
    }

private:
    juce::AudioProcessorParameter* getParameter() const
    {
        auto* plugin = host.getPlugin();
        const int index = controller.resolveParameterIndex (lane);
        return (plugin != nullptr && index >= 0) ? plugin->getParameters()[index] : nullptr;
    }

    juce::ValueTree firstItem() const       { return lane.getChildWithName (IDs::ITEM); }
    double itemStart() const                { return firstItem()[IDs::start]; }

    juce::Rectangle<int> curveBounds() const
    {
        return getLocalBounds().withTrimmedLeft (headerWidth).reduced (0, 8);
    }

    float valueToY (float v) const
    {
        auto r = curveBounds().toFloat();
        return r.getBottom() - v * r.getHeight();
    }

    float yToValue (float y) const
    {
        auto r = curveBounds().toFloat();
        return juce::jlimit (0.0f, 1.0f, (r.getBottom() - y) / r.getHeight());
    }

    double timeAt (float x) const
    {
        auto item = firstItem();
        const double start = item[IDs::start], length = item[IDs::length];
        const double t = makeMapping (*this, project.getLengthSeconds()).toSeconds (x) - start;
        return juce::jlimit (0.0, length, std::round (t * 1000.0) / 1000.0);   // millisecond grid
    }

    juce::ValueTree pointAt (juce::Point<float> pos) const
    {
        auto item = firstItem();
        auto map = makeMapping (*this, project.getLengthSeconds());
        const double start = item[IDs::start];

        for (auto p : item)
        {
            const float px = map.toX (start + (double) p[IDs::time]);
            const float py = valueToY ((float) (double) p[IDs::value]);
            if (pos.getDistanceFrom ({ px, py }) <= 7.0f)
                return p;
        }
        return {};
    }

    void showShapeMenu (juce::ValueTree point)
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
        m.addItem (9, "Delete point");

        juce::Component::SafePointer<LaneStrip> safeThis (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safeThis, point] (int result) mutable
        {
            if (safeThis == nullptr || result == 0)
                return;

            auto& um = safeThis->project.getUndoManager();
            um.beginNewTransaction ("Change segment shape");

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
                case 9: um.setCurrentTransactionName ("Delete point");
                        point.getParent().removeChild (point, &um); break;
                default: break;
            }
            safeThis->repaint();
        });
    }

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    juce::ValueTree lane, dragPoint;
    juce::TextButton removeButton;
};

//==============================================================================
LanesView::LanesView (Project& p, PluginHost& h, AutomationController& c)
    : project (p), host (h), controller (c)
{
    ruler = std::make_unique<Ruler> (project, host, controller);
    addAndMakeVisible (*ruler);

    viewport.setViewedComponent (&stripHolder, false);
    viewport.setScrollBarsShown (true, false);
    addAndMakeVisible (viewport);

    emptyHint.setText ("No lanes yet. Double-click a parameter in the list below to add a lane.",
                       juce::dontSendNotification);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    addAndMakeVisible (emptyHint);

    project.getState().addListener (this);
    rebuildStrips();
    startTimerHz (30);
}

LanesView::~LanesView()
{
    stopTimer();
    project.getState().removeListener (this);
}

void LanesView::rebuildStrips()
{
    strips.clear();

    for (auto lane : project.getLanes())
        if (lane.hasType (IDs::LANE))
            stripHolder.addAndMakeVisible (strips.add (new LaneStrip (project, host, controller, lane)));

    emptyHint.setVisible (strips.isEmpty());
    resized();
    repaint();
}

void LanesView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT)) rebuildStrips(); else repaint();
}

void LanesView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT))
    {
        // Rebuild after the current mouse event finishes (a strip may be removing itself).
        juce::Component::SafePointer<LanesView> safeThis (this);
        juce::MessageManager::callAsync ([safeThis] { if (safeThis != nullptr) safeThis->rebuildStrips(); });
    }
    else
    {
        repaint();
    }
}

void LanesView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (IDs::LANES)) rebuildStrips(); else repaint();
}

void LanesView::valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&)
{
    for (auto* s : strips) s->repaint();
    ruler->repaint();
}

void LanesView::timerCallback()
{
    // Move the playhead (and refresh header values) when the position changes.
    const double pos = host.getEngine().getPositionSeconds();
    if (! juce::approximatelyEqual (pos, lastDrawnPosition) || host.getEngine().isPlaying())
    {
        lastDrawnPosition = pos;
        ruler->repaint();
        for (auto* s : strips) s->repaint();
    }
}

void LanesView::paint (juce::Graphics& g)
{
    g.fillAll (laneBackground);
}

void LanesView::resized()
{
    auto area = getLocalBounds();
    ruler->setBounds (area.removeFromTop (rulerHeight));
    viewport.setBounds (area);
    emptyHint.setBounds (area);

    const int width = viewport.getMaximumVisibleWidth();
    stripHolder.setSize (width, juce::jmax (1, strips.size() * laneHeight));
    for (int i = 0; i < strips.size(); ++i)
        strips[i]->setBounds (0, i * laneHeight, width, laneHeight);
}
