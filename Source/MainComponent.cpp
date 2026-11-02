#include "MainComponent.h"

namespace
{
    const juce::Colour background   (0xff1e2126);
    const juce::Colour toolbarColour(0xff262a31);

    // A middle dot and an em dash, written so they survive any source-file encoding.
    const juce::String dot  = juce::String (juce::CharPointer_UTF8 ("\xc2\xb7"));
    const juce::String dash = juce::String (juce::CharPointer_UTF8 ("\xe2\x80\x94"));

    //==============================================================================
    // A simple window that owns a component; its owner deletes it on close.
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
    host.onPluginChanged       = [this] { pluginChanged(); };
    project.onHeldNoteChanged  = [this] { heldNoteModelChanged(); };
    project.onProjectLoaded    = [this] { loadPluginFromProject(); };

    for (auto* b : { &scanButton, &loadButton, &editButton, &audioButton, &audioOnButton })
        addAndMakeVisible (b);

    scanButton.setTooltip ("Scan for AU and VST3 plugins");
    scanButton.onClick  = [this] { showPluginScanner(); };
    loadButton.onClick  = [this] { showLoadMenu(); };
    editButton.onClick  = [this] { togglePluginWindow(); };
    audioButton.onClick = [this] { showAudioSettings(); };

    // Audio processing on/off, to save CPU and battery while you think and draw.
    audioOnButton.setClickingTogglesState (true);
    audioOnButton.setToggleState (true, juce::dontSendNotification);
    audioOnButton.setTooltip ("Pause audio processing to save CPU (the plugin stays loaded)");
    audioOnButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xff2e7d4f));
    audioOnButton.onClick = [this] { setAudioEnabled (audioOnButton.getToggleState()); };
    updateAudioOnButton();

    pluginLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    pluginLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (pluginLabel);

    // Held note: stored in the project, so it is saved and undoable.
    addAndMakeVisible (heldNoteToggle);
    heldNoteToggle.onClick = [this]
    {
        project.getUndoManager().beginNewTransaction ("Held note");
        heldNoteControlsChanged();
    };

    noteSlider.setSliderStyle (juce::Slider::LinearBar);
    noteSlider.setRange (0, 127, 1);
    noteSlider.textFromValueFunction = [] (double v)
    {
        return "Note " + juce::MidiMessage::getMidiNoteName ((int) v, true, true, 3);
    };
    addAndMakeVisible (noteSlider);

    velocitySlider.setSliderStyle (juce::Slider::LinearBar);
    velocitySlider.setRange (1, 127, 1);
    velocitySlider.textFromValueFunction = [] (double v) { return "Velocity " + juce::String ((int) v); };
    addAndMakeVisible (velocitySlider);

    for (auto* s : { &noteSlider, &velocitySlider })
    {
        // One undo step per drag, not one per pixel.
        s->onDragStart   = [this] { project.getUndoManager().beginNewTransaction ("Held note"); };
        s->onValueChange = [this] { heldNoteControlsChanged(); };
    }

    addAndMakeVisible (transportBar);
    addAndMakeVisible (scoreView);
    addAndMakeVisible (inspector);
    transportBar.onAddLane   = [this] (juce::Component& b) { showParameterPicker (b); };
    transportBar.onZoomToFit = [this] { scoreView.zoomToFit(); };
    scoreView.onStatus       = [this] (const juce::String& text) { showStatus (text); };

    // Snap settings are remembered between sessions.
    {
        auto* settings = host.appProperties.getUserSettings();
        snap.setOn (settings->getBoolValue ("snapOn", true));
        snap.setGrid (settings->getDoubleValue ("snapGrid", 0.1));
    }

    // Commands, keyboard shortcuts and the macOS menu bar.
    commandManager.registerAllCommandsForTarget (this);
    commandManager.setFirstCommandTarget (this);
    addKeyListener (commandManager.getKeyMappings());
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (this);
   #endif
    setApplicationCommandManagerToWatch (&commandManager);

    heldNoteModelChanged();
    pluginChanged();

    // Reopen the last project; without one, reload the last plugin.
    auto lastProject = project.getLastDocumentOpened();
    juce::Component::SafePointer<MainComponent> safeThis (this);

    if (lastProject.existsAsFile())
    {
        project.loadFromAsync (lastProject, false, [safeThis] (juce::Result result)
        {
            if (safeThis != nullptr && result.failed())
                safeThis->project.resetToDefault();
        });
    }
    else if (auto last = host.getLastPluginDescription())
    {
        loadPluginFromMenu (*last);
        project.setChangedFlag (false);
    }

    startTimerHz (4);
    setSize (1200, 760);
}

MainComponent::~MainComponent()
{
    stopTimer();
   #if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu (nullptr);
   #endif
    host.onPluginChanged = nullptr;
    project.onHeldNoteChanged = nullptr;
    project.onProjectLoaded = nullptr;
    pluginWindow = nullptr;     // the plugin's editor must go before the plugin itself
    scannerWindow = nullptr;

    auto* settings = host.appProperties.getUserSettings();
    settings->setValue ("snapOn", snap.isOn());
    settings->setValue ("snapGrid", snap.getGrid());
    settings->saveIfNeeded();
}

//==============================================================================
// Projects

void MainComponent::newProject()
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    project.saveIfNeededAndUserAgreesAsync ([safeThis] (juce::FileBasedDocument::SaveResult r)
    {
        if (safeThis == nullptr || r != juce::FileBasedDocument::savedOk)
            return;

        safeThis->closePluginWindow();
        safeThis->host.unloadPlugin();
        safeThis->project.resetToDefault();
        safeThis->updateWindowTitle();
    });
}

void MainComponent::openProject()
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    project.saveIfNeededAndUserAgreesAsync ([safeThis] (juce::FileBasedDocument::SaveResult r)
    {
        if (safeThis == nullptr || r != juce::FileBasedDocument::savedOk)
            return;

        safeThis->project.loadFromUserSpecifiedFileAsync (true, [safeThis] (juce::Result)
        {
            if (safeThis != nullptr)
                safeThis->updateWindowTitle();
        });
    });
}

void MainComponent::saveProject (bool saveAs)
{
    juce::Component::SafePointer<MainComponent> safeThis (this);
    auto done = [safeThis] (juce::FileBasedDocument::SaveResult)
    {
        if (safeThis != nullptr)
            safeThis->updateWindowTitle();
    };

    if (saveAs || ! project.getFile().existsAsFile())
        project.saveAsInteractiveAsync (true, done);
    else
        project.saveAsync (true, true, done);
}

void MainComponent::tryToQuit (std::function<void()> quit)
{
    project.saveIfNeededAndUserAgreesAsync ([quit] (juce::FileBasedDocument::SaveResult r)
    {
        if (r == juce::FileBasedDocument::savedOk && quit)
            quit();
    });
}

//==============================================================================
// Plugins

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
    juce::Array<juce::PluginDescription> instruments;
    for (auto& t : host.knownPlugins.getTypes())
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
                                safeThis->loadPluginFromMenu (instruments.getReference (index));
                        });
}

void MainComponent::loadPluginFromMenu (const juce::PluginDescription& description)
{
    closePluginWindow();
    statusText = "Loading " + description.name + " ...";
    repaint();

    juce::Component::SafePointer<MainComponent> safeThis (this);
    host.loadPlugin (description, [safeThis, description] (const juce::String& error)
    {
        if (safeThis == nullptr)
            return;

        safeThis->statusText = {};

        if (error.isNotEmpty())
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not load " + description.name, error);
        else
            safeThis->project.setPluginDescription (description);

        safeThis->repaint();
    });
}

void MainComponent::loadPluginFromProject()
{
    closePluginWindow();
    auto description = project.getPluginDescription();

    if (description == nullptr)
    {
        host.unloadPlugin();
        return;
    }

    statusText = "Loading " + description->name + " ...";
    repaint();

    juce::Component::SafePointer<MainComponent> safeThis (this);
    host.loadPlugin (*description, [safeThis, name = description->name] (const juce::String& error)
    {
        if (safeThis == nullptr)
            return;

        safeThis->statusText = {};

        if (error.isNotEmpty())
        {
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                    "Could not load " + name,
                                                    error + "\n\nThe project opened without its plugin.");
        }
        else
        {
            safeThis->project.restorePluginState();
            safeThis->controller.pluginReady();   // start in patch mode, from the restored patch

            // Restoring the patch makes the plugin report changes; those are not edits.
            juce::Timer::callAfterDelay (500, [safeThis]
            {
                if (safeThis != nullptr)
                {
                    safeThis->host.consumePatchChangedFlag();
                    safeThis->project.setChangedFlag (false);
                }
            });
        }

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
    controller.pluginReady();
    scoreView.repaint();
    commandManager.commandStatusChanged();
    repaint();
}

//==============================================================================
// Held note: controls -> model -> (controls + audio)

void MainComponent::heldNoteControlsChanged()
{
    project.setHeldNote (heldNoteToggle.getToggleState(),
                         (int) noteSlider.getValue(),
                         (int) velocitySlider.getValue());
}

void MainComponent::heldNoteModelChanged()
{
    auto heldNote = project.getHeldNote();
    bool on = heldNote[IDs::on];
    int note = heldNote[IDs::note];
    int velocity = heldNote[IDs::velocity];

    heldNoteToggle.setToggleState (on, juce::dontSendNotification);
    noteSlider.setValue (note, juce::dontSendNotification);
    velocitySlider.setValue (velocity, juce::dontSendNotification);
    noteSlider.updateText();
    velocitySlider.updateText();

    host.setHeldNote (on, note, velocity);
}

//==============================================================================
// Audio on/off

void MainComponent::setAudioEnabled (bool enabled)
{
    host.setAudioEnabled (enabled);
    updateAudioOnButton();
}

void MainComponent::updateAudioOnButton()
{
    audioOnButton.setToggleState (host.isAudioEnabled(), juce::dontSendNotification);
    audioOnButton.setButtonText (host.isAudioEnabled() ? "Audio on" : "Audio off");
    audioButton.setEnabled (host.isAudioEnabled());   // device settings only while audio runs
    commandManager.commandStatusChanged();
    repaint();
}

//==============================================================================

void MainComponent::addLaneForParameter (int paramIndex)
{
    auto* plugin = host.getPlugin();
    if (plugin == nullptr || ! juce::isPositiveAndBelow (paramIndex, plugin->getParameters().size()))
        return;

    if (project.findLaneForParameter (paramIndex).isValid())
    {
        showStatus ("That parameter already has a lane.");
        return;
    }

    project.getUndoManager().beginNewTransaction ("Add lane");
    project.addLane (paramIndex, plugin->getParameters()[paramIndex]->getName (128));
}

void MainComponent::showStatus (const juce::String& text)
{
    statusText = text;
    repaint();
    juce::Component::SafePointer<MainComponent> safeThis (this);
    juce::Timer::callAfterDelay (3500, [safeThis, text]
    {
        if (safeThis != nullptr && safeThis->statusText == text)
        {
            safeThis->statusText = {};
            safeThis->repaint();
        }
    });
}

void MainComponent::showParameterPicker (juce::Component& button)
{
    auto* plugin = host.getPlugin();
    if (plugin == nullptr)
        return;

    auto list = std::make_unique<ParameterListComponent>();
    list->setPlugin (plugin);
    list->setSize (560, 460);

    juce::Component::SafePointer<MainComponent> safeThis (this);
    list->onParameterDoubleClicked = [safeThis] (int index)
    {
        if (safeThis != nullptr)
            safeThis->addLaneForParameter (index);
    };

    juce::CallOutBox::launchAsynchronously (std::move (list), button.getScreenBounds(), nullptr);
}

void MainComponent::updateWindowTitle()
{
    if (auto* window = findParentComponentOfClass<juce::DocumentWindow>())
    {
        auto title = project.getDocumentTitle() + (project.hasChangedSinceSaved() ? " *" : "")
                     + "  " + dash + "  Trajet";
        if (window->getName() != title)
            window->setName (title);
    }
}

void MainComponent::timerCallback()
{
    // Turning dials in the plugin's own panel changes the patch: mark the project unsaved.
    if (host.consumePatchChangedFlag() && host.getPlugin() != nullptr)
        project.changed();

    // Keep Undo/Redo menu items in step with the undo history.
    auto& um = project.getUndoManager();
    auto undoState = juce::String (um.canUndo() ? "u" : "-") + (um.canRedo() ? "r" : "-")
                     + um.getUndoDescription() + "|" + um.getRedoDescription();
    if (undoState != lastUndoState)
    {
        lastUndoState = undoState;
        commandManager.commandStatusChanged();
    }

    updateWindowTitle();
    repaint (getLocalBounds().removeFromBottom (26));
}

//==============================================================================
// Commands and menus

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& commands)
{
    commands.addArray ({ cmdNew, cmdOpen, cmdSave, cmdSaveAs, cmdUndo, cmdRedo, cmdAudioOnOff, cmdEditPatch,
                         cmdPlayStop, cmdToStart, cmdPatchMode,
                         cmdDelete, cmdCopy, cmdCut, cmdPaste, cmdDuplicate, cmdSnap,
                         cmdZoomIn, cmdZoomOut, cmdZoomFit });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    const auto cmd = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;

    switch (id)
    {
        case cmdNew:    info.setInfo ("New project", "Start an empty project", "File", 0);
                        info.addDefaultKeypress ('n', cmd); break;
        case cmdOpen:   info.setInfo ("Open...", "Open a project", "File", 0);
                        info.addDefaultKeypress ('o', cmd); break;
        case cmdSave:   info.setInfo ("Save", "Save the project", "File", 0);
                        info.addDefaultKeypress ('s', cmd); break;
        case cmdSaveAs: info.setInfo ("Save as...", "Save the project under a new name", "File", 0);
                        info.addDefaultKeypress ('s', cmd | shift); break;

        case cmdUndo:
        {
            auto& um = project.getUndoManager();
            auto name = um.getUndoDescription();
            info.setInfo (name.isNotEmpty() ? "Undo " + name : juce::String ("Undo"), "Undo", "Edit", 0);
            info.setActive (um.canUndo());
            info.addDefaultKeypress ('z', cmd);
            break;
        }
        case cmdRedo:
        {
            auto& um = project.getUndoManager();
            auto name = um.getRedoDescription();
            info.setInfo (name.isNotEmpty() ? "Redo " + name : juce::String ("Redo"), "Redo", "Edit", 0);
            info.setActive (um.canRedo());
            info.addDefaultKeypress ('z', cmd | shift);
            break;
        }

        case cmdAudioOnOff:
            info.setInfo ("Audio processing", "Turn audio processing on or off", "Audio", 0);
            info.setTicked (host.isAudioEnabled());
            info.addDefaultKeypress ('a', cmd | shift);
            break;

        case cmdEditPatch:
            info.setInfo ("Edit patch", "Open the plugin's own panel", "Audio", 0);
            info.setActive (host.getPlugin() != nullptr);
            info.addDefaultKeypress ('e', cmd);
            break;

        case cmdPlayStop:
            info.setInfo ("Play / stop", "Start or stop playback", "Transport", 0);
            info.setActive (host.getPlugin() != nullptr);
            info.addDefaultKeypress (juce::KeyPress::spaceKey, 0);
            break;

        case cmdToStart:
            info.setInfo ("Back to start", "Move the playhead to the start", "Transport", 0);
            info.addDefaultKeypress (juce::KeyPress::returnKey, 0);
            break;

        case cmdPatchMode:
            info.setInfo ("Show patch", "Show the plain patch (off: follow the score)", "Transport", 0);
            info.setTicked (controller.isPatchMode());
            info.setActive (host.getPlugin() != nullptr);
            info.addDefaultKeypress ('p', 0);
            break;

        case cmdDelete:
            info.setInfo ("Delete", "Delete the selected points or items", "Edit", 0);
            info.addDefaultKeypress (juce::KeyPress::backspaceKey, 0);
            info.addDefaultKeypress (juce::KeyPress::deleteKey, 0);
            break;
        case cmdCopy:      info.setInfo ("Copy", "Copy the selected items", "Edit", 0);
                           info.addDefaultKeypress ('c', cmd); break;
        case cmdCut:       info.setInfo ("Cut", "Cut the selected items", "Edit", 0);
                           info.addDefaultKeypress ('x', cmd); break;
        case cmdPaste:     info.setInfo ("Paste at playhead", "Paste items at the playhead", "Edit", 0);
                           info.addDefaultKeypress ('v', cmd); break;
        case cmdDuplicate: info.setInfo ("Duplicate", "Duplicate the selected items right after themselves", "Edit", 0);
                           info.addDefaultKeypress ('d', cmd); break;
        case cmdSnap:      info.setInfo ("Snap to grid", "Turn snapping on or off", "Edit", 0);
                           info.setTicked (snap.isOn());
                           info.addDefaultKeypress ('s', 0); break;
        case cmdZoomIn:    info.setInfo ("Zoom in", "Zoom in on time", "View", 0);
                           info.addDefaultKeypress ('=', cmd); break;
        case cmdZoomOut:   info.setInfo ("Zoom out", "Zoom out on time", "View", 0);
                           info.addDefaultKeypress ('-', cmd); break;
        case cmdZoomFit:   info.setInfo ("Show whole project", "Fit the whole project in the window", "View", 0);
                           info.addDefaultKeypress ('0', cmd); break;

        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    switch (info.commandID)
    {
        case cmdNew:        newProject(); break;
        case cmdOpen:       openProject(); break;
        case cmdSave:       saveProject (false); break;
        case cmdSaveAs:     saveProject (true); break;
        case cmdUndo:       project.getUndoManager().undo(); break;
        case cmdRedo:       project.getUndoManager().redo(); break;
        case cmdAudioOnOff: setAudioEnabled (! host.isAudioEnabled()); break;
        case cmdEditPatch:  togglePluginWindow(); break;
        case cmdPlayStop:   controller.togglePlay(); break;
        case cmdToStart:    controller.toStart(); break;
        case cmdPatchMode:  controller.setPatchMode (! controller.isPatchMode()); break;
        case cmdDelete:     scoreView.deleteSelection(); break;
        case cmdCopy:       scoreView.copySelection(); break;
        case cmdCut:        scoreView.cutSelection(); break;
        case cmdPaste:      scoreView.paste(); break;
        case cmdDuplicate:  scoreView.duplicateSelection(); break;
        case cmdSnap:       snap.setOn (! snap.isOn()); break;
        case cmdZoomIn:     scoreView.zoomIn(); break;
        case cmdZoomOut:    scoreView.zoomOut(); break;
        case cmdZoomFit:    scoreView.zoomToFit(); break;
        default:            return false;
    }

    commandManager.commandStatusChanged();
    return true;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Transport", "Audio" };
}

juce::PopupMenu MainComponent::getMenuForIndex (int menuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    if (menuIndex == 0)
    {
        menu.addCommandItem (&commandManager, cmdNew);
        menu.addCommandItem (&commandManager, cmdOpen);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdSave);
        menu.addCommandItem (&commandManager, cmdSaveAs);
    }
    else if (menuIndex == 1)
    {
        menu.addCommandItem (&commandManager, cmdUndo);
        menu.addCommandItem (&commandManager, cmdRedo);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdCut);
        menu.addCommandItem (&commandManager, cmdCopy);
        menu.addCommandItem (&commandManager, cmdPaste);
        menu.addCommandItem (&commandManager, cmdDuplicate);
        menu.addCommandItem (&commandManager, cmdDelete);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdSnap);
    }
    else if (menuIndex == 2)
    {
        menu.addCommandItem (&commandManager, cmdZoomIn);
        menu.addCommandItem (&commandManager, cmdZoomOut);
        menu.addCommandItem (&commandManager, cmdZoomFit);
    }
    else if (menuIndex == 3)
    {
        menu.addCommandItem (&commandManager, cmdPlayStop);
        menu.addCommandItem (&commandManager, cmdToStart);
        menu.addSeparator();
        menu.addCommandItem (&commandManager, cmdPatchMode);
    }
    else if (menuIndex == 4)
    {
        menu.addCommandItem (&commandManager, cmdAudioOnOff);
        menu.addCommandItem (&commandManager, cmdEditPatch);
    }

    return menu;
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
    if (! host.isAudioEnabled())
    {
        text = "Audio processing off: the plugin uses no CPU. Click \"Audio off\" to resume.";
    }
    else if (auto* device = host.deviceManager.getCurrentAudioDevice())
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

    // Gesture reminder on the right.
    const juce::String opt  = juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\xa5"));
    const juce::String sh   = juce::String (juce::CharPointer_UTF8 ("\xe2\x87\xa7"));
    const juce::String cmdk = juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x98"));
    const juce::String ctl  = juce::String (juce::CharPointer_UTF8 ("\xe2\x8c\x83"));
    const auto gestures = opt + "-click add  " + dot + "  " + opt + "-drag draw  " + dot + "  " + opt + sh + "-drag ramp  " + dot + "  "
                          + sh + " constrain  " + dot + "  " + cmdk + " fine  " + dot + "  " + ctl + "-drag bend  " + dot + "  "
                          + "S snap  " + dot + "  " + cmdk + "-scroll zoom";
    g.setColour (juce::Colours::white.withAlpha (0.4f));
    g.drawText (gestures, status.reduced (12, 0), juce::Justification::centredRight);
}

void MainComponent::resized()
{
    auto area = getLocalBounds();
    auto toolbar = area.removeFromTop (52).reduced (10, 11);
    area.removeFromBottom (26);

    scanButton.setBounds (toolbar.removeFromLeft (100));   toolbar.removeFromLeft (8);
    loadButton.setBounds (toolbar.removeFromLeft (110));   toolbar.removeFromLeft (8);
    editButton.setBounds (toolbar.removeFromLeft (100));   toolbar.removeFromLeft (16);

    audioButton.setBounds (toolbar.removeFromRight (80));  toolbar.removeFromRight (6);
    audioOnButton.setBounds (toolbar.removeFromRight (84)); toolbar.removeFromRight (16);
    velocitySlider.setBounds (toolbar.removeFromRight (120)); toolbar.removeFromRight (6);
    noteSlider.setBounds (toolbar.removeFromRight (120));  toolbar.removeFromRight (6);
    heldNoteToggle.setBounds (toolbar.removeFromRight (96)); toolbar.removeFromRight (12);

    pluginLabel.setBounds (toolbar);

    transportBar.setBounds (area.removeFromTop (40));
    inspector.setBounds (area.removeFromRight (230));
    scoreView.setBounds (area);
}
