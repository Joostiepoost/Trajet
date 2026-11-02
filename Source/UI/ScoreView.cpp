#include "ScoreView.h"

juce::Array<juce::ValueTree> ScoreView::clipboard;
bool ScoreView::lastCopyWasPoints = false;

namespace
{
    const juce::Colour headerColour (0xff2a2f37);
    const juce::Colour accent       (0xffe8a33d);

    double rulerStep (double pixelsPerSecond)
    {
        for (double step : { 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 15.0, 30.0, 60.0, 120.0, 300.0 })
            if (step * pixelsPerSecond >= 70.0)
                return step;
        return 600.0;
    }
}

//==============================================================================
class ScoreView::Ruler : public juce::Component
{
public:
    explicit Ruler (ScoreView& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        auto& tv = owner.timeView;
        g.fillAll (headerColour);

        // Left corner: which state the plugin is in.
        const bool patch = owner.controller.isPatchMode();
        g.setColour (patch ? juce::Colour (0xffc58b4a) : juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (patch ? "PATCH" : "SCORE", 12, 0, headerWidth - 24, getHeight(), juce::Justification::centredLeft);

        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (headerWidth, 0, getWidth() - headerWidth, getHeight());
        g.setOrigin (headerWidth, 0);

        const double step = rulerStep (tv.getPixelsPerSecond());
        const int decimals = step >= 1.0 ? 0 : (step >= 0.1 ? 1 : 2);
        const float w = (float) (getWidth() - headerWidth);
        g.setFont (juce::FontOptions (11.0f));

        for (double t = std::floor (tv.getOffset() / step) * step; tv.toX (t) < w; t += step)
        {
            const float x = tv.toX (t);
            g.setColour (juce::Colours::white.withAlpha (0.35f));
            g.drawVerticalLine ((int) x, (float) getHeight() - 8.0f, (float) getHeight());
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.drawText (TimeFormat::format (t, decimals), (int) x + 3, 2, 80, getHeight() - 8, juce::Justification::centredLeft);

            // Minor ticks.
            g.setColour (juce::Colours::white.withAlpha (0.18f));
            for (int i = 1; i < 5; ++i)
                g.drawVerticalLine ((int) tv.toX (t + step * i / 5.0), (float) getHeight() - 4.0f, (float) getHeight());
        }

        // Playhead.
        const float px = tv.toX (owner.host.getEngine().getPositionSeconds());
        juce::Path tri;
        tri.addTriangle (px - 6.0f, 2.0f, px + 6.0f, 2.0f, px, 12.0f);
        g.setColour (accent);
        g.fillPath (tri);
        g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override  { locate (e); }
    void mouseDrag (const juce::MouseEvent& e) override  { locate (e); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        owner.handleWheel (e, w, *this);
    }

private:
    void locate (const juce::MouseEvent& e)
    {
        if (e.x < headerWidth)
            return;
        auto t = owner.timeView.toTime ((float) (e.x - headerWidth));
        if (! e.mods.isCommandDown())
            t = owner.snap.snap (t);
        owner.controller.locate (t);
        owner.repaint();
    }

    ScoreView& owner;
};

//==============================================================================
class ScoreView::LaneHeader : public juce::Component
{
public:
    LaneHeader (ScoreView& o, LaneRow& r, juce::ValueTree laneTree) : owner (o), row (r), lane (laneTree)
    {
        nameLabel.setText (lane[IDs::name].toString(), juce::dontSendNotification);
        nameLabel.setFont (juce::FontOptions (13.5f, juce::Font::bold));
        nameLabel.setEditable (false, true, false);   // double-click to rename
        nameLabel.setTooltip ("Double-click to rename");
        nameLabel.onTextChange = [this]
        {
            auto& um = owner.project.getUndoManager();
            um.beginNewTransaction ("Rename lane");
            lane.setProperty (IDs::name, nameLabel.getText().trim().isEmpty() ? lane[IDs::paramName].toString()
                                                                               : nameLabel.getText().trim(), &um);
        };
        nameLabel.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (nameLabel);

        for (auto* b : { &muteButton, &soloButton })
        {
            b->setClickingTogglesState (true);
            addAndMakeVisible (b);
        }
        muteButton.setTooltip ("Mute: this lane plays its patch value");
        soloButton.setTooltip ("Solo: only soloed lanes play their automation");
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8f3b3b));
        soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb08a2e));
        muteButton.onClick = [this] { setFlag (IDs::mute, muteButton.getToggleState(), "Mute lane"); };
        soloButton.onClick = [this] { setFlag (IDs::solo, soloButton.getToggleState(), "Solo lane"); };
        refresh();
    }

    void refresh()
    {
        nameLabel.setText (lane[IDs::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) lane[IDs::mute], juce::dontSendNotification);
        soloButton.setToggleState ((bool) lane[IDs::solo], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool active = owner.selection.getActiveLane() == lane;
        g.fillAll (active ? headerColour.brighter (0.12f) : headerColour);
        g.setColour (Project::laneColour (lane));
        g.fillRect (0, 0, active ? 7 : 4, getHeight());

        if (getHeight() >= 40)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (11.5f));
            juce::String text;
            const int index = owner.controller.resolveParameterIndex (lane);
            if (auto* plugin = owner.host.getPlugin(); plugin != nullptr && index >= 0)
            {
                auto* p = plugin->getParameters()[index];
                text = p->getCurrentValueAsText();
                if (p->getLabel().isNotEmpty() && ! text.endsWith (p->getLabel())) text << " " << p->getLabel();
            }
            else
            {
                g.setColour (juce::Colours::orange.withAlpha (0.8f));
                text = "parameter not found";
            }
            g.drawText (text, 12, 26, getWidth() - 70, 16, juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        nameLabel.setBounds (8, 4, getWidth() - 66, 22);
        soloButton.setBounds (getWidth() - 28, 6, 22, 18);
        muteButton.setBounds (getWidth() - 52, 6, 22, 18);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        setMouseCursor (e.y >= getHeight() - 5 ? juce::MouseCursor::UpDownResizeCursor
                                              : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragMode = none;
        owner.selection.setActiveLane (lane);
        if (e.mods.isPopupMenu()) { showMenu(); return; }

        if (e.y >= getHeight() - 5)
        {
            dragMode = resizing;
            startHeight = getHeight();
            owner.project.getUndoManager().beginNewTransaction ("Lane height");
        }
        else
        {
            dragMode = maybeReorder;
        }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragMode == resizing)
        {
            const int h = juce::jlimit (32, 400, startHeight + e.getDistanceFromDragStartY());
            lane.setProperty (IDs::height, h, &owner.project.getUndoManager());
        }
        else if (dragMode == maybeReorder && std::abs (e.getDistanceFromDragStartY()) > 5)
        {
            dragMode = reordering;
        }

        if (dragMode == reordering)
            owner.laneDragged (row, e.getEventRelativeTo (&owner.rowHolder).y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragMode == reordering)
            owner.laneDragEnded (row);
        dragMode = none;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y < 28)
            nameLabel.showEditor();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        owner.handleWheel (e, w, *this);
    }

private:
    void setFlag (const juce::Identifier& id, bool value, const juce::String& action)
    {
        auto& um = owner.project.getUndoManager();
        um.beginNewTransaction (action);
        lane.setProperty (id, value, &um);
    }

    void showMenu()
    {
        juce::PopupMenu colours;
        for (int i = 0; i < Project::paletteSize; ++i)
            colours.addColouredItem (100 + i, "Colour " + juce::String (i + 1), Project::paletteColour (i),
                                     true, (int) lane[IDs::colour] == i);

        juce::PopupMenu m;
        m.addItem (1, "Rename...");
        m.addSubMenu ("Colour", colours);
        m.addSeparator();
        m.addItem (2, "Remove lane");

        juce::Component::SafePointer<LaneHeader> safeThis (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0)
                return;
            auto& um = safeThis->owner.project.getUndoManager();
            if (result == 1)
            {
                safeThis->nameLabel.showEditor();
            }
            else if (result == 2)
            {
                um.beginNewTransaction ("Remove lane");
                safeThis->owner.project.removeLane (safeThis->lane);
            }
            else if (result >= 100)
            {
                um.beginNewTransaction ("Lane colour");
                safeThis->lane.setProperty (IDs::colour, result - 100, &um);
            }
        });
    }

    enum DragMode { none, maybeReorder, reordering, resizing };

    ScoreView& owner;
    LaneRow& row;
    juce::ValueTree lane;
    juce::Label nameLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    DragMode dragMode = none;
    int startHeight = 0;
};

//==============================================================================
class ScoreView::LaneRow : public juce::Component
{
public:
    LaneRow (ScoreView& o, juce::ValueTree laneTree)
        : lane (laneTree), header (o, *this, laneTree), track (o.trackContext, laneTree)
    {
        addAndMakeVisible (header);
        addAndMakeVisible (track);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header.setBounds (r.removeFromLeft (headerWidth));
        track.setBounds (r);
    }

    int preferredHeight() const     { return juce::jlimit (32, 400, (int) lane.getProperty (IDs::height, 72)); }

    juce::ValueTree lane;
    LaneHeader header;
    LaneTrack track;
};

//==============================================================================
ScoreView::ScoreView (Project& p, PluginHost& h, AutomationController& c, SnapSettings& s)
    : project (p), host (h), controller (c), snap (s),
      trackContext (p, h, c, timeView, s, selection)
{
    trackContext.showStatus  = [this] (const juce::String& text) { showStatus (text); };
    trackContext.handleWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w, juce::Component& src) { handleWheel (e, w, src); };
    trackContext.rubberBand  = [this] (juce::Rectangle<int> area, bool add, bool finished) { rubberBand (area, add, finished); };
    trackContext.openItemEditor = [this] (juce::ValueTree item) { if (onOpenItemEditor) onOpenItemEditor (item); };
    trackContext.laneAt      = [this] (juce::Point<int> screenPos)
    {
        for (auto* row : rows)
            if (row->getScreenBounds().contains (screenPos))
                return row->lane;
        return juce::ValueTree();
    };

    ruler = std::make_unique<Ruler> (*this);
    addAndMakeVisible (*ruler);

    viewport.setViewedComponent (&rowHolder, false);
    viewport.setScrollBarsShown (true, false);
    viewport.setScrollOnDragMode (juce::Viewport::ScrollOnDragMode::never);
    addAndMakeVisible (viewport);

    hScroll.setAutoHide (false);
    hScroll.addListener (this);
    addAndMakeVisible (hScroll);

    emptyHint.setText ("No lanes yet. Click \"+ Lane\" to add one for a parameter.", juce::dontSendNotification);
    emptyHint.setJustificationType (juce::Justification::centred);
    emptyHint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    emptyHint.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (emptyHint);

    timeView.addChangeListener (this);
    selection.addChangeListener (this);
    snap.addChangeListener (this);
    project.getState().addListener (this);

    timeView.setLength (project.getLengthSeconds());
    rebuildRows();
    startTimerHz (30);
}

ScoreView::~ScoreView()
{
    stopTimer();
    project.getState().removeListener (this);
    snap.removeChangeListener (this);
    selection.removeChangeListener (this);
    timeView.removeChangeListener (this);
    hScroll.removeListener (this);
}

//==============================================================================
void ScoreView::rebuildRows()
{
    rebuildPending = false;
    const auto scroll = viewport.getViewPosition();
    rows.clear();

    for (auto lane : project.getLanes())
        if (lane.hasType (IDs::LANE))
            rowHolder.addAndMakeVisible (rows.add (new LaneRow (*this, lane)));

    emptyHint.setVisible (rows.isEmpty());
    selection.prune();
    layoutRows();
    viewport.setViewPosition (scroll);
    repaint();
}

void ScoreView::layoutRows()
{
    const int width = viewport.getMaximumVisibleWidth();
    int y = 0;
    for (auto* row : rows)
    {
        row->setBounds (0, y, width, row->preferredHeight());
        y += row->getHeight();
    }
    rowHolder.setSize (width, juce::jmax (1, y));
}

void ScoreView::resized()
{
    auto r = getLocalBounds();
    ruler->setBounds (r.removeFromTop (rulerHeight));
    auto bottom = r.removeFromBottom (14);
    hScroll.setBounds (bottom.withTrimmedLeft (headerWidth).withTrimmedRight (viewport.getScrollBarThickness()));
    viewport.setBounds (r);
    emptyHint.setBounds (r);

    timeView.setViewWidth (trackWidth());
    layoutRows();
}

void ScoreView::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));
}

void ScoreView::paintOverChildren (juce::Graphics& g)
{
    if (! rubberArea.isEmpty())
    {
        g.setColour (accent.withAlpha (0.10f));
        g.fillRect (rubberArea);
        g.setColour (accent.withAlpha (0.7f));
        g.drawRect (rubberArea, 1);
    }

    if (dropIndicatorY >= 0)
    {
        const int y = viewport.getY() + dropIndicatorY - viewport.getViewPositionY();
        g.setColour (accent);
        g.fillRect (0, y - 1, getWidth(), 3);
    }
}

//==============================================================================
void ScoreView::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel, juce::Component& source)
{
    if (handlingWheel)
        return;
    const juce::ScopedValueSetter<bool> guard (handlingWheel, true);

    if (e.mods.isCommandDown())
    {
        // Zoom around the mouse (trackpad momentum after the gesture is ignored).
        if (wheel.isInertial)
            return;
        const float x = (float) e.getEventRelativeTo (this).x - (float) headerWidth;
        const double factor = juce::jlimit (0.5, 2.0, std::pow (2.0, (double) wheel.deltaY * 1.5));
        timeView.zoomAround (factor, juce::jmax (0.0f, x));
        return;
    }

    if (e.mods.isAltDown())
    {
        // Resize the lane under the mouse.
        for (auto* row : rows)
            if (row == &source || row->isParentOf (&source))
            {
                const int h = juce::jlimit (32, 400, row->preferredHeight() + juce::roundToInt (wheel.deltaY * 60.0f));
                row->lane.setProperty (IDs::height, h, nullptr);
                return;
            }
        return;
    }

    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float delta = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        timeView.scrollBy (-(double) delta * timeView.visibleSeconds() * 0.25);
        return;
    }

    // Vertical: move the lanes directly (never hand the event back to the viewport).
    const int maxY = juce::jmax (0, rowHolder.getHeight() - viewport.getViewHeight());
    if (maxY > 0)
    {
        const int dy = juce::roundToInt (wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 400.0f);
        viewport.setViewPosition (0, juce::jlimit (0, maxY, viewport.getViewPositionY() - dy));
    }
}

void ScoreView::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    handleWheel (e, wheel, *this);
}

//==============================================================================
int ScoreView::dropIndexFor (int yInHolder) const
{
    for (int i = 0; i < rows.size(); ++i)
        if (yInHolder < rows[i]->getY() + rows[i]->getHeight() / 2)
            return i;
    return rows.size();
}

void ScoreView::laneDragged (LaneRow&, int yInHolder)
{
    const int index = dropIndexFor (yInHolder);
    dropIndicatorY = index < rows.size() ? rows[index]->getY() : rowHolder.getHeight();
    repaint();
}

void ScoreView::laneDragEnded (LaneRow& row)
{
    const int target = dropIndexFor (dropIndicatorY >= 0 ? dropIndicatorY : row.getY());
    dropIndicatorY = -1;
    repaint();

    auto lanes = project.getLanes();
    const int from = lanes.indexOf (row.lane);
    int to = target > from ? target - 1 : target;
    to = juce::jlimit (0, lanes.getNumChildren() - 1, to);

    if (from >= 0 && from != to)
    {
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Move lane");
        lanes.moveChild (from, to, &um);
    }
}

//==============================================================================
// Rubber band across lanes

void ScoreView::rubberBand (juce::Rectangle<int> screenArea, bool add, bool finished)
{
    if (rubberArea.isEmpty())
        selectionBeforeRubberBand = add ? selection.getPoints() : juce::Array<juce::ValueTree>();

    juce::Array<juce::ValueTree> found = selectionBeforeRubberBand;
    for (auto* row : rows)
    {
        auto local = row->track.getLocalArea (nullptr, screenArea).toFloat();
        if (local.intersects (row->track.getLocalBounds().toFloat()))
            row->track.collectPointsIn (local, found);
    }

    selection.clear();
    for (auto& p : found)
        selection.selectPoint (p, true);

    rubberArea = finished ? juce::Rectangle<int>() : getLocalArea (nullptr, screenArea);
    repaint();
}

//==============================================================================
// Editing commands

void ScoreView::nudge (int direction, bool vertical, bool large)
{
    auto points = selection.getPoints();
    if (points.isEmpty())
        return;

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Nudge points");
    const double dt = vertical ? 0.0 : direction * snap.getGrid() * (large ? 10.0 : 1.0);
    const float dv = vertical ? (float) direction * (large ? 0.1f : 0.01f) : 0.0f;
    PointOps::nudge (points, dt, dv, um);
}

void ScoreView::applyPointOp (const juce::String& name)
{
    auto points = selection.getPoints();
    if (points.size() < 2)
    {
        showStatus ("Select two or more points first.");
        return;
    }

    auto& um = project.getUndoManager();
    um.beginNewTransaction (name.substring (0, 1).toUpperCase() + name.substring (1) + " points");

    if (name == "smooth")       PointOps::smooth (points, um);
    else if (name == "reverse") PointOps::reverse (points, um);
    else if (name == "invert")  PointOps::invert (points, um);
    else if (name == "thin")
    {
        const int removed = PointOps::thin (points, um);
        selection.prune();
        showStatus (juce::String (removed) + " point(s) removed.");
    }
}

void ScoreView::selectAllInActiveLane()
{
    auto lane = selection.getActiveLane();
    if (! lane.isValid())
        return;

    selection.clear();
    for (auto item : lane)
        for (auto p : item)
            if (p.hasType (IDs::POINT))
                selection.selectPoint (p, true);
    selection.setActiveLane (lane);
}

void ScoreView::deleteSelection()
{
    auto& um = project.getUndoManager();
    auto points = selection.getPoints();
    auto items = selection.getItems();

    if (! points.isEmpty())
    {
        um.beginNewTransaction ("Delete points");
        for (auto p : points)
            if (p.getParent().isValid())
                p.getParent().removeChild (p, &um);
    }
    else if (! items.isEmpty())
    {
        um.beginNewTransaction ("Delete items");
        for (auto item : items)
            if (item.getParent().isValid())
                item.getParent().removeChild (item, &um);
    }
    selection.clear();
}

void ScoreView::copySelection()
{
    if (! selection.getPoints().isEmpty())
    {
        PointOps::copy (selection.getPoints());
        lastCopyWasPoints = true;
        showStatus (juce::String (selection.getPoints().size()) + " point(s) copied.");
        return;
    }

    lastCopyWasPoints = false;
    clipboard.clearQuick();
    for (auto& item : selection.getItems())
    {
        auto copy = item.createCopy();
        copy.setProperty ("sourceLane", item.getParent()[IDs::laneId], nullptr);
        clipboard.add (copy);
    }
    if (! clipboard.isEmpty())
        showStatus (juce::String (clipboard.size()) + (clipboard.size() == 1 ? " item copied." : " items copied."));
}

void ScoreView::cutSelection()
{
    if (! selection.getPoints().isEmpty())
    {
        copySelection();
        deleteSelection();
        return;
    }

    copySelection();
    if (! clipboard.isEmpty())
    {
        selection.clear();
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Cut items");
        for (auto& c : clipboard)
            for (auto lane : project.getLanes())
                if (lane[IDs::laneId] == c["sourceLane"])
                    for (int i = lane.getNumChildren(); --i >= 0;)
                    {
                        auto item = lane.getChild (i);
                        if (item.hasType (IDs::ITEM) && std::abs ((double) item[IDs::start] - (double) c[IDs::start]) < 1.0e-9)
                            lane.removeChild (i, &um);
                    }
    }
}

void ScoreView::paste()
{
    if (lastCopyWasPoints && PointOps::hasCopiedPoints())
    {
        // Points go into the item under the playhead, in the selected or active lane.
        const double at = snap.snap (host.getEngine().getPositionSeconds());
        auto lane = selection.getItems().size() == 1 ? selection.getItems().getFirst().getParent() : selection.getActiveLane();
        juce::ValueTree target;
        for (auto item : lane)
            if (item.hasType (IDs::ITEM) && at >= (double) item[IDs::start] - 1.0e-9
                && at <= (double) item[IDs::start] + (double) item[IDs::length] + 1.0e-9)
                target = item;

        if (! target.isValid())
        {
            showStatus ("To paste points, put the playhead inside an item of the lane (click in it).");
            return;
        }

        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Paste points");
        auto added = PointOps::paste (target, at, um);
        selection.clear();
        for (auto& p : added) selection.selectPoint (p, true);
        return;
    }

    if (clipboard.isEmpty())
        return;

    auto lanes = project.getLanes();
    auto laneIndexOf = [&lanes] (const juce::var& id)
    {
        for (int i = 0; i < lanes.getNumChildren(); ++i)
            if (lanes.getChild (i)[IDs::laneId] == id)
                return i;
        return -1;
    };

    // Paste at the playhead, keeping the copied items' spacing in time...
    double earliest = 1.0e12;
    int topSource = 1 << 30;
    for (auto& c : clipboard)
    {
        earliest = juce::jmin (earliest, (double) c[IDs::start]);
        const int li = laneIndexOf (c["sourceLane"]);
        if (li >= 0) topSource = juce::jmin (topSource, li);
    }

    // ...and in lanes: the topmost copied lane lands on the lane you clicked last.
    auto active = selection.getActiveLane();
    const int laneShift = (active.isValid() && topSource < (1 << 30)) ? lanes.indexOf (active) - topSource : 0;

    const double at = snap.snap (host.getEngine().getPositionSeconds());
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Paste");
    int skipped = 0;
    juce::Array<juce::ValueTree> pasted;

    for (auto& c : clipboard)
    {
        const int source = laneIndexOf (c["sourceLane"]);
        const int target = source >= 0 ? source + laneShift : lanes.indexOf (active);
        auto lane = lanes.getChild (target);
        if (! lane.isValid()) { ++skipped; continue; }

        const double start = at + ((double) c[IDs::start] - earliest);
        const double length = c[IDs::length];
        if (! project.isFree (lane, start, length)) { ++skipped; continue; }

        auto item = c.createCopy();
        item.removeProperty ("sourceLane", nullptr);
        item.setProperty (IDs::start, start, nullptr);
        lane.appendChild (item, &um);
        pasted.add (item);
    }

    selection.clear();
    for (auto& item : pasted)
        selection.selectItem (item, true);
    if (active.isValid())
        selection.setActiveLane (active);

    if (skipped > 0)
        showStatus (juce::String (skipped) + " item(s) not pasted: no free room there.");
}

void ScoreView::duplicateSelection()
{
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Duplicate");
    juce::Array<juce::ValueTree> copies;
    int skipped = 0;

    for (auto item : selection.getItems())
    {
        auto lane = item.getParent();
        const double start = (double) item[IDs::start] + (double) item[IDs::length];
        const double length = item[IDs::length];
        if (! lane.isValid() || ! project.isFree (lane, start, length)) { ++skipped; continue; }

        auto copy = item.createCopy();
        copy.setProperty (IDs::start, start, nullptr);
        lane.appendChild (copy, &um);
        copies.add (copy);
    }

    selection.clear();
    for (auto& c : copies)
        selection.selectItem (c, true);

    if (skipped > 0)
        showStatus (juce::String (skipped) + " item(s) not duplicated: no room right after them.");
}

//==============================================================================
void ScoreView::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == &timeView)
    {
        // Keep the horizontal scrollbar in step with zoom/scroll.
        hScroll.setRangeLimits (0.0, timeView.getLength() + timeView.visibleSeconds() * 0.02, juce::dontSendNotification);
        hScroll.setCurrentRange (timeView.getOffset(), timeView.visibleSeconds(), juce::dontSendNotification);
    }
    repaint();
    for (auto* row : rows) row->track.repaint();
}

void ScoreView::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    timeView.setOffset (newRangeStart);
}

void ScoreView::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (IDs::PROJECT) && property == IDs::lengthSeconds)
        timeView.setLength (project.getLengthSeconds());

    if (tree.hasType (IDs::LANE))
    {
        for (auto* row : rows)
            if (row->lane == tree)
                row->header.refresh();

        if (property == IDs::height)
            layoutRows();
    }

    for (auto* row : rows) row->track.repaint();
}

void ScoreView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT))
        rebuildPending = true;
    else
        for (auto* row : rows) row->track.repaint();
}

void ScoreView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT))
        rebuildPending = true;   // rebuilt on the timer, after the current mouse event
    else
    {
        selection.prune();
        for (auto* row : rows) row->track.repaint();
    }
}

void ScoreView::valueTreeChildOrderChanged (juce::ValueTree& parent, int, int)
{
    if (parent.hasType (IDs::LANES))
        rebuildPending = true;
}

void ScoreView::timerCallback()
{
    if (rebuildPending)
        rebuildRows();

    auto& engine = host.getEngine();
    const double pos = engine.getPositionSeconds();

    if (! juce::approximatelyEqual (pos, lastPosition) || engine.isPlaying())
    {
        lastPosition = pos;
        if (engine.isPlaying())
            timeView.keepVisible (pos);
        ruler->repaint();
        for (auto* row : rows)
        {
            row->track.repaint();
            row->header.repaint();
        }
    }
}
