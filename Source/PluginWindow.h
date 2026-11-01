// PluginWindow.h — a separate window showing the plugin's own editor
// (for Aalto: its full patch panel), so you can set the starting patch.

#pragma once

#include <JuceHeader.h>

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioPluginInstance& plugin, std::function<void()> onCloseRequested)
        : DocumentWindow (plugin.getName(), juce::Colours::black, DocumentWindow::closeButton),
          onClose (std::move (onCloseRequested))
    {
        setUsingNativeTitleBar (true);

        juce::AudioProcessorEditor* editor = plugin.createEditorAndMakeActive();
        if (editor == nullptr)
            editor = new juce::GenericAudioProcessorEditor (plugin);   // fallback: plain sliders

        setContentOwned (editor, true);
        setResizable (editor->isResizable(), false);
        setTopLeftPosition (120, 120);
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        if (onClose) onClose();
    }

private:
    std::function<void()> onClose;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginWindow)
};
