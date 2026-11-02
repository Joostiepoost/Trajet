// Project.h — one Trajet project: the data model, undo, and loading/saving.
//
// The project file (*.trajet) is plain JSON, readable in any text editor.

#pragma once

#include <JuceHeader.h>
#include "Identifiers.h"
#include "../PluginHost.h"
#include "../Engine/Automation.h"

class Project : public juce::FileBasedDocument,
                private juce::ValueTree::Listener
{
public:
    static constexpr int currentFormatVersion = 1;
    static constexpr const char* fileExtension = ".trajet";

    explicit Project (PluginHost& hostToUse);
    ~Project() override;

    juce::ValueTree& getState() noexcept            { return state; }
    juce::UndoManager& getUndoManager() noexcept    { return undoManager; }

    /** Start a fresh, empty project (no plugin, default settings). */
    void resetToDefault();

    //==============================================================================
    // Plugin
    /** Records which plugin the project uses (called when you pick one from the menu). */
    void setPluginDescription (const juce::PluginDescription&);
    std::unique_ptr<juce::PluginDescription> getPluginDescription() const;

    /** Applies the plugin state stored in the project to the loaded plugin. */
    void restorePluginState();

    //==============================================================================
    // Held note (undoable)
    juce::ValueTree getHeldNote()                   { return state.getChildWithName (IDs::HELDNOTE); }
    void setHeldNote (bool on, int note, int velocity);

    //==============================================================================
    // Lanes, items and points (all undoable)
    juce::ValueTree getLanes()                      { return state.getChildWithName (IDs::LANES); }
    double getLengthSeconds() const                 { return state[IDs::lengthSeconds]; }
    void setLengthSeconds (double seconds);

    /** Adds a lane for a parameter, with one item spanning the whole project. */
    juce::ValueTree addLane (int paramIndex, const juce::String& paramName);
    void removeLane (juce::ValueTree lane);
    juce::ValueTree findLaneForParameter (int paramIndex);

    juce::ValueTree addPoint (juce::ValueTree item, double time, float value);

    /** An item's points as Automation::Point, sorted, times relative to the item. */
    static std::vector<Automation::Point> getPoints (const juce::ValueTree& item);

    /** Called just before / after the plugin state is read for saving, so the
        automation can step aside and the plain patch gets saved. */
    std::function<void()> beforeStoringPluginState, afterStoringPluginState;

    //==============================================================================
    /** Called after a project file has been loaded, so the UI and audio can follow. */
    std::function<void()> onProjectLoaded;

    /** Called whenever the held-note settings change (also by undo/redo). */
    std::function<void()> onHeldNoteChanged;

    // JSON conversion, also useful for export later.
    static juce::var toVar (const juce::ValueTree&);
    static juce::ValueTree fromVar (const juce::var&);

    // FileBasedDocument
    juce::String getDocumentTitle() override;
    juce::Result loadDocument (const juce::File&) override;
    juce::Result saveDocument (const juce::File&) override;
    juce::File getLastDocumentOpened() override;
    void setLastDocumentOpened (const juce::File&) override;

private:
    static juce::ValueTree createDefaultState();
    void storePluginState();

    // ValueTree::Listener: any change to the model marks the project as unsaved.
    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { changed(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { changed(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { changed(); }

    PluginHost& host;
    juce::ValueTree state { createDefaultState() };
    juce::UndoManager undoManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Project)
};
