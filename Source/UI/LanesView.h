// LanesView.h — step 3's simple lane display: a time ruler and one strip per lane.
//
// This is a temporary editor to test the engine; step 4 replaces it with the
// real main window (zoom, items, groups, gestures).
//
// In a strip:  click = add a point (keep dragging to place it)
//              drag a point = move it
//              double-click a point = delete it
//              right-click a point = shape of the segment that starts there

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"

class LanesView : public juce::Component,
                  private juce::ValueTree::Listener,
                  private juce::Timer
{
public:
    LanesView (Project&, PluginHost&, AutomationController&);
    ~LanesView() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    static constexpr int headerWidth = 180;
    static constexpr int rulerHeight = 26;
    static constexpr int laneHeight  = 72;

private:
    class Ruler;
    class LaneStrip;

    void rebuildStrips();
    void valueTreeChildAdded (juce::ValueTree& parent, juce::ValueTree&) override;
    void valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged (juce::ValueTree& parent, int, int) override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override   { rebuildStrips(); }
    void timerCallback() override;

    Project& project;
    PluginHost& host;
    AutomationController& controller;

    std::unique_ptr<Ruler> ruler;
    juce::Viewport viewport;
    juce::Component stripHolder;
    juce::OwnedArray<LaneStrip> strips;
    juce::Label emptyHint;
    double lastDrawnPosition = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanesView)
};
