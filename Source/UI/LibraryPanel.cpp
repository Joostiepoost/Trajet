#include "LibraryPanel.h"

namespace
{
    const juce::Colour panelColour (0xff262a31);
    const juce::Colour thumbColour (0xff5b9bd5), noteColour (0xff4fb8c9);
}

LibraryPanel::LibraryPanel (Library& l, Project& p, Selection& s, std::function<double()> playheadSeconds)
    : library (l), project (p), selection (s), playhead (std::move (playheadSeconds))
{
    searchBox.setTextToShowWhenEmpty ("Search names and tags", juce::Colours::grey);
    searchBox.onTextChange = [this] { refresh(); };
    addAndMakeVisible (searchBox);

    list.setRowHeight (52);
    list.setColour (juce::ListBox::backgroundColourId, panelColour);
    list.setMultipleSelectionEnabled (false);
    addAndMakeVisible (list);

    folderButton.setTooltip ("Choose the library folder");
    folderButton.onClick = [this]
    {
        folderChooser = std::make_unique<juce::FileChooser> ("Choose the library folder", library.getFolder());
        folderChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                                    [this] (const juce::FileChooser& fc)
        {
            if (fc.getResult().isDirectory())
            {
                library.setFolder (fc.getResult());
                refresh();
            }
        });
    };
    refreshButton.onClick = [this] { refresh(); };
    addAndMakeVisible (folderButton);
    addAndMakeVisible (refreshButton);

    folderLabel.setFont (juce::FontOptions (10.5f));
    folderLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    addAndMakeVisible (folderLabel);

    hintLabel.setFont (juce::FontOptions (11.0f));
    hintLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.5f));
    hintLabel.setJustificationType (juce::Justification::topLeft);
    hintLabel.setText ("Drag an entry onto a lane, or double-click it to put it at the playhead in the active lane. "
                       "Save an item: select it and press Cmd-L (or right-click it).",
                       juce::dontSendNotification);
    addAndMakeVisible (hintLabel);
}

void LibraryPanel::refresh()
{
    library.scan();
    shown.clear();
    for (auto& e : library.getEntries())
        if (e.matches (searchBox.getText()))
            shown.push_back (e);
    folderLabel.setText (library.getFolder().getFullPathName(), juce::dontSendNotification);
    folderLabel.setTooltip (library.getFolder().getFullPathName());
    list.updateContent();
    list.repaint();
}

void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size())) return;
    const auto& e = shown[(size_t) row];

    g.fillAll (selected ? juce::Colour (0xff3a4250) : (row % 2 ? panelColour : panelColour.brighter (0.03f)));
    Library::drawThumbnail (g, e.item, { 6.0f, 6.0f, 70.0f, (float) h - 12.0f }, e.kind == "notes" ? noteColour : thumbColour);

    g.setColour (juce::Colours::white);
    g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
    g.drawText (e.name, 84, 5, w - 88, 18, juce::Justification::centredLeft, true);

    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (11.0f));
    juce::String info = e.lengthText();
    if (e.kind == "notes") info << ", notes";
    if (e.item[IDs::generator].toString().isNotEmpty()) info << ", " << e.item[IDs::generator].toString();
    if (e.tags.size() > 0) info << "   #" << e.tags.joinIntoString (" #");
    g.drawText (info, 84, 24, w - 88, 16, juce::Justification::centredLeft, true);
}

juce::String LibraryPanel::getTooltipForRow (int row)
{
    if (! juce::isPositiveAndBelow (row, (int) shown.size())) return {};
    const auto& e = shown[(size_t) row];
    return e.name + (e.source.isNotEmpty() ? " (from " + e.source + ")" : juce::String());
}

juce::var LibraryPanel::getDragSourceDescription (const juce::SparseSet<int>& rows)
{
    if (rows.size() != 1 || ! juce::isPositiveAndBelow (rows[0], (int) shown.size())) return {};
    return juce::String (Library::dragPrefix) + shown[(size_t) rows[0]].file.getFullPathName();
}

void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow (row, (int) shown.size()))
        insertAtPlayhead (shown[(size_t) row]);
}

void LibraryPanel::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu() && juce::isPositiveAndBelow (row, (int) shown.size()))
        showEntryMenu (shown[(size_t) row]);
}

void LibraryPanel::insertAtPlayhead (const Library::Entry& e)
{
    juce::ValueTree inserted;
    const auto error = Library::insert (project, e, selection.getActiveLane(), playhead ? playhead() : 0.0, &inserted);
    if (showStatus) showStatus (error.isEmpty() ? "Inserted \"" + e.name + "\"." : error);
    if (inserted.isValid()) selection.selectItem (inserted);
}

void LibraryPanel::showEntryMenu (const Library::Entry& e)
{
    juce::PopupMenu m;
    m.addItem (1, "Insert at the playhead (active lane)");
    m.addItem (2, "Rename...");
    m.addItem (3, "Edit tags...");
    m.addItem (4, "Show in Finder");
    m.addSeparator();
    m.addItem (5, "Delete (to the bin)");
    juce::Component::SafePointer<LibraryPanel> safeThis (this);
    m.showMenuAsync (juce::PopupMenu::Options(), [safeThis, e] (int r)
    {
        if (safeThis == nullptr) return;
        if (r == 1) safeThis->insertAtPlayhead (e);
        if (r == 2) safeThis->editEntry (e, false);
        if (r == 3) safeThis->editEntry (e, true);
        if (r == 4) e.file.revealToUser();
        if (r == 5) { Library::remove (e); safeThis->refresh(); }
    });
}

void LibraryPanel::editEntry (const Library::Entry& e, bool tagsOnly)
{
    auto* w = new juce::AlertWindow (tagsOnly ? "Edit tags" : "Rename", e.name, juce::MessageBoxIconType::NoIcon, this);
    if (! tagsOnly) w->addTextEditor ("name", e.name, "Name");
    else            w->addTextEditor ("tags", e.tags.joinIntoString (" "), "Tags (separated by spaces)");
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    juce::Component::SafePointer<LibraryPanel> safeThis (this);
    w->enterModalState (true, juce::ModalCallbackFunction::create ([safeThis, w, e, tagsOnly] (int result)
    {
        if (safeThis != nullptr && result == 1)
        {
            auto edited = e;
            if (tagsOnly) edited.tags = juce::StringArray::fromTokens (w->getTextEditorContents ("tags"), " ,", "\"");
            else          edited.name = w->getTextEditorContents ("name").trim();
            edited.tags.removeEmptyStrings();
            safeThis->library.rewrite (edited);
            safeThis->refresh();
        }
    }), true);
}

void LibraryPanel::paint (juce::Graphics& g)
{
    g.fillAll (panelColour);
}

void LibraryPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    searchBox.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);
    auto bottom = r.removeFromBottom (24);
    folderButton.setBounds (bottom.removeFromLeft (80));
    bottom.removeFromLeft (6);
    refreshButton.setBounds (bottom.removeFromLeft (70));
    r.removeFromBottom (4);
    folderLabel.setBounds (r.removeFromBottom (16));
    r.removeFromBottom (2);
    hintLabel.setBounds (r.removeFromBottom (58));
    r.removeFromBottom (4);
    list.setBounds (r);
}
