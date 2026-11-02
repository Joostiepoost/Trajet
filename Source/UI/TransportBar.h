// TransportBar.h — play/stop, back to start, loop, time display, patch/score
// switch, project length and the live update interval.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"

class TransportBar : public juce::Component,
                     private juce::Timer,
                     private juce::ValueTree::Listener,
                     private juce::ChangeListener
{
public:
    TransportBar (Project&, PluginHost&, AutomationController&, SnapSettings&);

    std::function<void (juce::Component& button)> onAddLane;
    std::function<void()> onAddNoteLane;
    std::function<void()> onZoomToFit;
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override   { updateLength(); }
    void updateLength();
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    SnapSettings& snap;

    juce::TextButton startButton { "|<" }, playButton { "Play" }, loopButton { "Loop" }, patchButton { "Patch" },
                     snapButton { "Snap" }, addLaneButton { "+ Lane" }, addNotesButton { "+ Notes" }, fitButton { "Fit" };
    juce::ComboBox gridBox;
    juce::Label timeLabel, lengthLabel, intervalLabel;
    juce::Slider lengthSlider;
    juce::ComboBox intervalBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
