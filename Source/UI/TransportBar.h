// TransportBar.h — play/stop, back to start, loop, time display, patch/score
// switch, project length and the live update interval.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"

class TransportBar : public juce::Component,
                     private juce::Timer,
                     private juce::ValueTree::Listener
{
public:
    TransportBar (Project&, PluginHost&, AutomationController&);
    ~TransportBar() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeRedirected (juce::ValueTree&) override   { updateLength(); }
    void updateLength();

    Project& project;
    PluginHost& host;
    AutomationController& controller;

    juce::TextButton startButton { "|<" }, playButton { "Play" }, loopButton { "Loop" }, patchButton { "Patch" };
    juce::Label timeLabel, lengthLabel, intervalLabel;
    juce::Slider lengthSlider;
    juce::ComboBox intervalBox;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (TransportBar)
};
