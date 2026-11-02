// Inspector.h — the panel on the right: details of what is selected.
// Click a value to type a new one.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"
#include "GeneratorPanel.h"

class Inspector : public juce::Component,
                  private juce::ChangeListener,
                  private juce::ValueTree::Listener
{
public:
    Inspector (Project&, PluginHost&, AutomationController&, Selection&);

    std::function<void (const juce::String&)> showStatus;
    ~Inspector() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Field
    {
        juce::Label name, value;
    };

    void refresh();
    void changeListenerCallback (juce::ChangeBroadcaster*) override   { refresh(); }
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override  { refresh(); }
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override             { refresh(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override      { refresh(); }

    Field& addField (const juce::String& name, bool editable);
    juce::AudioProcessorParameter* parameterFor (const juce::ValueTree& lane) const;

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    Selection& selection;

    juce::Label title, itemHeading, pointHeading, hint;
    juce::OwnedArray<Field> fields;
    Field *laneField, *startField, *lengthField, *countField, *timeField, *valueField, *timebaseField, *tempoField;
    juce::ComboBox shapeBox;
    juce::Label generatorHeading;
    GeneratorPanel generatorPanel;

    juce::ValueTree currentItem, currentPoint;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Inspector)
};
