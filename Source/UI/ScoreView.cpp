#include "ScoreView.h"
#include "../Model/ItemSplit.h"
#include "../Model/ItemTime.h"

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
        const int decimals = step >= 1.0 ? 0 : (std::abs (step * 10.0 - std::round (step * 10.0)) < 1.0e-9 ? 1 : 2);
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
        indent = owner.project.groupOf (lane).isValid() ? 8 : 0;
        resized();
        nameLabel.setText (lane[IDs::name].toString(), juce::dontSendNotification);
        muteButton.setToggleState ((bool) lane[IDs::mute], juce::dontSendNotification);
        soloButton.setToggleState ((bool) lane[IDs::solo], juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const bool active = owner.selection.getActiveLane() == lane;
        g.fillAll (active ? headerColour.brighter (0.12f) : headerColour);
        if (auto group = owner.project.groupOf (lane); group.isValid())
        {
            g.setColour (Project::groupColour (group).withAlpha (0.45f));
            g.fillRect (0, 0, indent - 2, getHeight());
        }
        g.setColour (Project::laneColour (lane));
        g.fillRect (indent, 0, active ? 7 : 4, getHeight());

        if (getHeight() >= 40)
        {
            g.setColour (juce::Colours::white.withAlpha (0.6f));
            g.setFont (juce::FontOptions (11.5f));
            juce::String text;
            const int index = owner.controller.resolveParameterIndex (lane);
            if (Project::isNoteLane (lane))
            {
                text = "MIDI notes";
            }
            else if (auto* plugin = owner.host.getPlugin(); plugin != nullptr && index >= 0)
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
            g.drawText (text, 12 + indent, 26, getWidth() - 70 - indent, 16, juce::Justification::centredLeft);
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        nameLabel.setBounds (8 + indent, 4, getWidth() - 66 - indent, 22);
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
            owner.laneDragged (e.getEventRelativeTo (&owner.rowHolder).y);
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

        auto currentGroup = owner.project.groupOf (lane);
        juce::PopupMenu groups;
        groups.addItem (3, "New group with this lane");
        int n = 0;
        for (auto g : owner.project.getGroups())
        {
            groups.addItem (200 + n, "Move to \"" + g[IDs::name].toString() + "\"", g != currentGroup, g == currentGroup);
            ++n;
        }
        if (currentGroup.isValid())
        {
            groups.addSeparator();
            groups.addItem (4, "Take out of \"" + currentGroup[IDs::name].toString() + "\"");
        }

        juce::PopupMenu m;
        m.addItem (1, "Rename...");
        m.addSubMenu ("Colour", colours);
        m.addSubMenu ("Group", groups);
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
            else if (result == 3)
            {
                safeThis->owner.selection.setActiveLane (safeThis->lane);
                safeThis->owner.groupActiveLane();
            }
            else if (result == 4)
            {
                um.beginNewTransaction ("Take lane out of group");
                safeThis->owner.project.setLaneGroup (safeThis->lane, {});
            }
            else if (result >= 200)
            {
                um.beginNewTransaction ("Move lane to group");
                safeThis->owner.project.setLaneGroup (safeThis->lane, safeThis->owner.project.getGroups().getChild (result - 200));
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
    int startHeight = 0, indent = 0;
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
// A group's bar: name, fold triangle, group mute/solo; folded, it shows all its
// lanes' curves on top of each other.
class ScoreView::GroupBar : public juce::Component
{
public:
    GroupBar (ScoreView& o, juce::ValueTree groupTree) : group (groupTree), owner (o)
    {
        nameLabel.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        nameLabel.setEditable (false, true, false);
        nameLabel.setInterceptsMouseClicks (false, false);
        nameLabel.onTextChange = [this]
        {
            auto& um = owner.project.getUndoManager();
            um.beginNewTransaction ("Rename group");
            const auto text = nameLabel.getText().trim();
            group.setProperty (IDs::name, text.isEmpty() ? juce::String ("Group") : text, &um);
        };
        addAndMakeVisible (nameLabel);

        for (auto* b : { &muteButton, &soloButton })
            addAndMakeVisible (b);
        muteButton.setTooltip ("Mute all lanes of this group");
        soloButton.setTooltip ("Solo all lanes of this group");
        muteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8f3b3b));
        soloButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb08a2e));
        muteButton.onClick = [this] { setAll (IDs::mute, "Mute group"); };
        soloButton.onClick = [this] { setAll (IDs::solo, "Solo group"); };
        refresh();
    }

    bool isFolded() const           { return (bool) group[IDs::folded]; }
    int preferredHeight() const     { return isFolded() ? 56 : 26; }

    void refresh()
    {
        nameLabel.setText (group[IDs::name].toString(), juce::dontSendNotification);
        auto lanes = owner.project.lanesInGroup (group);
        auto all = [&lanes] (const juce::Identifier& id)
        {
            for (auto& l : lanes) if (! (bool) l[id]) return false;
            return ! lanes.isEmpty();
        };
        muteButton.setToggleState (all (IDs::mute), juce::dontSendNotification);
        soloButton.setToggleState (all (IDs::solo), juce::dontSendNotification);
        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        const auto colour = Project::groupColour (group);
        auto header = getLocalBounds().removeFromLeft (headerWidth);
        auto overview = getLocalBounds().withTrimmedLeft (headerWidth);

        g.setColour (headerColour.interpolatedWith (colour, 0.22f));
        g.fillRect (header);
        g.setColour (colour);
        g.fillRect (header.removeFromLeft (6));

        // Fold triangle.
        juce::Path tri;
        const float cx = 16.0f, cy = 13.0f;
        if (isFolded()) tri.addTriangle (cx - 3.5f, cy - 5.0f, cx - 3.5f, cy + 5.0f, cx + 4.5f, cy);
        else            tri.addTriangle (cx - 5.0f, cy - 3.5f, cx + 5.0f, cy - 3.5f, cx, cy + 4.5f);
        g.setColour (juce::Colours::white.withAlpha (0.8f));
        g.fillPath (tri);

        const auto lanes = owner.project.lanesInGroup (group);
        g.setColour (juce::Colours::white.withAlpha (0.5f));
        g.setFont (juce::FontOptions (11.0f));
        if (isFolded())
            g.drawText (juce::String (lanes.size()) + (lanes.size() == 1 ? " lane" : " lanes") + " (folded)",
                        28, 28, headerWidth - 36, 16, juce::Justification::centredLeft);

        // Right side: a band in the group colour; folded, the lanes' curves.
        g.setColour (colour.withAlpha (isFolded() ? 0.10f : 0.16f));
        g.fillRect (overview);
        g.setColour (colour.withAlpha (0.6f));
        g.drawHorizontalLine (0, (float) overview.getX(), (float) overview.getRight());

        if (isFolded())
            paintOverview (g, overview.reduced (0, 5), lanes);

        // Playhead.
        const float px = (float) overview.getX() + owner.timeView.toX (owner.host.getEngine().getPositionSeconds());
        if (px >= (float) overview.getX())
        {
            g.setColour (accent.withAlpha (0.8f));
            g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());
        g.drawVerticalLine (headerWidth - 1, 0.0f, (float) getHeight());
    }

    void resized() override
    {
        nameLabel.setBounds (26, 2, headerWidth - 26 - 58, 22);
        soloButton.setBounds (headerWidth - 28, 4, 22, 18);
        muteButton.setBounds (headerWidth - 52, 4, 22, 18);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        dragging = false;
        if (e.mods.isPopupMenu()) { showMenu(); return; }
        if (e.x < 28 && e.y < 26) { toggleFold(); return; }
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (e.mods.isPopupMenu() || e.mouseDownPosition.x >= headerWidth || (e.mouseDownPosition.x < 28 && e.mouseDownPosition.y < 26))
            return;
        if (! dragging && std::abs (e.getDistanceFromDragStartY()) > 5)
            dragging = true;
        if (dragging)
            owner.laneDragged (e.getEventRelativeTo (&owner.rowHolder).y);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (dragging)
            owner.groupDragEnded (*this);
        dragging = false;
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.x >= 28 && e.x < headerWidth - 58 && e.y < 26)
            nameLabel.showEditor();
        else if (e.x >= headerWidth)
            toggleFold();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override
    {
        owner.handleWheel (e, w, *this);
    }

    juce::ValueTree group;

private:
    void toggleFold()
    {
        // Folding is a view setting: saved with the project, but not an undo step.
        group.setProperty (IDs::folded, ! isFolded(), nullptr);
    }

    void setAll (const juce::Identifier& id, const juce::String& action)
    {
        auto lanes = owner.project.lanesInGroup (group);
        bool allOn = ! lanes.isEmpty();
        for (auto& l : lanes) allOn = allOn && (bool) l[id];
        auto& um = owner.project.getUndoManager();
        um.beginNewTransaction (action);
        for (auto& l : lanes) l.setProperty (id, ! allOn, &um);
    }

    void paintOverview (juce::Graphics& g, juce::Rectangle<int> area, const juce::Array<juce::ValueTree>& lanes)
    {
        auto& tv = owner.timeView;
        const int x0 = area.getX(), w = area.getWidth();
        const float top = (float) area.getY(), h = (float) area.getHeight();

        for (auto& laneTree : lanes)
        {
            if (Project::isNoteLane (laneTree))
                continue;
            Automation::Lane lane;
            AutomationController::buildItems (laneTree, lane);
            if (lane.items.empty()) continue;
            const int index = owner.controller.resolveParameterIndex (laneTree);
            lane.baseValue = index >= 0 ? owner.controller.getBaseValue (index) : lane.items.front().valueAtLocal (0.0);

            juce::Path path;
            for (int px = 0; px <= w; px += 2)
            {
                const double t = tv.toTime ((float) px);
                const float v = juce::jlimit (0.0f, 1.0f, Automation::laneValueAt (lane, t));
                const float y = top + (1.0f - v) * h;
                if (px == 0) path.startNewSubPath ((float) (x0 + px), y);
                else         path.lineTo ((float) (x0 + px), y);
            }
            const bool muted = (bool) laneTree[IDs::mute];
            g.setColour (Project::laneColour (laneTree).withAlpha (muted ? 0.3f : 0.9f));
            g.strokePath (path, juce::PathStrokeType (1.4f));
        }
    }

    void showMenu()
    {
        juce::PopupMenu colours;
        for (int i = 0; i < Project::paletteSize; ++i)
            colours.addColouredItem (100 + i, "Colour " + juce::String (i + 1), Project::paletteColour (i),
                                     true, (int) group[IDs::colour] == i);
        juce::PopupMenu m;
        m.addItem (1, "Rename...");
        m.addSubMenu ("Colour", colours);
        m.addItem (2, isFolded() ? "Open" : "Fold");
        m.addItem (5, "Give the lanes the group colour");
        m.addSeparator();
        m.addItem (3, "Ungroup (keep the lanes)");
        m.addItem (4, "Remove group and its lanes");

        juce::Component::SafePointer<GroupBar> safeThis (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safeThis] (int result)
        {
            if (safeThis == nullptr || result == 0) return;
            auto& proj = safeThis->owner.project;
            auto& um = proj.getUndoManager();
            auto grp = safeThis->group;
            if (result == 1)       safeThis->nameLabel.showEditor();
            else if (result == 2)  safeThis->toggleFold();
            else if (result == 3)  { um.beginNewTransaction ("Ungroup"); proj.ungroup (grp); }
            else if (result == 4)  { um.beginNewTransaction ("Remove group"); proj.removeGroupWithLanes (grp); }
            else if (result == 5)
            {
                um.beginNewTransaction ("Lane colours");
                for (auto& l : proj.lanesInGroup (grp))
                    l.setProperty (IDs::colour, grp[IDs::colour], &um);
            }
            else if (result >= 100)
            {
                um.beginNewTransaction ("Group colour");
                grp.setProperty (IDs::colour, result - 100, &um);
            }
        });
    }

    ScoreView& owner;
    juce::Label nameLabel;
    juce::TextButton muteButton { "M" }, soloButton { "S" };
    bool dragging = false;
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
    trackContext.saveToLibrary  = [this] { if (onSaveToLibrary) onSaveToLibrary(); };
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
    groupBars.clear();
    displayOrder.clear();

    int currentGroup = 0;
    bool folded = false;
    for (auto lane : project.getLanes())
    {
        if (! lane.hasType (IDs::LANE))
            continue;

        const int g = lane[IDs::group];
        if (g != 0 && g != currentGroup)
        {
            auto groupTree = project.findGroup (g);
            if (groupTree.isValid())
            {
                auto* bar = groupBars.add (new GroupBar (*this, groupTree));
                rowHolder.addAndMakeVisible (bar);
                displayOrder.push_back (bar);
                folded = bar->isFolded();
            }
            else
            {
                folded = false;
            }
        }
        if (g == 0) folded = false;
        currentGroup = g;

        if (folded)
            continue;

        auto* row = rows.add (new LaneRow (*this, lane));
        rowHolder.addAndMakeVisible (row);
        displayOrder.push_back (row);
    }

    emptyHint.setVisible (displayOrder.empty());
    selection.prune();
    selection.keepOnlyLanes ([this] (const juce::ValueTree& lane)
    {
        for (auto* row : rows) if (row->lane == lane) return true;
        return false;
    });
    layoutRows();
    viewport.setViewPosition (scroll);
    repaint();
}

void ScoreView::layoutRows()
{
    const int width = viewport.getMaximumVisibleWidth();
    int y = 0;
    for (auto* c : displayOrder)
    {
        const int h = (dynamic_cast<GroupBar*> (c) != nullptr) ? static_cast<GroupBar*> (c)->preferredHeight()
                                                                : static_cast<LaneRow*> (c)->preferredHeight();
        c->setBounds (0, y, width, h);
        y += h;
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
    for (size_t i = 0; i < displayOrder.size(); ++i)
        if (yInHolder < displayOrder[i]->getY() + displayOrder[i]->getHeight() / 2)
            return (int) i;
    return (int) displayOrder.size();
}

juce::ValueTree ScoreView::laneBefore (int displayIndex) const
{
    if (displayIndex < 0 || displayIndex >= (int) displayOrder.size())
        return {};
    auto* c = displayOrder[(size_t) displayIndex];
    if (auto* bar = dynamic_cast<GroupBar*> (c))
    {
        auto lanes = project.lanesInGroup (bar->group);
        return lanes.isEmpty() ? juce::ValueTree() : lanes.getFirst();
    }
    return static_cast<LaneRow*> (c)->lane;
}

void ScoreView::laneDragged (int yInHolder)
{
    const int index = dropIndexFor (yInHolder);
    dropIndicatorY = index < (int) displayOrder.size() ? displayOrder[(size_t) index]->getY() : rowHolder.getHeight();
    repaint();
}

void ScoreView::laneDragEnded (LaneRow& row)
{
    const int target = dropIndexFor (dropIndicatorY >= 0 ? dropIndicatorY : row.getY());
    dropIndicatorY = -1;
    repaint();

    // Which group does the drop line belong to? Right below a group's bar, or
    // between two lanes of the same group: that group. Anywhere else: none.
    auto groupAt = [this] (int i) -> int
    {
        if (i < 0 || i >= (int) displayOrder.size()) return -1;
        if (auto* bar = dynamic_cast<GroupBar*> (displayOrder[(size_t) i])) return (int) bar->group[IDs::groupId];
        return (int) static_cast<LaneRow*> (displayOrder[(size_t) i])->lane[IDs::group];
    };
    const bool aboveIsBar = target > 0 && dynamic_cast<GroupBar*> (displayOrder[(size_t) target - 1]) != nullptr;
    const auto* aboveRow = target > 0 ? dynamic_cast<LaneRow*> (displayOrder[(size_t) target - 1]) : nullptr;
    const auto* belowRow = target < (int) displayOrder.size() ? dynamic_cast<LaneRow*> (displayOrder[(size_t) target]) : nullptr;

    int newGroup = 0;
    if (aboveIsBar)
        newGroup = groupAt (target - 1);
    else if (aboveRow != nullptr && belowRow != nullptr && (int) aboveRow->lane[IDs::group] != 0
             && (int) aboveRow->lane[IDs::group] == (int) belowRow->lane[IDs::group])
        newGroup = aboveRow->lane[IDs::group];

    // Moving a lane onto its own place in the same group changes nothing.
    auto lanes = project.getLanes();
    auto beforeLane = laneBefore (target);
    if (beforeLane == row.lane || (target > 0 && displayOrder[(size_t) target - 1] == &row))
        if ((int) row.lane[IDs::group] == newGroup)
            return;

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Move lane");
    auto lane = row.lane;

    if (newGroup == 0) lane.removeProperty (IDs::group, &um);
    else               lane.setProperty (IDs::group, newGroup, &um);

    const int from = lanes.indexOf (lane);
    int to = beforeLane.isValid() ? lanes.indexOf (beforeLane) : lanes.getNumChildren();
    if (to > from) --to;
    to = juce::jlimit (0, lanes.getNumChildren() - 1, to);
    if (from >= 0 && from != to)
        lanes.moveChild (from, to, &um);

    project.normaliseGroups();
}

void ScoreView::groupDragEnded (GroupBar& bar)
{
    int target = dropIndexFor (dropIndicatorY >= 0 ? dropIndicatorY : bar.getY());
    dropIndicatorY = -1;
    repaint();

    // A group cannot land inside another group: move the drop line up to that group's bar.
    while (target > 0 && target < (int) displayOrder.size())
    {
        auto* row = dynamic_cast<LaneRow*> (displayOrder[(size_t) target]);
        if (row == nullptr || (int) row->lane[IDs::group] == 0 || (int) row->lane[IDs::group] == (int) bar.group[IDs::groupId])
            break;
        --target;
    }

    auto members = project.lanesInGroup (bar.group);
    auto beforeLane = laneBefore (target);
    while (beforeLane.isValid() && members.contains (beforeLane))
        beforeLane = laneBefore (++target);

    int toIndex = 0;
    for (auto lane : project.getLanes())
    {
        if (lane == beforeLane) break;
        if (! members.contains (lane)) ++toIndex;
    }

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Move group");
    project.moveGroup (bar.group, toIndex);
}

void ScoreView::repaintRows()
{
    for (auto* row : rows) row->track.repaint();
    for (auto* bar : groupBars) bar->repaint();
}

void ScoreView::setAllGroupsFolded (bool folded)
{
    for (auto group : project.getGroups())
        group.setProperty (IDs::folded, folded, nullptr);
}

void ScoreView::groupActiveLane()
{
    auto lane = selection.getActiveLane();
    if (! lane.isValid())
    {
        showStatus ("Click a lane first, then make a group of it.");
        return;
    }
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("New group");
    project.setLaneGroup (lane, {});
    auto group = project.createGroup ("Group " + juce::String (project.getGroups().getNumChildren() + 1), { lane });
    juce::ignoreUnused (group);
    showStatus ("New group. Drag lanes just below its bar to add them; right-click a lane > Group to move it in.");
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
    const int steps = vertical ? 0 : direction * (large ? 10 : 1);
    const float dv = vertical ? (float) direction * (large ? 0.1f : 0.01f) : 0.0f;
    PointOps::nudge (points, steps, snap.getGrid(), dv, um);
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
        if (Project::isNoteItem (item)) copy.setProperty ("fromNoteLane", true, nullptr);
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

    // Lanes as shown (folded groups left out), so a paste never lands in a hidden lane.
    juce::Array<juce::ValueTree> lanes;
    for (auto* row : rows) lanes.add (row->lane);
    auto laneIndexOf = [&lanes] (const juce::var& id)
    {
        for (int i = 0; i < lanes.size(); ++i)
            if (lanes.getReference (i)[IDs::laneId] == id)
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
        auto lane = juce::isPositiveAndBelow (target, lanes.size()) ? lanes.getReference (target) : juce::ValueTree();
        if (! lane.isValid()) { ++skipped; continue; }

        const double start = at + ((double) c[IDs::start] - earliest);
        const double length = c[IDs::length];
        const bool hasNotes = c.getChildWithName (IDs::NOTE).isValid() || (bool) c.getProperty ("fromNoteLane", false);
        if (hasNotes != Project::isNoteLane (lane)) { ++skipped; continue; }
        if (! project.isFree (lane, start, length)) { ++skipped; continue; }

        auto item = c.createCopy();
        item.removeProperty ("sourceLane", nullptr);
        item.removeProperty ("fromNoteLane", nullptr);
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
    repaintRows();
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
        if (property == IDs::group)
            rebuildPending = true;
        if (property == IDs::mute || property == IDs::solo)
            for (auto* bar : groupBars) bar->refresh();
    }

    if (tree.hasType (IDs::GROUP))
    {
        if (property == IDs::folded)
            rebuildPending = true;
        for (auto* bar : groupBars)
            if (bar->group == tree) bar->refresh();
        for (auto* row : rows) row->header.repaint();
    }

    repaintRows();
}

void ScoreView::valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT) || parent.hasType (IDs::GROUPS))
        rebuildPending = true;
    else
        repaintRows();
}

void ScoreView::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int)
{
    if (parent.hasType (IDs::LANES) || parent.hasType (IDs::PROJECT) || parent.hasType (IDs::GROUPS))
        rebuildPending = true;   // rebuilt on the timer, after the current mouse event
    else
    {
        selection.prune();
        repaintRows();
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
        for (auto* bar : groupBars)
            bar->repaint();
    }
}

//==============================================================================
// Splitting items

void ScoreView::splitAtPlayhead()
{
    const double at = snap.snap (host.getEngine().getPositionSeconds());
    juce::Array<juce::ValueTree> targets;
    const bool fromSelection = ! selection.getItems().isEmpty();

    if (fromSelection)
    {
        for (auto& item : selection.getItems())
            if (ItemSplit::canSplit (item, at))
                targets.add (item);
    }
    else
    {
        for (const auto& lane : project.getLanes())
            for (const auto& item : lane)
                if (item.hasType (IDs::ITEM) && ItemSplit::canSplit (item, at))
                    targets.add (item);
    }

    if (targets.isEmpty())
    {
        showStatus (fromSelection ? "The playhead is not inside a selected item (or too close to its edge)."
                                  : "No item under the playhead to split.");
        return;
    }

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Split at playhead");
    selection.clear();
    for (auto& item : targets)
        for (auto& piece : ItemSplit::splitAt (item, { at }, &um))
            selection.selectItem (piece, true);

    showStatus ("Split " + juce::String (targets.size()) + (targets.size() == 1 ? " item" : " items")
                + (fromSelection ? juce::String() : juce::String (" (nothing was selected: every item under the playhead)")) + ".");
}

void ScoreView::splitAtSelectedPoints()
{
    juce::Array<juce::ValueTree> items;
    std::vector<std::vector<double>> cuts;
    for (auto& p : selection.getPoints())
    {
        auto item = p.getParent();
        int i = items.indexOf (item);
        if (i < 0) { items.add (item); cuts.emplace_back(); i = items.size() - 1; }
        cuts[(size_t) i].push_back (ItemTime (item).absolute ((double) p[IDs::time]));
    }

    if (items.isEmpty())
    {
        showStatus ("Select points to split at (Cmd-click or drag a box around them).");
        return;
    }

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Split at points");
    selection.clear();
    int pieces = 0;
    for (int i = 0; i < items.size(); ++i)
        for (auto& piece : ItemSplit::splitAt (items[i], cuts[(size_t) i], &um))
        {
            selection.selectItem (piece, true);
            ++pieces;
        }
    showStatus ("Split into " + juce::String (pieces) + " items.");
}
