#include "ParameterListComponent.h"

ParameterListComponent::ParameterListComponent()
{
    titleLabel.setText ("Parameters", juce::dontSendNotification);
    titleLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    countLabel.setJustificationType (juce::Justification::centredRight);
    countLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (countLabel);

    filterBox.setTextToShowWhenEmpty ("Filter parameters (e.g. osc, gate, delay)", juce::Colours::grey);
    filterBox.onTextChange = [this] { rebuildVisibleList(); };
    addAndMakeVisible (filterBox);

    auto& header = table.getHeader();
    header.addColumn ("#",          colIndex, 50,  40,  80,  juce::TableHeaderComponent::notSortable);
    header.addColumn ("Parameter",  colName,  280, 120, 600, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Value",      colValue, 180, 80,  400, juce::TableHeaderComponent::notSortable);
    header.addColumn ("Normalised", colBar,   260, 80,  800, juce::TableHeaderComponent::notSortable);
    header.setStretchToFitActive (true);

    table.setRowHeight (24);
    table.setColour (juce::ListBox::backgroundColourId, juce::Colour (0xff1e2126));
    addAndMakeVisible (table);

    rebuildVisibleList();
    startTimerHz (15);
}

ParameterListComponent::~ParameterListComponent()
{
    stopTimer();
}

void ParameterListComponent::setPlugin (juce::AudioPluginInstance* newPlugin)
{
    plugin = newPlugin;
    titleLabel.setText (plugin != nullptr ? "Parameters of " + plugin->getName() + "  (double-click to add a lane)"
                                         : juce::String ("Parameters"),
                        juce::dontSendNotification);
    rebuildVisibleList();
}

void ParameterListComponent::rebuildVisibleList()
{
    visible.clear();

    if (plugin != nullptr)
    {
        auto filter = filterBox.getText().trim();
        auto& params = plugin->getParameters();

        for (int i = 0; i < params.size(); ++i)
            if (filter.isEmpty() || params[i]->getName (128).containsIgnoreCase (filter))
                visible.push_back (i);

        countLabel.setText (juce::String (params.size()) + " parameters, "
                                + juce::String ((int) visible.size()) + " shown",
                            juce::dontSendNotification);
    }
    else
    {
        countLabel.setText ("No plugin loaded", juce::dontSendNotification);
    }

    table.updateContent();
    table.repaint();
}

juce::AudioProcessorParameter* ParameterListComponent::parameterForRow (int row) const
{
    if (plugin == nullptr || row < 0 || row >= (int) visible.size())
        return nullptr;

    auto& params = plugin->getParameters();
    auto index = visible[(size_t) row];
    return index < params.size() ? params[index] : nullptr;
}

int ParameterListComponent::getNumRows()
{
    return (int) visible.size();
}

void ParameterListComponent::paintRowBackground (juce::Graphics& g, int row, int, int, bool selected)
{
    if (selected)
        g.fillAll (juce::Colour (0xff2f4a6d));
    else if (row % 2 == 1)
        g.fillAll (juce::Colours::white.withAlpha (0.03f));
}

void ParameterListComponent::paintCell (juce::Graphics& g, int row, int columnId,
                                        int width, int height, bool)
{
    auto* param = parameterForRow (row);
    if (param == nullptr)
        return;

    auto bounds = juce::Rectangle<int> (0, 0, width, height).reduced (6, 0);
    g.setFont (juce::FontOptions (13.0f));

    switch (columnId)
    {
        case colIndex:
            g.setColour (juce::Colours::white.withAlpha (0.5f));
            g.drawText (juce::String (visible[(size_t) row]), bounds, juce::Justification::centredLeft);
            break;

        case colName:
            g.setColour (juce::Colours::white);
            g.drawText (param->getName (128), bounds, juce::Justification::centredLeft);
            break;

        case colValue:
        {
            auto text = param->getCurrentValueAsText();
            auto label = param->getLabel();
            if (label.isNotEmpty() && ! text.endsWith (label))
                text << " " << label;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            g.drawText (text, bounds, juce::Justification::centredLeft);
            break;
        }

        case colBar:
        {
            auto v = juce::jlimit (0.0f, 1.0f, param->getValue());
            auto bar = bounds.reduced (0, 7).toFloat();
            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (juce::Colour (0xff5b9bd5));
            g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v), 3.0f);
            g.setColour (juce::Colours::white.withAlpha (0.7f));
            g.setFont (juce::FontOptions (11.0f));
            g.drawText (juce::String (v, 3), bounds, juce::Justification::centredRight);
            break;
        }

        default:
            break;
    }
}

void ParameterListComponent::cellDoubleClicked (int row, int, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) visible.size()) && onParameterDoubleClicked)
        onParameterDoubleClicked (visible[(size_t) row]);
}

void ParameterListComponent::timerCallback()
{
    if (plugin != nullptr && isShowing())
        table.repaint();
}

void ParameterListComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));
}

void ParameterListComponent::resized()
{
    auto area = getLocalBounds().reduced (12, 8);

    auto headerRow = area.removeFromTop (28);
    countLabel.setBounds (headerRow.removeFromRight (240));
    titleLabel.setBounds (headerRow);

    area.removeFromTop (4);
    filterBox.setBounds (area.removeFromTop (28));
    area.removeFromTop (8);
    table.setBounds (area);
}
