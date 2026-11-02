// OutputStrip.h — the end of the signal chain: output gain, limiter switch,
// and a level meter with a clip light. Stored in the project, so playback and
// render sound the same.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"

class OutputStrip : public juce::Component,
                    private juce::Timer,
                    private juce::ValueTree::Listener
{
public:
    OutputStrip (Project&, PluginHost&);
    ~OutputStrip() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void refresh();
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { refresh(); }
    void valueTreeRedirected (juce::ValueTree&) override                               { refresh(); }
    juce::Rectangle<int> meterArea() const;

    Project& project;
    PluginHost& host;

    juce::Slider gainSlider;
    struct LimiterButton : public juce::TextButton
    {
        std::function<void (bool popup)> onClicked;
        void clicked (const juce::ModifierKeys& mods) override   { if (onClicked) onClicked (mods.isPopupMenu()); }
    };
    LimiterButton limiterButton;
    void showCeilingMenu();

    float level = 0.0f, hold = 0.0f, limiterDb = 0.0f;
    int holdFrames = 0;
    bool clipped = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (OutputStrip)
};
