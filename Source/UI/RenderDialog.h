// RenderDialog.h — settings for rendering to WAV, or for exporting the automation as CSV.

#pragma once

#include <JuceHeader.h>
#include "../Render/Renderer.h"

class RenderDialog : public juce::Component
{
public:
    enum class Mode { render, csv };

    RenderDialog (Mode, RenderSettings initial, double projectLength, const juce::String& heldNoteText,
                  std::function<void (RenderSettings)> onConfirm);

    static void show (Mode, RenderSettings initial, double projectLength, const juce::String& heldNoteText,
                      juce::Component* parent, std::function<void (RenderSettings)> onConfirm);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void confirm();
    void close();
    void updateEnablement();

    Mode mode;
    RenderSettings settings;
    double projectLength;
    std::function<void (RenderSettings)> onConfirm;

    juce::Label rangeLabel, fromLabel, toLabel, rateLabel, formatLabel, tailLabel, intervalLabel,
                heldNoteLabel, stepLabel, valuesLabel, noteLabel, errorLabel;
    juce::ComboBox rangeBox, rateBox, formatBox, intervalBox, stepBox, valuesBox;
    juce::TextEditor fromEditor, toEditor, tailEditor;
    juce::ToggleButton csvToggle { "Also write the automation as CSV (same name)" };
    juce::TextButton cancelButton { "Cancel" }, okButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (RenderDialog)
};
