// LibraryPanel.h — the library tab beside the inspector: search, a list with small
// pictures, drag an entry onto a lane (or double-click to insert it at the playhead).

#pragma once

#include <JuceHeader.h>
#include "../Model/Library.h"
#include "EditorState.h"

class LibraryPanel : public juce::Component,
                     private juce::ListBoxModel
{
public:
    LibraryPanel (Library&, Project&, Selection&, std::function<double()> playheadSeconds);

    std::function<void (const juce::String&)> showStatus;

    void refresh();
    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override       { if (isVisible()) refresh(); }

private:
    int getNumRows() override                                               { return (int) shown.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    juce::var getDragSourceDescription (const juce::SparseSet<int>& rows) override;
    juce::String getTooltipForRow (int row) override;

    void insertAtPlayhead (const Library::Entry&);
    void showEntryMenu (const Library::Entry&);
    void editEntry (const Library::Entry&, bool tagsOnly);

    Library& library;
    Project& project;
    Selection& selection;
    std::function<double()> playhead;

    std::vector<Library::Entry> shown;
    juce::TextEditor searchBox;
    juce::ListBox list { "Library", this };
    juce::TextButton folderButton { "Folder..." }, refreshButton { "Refresh" };
    juce::Label folderLabel, hintLabel;
    std::unique_ptr<juce::FileChooser> folderChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LibraryPanel)
};
