// MainComponent.h — everything inside the main window.
// Step 2: projects (new, open, save), undo/redo, and the macOS menu bar,
// on top of step 1's plugin host and parameter list.

#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "PluginWindow.h"
#include "ParameterListComponent.h"
#include "Model/Project.h"

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      private juce::Timer
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    /** Asks to save unsaved changes, then calls quit (if not cancelled). */
    void tryToQuit (std::function<void()> quit);

    //==============================================================================
    // Commands (menu items with keyboard shortcuts)
    enum CommandIDs
    {
        cmdNew = 0x2000, cmdOpen, cmdSave, cmdSaveAs,
        cmdUndo, cmdRedo,
        cmdAudioOnOff, cmdEditPatch
    };

    juce::ApplicationCommandTarget* getNextCommandTarget() override   { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int, int) override {}

private:
    void newProject();
    void openProject();
    void saveProject (bool saveAs);

    void showPluginScanner();
    void showLoadMenu();
    void showAudioSettings();
    void togglePluginWindow();
    void closePluginWindow();
    void loadPluginFromMenu (const juce::PluginDescription&);
    void loadPluginFromProject();
    void pluginChanged();
    void heldNoteControlsChanged();
    void heldNoteModelChanged();
    void setAudioEnabled (bool);
    void updateAudioOnButton();
    void updateWindowTitle();
    void timerCallback() override;

    PluginHost host;
    Project project { host };
    juce::ApplicationCommandManager commandManager;

    juce::TextButton scanButton    { "Plugins..." },
                     loadButton    { "Load plugin" },
                     editButton    { "Edit patch" },
                     audioButton   { "Audio..." },
                     audioOnButton { "Audio on" };
    juce::Label      pluginLabel;
    juce::ToggleButton heldNoteToggle { "Held note" };
    juce::Slider     noteSlider, velocitySlider;

    ParameterListComponent parameterList;
    juce::String statusText, lastUndoState;

    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
