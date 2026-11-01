#include "MainComponent.h"

MainComponent::MainComponent()
{
    setSize (1200, 760);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e2126));

    auto area = getLocalBounds().toFloat();

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (42.0f, juce::Font::bold));
    g.drawText ("Trajet", area.removeFromTop (area.getHeight() * 0.5f).withTrimmedTop (40.0f),
                juce::Justification::centredBottom);

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (16.0f));
    g.drawText ("Step 0: toolchain OK  ·  JUCE " + juce::SystemStats::getJUCEVersion().fromFirstOccurrenceOf (" v", false, false)
                    + "  ·  " + juce::SystemStats::getOperatingSystemName(),
                area.withTrimmedTop (16.0f), juce::Justification::centredTop);
}

void MainComponent::resized()
{
}
