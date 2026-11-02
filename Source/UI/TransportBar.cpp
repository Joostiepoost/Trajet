#include "TransportBar.h"

namespace
{
    juce::String formatTime (double seconds)
    {
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (minutes) + ":" + juce::String (rest, 3).paddedLeft ('0', 6);
    }
}

namespace
{
    const double gridValues[] = { 0.001, 0.01, 0.05, 0.1, 0.25, 0.5, 1.0 };
    const char* const gridNames[] = { "1 ms", "10 ms", "50 ms", "100 ms", "250 ms", "500 ms", "1 s" };
}

TransportBar::TransportBar (Project& p, PluginHost& h, AutomationController& c, SnapSettings& s)
    : project (p), host (h), controller (c), snap (s)
{
    for (auto* b : { &startButton, &playButton, &loopButton, &patchButton, &snapButton, &addLaneButton, &addNotesButton, &fitButton })
        addAndMakeVisible (b);

    snapButton.setClickingTogglesState (true);
    snapButton.setTooltip ("Snap to grid (S). Hold Cmd while dragging to bypass.");
    snapButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3b5f8f));
    snapButton.onClick = [this] { snap.setOn (snapButton.getToggleState()); };

    for (int i = 0; i < 7; ++i)
        gridBox.addItem (gridNames[i], i + 1);
    gridBox.setTooltip ("Grid size");
    gridBox.onChange = [this] { snap.setGrid (gridValues[juce::jlimit (0, 6, gridBox.getSelectedId() - 1)]); };
    addAndMakeVisible (gridBox);
    snap.addChangeListener (this);
    changeListenerCallback (nullptr);

    addLaneButton.setTooltip ("Add a lane for one of the plugin's parameters");
    addLaneButton.onClick = [this] { if (onAddLane) onAddLane (addLaneButton); };
    addNotesButton.setTooltip ("Add a note lane: MIDI notes for the plugin, edited in a piano roll (Shift-Cmd-N)");
    addNotesButton.onClick = [this] { if (onAddNoteLane) onAddNoteLane(); };
    fitButton.setTooltip ("Show the whole project (Cmd-0)");
    fitButton.onClick = [this] { if (onZoomToFit) onZoomToFit(); };

    startButton.setTooltip ("Back to the start (Return)");
    startButton.onClick = [this] { controller.toStart(); };

    playButton.setTooltip ("Play / stop (Space)");
    playButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e7d4f));
    playButton.onClick = [this] { controller.togglePlay(); };

    loopButton.setClickingTogglesState (true);
    loopButton.setTooltip ("Loop the whole project");
    loopButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff3b5f8f));
    loopButton.onClick = [this] { controller.setLooping (loopButton.getToggleState()); };

    patchButton.setClickingTogglesState (true);
    patchButton.setTooltip ("Patch: show the plain patch so you can edit the starting state (P).\n"
                            "Off: the plugin follows the score at the playhead.");
    patchButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff8f5f2e));
    patchButton.onClick = [this] { controller.setPatchMode (patchButton.getToggleState()); };

    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 17.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centred);
    timeLabel.setColour (juce::Label::backgroundColourId, juce::Colours::black.withAlpha (0.3f));
    addAndMakeVisible (timeLabel);

    lengthLabel.setText ("Length (s)", juce::dontSendNotification);
    lengthLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (lengthLabel);

    lengthSlider.setSliderStyle (juce::Slider::IncDecButtons);
    lengthSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 56, 24);
    lengthSlider.setRange (1.0, 7200.0, 1.0);
    lengthSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Project length"); };
    lengthSlider.onValueChange = [this]
    {
        if (! juce::approximatelyEqual (lengthSlider.getValue(), project.getLengthSeconds()))
        {
            if (! lengthSlider.isMouseButtonDown())
                project.getUndoManager().beginNewTransaction ("Project length");
            project.setLengthSeconds (lengthSlider.getValue());
        }
    };
    addAndMakeVisible (lengthSlider);

    intervalLabel.setText ("Live update", juce::dontSendNotification);
    intervalLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (intervalLabel);

    intervalBox.addItem ("per buffer", 1);
    int id = 2;
    for (int samples : { 256, 128, 64, 32, 16 })
        intervalBox.addItem (juce::String (samples) + " samples", id++);
    intervalBox.setTooltip ("How often parameters are updated during live playback.\n"
                            "Smaller = finer, but more CPU. Rendering always uses fine resolution.");
    {
        const int current = controller.getLiveInterval();
        const int ids[] = { 0, 256, 128, 64, 32, 16 };
        for (int i = 0; i < 6; ++i)
            if (ids[i] == current)
                intervalBox.setSelectedId (i + 1, juce::dontSendNotification);
        if (intervalBox.getSelectedId() == 0)
            intervalBox.setSelectedId (1, juce::dontSendNotification);
    }
    intervalBox.onChange = [this]
    {
        const int ids[] = { 0, 256, 128, 64, 32, 16 };
        controller.setLiveInterval (ids[juce::jlimit (0, 5, intervalBox.getSelectedId() - 1)]);
    };
    addAndMakeVisible (intervalBox);

    project.getState().addListener (this);
    updateLength();
    startTimerHz (20);
}

TransportBar::~TransportBar()
{
    stopTimer();
    snap.removeChangeListener (this);
    project.getState().removeListener (this);
}

void TransportBar::changeListenerCallback (juce::ChangeBroadcaster*)
{
    snapButton.setToggleState (snap.isOn(), juce::dontSendNotification);
    for (int i = 0; i < 7; ++i)
        if (std::abs (gridValues[i] - snap.getGrid()) < 1.0e-9)
            gridBox.setSelectedId (i + 1, juce::dontSendNotification);
}

void TransportBar::updateLength()
{
    lengthSlider.setValue (project.getLengthSeconds(), juce::dontSendNotification);
}

void TransportBar::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier& property)
{
    if (tree.hasType (IDs::PROJECT) && property == IDs::lengthSeconds)
        updateLength();
}

void TransportBar::timerCallback()
{
    auto& engine = host.getEngine();
    timeLabel.setText (formatTime (engine.getPositionSeconds()), juce::dontSendNotification);

    const bool playing = engine.isPlaying();
    playButton.setButtonText (playing ? "Stop" : "Play");
    playButton.setToggleState (playing, juce::dontSendNotification);
    patchButton.setToggleState (controller.isPatchMode(), juce::dontSendNotification);
    loopButton.setToggleState (engine.isLooping(), juce::dontSendNotification);

    const bool hasPlugin = host.getPlugin() != nullptr;
    playButton.setEnabled (hasPlugin);
    patchButton.setEnabled (hasPlugin);
    addLaneButton.setEnabled (hasPlugin);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff2a2f37));
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (10, 6);

    startButton.setBounds (r.removeFromLeft (40));  r.removeFromLeft (6);
    playButton.setBounds (r.removeFromLeft (70));   r.removeFromLeft (6);
    loopButton.setBounds (r.removeFromLeft (60));   r.removeFromLeft (12);
    timeLabel.setBounds (r.removeFromLeft (120));   r.removeFromLeft (12);
    patchButton.setBounds (r.removeFromLeft (64));  r.removeFromLeft (20);
    snapButton.setBounds (r.removeFromLeft (56));   r.removeFromLeft (4);
    gridBox.setBounds (r.removeFromLeft (90));      r.removeFromLeft (12);
    fitButton.setBounds (r.removeFromLeft (44));    r.removeFromLeft (20);
    addLaneButton.setBounds (r.removeFromLeft (76));
    r.removeFromLeft (4);
    addNotesButton.setBounds (r.removeFromLeft (76));

    intervalBox.setBounds (r.removeFromRight (130)); r.removeFromRight (6);
    intervalLabel.setBounds (r.removeFromRight (90)); r.removeFromRight (16);
    lengthSlider.setBounds (r.removeFromRight (120)); r.removeFromRight (6);
    lengthLabel.setBounds (r.removeFromRight (80));
}
