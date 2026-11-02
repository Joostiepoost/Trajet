// GeneratorPanel.h — settings of one item's generator (LFO, random, formula),
// shown in the inspector and in the item editor.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../Model/ItemTime.h"

class GeneratorPanel : public juce::Component,
                       private juce::ValueTree::Listener
{
public:
    explicit GeneratorPanel (Project&);
    ~GeneratorPanel() override;

    /** The item to show (invalid tree = none). */
    void setItem (juce::ValueTree item);

    /** Supplies the lane's plugin parameter, so min/max show in its units. */
    std::function<juce::AudioProcessorParameter* (const juce::ValueTree& lane)> parameterFor;
    std::function<void (const juce::String&)> showStatus;

    /** Height needed for the current item's controls. */
    int getPreferredHeight() const;

    void resized() override;

private:
    void refresh();
    void setProperty (const juce::Identifier&, const juce::var&, const juce::String& action, bool newTransaction = true);
    void applyFormula();
    juce::String valueText (double v) const;

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;

    Project& project;
    juce::ValueTree item;
    bool updating = false;

    juce::Label typeLabel, shapeLabel, rateLabel, minLabel, maxLabel, phaseLabel, seedLabel, formulaLabel, errorLabel, helpLabel;
    juce::ComboBox typeBox, shapeBox;
    juce::Slider rateSlider, minSlider, maxSlider, phaseSlider, seedSlider;
    juce::TextButton newSeedButton { "New" }, bakeButton { "Bake to points" };
    juce::TextEditor formulaEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GeneratorPanel)
};
