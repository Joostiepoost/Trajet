// MainComponent.h — everything inside the main window.
// Step 2: projects (new, open, save), undo/redo, and the macOS menu bar,
// on top of step 1's plugin host and parameter list.

#pragma once

#include <JuceHeader.h>
#include "PluginHost.h"
#include "PluginWindow.h"
#include "ParameterListComponent.h"
#include "Model/Project.h"
#include "Engine/AutomationController.h"
#include "UI/ScoreView.h"
#include "UI/Inspector.h"
#include "UI/ItemEditor.h"
#include "UI/NoteEditor.h"
#include "UI/TransportBar.h"
#include "UI/RenderDialog.h"
#include "UI/OutputStrip.h"
#include "UI/ExportDialog.h"
#include "UI/LibraryPanel.h"

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::MenuBarModel,
                      public juce::DragAndDropContainer,
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
        cmdAudioOnOff, cmdEditPatch,
        cmdPlayStop, cmdToStart, cmdPatchMode,
        cmdDelete, cmdCopy, cmdCut, cmdPaste, cmdDuplicate, cmdSnap,
        cmdZoomIn, cmdZoomOut, cmdZoomFit,
        cmdNudgeLeft, cmdNudgeRight, cmdNudgeUp, cmdNudgeDown,
        cmdNudgeLeftBig, cmdNudgeRightBig, cmdNudgeUpBig, cmdNudgeDownBig,
        cmdSelectAll, cmdSmooth, cmdThin, cmdReverse, cmdInvert, cmdOpenItemEditor,
        cmdRender, cmdExportCsv,
        cmdNewGroup, cmdFoldAll, cmdOpenAll, cmdAddNoteLane, cmdExportLanes, cmdSaveToLibrary, cmdShowLibrary
    };

    juce::ApplicationCommandTarget* getNextCommandTarget() override   { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>&) override;
    void getCommandInfo (juce::CommandID, juce::ApplicationCommandInfo&) override;
    bool perform (const InvocationInfo&) override;

    // MenuBarModel
    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex (int menuIndex, const juce::String& menuName) override;
    void menuItemSelected (int menuItemID, int topLevelMenuIndex) override;

private:
    void newProject();
    void openProject();
    void openRecentProject (const juce::File&);
    juce::RecentlyOpenedFilesList getRecentFiles();
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
    void addLaneForParameter (int paramIndex);
    void addNoteLane();
    void placeNewLane (juce::ValueTree lane);
    void showParameterPicker (juce::Component& button);
    void showStatus (const juce::String& text);
    void openItemEditor (juce::ValueTree item);
    void showRenderDialog (bool csvOnly);
    void showExportDialog();
    void saveSelectedItemToLibrary();
    void showLibrary();
    juce::File defaultExportFile (const juce::String& extension) const;

    PluginHost host;
    Project project { host };
    AutomationController controller { project, host };
    juce::ApplicationCommandManager commandManager;

    juce::TextButton scanButton    { "Plugins..." },
                     loadButton    { "Load plugin" },
                     editButton    { "Edit patch" },
                     audioButton   { "Audio..." },
                     audioOnButton { "Audio on" };
    juce::Label      pluginLabel;
    juce::ToggleButton heldNoteToggle { "Held note" };
    juce::Slider     noteSlider, velocitySlider;

    SnapSettings snap;
    TransportBar transportBar { project, host, controller, snap };
    ScoreView scoreView { project, host, controller, snap };
    Inspector inspector { project, host, controller, scoreView.getSelection() };
    Library library { *host.appProperties.getUserSettings() };
    LibraryPanel libraryPanel { library, project, scoreView.getSelection(), [this] { return host.getEngine().getPositionSeconds(); } };
    juce::TabbedComponent rightTabs { juce::TabbedButtonBar::TabsAtTop };
    OutputStrip outputStrip { project, host };
    juce::String statusText, lastUndoState;

    std::unique_ptr<PluginWindow> pluginWindow;
    std::unique_ptr<juce::DocumentWindow> scannerWindow;
    std::unique_ptr<ItemEditorWindow> itemEditorWindow;
    std::unique_ptr<juce::FileChooser> exportChooser;
    juce::File lastExportFolder, lastTitleFile;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
