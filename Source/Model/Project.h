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

    // Note lanes: MIDI notes for the plugin, in items like automation.
    juce::ValueTree addNoteLane();
    static bool isNoteLane (const juce::ValueTree& lane)        { return lane[IDs::kind].toString() == "notes"; }
    static bool isNoteItem (const juce::ValueTree& item)        { return isNoteLane (item.getParent()); }
    /** Time and length in the item's own units (seconds or beats). */
    juce::ValueTree addNote (juce::ValueTree item, double time, double length, int pitch, int velocity);
    /** The notes of an item, in absolute seconds, clipped to the item. */
    static std::vector<Automation::Note> getNotes (const juce::ValueTree& item);

    //==============================================================================
    // Groups (undoable). A lane belongs to at most one group; a group's lanes are
    // always next to each other in the lane order.
    juce::ValueTree getGroups()                     { return state.getChildWithName (IDs::GROUPS); }
    juce::ValueTree findGroup (const juce::var& groupId);
    juce::ValueTree groupOf (const juce::ValueTree& lane);
    juce::Array<juce::ValueTree> lanesInGroup (const juce::ValueTree& group);
    static juce::Colour groupColour (const juce::ValueTree& group);

    /** Makes a new group holding these lanes (moved together at the first one's place). */
    juce::ValueTree createGroup (const juce::String& name, const juce::Array<juce::ValueTree>& lanes);
    /** Moves a lane into a group (at its end), or out of any group (invalid group). */
    void setLaneGroup (juce::ValueTree lane, const juce::ValueTree& group);
    /** Dissolves a group; its lanes stay where they are. */
    void ungroup (juce::ValueTree group);
    /** Removes a group together with its lanes. */
    void removeGroupWithLanes (juce::ValueTree group);
    /** Moves all lanes of a group so they start at this index in the lane order
        (counted without the group's own lanes). */
    void moveGroup (const juce::ValueTree& group, int toIndex);
    /** Keeps each group's lanes together and removes empty groups. */
    void normaliseGroups();

    // Items: never overlapping on one lane.
    static constexpr double minItemLength = 0.01;
    static juce::Array<juce::ValueTree> getSortedItems (const juce::ValueTree& lane);
    /** Free space around an item: end of the previous item and start of the next
        (0 and the project length if there are none). Pass an invalid item to ask about a time. */
    juce::Range<double> getFreeRange (const juce::ValueTree& lane, const juce::ValueTree& item, double atTime) const;
    juce::ValueTree addItem (juce::ValueTree lane, double start, double length);
    /** True if [start, start + length) fits in the project and overlaps no item except `exclude`. */
    bool isFree (const juce::ValueTree& lane, double start, double length, const juce::ValueTree& exclude = {}) const;

    // Time bases
    /** Convert an item between linear (seconds) and musical (beats with a tempo map).
        Points keep their place in time. */
    void setTimebase (juce::ValueTree item, bool musical, double bpm = 120.0);
    /** After a tempo change: recompute the item's length in seconds. Returns false
        (and changes nothing) if the item would then overlap the next one. */
    bool refreshMusicalLength (juce::ValueTree item);
    /** Set a musical item's length in beats (if it fits). */
    bool setLengthBeats (juce::ValueTree item, double beats);

    // Generators
    /** Make an item a generator ("lfo", "random", "formula") or back to points (""). */
    void setGenerator (juce::ValueTree item, const juce::String& type);
    /** Replace a generator by ordinary points that trace the same curve. Returns the number of points. */
    int bakeGenerator (juce::ValueTree item);

    /** Lane colours: a palette of distinct hues. */
    static juce::Colour laneColour (const juce::ValueTree& lane);
    static juce::Colour paletteColour (int index);
    static constexpr int paletteSize = 16;

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
