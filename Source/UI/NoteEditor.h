// NoteEditor.h — the piano roll: edits the notes of one item of a note lane.
//
//   Option-drag on empty space    draw a note (drag sets its length)
//   double-click empty / a note   add a note / delete it
//   drag a note                   move (time snaps to the grid; up/down = pitch)
//   Option-drag a note            copy it
//   drag a note's right edge      change its length
//   drag on empty space           rubber-band selection
//   velocity strip: drag a bar    change velocity (all selected notes together;
//                                 Shift: proportionally; Cmd: fine)
//   Option-drag on the strip      draw a velocity line across the notes
//   arrows: move by grid / pitch (Shift: octave)   Delete, Cmd-A, Cmd-C/V, Cmd-Z

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../Model/ItemTime.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"

class NoteEditor : public juce::Component,
                   private juce::ValueTree::Listener,
                   private juce::ChangeListener,
                   private juce::ScrollBar::Listener,
                   private juce::Timer
{
public:
    NoteEditor (Project&, PluginHost&, AutomationController&, SnapSettings&, juce::ValueTree item);
    ~NoteEditor() override;

    juce::ValueTree getItem() const     { return item; }
    std::function<void()> onItemGone;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    class Ruler;
    class Keyboard;
    class Roll;
    class VelocityStrip;

    // Geometry and time
    ItemTime itemTime() const                   { return ItemTime (item); }
    float localToX (double local) const;
    double xToLocal (float x) const;
    double snapLocal (double local, bool bypass) const;
    double gridLocal() const;
    float pitchToY (int pitch) const            { return (float) (topPitch - pitch) * rowHeight; }
    int yToPitch (float y) const                { return juce::jlimit (0, 127, topPitch - (int) std::floor (y / rowHeight)); }
    int visibleRows() const;
    void setTopPitch (int pitch);
    void fitToItem();
    void handleWheel (const juce::MouseEvent&, const juce::MouseWheelDetails&);

    // Notes
    juce::Array<juce::ValueTree> notes() const;
    juce::ValueTree noteAt (juce::Point<float> rollPos, bool* onRightEdge = nullptr) const;
    juce::Rectangle<float> noteBounds (const juce::ValueTree& note) const;
    void select (const juce::ValueTree& note, bool add);
    void deleteSelected();
    void copySelected();
    void pasteAtPlayhead();
    void moveSelected (double dLocal, int dPitch);
    void refreshStatus();
    void refreshGridBox();
    void repaintAll();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override   { repaintAll(); refreshStatus(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override              { repaintAll(); refreshStatus(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override                     { repaintAll(); }
    void scrollBarMoved (juce::ScrollBar*, double newRangeStart) override;
    void timerCallback() override;

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    SnapSettings& snap;
    juce::ValueTree item, lane;

    TimeView timeView;
    int topPitch = 84;
    float rowHeight = 14.0f;
    juce::Array<juce::ValueTree> selected;
    int newVelocity = 100;
    bool gridMusical = false;
    double lastPosition = -1.0;

    std::unique_ptr<Ruler> ruler;
    std::unique_ptr<Keyboard> keyboard;
    std::unique_ptr<Roll> roll;
    std::unique_ptr<VelocityStrip> velocityStrip;
    juce::ScrollBar vScroll { true };

    juce::Label titleLabel, gridLabel, velocityLabel, statusLabel;
    juce::ComboBox gridBox;
    juce::Slider velocitySlider;
    juce::TextButton fitButton { "Fit" };

    struct CopiedNote { double time, length; int pitch, velocity; };
    static std::vector<CopiedNote> clipboard;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NoteEditor)
};
