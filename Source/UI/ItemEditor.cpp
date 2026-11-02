#include "ItemEditor.h"

namespace
{
    const juce::Colour panelColour (0xff262a31);
    const juce::Colour accent      (0xffe8a33d);
    constexpr int axisWidth = 90, tableWidth = 300, toolbarHeight = 44, rulerHeight = 22;

    juce::String paramText (juce::AudioProcessorParameter* p, float v)
    {
        if (p == nullptr) return juce::String (v, 3);
        auto text = p->getText (v, 32);
        if (p->getLabel().isNotEmpty() && ! text.endsWith (p->getLabel())) text << " " << p->getLabel();
        return text;
    }
}

//==============================================================================
/** Value labels next to the curve, in the plugin's own units. */
class ItemEditor::ValueAxis : public juce::Component
{
public:
    explicit ValueAxis (ItemEditor& o) : owner (o) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panelColour);
        auto* p = owner.parameter();
        g.setFont (juce::FontOptions (11.0f));

        for (float v : { 0.0f, 0.25f, 0.5f, 0.75f, 1.0f })
        {
            const float y = owner.track->valueToY (v) + (float) (owner.track->getY() - getY());
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawHorizontalLine ((int) y, (float) getWidth() - 6.0f, (float) getWidth());
            g.setColour (juce::Colours::white.withAlpha (0.65f));
            g.drawText (paramText (p, v), 4, (int) y - 8, getWidth() - 12, 16, juce::Justification::centredRight);
        }
    }

private:
    ItemEditor& owner;
};

//==============================================================================
ItemEditor::ItemEditor (Project& p, PluginHost& h, AutomationController& c, SnapSettings& s, Selection& sel, juce::ValueTree itemTree)
    : project (p), host (h), controller (c), snap (s), selection (sel), item (itemTree), lane (itemTree.getParent()),
      context (p, h, c, timeView, s, sel)
{
    context.tool = &tool;
    context.showStatus = [this] (const juce::String& text) { hintLabel.setText (text, juce::dontSendNotification); };
    context.handleWheel = [this] (const juce::MouseEvent& e, const juce::MouseWheelDetails& w, juce::Component&)
    {
        if (e.mods.isCommandDown() && ! w.isInertial)
            timeView.zoomAround (juce::jlimit (0.5, 2.0, std::pow (2.0, (double) w.deltaY * 1.5)),
                                 (float) e.getEventRelativeTo (track.get()).x);
        else if (std::abs (w.deltaX) > std::abs (w.deltaY) || e.mods.isShiftDown())
            timeView.scrollBy (-(double) (std::abs (w.deltaX) > std::abs (w.deltaY) ? w.deltaX : w.deltaY)
                               * timeView.visibleSeconds() * 0.25);
    };

    track = std::make_unique<LaneTrack> (context, lane);
    addAndMakeVisible (*track);
    axis = std::make_unique<ValueAxis> (*this);
    addAndMakeVisible (*axis);

    // Tools
    for (auto* b : { &selectButton, &pencilButton, &lineButton, &bendButton })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (1);
        b->setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3b5f8f));
        addAndMakeVisible (b);
    }
    selectButton.setTooltip ("Select (1): click points, drag to move, drag on empty space for a rubber band");
    pencilButton.setTooltip ("Pencil (2): click to add points, drag to draw freehand");
    lineButton.setTooltip ("Line (3): drag a straight ramp");
    bendButton.setTooltip ("Bend (4): drag a segment up or down to curve it");
    selectButton.onClick = [this] { setTool (LaneTrack::Tool::select); };
    pencilButton.onClick = [this] { setTool (LaneTrack::Tool::pencil); };
    lineButton.onClick   = [this] { setTool (LaneTrack::Tool::line); };
    bendButton.onClick   = [this] { setTool (LaneTrack::Tool::bend); };
    selectButton.setToggleState (true, juce::dontSendNotification);

    // Operations
    for (auto* b : { &smoothButton, &thinButton, &reverseButton, &invertButton, &fitButton })
        addAndMakeVisible (b);
    smoothButton.setTooltip ("Smooth the selected points (or all points of this item)");
    thinButton.setTooltip ("Remove points that hardly change the curve");
    reverseButton.setTooltip ("Mirror the points in time");
    invertButton.setTooltip ("Mirror the values (high becomes low)");
    fitButton.setTooltip ("Show the whole item");
    smoothButton.onClick  = [this] { runOp ("smooth"); };
    thinButton.onClick    = [this] { runOp ("thin"); };
    reverseButton.onClick = [this] { runOp ("reverse"); };
    invertButton.onClick  = [this] { runOp ("invert"); };
    fitButton.onClick     = [this] { fitToItem(); };

    titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hintLabel.setFont (juce::FontOptions (11.5f));
    hintLabel.setText ("Drag the dashed box's handles to scale (Option: around the centre). Double-click a time or value in the table to type it.",
                       juce::dontSendNotification);
    addAndMakeVisible (hintLabel);

    // Point table
    auto& header = table.getHeader();
    header.addColumn ("#", 1, 36, 30, 60, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Time", 2, 90, 60, 160, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Value", 3, 100, 60, 200, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Shape", 4, 70, 50, 140, juce::TableHeaderComponent::notSortable);
    table.setRowHeight (22);
    table.setMultipleSelectionEnabled (true);
    table.setColour (juce::ListBox::backgroundColourId, panelColour);
    addAndMakeVisible (table);

    timeView.setLength (project.getLengthSeconds());
    project.getState().addListener (this);
    selection.addChangeListener (this);
    timeView.addChangeListener (this);

    setWantsKeyboardFocus (true);
    refreshTitle();
    startTimerHz (30);
    setSize (1100, 620);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<ItemEditor> (this)]
    {
        if (safe != nullptr) { safe->fitToItem(); safe->grabKeyboardFocus(); }
    });
}

ItemEditor::~ItemEditor()
{
    stopTimer();
    timeView.removeChangeListener (this);
    selection.removeChangeListener (this);
    project.getState().removeListener (this);
}

juce::AudioProcessorParameter* ItemEditor::parameter() const
{
    auto* plugin = host.getPlugin();
    const int index = controller.resolveParameterIndex (lane);
    return (plugin != nullptr && index >= 0) ? plugin->getParameters()[index] : nullptr;
}

void ItemEditor::setTool (LaneTrack::Tool t)
{
    tool = t;
    selectButton.setToggleState (t == LaneTrack::Tool::select, juce::dontSendNotification);
    pencilButton.setToggleState (t == LaneTrack::Tool::pencil, juce::dontSendNotification);
    lineButton.setToggleState   (t == LaneTrack::Tool::line,   juce::dontSendNotification);
    bendButton.setToggleState   (t == LaneTrack::Tool::bend,   juce::dontSendNotification);
}

void ItemEditor::fitToItem()
{
    const double start = item[IDs::start], length = item[IDs::length];
    const double margin = length * 0.03;
    timeView.fitRange (start - margin, start + length + margin);
}

juce::Array<juce::ValueTree> ItemEditor::pointsHere() const
{
    juce::Array<juce::ValueTree> pts;
    for (auto& p : selection.getPoints())
        if (p.getParent() == item)
            pts.add (p);

    if (pts.isEmpty())   // nothing selected: work on the whole item
        for (auto p : item)
            if (p.hasType (IDs::POINT))
                pts.add (p);
    return pts;
}

void ItemEditor::runOp (const juce::String& name)
{
    auto pts = pointsHere();
    if (pts.size() < 2)
        return;

    auto& um = project.getUndoManager();
    um.beginNewTransaction (name.substring (0, 1).toUpperCase() + name.substring (1) + " points");

    if (name == "smooth")       PointOps::smooth (pts, um);
    else if (name == "reverse") PointOps::reverse (pts, um);
    else if (name == "invert")  PointOps::invert (pts, um);
    else if (name == "thin")
    {
        const int removed = PointOps::thin (pts, um);
        selection.prune();
        hintLabel.setText (juce::String (removed) + " point(s) removed.", juce::dontSendNotification);
    }
}

void ItemEditor::refreshTitle()
{
    if (! item.getParent().isValid())
        return;

    lane = item.getParent();
    const double start = item[IDs::start], length = item[IDs::length];
    titleLabel.setText (lane[IDs::name].toString() + "   " + TimeFormat::format (start) + " - "
                            + TimeFormat::format (start + length),
                        juce::dontSendNotification);
    if (auto* w = findParentComponentOfClass<juce::DocumentWindow>())
        w->setName ("Item editor: " + lane[IDs::name].toString());
}

//==============================================================================
bool ItemEditor::keyPressed (const juce::KeyPress& key)
{
    auto& um = project.getUndoManager();
    const auto mods = key.getModifiers();
    const bool cmd = mods.isCommandDown(), shift = mods.isShiftDown();
    const int code = key.getKeyCode();

    if (cmd && (code == 'z' || code == 'Z')) { if (shift) um.redo(); else um.undo(); return true; }

    if (cmd && (code == 'a' || code == 'A'))
    {
        selection.clear();
        for (auto p : item) if (p.hasType (IDs::POINT)) selection.selectPoint (p, true);
        return true;
    }

    if (cmd && (code == 'c' || code == 'C' || code == 'x' || code == 'X'))
    {
        auto pts = selection.getPoints();
        if (pts.isEmpty()) return true;
        PointOps::copy (pts);
        if (code == 'x' || code == 'X')
        {
            um.beginNewTransaction ("Cut points");
            for (auto p : pts) if (p.getParent().isValid()) p.getParent().removeChild (p, &um);
            selection.clear();
        }
        return true;
    }

    if (cmd && (code == 'v' || code == 'V'))
    {
        um.beginNewTransaction ("Paste points");
        auto added = PointOps::paste (item, snap.snap (host.getEngine().getPositionSeconds()), um);
        selection.clear();
        for (auto& p : added) selection.selectPoint (p, true);
        if (added.isEmpty())
            hintLabel.setText ("Put the playhead inside this item to paste points there (click in the ruler).", juce::dontSendNotification);
        return true;
    }

    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey)
    {
        auto pts = selection.getPoints();
        um.beginNewTransaction ("Delete points");
        for (auto p : pts) if (p.getParent().isValid()) p.getParent().removeChild (p, &um);
        selection.clear();
        return true;
    }

    const bool horizontal = code == juce::KeyPress::leftKey || code == juce::KeyPress::rightKey;
    const bool vertical   = code == juce::KeyPress::upKey || code == juce::KeyPress::downKey;
    if (horizontal || vertical)
    {
        auto pts = selection.getPoints();
        if (pts.isEmpty()) return true;
        const int dir = (code == juce::KeyPress::rightKey || code == juce::KeyPress::upKey) ? 1 : -1;
        um.beginNewTransaction ("Nudge points");
        PointOps::nudge (pts, horizontal ? dir * snap.getGrid() * (shift ? 10.0 : 1.0) : 0.0,
                         vertical ? (float) dir * (shift ? 0.1f : 0.01f) : 0.0f, um);
        return true;
    }

    if (! cmd)
    {
        if (code == '1') { setTool (LaneTrack::Tool::select); return true; }
        if (code == '2') { setTool (LaneTrack::Tool::pencil); return true; }
        if (code == '3') { setTool (LaneTrack::Tool::line);   return true; }
        if (code == '4') { setTool (LaneTrack::Tool::bend);   return true; }
    }

    return false;
}

//==============================================================================
// Point table

int ItemEditor::getNumRows()
{
    return (int) rowPoints.size();
}

void ItemEditor::paintRowBackground (juce::Graphics& g, int row, int, int, bool)
{
    const bool selected = juce::isPositiveAndBelow (row, (int) rowPoints.size())
                          && selection.getPoints().contains (rowPoints[(size_t) row]);
    if (selected)          g.fillAll (accent.withAlpha (0.25f));
    else if (row % 2 == 1) g.fillAll (juce::Colours::white.withAlpha (0.03f));
}

void ItemEditor::paintCell (juce::Graphics& g, int row, int col, int w, int h, bool)
{
    if (! juce::isPositiveAndBelow (row, (int) rowPoints.size()))
        return;

    const auto& p = rowPoints[(size_t) row];
    const double start = item[IDs::start];
    juce::String text;

    switch (col)
    {
        case 1: text = juce::String (row + 1); break;
        case 2: text = TimeFormat::format (start + (double) p[IDs::time]); break;
        case 3: text = paramText (parameter(), (float) (double) p[IDs::value]); break;
        case 4:
        {
            const int shape = p[IDs::shape];
            const float tension = (float) (double) p[IDs::tension];
            text = shape == 0 ? "linear" : shape == 1 ? (tension >= 0.0f ? "slow start" : "fast start")
                 : shape == 2 ? "smooth" : "hold";
            break;
        }
        default: break;
    }

    g.setColour (juce::Colours::white.withAlpha (col == 1 ? 0.45f : 0.85f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (text, 6, 0, w - 8, h, juce::Justification::centredLeft);
}

void ItemEditor::cellClicked (int row, int, const juce::MouseEvent& e)
{
    if (! juce::isPositiveAndBelow (row, (int) rowPoints.size()))
        return;
    selection.selectPoint (rowPoints[(size_t) row], e.mods.isCommandDown() || e.mods.isShiftDown());
}

void ItemEditor::cellDoubleClicked (int row, int col, const juce::MouseEvent&)
{
    editCell (row, col);
}

void ItemEditor::editCell (int row, int col)
{
    if (! juce::isPositiveAndBelow (row, (int) rowPoints.size()) || (col != 2 && col != 3 && col != 4))
        return;

    auto point = rowPoints[(size_t) row];

    if (col == 4)
    {
        juce::PopupMenu m;
        m.addItem (1, "Linear");
        m.addItem (2, "Curve: slow start");
        m.addItem (3, "Curve: fast start");
        m.addItem (4, "Smooth (S-curve)");
        m.addItem (5, "Hold, then jump");
        m.showMenuAsync (juce::PopupMenu::Options(), [this, point] (int r) mutable
        {
            if (r == 0 || ! point.getParent().isValid()) return;
            auto& um = project.getUndoManager();
            um.beginNewTransaction ("Change segment shape");
            const int shape = r == 1 ? 0 : (r <= 3 ? 1 : r - 2);
            point.setProperty (IDs::shape, shape, &um);
            point.setProperty (IDs::tension, r == 2 ? 0.6 : (r == 3 ? -0.6 : 0.0), &um);
        });
        return;
    }

    const double start = item[IDs::start];
    const auto current = col == 2 ? TimeFormat::format (start + (double) point[IDs::time])
                                  : paramText (parameter(), (float) (double) point[IDs::value]);

    auto* window = new juce::AlertWindow (col == 2 ? "Point time" : "Point value",
                                          col == 2 ? "Absolute time (m:ss.mmm or seconds)" : "Value in the plugin's own units",
                                          juce::MessageBoxIconType::NoIcon);
    window->addTextEditor ("v", current);
    window->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    window->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<ItemEditor> safeThis (this);
    window->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, window, point, col] (int result) mutable
    {
        std::unique_ptr<juce::AlertWindow> owner (window);
        if (safeThis == nullptr || result != 1 || ! point.getParent().isValid())
            return;

        auto& um = safeThis->project.getUndoManager();
        const auto text = window->getTextEditorContents ("v");
        auto parentItem = point.getParent();

        if (col == 2)
        {
            um.beginNewTransaction ("Point time");
            const double t = TimeFormat::parse (text) - (double) parentItem[IDs::start];
            point.setProperty (IDs::time, juce::jlimit (0.0, (double) parentItem[IDs::length], t), &um);
        }
        else
        {
            um.beginNewTransaction ("Point value");
            auto* p = safeThis->parameter();
            const float v = p != nullptr ? p->getValueForText (text) : (float) text.getDoubleValue();
            point.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, v), &um);
        }
    }), false);
}

//==============================================================================
void ItemEditor::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (IDs::PROJECT) && property == IDs::lengthSeconds)
        timeView.setLength (project.getLengthSeconds());

    if (tree == item || tree.getParent() == item || tree == lane)
    {
        tableDirty = true;
        refreshTitle();
    }
}

void ItemEditor::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent == item) tableDirty = true;
}

void ItemEditor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent == item) tableDirty = true;
}

void ItemEditor::changeListenerCallback (juce::ChangeBroadcaster*)
{
    track->repaint();
    axis->repaint();
    table.repaint();
    repaint (0, toolbarHeight, getWidth(), rulerHeight);
}

void ItemEditor::timerCallback()
{
    // The item may have been deleted (or moved to another lane) meanwhile.
    if (! item.getParent().isValid() || ! item.getParent().getParent().isValid())
    {
        stopTimer();
        if (onItemGone) onItemGone();
        return;
    }

    if (item.getParent() != lane)
    {
        // Moved to another lane: follow it.
        lane = item.getParent();
        track = std::make_unique<LaneTrack> (context, lane);
        addAndMakeVisible (*track);
        resized();
    }

    if (tableDirty)
    {
        tableDirty = false;
        rowPoints.clear();
        for (auto p : item)
            if (p.hasType (IDs::POINT))
                rowPoints.push_back (p);
        std::stable_sort (rowPoints.begin(), rowPoints.end(),
                          [] (const auto& a, const auto& b) { return (double) a[IDs::time] < (double) b[IDs::time]; });
        table.updateContent();
        table.repaint();
        track->repaint();
    }

    const double pos = host.getEngine().getPositionSeconds();
    if (! juce::approximatelyEqual (pos, lastPosition) || host.getEngine().isPlaying())
    {
        lastPosition = pos;
        track->repaint();
        repaint (0, toolbarHeight, getWidth(), rulerHeight);
    }
}

//==============================================================================
void ItemEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));
    g.setColour (panelColour);
    g.fillRect (0, 0, getWidth(), toolbarHeight);

    // A small time ruler above the curve; click it to move the playhead.
    auto ruler = juce::Rectangle<int> (axisWidth, toolbarHeight, track->getWidth(), rulerHeight);
    g.setColour (panelColour.brighter (0.05f));
    g.fillRect (ruler);

    double step = 0.001;
    for (double s : { 0.001, 0.005, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
        if (s * timeView.getPixelsPerSecond() >= 70.0) { step = s; break; }

    const int decimals = step >= 1.0 ? 0 : (step >= 0.1 ? 1 : (step >= 0.01 ? 2 : 3));
    g.setFont (juce::FontOptions (11.0f));
    for (double t = std::floor (timeView.getOffset() / step) * step; timeView.toX (t) < (float) ruler.getWidth(); t += step)
    {
        const int x = ruler.getX() + (int) timeView.toX (t);
        if (x < ruler.getX()) continue;
        g.setColour (juce::Colours::white.withAlpha (0.35f));
        g.drawVerticalLine (x, (float) ruler.getBottom() - 6.0f, (float) ruler.getBottom());
        g.setColour (juce::Colours::white.withAlpha (0.65f));
        g.drawText (TimeFormat::format (t, decimals), x + 3, ruler.getY(), 80, ruler.getHeight() - 4, juce::Justification::centredLeft);
    }

    const int px = ruler.getX() + (int) timeView.toX (host.getEngine().getPositionSeconds());
    if (px >= ruler.getX() && px <= ruler.getRight())
    {
        g.setColour (accent);
        g.drawVerticalLine (px, (float) ruler.getY(), (float) ruler.getBottom());
    }
}

void ItemEditor::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (toolbarHeight).reduced (8, 8);

    for (auto* b : { &selectButton, &pencilButton, &lineButton, &bendButton })
    {
        b->setBounds (toolbar.removeFromLeft (66));
        toolbar.removeFromLeft (2);
    }
    toolbar.removeFromLeft (14);
    for (auto* b : { &smoothButton, &thinButton, &reverseButton, &invertButton })
    {
        b->setBounds (toolbar.removeFromLeft (66));
        toolbar.removeFromLeft (2);
    }
    toolbar.removeFromLeft (14);
    fitButton.setBounds (toolbar.removeFromLeft (44));
    toolbar.removeFromLeft (14);
    titleLabel.setBounds (toolbar);

    auto bottom = r.removeFromBottom (24);
    hintLabel.setBounds (bottom.reduced (8, 0));

    table.setBounds (r.removeFromRight (tableWidth));
    r.removeFromTop (rulerHeight);
    axis->setBounds (r.removeFromLeft (axisWidth));
    track->setBounds (r);
    timeView.setViewWidth (track->getWidth());
}

// Clicking the ruler moves the playhead.
void ItemEditor::mouseDown (const juce::MouseEvent& e)
{
    if (e.y >= toolbarHeight && e.y < toolbarHeight + rulerHeight && e.x >= axisWidth && e.x < axisWidth + track->getWidth())
    {
        auto t = timeView.toTime ((float) (e.x - axisWidth));
        if (! e.mods.isCommandDown()) t = snap.snap (t);
        controller.locate (t);
    }
}

void ItemEditor::mouseDrag (const juce::MouseEvent& e)
{
    mouseDown (e);
}
