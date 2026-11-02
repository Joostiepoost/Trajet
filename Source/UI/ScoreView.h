// ScoreView.h — the main score: a time ruler, and one row per lane
// (header on the left, timeline on the right), with zoom and scroll.
//
//   Scroll                 vertical scroll        Cmd-scroll     zoom time
//   Shift-scroll           horizontal scroll      Option-scroll  lane height
//   drag a lane header     reorder lanes          drag its lower edge  resize
//   click/drag the ruler   move the playhead

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"
#include "LaneTrack.h"
#include "../Model/PointOps.h"

class ScoreView : public juce::Component,
                  private juce::ChangeListener,
                  private juce::ValueTree::Listener,
                  private juce::ScrollBar::Listener,
                  private juce::Timer
{
public:
    ScoreView (Project&, PluginHost&, AutomationController&, SnapSettings&);
    ~ScoreView() override;

    Selection& getSelection() noexcept      { return selection; }
    TimeView& getTimeView() noexcept        { return timeView; }

    // Editing commands
    void deleteSelection();
    void copySelection();
    void cutSelection();
    void paste();
    void duplicateSelection();
    void nudge (int direction, bool vertical, bool large);   // arrow keys
    void applyPointOp (const juce::String& name);            // "smooth", "thin", "reverse", "invert"
    void selectAllInActiveLane();
    void zoomIn()                           { timeView.zoomAround (1.5, (float) trackWidth() * 0.5f); }
    void zoomOut()                          { timeView.zoomAround (1.0 / 1.5, (float) trackWidth() * 0.5f); }
    void zoomToFit()                        { timeView.fitAll(); }

    std::function<void (const juce::String&)> onStatus;
    std::function<void (juce::ValueTree item)> onOpenItemEditor;
    std::function<void()> onSaveToLibrary;

    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int headerWidth = 190;
    static constexpr int rulerHeight = 26;

private:
    class Ruler;
    class LaneHeader;
    class LaneRow;
    class GroupBar;

    int trackWidth() const      { return juce::jmax (50, getWidth() - headerWidth - viewport.getScrollBarThickness()); }

    void rebuildRows();
    void layoutRows();
    void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&, juce::Component& source);
    void showStatus (const juce::String& text)   { if (onStatus) onStatus (text); }

    // Lane and group drag-to-reorder (driven by the headers and group bars)
    void laneDragged (int yInHolder);
    void laneDragEnded (LaneRow& row);
    void groupDragEnded (GroupBar& bar);
    int dropIndexFor (int yInHolder) const;         // index in the display order
    juce::ValueTree laneBefore (int displayIndex) const;   // first lane at or after it (in lane order)
    void repaintRows();

public:
    /** Fold or open every group. */
    void setAllGroupsFolded (bool folded);
    /** Makes a group of the active lane (asks for nothing: named "Group n"). */
    void groupActiveLane();
private:

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override;
    void valueTreeRedirected (juce::ValueTree&) override    { rebuildRows(); }
    void timerCallback() override;

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    SnapSettings& snap;
    TimeView timeView;
    Selection selection;
    LaneTrack::Context trackContext;

    std::unique_ptr<Ruler> ruler;
    juce::Viewport viewport;
    juce::Component rowHolder;
    juce::OwnedArray<LaneRow> rows;
    juce::OwnedArray<GroupBar> groupBars;
    std::vector<juce::Component*> displayOrder;     // group bars and lane rows, top to bottom
    juce::ScrollBar hScroll { false };
    juce::Label emptyHint;

    void rubberBand (juce::Rectangle<int> screenArea, bool add, bool finished);
    juce::Rectangle<int> rubberArea;   // in this component's coordinates
    juce::Array<juce::ValueTree> selectionBeforeRubberBand;

    int dropIndicatorY = -1;
    double lastPosition = -1.0;
    bool rebuildPending = false, handlingWheel = false;

    static juce::Array<juce::ValueTree> clipboard;   // copied items (with their lane id)
    static bool lastCopyWasPoints;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ScoreView)
};
