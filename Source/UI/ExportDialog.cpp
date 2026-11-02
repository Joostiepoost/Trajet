#include "ExportDialog.h"

namespace
{
    constexpr int rowH = 26, gapH = 6, labelW = 150;
    const double steps[] = { 0.001, 0.005, 0.01, 0.02, 0.05 };
}

ExportDialog::ExportDialog (ExportSettings initial, std::function<void (ExportSettings)> confirmed)
    : settings (initial), onConfirm (std::move (confirmed))
{
    auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f));
        addAndMakeVisible (l);
    };
    label (formatLabel, "Format");
    label (lanesLabel, "Lanes");
    label (stepLabel, "Curve resolution");
    label (curvesLabel, "Points");
    label (ccLabel, "First controller (CC)");
    label (bitsLabel, "Resolution");
    label (channelLabel, "MIDI channel");
    label (tempoLabel, "REAPER project tempo");
    label (templateLabel, "Template project");
    label (templateName, settings.reaperTemplate.existsAsFile() ? settings.reaperTemplate.getFileName() : juce::String ("(none chosen)"));
    templateName.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.7f));
    label (noteLabel, {});
    noteLabel.setFont (juce::FontOptions (12.0f));
    noteLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    noteLabel.setJustificationType (juce::Justification::topLeft);
    label (errorLabel, {});
    errorLabel.setColour (juce::Label::textColourId, juce::Colours::orange);

    formatBox.addItemList ({ "MIDI file (controllers + notes)", "REAPER automation items (one file per lane)",
                             "REAPER project (from a template)" }, 1);
    formatBox.setSelectedItemIndex ((int) settings.format, juce::dontSendNotification);
    formatBox.onChange = [this] { updateVisibility(); };

    lanesBox.addItemList ({ "All lanes", "The active lane's group", "Only the active lane" }, 1);
    lanesBox.setSelectedItemIndex ((int) settings.lanes, juce::dontSendNotification);

    stepBox.addItemList ({ "1 ms", "5 ms", "10 ms", "20 ms", "50 ms" }, 1);
    stepBox.setSelectedItemIndex (2, juce::dontSendNotification);
    for (int i = 0; i < 5; ++i)
        if (juce::approximatelyEqual (steps[i], settings.step)) stepBox.setSelectedItemIndex (i, juce::dontSendNotification);

    curvesBox.addItemList ({ "Keep points editable (curves sampled)", "Sample everything (exact)" }, 1);
    curvesBox.setSelectedItemIndex (settings.exactCurves ? 1 : 0, juce::dontSendNotification);

    bitsBox.addItemList ({ "7-bit (0-127)", "14-bit (CC n + CC n+32)" }, 1);
    bitsBox.setSelectedItemIndex (settings.fourteenBit ? 1 : 0, juce::dontSendNotification);

    for (int i = 1; i <= 16; ++i) channelBox.addItem (juce::String (i), i);
    channelBox.setSelectedId (juce::jlimit (1, 16, settings.midiChannel), juce::dontSendNotification);

    ccEditor.setText (juce::String (settings.firstCC));
    ccEditor.setInputRestrictions (3, "0123456789");
    tempoEditor.setText (juce::String (settings.reaperTempo, 1));
    tempoEditor.setInputRestrictions (6, "0123456789.");

    templateButton.onClick = [this]
    {
        templateChooser = std::make_unique<juce::FileChooser> ("Choose a REAPER project with the plugin on its first track",
                                                               settings.reaperTemplate, "*.rpp;*.RPP");
        templateChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                      [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().existsAsFile())
            {
                settings.reaperTemplate = fc.getResult();
                templateName.setText (settings.reaperTemplate.getFileName(), juce::dontSendNotification);
            }
        });
    };

    for (juce::Component* c : { (juce::Component*) &formatBox, (juce::Component*) &lanesBox, (juce::Component*) &stepBox,
                                (juce::Component*) &curvesBox, (juce::Component*) &bitsBox, (juce::Component*) &channelBox,
                                (juce::Component*) &ccEditor, (juce::Component*) &tempoEditor, (juce::Component*) &templateButton,
                                (juce::Component*) &cancelButton, (juce::Component*) &okButton })
        addAndMakeVisible (c);

    okButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { close(); };
    okButton.addShortcut (juce::KeyPress (juce::KeyPress::returnKey));
    cancelButton.addShortcut (juce::KeyPress (juce::KeyPress::escapeKey));

    updateVisibility();
    setSize (560, 380);
}

void ExportDialog::show (ExportSettings initial, juce::Component* parent, std::function<void (ExportSettings)> confirmed)
{
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned (new ExportDialog (initial, std::move (confirmed)));
    o.dialogTitle = "Export lanes";
    o.dialogBackgroundColour = juce::Colour (0xff262a31);
    o.componentToCentreAround = parent;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.escapeKeyTriggersCloseButton = true;
    o.launchAsync();
}

void ExportDialog::updateVisibility()
{
    const int f = formatBox.getSelectedItemIndex();
    const bool midi = f == 0, reaper = f > 0, project = f == 2;
    for (juce::Component* c : { (juce::Component*) &ccLabel, (juce::Component*) &ccEditor, (juce::Component*) &bitsLabel,
                                (juce::Component*) &bitsBox, (juce::Component*) &channelLabel, (juce::Component*) &channelBox })
        c->setVisible (midi);
    curvesLabel.setVisible (reaper);
    curvesBox.setVisible (reaper);
    tempoLabel.setVisible (f == 1);
    tempoEditor.setVisible (f == 1);
    for (juce::Component* c : { (juce::Component*) &templateLabel, (juce::Component*) &templateName, (juce::Component*) &templateButton })
        c->setVisible (project);

    if (midi)
        noteLabel.setText ("Each automation lane becomes one controller, in lane order from the first CC. Values are sampled at the "
                           "resolution and written when they change. Note lanes become notes. The file is at 120 bpm.",
                           juce::dontSendNotification);
    else if (f == 1)
        noteLabel.setText ("One .ReaperAutoItem file per automation lane, in a folder you choose. Positions are in beats, so set the "
                           "tempo of the REAPER project they go into. Between items the value holds, as in Trajet.",
                           juce::dontSendNotification);
    else
        noteLabel.setText ("Once, in REAPER: put the plugin on the first track of an empty project and save it. Trajet copies that "
                           "project, adds one envelope per lane to the plugin and the notes as a MIDI item.",
                           juce::dontSendNotification);
    resized();
}

void ExportDialog::close()
{
    if (auto* w = findParentComponentOfClass<juce::DialogWindow>())
        w->exitModalState (0);
}

void ExportDialog::confirm()
{
    settings.format = (ExportSettings::Format) juce::jlimit (0, 2, formatBox.getSelectedItemIndex());
    settings.lanes = (ExportSettings::Lanes) juce::jlimit (0, 2, lanesBox.getSelectedItemIndex());
    settings.step = steps[juce::jlimit (0, 4, stepBox.getSelectedItemIndex())];
    settings.exactCurves = curvesBox.getSelectedItemIndex() == 1;
    settings.fourteenBit = bitsBox.getSelectedItemIndex() == 1;
    settings.midiChannel = channelBox.getSelectedId();
    settings.firstCC = juce::jlimit (0, 119, ccEditor.getText().getIntValue());
    settings.reaperTempo = juce::jlimit (10.0, 999.0, tempoEditor.getText().getDoubleValue());

    if (settings.format == ExportSettings::Format::reaperProject && ! settings.reaperTemplate.existsAsFile())
    {
        errorLabel.setText ("Choose a template project first.", juce::dontSendNotification);
        return;
    }
    if (settings.format == ExportSettings::Format::midi && settings.fourteenBit && settings.firstCC > 31)
    {
        errorLabel.setText ("14-bit controllers use CC 0 to 31: choose a lower first CC.", juce::dontSendNotification);
        return;
    }

    auto callback = onConfirm;
    auto s = settings;
    close();
    if (callback)
        juce::MessageManager::callAsync ([callback, s] { callback (s); });
}

void ExportDialog::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262a31));
}

void ExportDialog::resized()
{
    auto r = getLocalBounds().reduced (18, 16);
    auto row = [&r] { auto x = r.removeFromTop (rowH); r.removeFromTop (gapH); return x; };
    auto place = [&] (juce::Label& l, juce::Component& c)
    {
        if (! c.isVisible()) return;
        auto x = row();
        l.setBounds (x.removeFromLeft (labelW));
        c.setBounds (x);
    };

    place (formatLabel, formatBox);
    place (lanesLabel, lanesBox);
    place (stepLabel, stepBox);
    place (curvesLabel, curvesBox);
    place (ccLabel, ccEditor);
    place (bitsLabel, bitsBox);
    place (channelLabel, channelBox);
    place (tempoLabel, tempoEditor);
    if (templateButton.isVisible())
    {
        auto x = row();
        templateLabel.setBounds (x.removeFromLeft (labelW));
        templateButton.setBounds (x.removeFromRight (90));
        templateName.setBounds (x);
    }

    auto buttons = r.removeFromBottom (30);
    okButton.setBounds (buttons.removeFromRight (110));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
    r.removeFromBottom (6);
    errorLabel.setBounds (r.removeFromBottom (22));
    r.removeFromTop (6);
    noteLabel.setBounds (r);
}
