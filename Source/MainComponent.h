// MainComponent.h — everything inside the main window.
// Step 1: a toolbar to scan, load and open plugins, audio settings,
// a held-note option, and the live parameter list.

#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "PluginWindow.h"
#include "ParameterListComponent.h"

class MainComponent : public juce::Component,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void showPluginScanner();
    void showLoadMenu();
    void showAudioSettings();
    void togglePluginWindow();
    void closePluginWindow();
    void loadPlugin (const juce::PluginDescription&);
    void pluginChanged();
    void updateHeldNote();
    void updateAudioOnButton();
    void timerCallback() override;   // refreshes the status bar

    PluginHost host;

    juce::TextButton scanButton    { "Plugins..." },
                     loadButton    { "Load plugin" },
                     editButton    { "Edit patch" },
                     audioButton   { "Audio..." },
                     audioOnButton { "Audio on" };
    juce::Label      pluginLabel;
    juce::ToggleButton heldNoteToggle { "Held note" };
    juce::Slider     noteSlider, velocitySlider;

    ParameterListComponent parameterList;
    juce::String statusText;

    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
