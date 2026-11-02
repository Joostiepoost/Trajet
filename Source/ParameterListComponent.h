// ParameterListComponent.h — a table of all parameters of the loaded plugin,
// with their live values. In step 4 this list becomes the source for lanes.

#pragma once

#include <JuceHeader.h>

class ParameterListComponent : public juce::Component,
                               private juce::TableListBoxModel,
                               private juce::Timer
{
public:
    ParameterListComponent();
    ~ParameterListComponent() override;

    /** Point the list at a plugin (or nullptr when none is loaded). */
    void setPlugin (juce::AudioPluginInstance* newPlugin);

    void resized() override;
    void paint (juce::Graphics&) override;

    /** Called with the parameter index when a row is double-clicked. */
    std::function<void (int)> onParameterDoubleClicked;

private:
    enum ColumnIds { colIndex = 1, colName, colValue, colBar };

    // TableListBoxModel
    int getNumRows() override;
    void paintRowBackground (juce::Graphics&, int row, int width, int height, bool selected) override;
    void paintCell (juce::Graphics&, int row, int columnId, int width, int height, bool selected) override;
    void cellDoubleClicked (int row, int columnId, const juce::MouseEvent&) override;

    // Timer: refresh values while you turn dials in the plugin's editor.
    void timerCallback() override;

    void rebuildVisibleList();
    juce::AudioProcessorParameter* parameterForRow (int row) const;

    juce::AudioPluginInstance* plugin = nullptr;
    std::vector<int> visible;   // parameter indices that pass the filter

    juce::Label titleLabel, countLabel;
    juce::TextEditor filterBox;
    juce::TableListBox table { {}, this };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ParameterListComponent)
};
