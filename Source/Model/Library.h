// Library.h — items kept for reuse, one small JSON file per item (.trajetitem) in a
// folder (by default ~/Music/Trajet Library). Point, generator and note items alike.

#pragma once

#include <JuceHeader.h>
#include "Project.h"

class Library
{
public:
    struct Entry
    {
        juce::File file;
        juce::String name, kind, source;    // kind: "automation" or "notes"; source: the lane it came from
        juce::StringArray tags;
        juce::ValueTree item;               // start = 0
        juce::Time created;

        bool matches (const juce::String& search) const;
        juce::String lengthText() const;
    };

    explicit Library (juce::PropertiesFile& settings);

    juce::File getFolder() const            { return folder; }
    void setFolder (const juce::File&);

    /** Reads all entries in the folder (sorted by name). */
    void scan();
    const std::vector<Entry>& getEntries() const    { return entries; }

    /** Saves an item; returns the new file or an error message in `error`. */
    juce::File save (const juce::ValueTree& item, const juce::String& name, const juce::StringArray& tags,
                     const juce::String& source, juce::String& error);
    bool rewrite (const Entry&);            // after renaming or retagging
    static bool remove (const Entry&);      // to the bin

    static bool load (const juce::File&, Entry& result);

    /** Puts a copy of the entry's item into a lane at a time (undoable). Returns "" or why not. */
    static juce::String insert (Project&, const Entry&, juce::ValueTree lane, double time, juce::ValueTree* inserted = nullptr);

    /** A small picture of an item: its curve or its notes. */
    static void drawThumbnail (juce::Graphics&, const juce::ValueTree& item, juce::Rectangle<float>, juce::Colour);

    static constexpr const char* dragPrefix = "trajet-library:";
    static constexpr const char* fileExtension = ".trajetitem";

private:
    juce::PropertiesFile& settings;
    juce::File folder;
    std::vector<Entry> entries;
};
