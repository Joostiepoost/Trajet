// ExportDialog.h — settings for File > Export lanes (MIDI file, REAPER automation items,
// REAPER project from a template).

#pragma once

#include <JuceHeader.h>
#include "../Render/Exporter.h"

class ExportDialog : public juce::Component
{
public:
    ExportDialog (ExportSettings initial, std::function<void (ExportSettings)> onConfirm);

    static void show (ExportSettings initial, juce::Component* parent, std::function<void (ExportSettings)> onConfirm);

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateVisibility();
    void confirm();
    void close();

    ExportSettings settings;
    std::function<void (ExportSettings)> onConfirm;
    std::unique_ptr<juce::FileChooser> templateChooser;

    juce::Label formatLabel, lanesLabel, stepLabel, curvesLabel, ccLabel, bitsLabel, channelLabel, tempoLabel,
                templateLabel, templateName, noteLabel, errorLabel;
    juce::ComboBox formatBox, lanesBox, stepBox, curvesBox, bitsBox, channelBox;
    juce::TextEditor ccEditor, tempoEditor;
    juce::TextButton templateButton { "Choose..." }, cancelButton { "Cancel" }, okButton { "Export..." };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ExportDialog)
};
