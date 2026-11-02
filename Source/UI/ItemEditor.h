// ItemEditor.h — a window for detailed editing of one item (double-click an item).
//
// Tools (keys 1-4): Select, Pencil (draw), Line (ramp), Bend.  The main window's
// modifier gestures work here too. Operations on selected points: Smooth, Thin,
// Reverse, Invert. Drag the dashed box's handles to scale (Option: around the centre).
// The table on the right lists every point; double-click a time or value to type it.
//
// Keys: Cmd-Z / Shift-Cmd-Z undo/redo, Cmd-A select all, Cmd-C/X/V copy/cut/paste
// points (paste at the playhead), Delete, arrows nudge (Shift: 10x), 1-4 tools.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../Model/PointOps.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"
#include "LaneTrack.h"
#include "../Model/ItemTime.h"
#include "GeneratorPanel.h"

class ItemEditor : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::ChangeListener,
                   private juce::TableListBoxModel,
                   private juce::Timer
{
public:
    ItemEditor (Project&, PluginHost&, AutomationController&, SnapSettings&, Selection&, juce::ValueTree item);
    ~ItemEditor() override;

    juce::ValueTree getItem() const     { return item; }
    std::function<void()> onItemGone;   // the item was deleted (e.g. by undo)

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    class ValueAxis;
    class TempoStrip;
    void refreshTimebaseControls();
    bool isMusical() const      { return item[IDs::timebase].toString() == "musical"; }

    void setTool (LaneTrack::Tool);
    void fitToItem();
    juce::Array<juce::ValueTree> pointsHere() const;   // selected points of this item, or all
    void runOp (const juce::String& name);
    void refreshTitle();
    juce::AudioProcessorParameter* parameter() const;

    // TableListBoxModel: the point list
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int, int, int, bool) override;
    void paintCell (juce::Graphics&, int row, int col, int w, int h, bool) override;
    void cellClicked (int row, int col, const juce::MouseEvent&) override;
    void cellDoubleClicked (int row, int col, const juce::MouseEvent&) override;
    void editCell (int row, int col);

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    SnapSettings& snap;
    Selection& selection;
    juce::ValueTree item, lane;

    TimeView timeView;
    LaneTrack::Tool tool = LaneTrack::Tool::select;
    LaneTrack::Context context;
    std::unique_ptr<LaneTrack> track;
    std::unique_ptr<ValueAxis> axis;
    std::unique_ptr<TempoStrip> tempoStrip;

    juce::Label timebaseLabel, metreLabel, metreValue, gridLabel, lengthLabel, lengthValue, tempoHint;
    juce::ComboBox timebaseBox, gridBox;

    juce::TextButton selectButton { "Select" }, pencilButton { "Pencil" }, lineButton { "Line" }, bendButton { "Bend" };
    juce::TextButton smoothButton { "Smooth" }, thinButton { "Thin" }, reverseButton { "Reverse" }, invertButton { "Invert" };
    juce::TextButton fitButton { "Fit" };
    juce::Label titleLabel, hintLabel;
    juce::TableListBox table { {}, this };
    GeneratorPanel generatorPanel;
    void refreshGeneratorMode();
    std::vector<juce::ValueTree> rowPoints;     // points in time order
    bool tableDirty = true;
    double lastPosition = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ItemEditor)
};

//==============================================================================
class ItemEditorWindow : public juce::DocumentWindow
{
public:
    ItemEditorWindow (std::unique_ptr<ItemEditor> editor, std::function<void()> onCloseRequested)
        : DocumentWindow ("Item editor", juce::Colour (0xff1e2126), DocumentWindow::allButtons),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);
        editor->onItemGone = [this] { if (onClose) onClose(); };
        setContentOwned (editor.release(), true);
        setResizable (true, false);
        setResizeLimits (720, 420, 4000, 3000);
        centreWithSize (1100, 620);
        setVisible (true);
    }

    ItemEditor* getEditor() const   { return dynamic_cast<ItemEditor*> (getContentComponent()); }
    void closeButtonPressed() override   { if (onClose) onClose(); }

private:
    std::function<void()> onClose;
};
