#include "RenderDialog.h"
#include "EditorState.h"

namespace
{
    constexpr int rowH = 26, gapH = 6, labelW = 150;
    const double rates[]    = { 44100.0, 48000.0, 88200.0, 96000.0 };
    const int    depths[]   = { 16, 24, 32 };
    const int    intervals[] = { 1, 4, 16, 64 };
    const double steps[]    = { 0.001, 0.005, 0.01, 0.02, 0.05, 0.1 };
}

RenderDialog::RenderDialog (Mode m, RenderSettings initial, double length, const juce::String& heldNoteText,
                            std::function<void (RenderSettings)> confirmed)
    : mode (m), settings (initial), projectLength (length), onConfirm (std::move (confirmed))
{
    auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    };
    label (rangeLabel, "Range");
    label (fromLabel, "From");
    label (toLabel, "to");
    label (rateLabel, "Sample rate");
    label (formatLabel, "Format");
    label (tailLabel, "Tail after the end (s)");
    label (intervalLabel, "Automation update");
    label (heldNoteLabel, heldNoteText);
    label (stepLabel, "CSV: one row every");
    label (valuesLabel, "CSV values");
    label (noteLabel, mode == Mode::render
                          ? "Muted lanes play their patch value, as in playback. The audio output is silent while rendering."
                          : "Every lane is exported, muted or not. Before a lane's first item the cell holds its patch value.");
    noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    noteLabel.setJustificationType (juce::Justification::topLeft);
    heldNoteLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    label (errorLabel, {});
    errorLabel.setColour (juce::Label::textColourId, juce::Colours::orange);

    rangeBox.addItem ("Whole score", 1);
    rangeBox.addItem ("From... to...", 2);
    const bool whole = settings.start <= 0.0 && settings.end >= length - 1.0e-6;
    rangeBox.setSelectedId (whole ? 1 : 2, juce::dontSendNotification);
    rangeBox.onChange = [this] { updateEnablement(); };
    addAndMakeVisible (rangeBox);

    for (auto* e : { &fromEditor, &toEditor, &tailEditor })
    {
        e->setJustification (juce::Justification::centredLeft);
        addAndMakeVisible (e);
    }
    fromEditor.setText (TimeFormat::format (whole ? 0.0 : settings.start));
    toEditor.setText (TimeFormat::format (whole ? length : settings.end));
    tailEditor.setText (juce::String (settings.tail, 1));

    rateBox.addItemList ({ "44.1 kHz", "48 kHz", "88.2 kHz", "96 kHz" }, 1);
    rateBox.setSelectedItemIndex (1, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
        if (juce::approximatelyEqual (rates[i], settings.sampleRate)) rateBox.setSelectedItemIndex (i, juce::dontSendNotification);

    formatBox.addItemList ({ "WAV 16-bit", "WAV 24-bit", "WAV 32-bit float" }, 1);
    formatBox.setSelectedItemIndex (1, juce::dontSendNotification);
    for (int i = 0; i < 3; ++i)
        if (depths[i] == settings.bitDepth) formatBox.setSelectedItemIndex (i, juce::dontSendNotification);

    intervalBox.addItemList ({ "every sample (slowest)", "every 4 samples", "every 16 samples", "every 64 samples" }, 1);
    intervalBox.setSelectedItemIndex (2, juce::dontSendNotification);
    for (int i = 0; i < 4; ++i)
        if (intervals[i] == settings.interval) intervalBox.setSelectedItemIndex (i, juce::dontSendNotification);

    stepBox.addItemList ({ "1 ms", "5 ms", "10 ms", "20 ms", "50 ms", "100 ms" }, 1);
    stepBox.setSelectedItemIndex (2, juce::dontSendNotification);
    for (int i = 0; i < 6; ++i)
        if (juce::approximatelyEqual (steps[i], settings.csvStep)) stepBox.setSelectedItemIndex (i, juce::dontSendNotification);

    valuesBox.addItemList ({ "Normalised 0 to 1", "Plugin units (as the plugin shows them)" }, 1);
    valuesBox.setSelectedItemIndex (settings.csvPluginUnits ? 1 : 0, juce::dontSendNotification);

    for (auto* b : { &rateBox, &formatBox, &intervalBox, &stepBox, &valuesBox })
        addAndMakeVisible (b);

    csvToggle.setToggleState (settings.writeCsv, juce::dontSendNotification);
    csvToggle.onClick = [this] { updateEnablement(); };
    addAndMakeVisible (csvToggle);

    okButton.setButtonText (mode == Mode::render ? "Render..." : "Export...");
    okButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { close(); };
    okButton.addShortcut (juce::KeyPress (juce::KeyPress::returnKey));
    cancelButton.addShortcut (juce::KeyPress (juce::KeyPress::escapeKey));
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);

    const bool audio = mode == Mode::render;
    for (juce::Component* c : { (juce::Component*) &rateLabel, (juce::Component*) &rateBox, (juce::Component*) &formatLabel,
                                (juce::Component*) &formatBox, (juce::Component*) &tailLabel, (juce::Component*) &tailEditor,
                                (juce::Component*) &intervalLabel, (juce::Component*) &intervalBox,
                                (juce::Component*) &heldNoteLabel, (juce::Component*) &csvToggle })
        c->setVisible (audio);

    updateEnablement();
    setSize (520, audio ? 450 : 250);
}

void RenderDialog::show (Mode m, RenderSettings initial, double length, const juce::String& heldNoteText,
                         juce::Component* parent, std::function<void (RenderSettings)> confirmed)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new RenderDialog (m, initial, length, heldNoteText, std::move (confirmed)));
    o.dialogTitle = m == Mode::render ? "Render to WAV" : "Export automation as CSV";
    o.dialogBackgroundColour = juce::Colour (0xff262a31);
    o.componentToCentreAround = parent;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    o.launchAsync();
}

void RenderDialog::updateEnablement()
{
    const bool custom = rangeBox.getSelectedId() == 2;
    fromEditor.setEnabled (custom);
    toEditor.setEnabled (custom);
    const bool csv = mode == Mode::csv || csvToggle.getToggleState();
    stepBox.setEnabled (csv);
    valuesBox.setEnabled (csv);
}

void RenderDialog::close()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
        w->exitModalState (0);
}

void RenderDialog::confirm()
{
    if (rangeBox.getSelectedId() == 1)
    {
        settings.start = 0.0;
        settings.end = projectLength;
    }
    else
    {
        settings.start = juce::jlimit (0.0, projectLength, TimeFormat::parse (fromEditor.getText()));
        settings.end   = juce::jlimit (0.0, projectLength, TimeFormat::parse (toEditor.getText()));
    }
    if (settings.end - settings.start < 0.01)
    {
        errorLabel.setText ("The range is empty: 'to' must come after 'from'.", juce::dontSendNotification);
        return;
    }

    settings.sampleRate = rates[juce::jlimit (0, 3, rateBox.getSelectedItemIndex())];
    settings.bitDepth   = depths[juce::jlimit (0, 2, formatBox.getSelectedItemIndex())];
    settings.tail       = juce::jlimit (0.0, 60.0, tailEditor.getText().getDoubleValue());
    settings.interval   = intervals[juce::jlimit (0, 3, intervalBox.getSelectedItemIndex())];
    settings.writeCsv   = csvToggle.getToggleState();
    settings.csvStep    = steps[juce::jlimit (0, 5, stepBox.getSelectedItemIndex())];
    settings.csvPluginUnits = valuesBox.getSelectedItemIndex() == 1;

    auto callback = onConfirm;
    auto s = settings;
    close();    // deletes this component
    if (callback)
        juce::MessageManager::callAsync ([callback, s] { callback (s); });
}

void RenderDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262a31));
}

void RenderDialog::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    auto row = [&r] { auto x = r.removeFromTop (rowH); r.removeFromTop (gapH); return x; };
    auto place = [&] (juce::Label& l, juce::Component& c)
    {
        auto x = row();
        l.setBounds (x.removeFromLeft (labelW));
        c.setBounds (x);
    };

    place (rangeLabel, rangeBox);
    {
        auto x = row();
        fromLabel.setBounds (x.removeFromLeft (labelW).withTrimmedLeft (16));
        fromEditor.setBounds (x.removeFromLeft (110));
        toLabel.setBounds (x.removeFromLeft (34).withTrimmedLeft (8));
        toEditor.setBounds (x.removeFromLeft (110));
    }
    r.removeFromTop (8);

    if (mode == Mode::render)
    {
        place (rateLabel, rateBox);
        place (formatLabel, formatBox);
        place (tailLabel, tailEditor);
        place (intervalLabel, intervalBox);
        heldNoteLabel.setBounds (row().withTrimmedLeft (labelW));
        r.removeFromTop (8);
        csvToggle.setBounds (row());
    }
    place (stepLabel, stepBox);
    place (valuesLabel, valuesBox);

    auto buttons = r.removeFromBottom (30);
    okButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
    r.removeFromBottom (6);
    errorLabel.setBounds (r.removeFromBottom (22));
    noteLabel.setBounds (r);
}
