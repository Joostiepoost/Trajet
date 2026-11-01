#include "MainComponent.h"

namespace
{
    const juce::Colour background   (0xff1e2126);
    const juce::Colour toolbarColour(0xff262a31);

    // A middle dot, written so it survives any source-file encoding.
    const juce::String dot = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"));

    //==============================================================================
    // A simple window that owns a component and deletes itself from its owner on close.
    class ToolWindow : public juce::DocumentWindow
    {
    public:
        ToolWindow (const juce::String& title, juce::Component* content, std::function<void()> onCloseRequested)
            : DocumentWindow (title, background, DocumentWindow::closeButton),
              onClose (std::move (onCloseRequested))
        {
            setUsingNativeTitleBar (true);
            setContentOwned (content, true);
            setResizable (true, false);
            centreWithSize (content->getWidth(), content->getHeight());
            setVisible (true);
        }

        void closeButtonPressed() override   { if (onClose) onClose(); }

    private:
        std::function<void()> onClose;
    };
}

//==============================================================================
MainComponent::MainComponent()
{
    host.onPluginChanged = [this] { pluginChanged(); };

    for (auto* b : { &scanButton, &loadButton, &editButton, &audioButton })
        addAndMakeVisible (b);

    scanButton.setTooltip ("Scan for AU and VST3 plugins");
    scanButton.onClick  = [this] { showPluginScanner(); };
    loadButton.onClick  = [this] { showLoadMenu(); };
    editButton.onClick  = [this] { togglePluginWindow(); };
    audioButton.onClick = [this] { showAudioSettings(); };

    pluginLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    pluginLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pluginLabel);

    // Held note: note number (shown as a note name) and velocity.
    addAndMakeVisible (heldNoteToggle);
    heldNoteToggle.onClick = [this] { updateHeldNote(); };

    noteSlider.setSliderStyle (juce::Slider::LinearBar);
    noteSlider.setRange (0, 127, 1);
    noteSlider.setValue (48, juce::dontSendNotification);
    noteSlider.textFromValueFunction = [] (double v)
    {
        return "Note " + juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3);
    };
    noteSlider.onValueChange = [this] { updateHeldNote(); };
    noteSlider.updateText();
    addAndMakeVisible (noteSlider);

    velocitySlider.setSliderStyle (juce::Slider::LinearBar);
    velocitySlider.setRange (1, 127, 1);
    velocitySlider.setValue (100, juce::dontSendNotification);
    velocitySlider.textFromValueFunction = [] (double v) { return "Velocity " + juce::String ((int) v); };
    velocitySlider.onValueChange = [this] { updateHeldNote(); };
    velocitySlider.updateText();
    addAndMakeVisible (velocitySlider);

    addAndMakeVisible (parameterList);

    pluginChanged();

    // Reload the plugin used last time, if any.
    if (auto last = host.getLastPluginDescription())
        loadPlugin (*last);

    startTimerHz (2);
    setSize (1200, 760);
}

MainComponent::~MainComponent()
{
    stopTimer();
    host.onPluginChanged = nullptr;
    pluginWindow = nullptr;     // the plugin's editor must go before the plugin itself
    scannerWindow = nullptr;
    parameterList.setPlugin (nullptr);
}

//==============================================================================
void MainComponent::showPluginScanner()
{
    if (scannerWindow != nullptr)
    {
        scannerWindow->toFront (true);
        return;
    }

    auto* list = new juce::PluginListComponent (host.formatManager, host.knownPlugins,
                                                host.getDeadMansPedalFile(),
                                                host.appProperties.getUserSettings(), true);
    list->setSize (760, 520);

    juce::Component::SafePointer<MainComponent> safeThis (this);
    scannerWindow = std::make_unique<ToolWindow> ("Plugins", list, [safeThis]
    {
        juce::MessageManager::callAsync ([safeThis]
        {
            if (safeThis != nullptr)
                safeThis->scannerWindow = nullptr;
        });
    });
}

void MainComponent::showLoadMenu()
{
    auto types = host.knownPlugins.getTypes();

    // Only instruments are interesting here.
    juce::Array<juce::PluginDescription> instruments;
    for (auto& t : types)
        if (t.isInstrument)
            instruments.add (t);

    juce::PopupMenu menu;

    if (instruments.isEmpty())
    {
        menu.addItem (1, "No instruments found: click \"Plugins...\" and scan first", false);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loadButton));
        return;
    }

    auto* current = host.getPlugin();
    juce::KnownPluginList::addToMenu (menu, instruments, juce::KnownPluginList::sortByManufacturer,
                                      current != nullptr ? current->getPluginDescription().createIdentifierString()
                                                         : juce::String());

    juce::Component::SafePointer<MainComponent> safeThis (this);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (loadButton),
                        [safeThis, instruments] (int result)
                        {
                            if (safeThis == nullptr || result == 0)
                                return;

                            auto index = juce::KnownPluginList::getIndexChosenByMenu (instruments, result);
                            if (juce::isPositiveAndBelow (index, instruments.size()))
                                safeThis->loadPlugin (instruments.getReference (index));
                        });
}

void MainComponent::loadPlugin (const juce::PluginDescription& description)
{
    closePluginWindow();
    statusText = "Loading " + description.name + " ...";
    repaint();

    juce::Component::SafePointer<MainComponent> safeThis (this);
    host.loadPlugin (description, [safeThis, name = description.name] (const juce::String& error)
    {
        if (safeThis == nullptr)
            return;

        safeThis->statusText = {};

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not load " + name, error);
        safeThis->repaint();
    });
}

void MainComponent::showAudioSettings()
{
    auto* selector = new juce::AudioDeviceSelectorComponent (host.deviceManager,
                                                             0, 0,     // no audio inputs
                                                             2, 2,     // stereo output
                                                             false,    // no MIDI inputs
                                                             false,    // no MIDI output
                                                             true,     // stereo pairs
                                                             false);
    selector->setSize (520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned (selector);
    options.dialogTitle = "Audio settings";
    options.dialogBackgroundColour = background;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.launchAsync();
}

void MainComponent::togglePluginWindow()
{
    if (pluginWindow != nullptr)
    {
        closePluginWindow();
        return;
    }

    if (auto* plugin = host.getPlugin())
    {
        juce::Component::SafePointer<MainComponent> safeThis (this);
        pluginWindow = std::make_unique<PluginWindow> (*plugin, [safeThis]
        {
            juce::MessageManager::callAsync ([safeThis]
            {
                if (safeThis != nullptr)
                    safeThis->closePluginWindow();
            });
        });
    }
}

void MainComponent::closePluginWindow()
{
    pluginWindow = nullptr;
}

void MainComponent::pluginChanged()
{
    auto* plugin = host.getPlugin();

    if (plugin == nullptr)
        closePluginWindow();

    pluginLabel.setText (plugin != nullptr
                             ? plugin->getName() + "  (" + plugin->getPluginDescription().pluginFormatName + ")"
                             : juce::String ("No plugin loaded"),
                         juce::dontSendNotification);

    editButton.setEnabled (plugin != nullptr);
    parameterList.setPlugin (plugin);
    repaint();
}

void MainComponent::updateHeldNote()
{
    host.setHeldNote (heldNoteToggle.getToggleState(),
                      (int) noteSlider.getValue(),
                      (int) velocitySlider.getValue());
}

void MainComponent::timerCallback()
{
    repaint (getLocalBounds().removeFromBottom (26));
}

//==============================================================================
void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (background);

    auto area = getLocalBounds();
    g.setColour (toolbarColour);
    g.fillRect (area.removeFromTop (52));

    // Status bar: audio device, sample rate, buffer, CPU.
    auto status = area.removeFromBottom (26);
    g.setColour (toolbarColour);
    g.fillRect (status);

    juce::String text;
    if (auto* device = host.deviceManager.getCurrentAudioDevice())
    {
        text << device->getName() << "   " << dot << "   "
             << juce::String (device->getCurrentSampleRate() / 1000.0, 1) << " kHz   " << dot << "   "
             << device->getCurrentBufferSizeSamples() << " samples   " << dot << "   CPU "
             << juce::String (host.deviceManager.getCpuUsage() * 100.0, 1) << " %";
    }
    else
    {
        text = "No audio device: click \"Audio...\"";
    }

    if (statusText.isNotEmpty())
        text = statusText + "     " + text;

    g.setColour (juce::Colours::white.withAlpha (0.6f));
    g.setFont (juce::FontOptions (12.0f));
    g.drawText (text, status.reduced (12, 0), juce::Justification::centredLeft);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (52).reduced (10, 11);
    area.removeFromBottom (26);

    scanButton.setBounds (toolbar.removeFromLeft (100));   toolbar.removeFromLeft (8);
    loadButton.setBounds (toolbar.removeFromLeft (110));   toolbar.removeFromLeft (8);
    editButton.setBounds (toolbar.removeFromLeft (100));   toolbar.removeFromLeft (16);

    audioButton.setBounds (toolbar.removeFromRight (90));  toolbar.removeFromRight (16);
    velocitySlider.setBounds (toolbar.removeFromRight (120)); toolbar.removeFromRight (6);
    noteSlider.setBounds (toolbar.removeFromRight (120));  toolbar.removeFromRight (6);
    heldNoteToggle.setBounds (toolbar.removeFromRight (96)); toolbar.removeFromRight (12);

    pluginLabel.setBounds (toolbar);

    parameterList.setBounds (area);
}
