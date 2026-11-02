#include "Project.h"

namespace
{
    const char* const keyLastProject = "lastProject";
}

Project::Project (PluginHost& hostToUse)
    : FileBasedDocument (fileExtension, "*.trajet", "Open a Trajet project", "Save Trajet project"),
      host (hostToUse)
{
    state.addListener (this);
}

Project::~Project()
{
    state.removeListener (this);
}

juce::ValueTree Project::createDefaultState()
{
    juce::ValueTree project (IDs::PROJECT);
    project.setProperty (IDs::formatVersion, currentFormatVersion, nullptr);
    project.setProperty (IDs::lengthSeconds, 60.0, nullptr);

    juce::ValueTree plugin (IDs::PLUGIN);
    project.appendChild (plugin, nullptr);

    juce::ValueTree heldNote (IDs::HELDNOTE);
    heldNote.setProperty (IDs::on, false, nullptr);
    heldNote.setProperty (IDs::note, 48, nullptr);
    heldNote.setProperty (IDs::velocity, 100, nullptr);
    project.appendChild (heldNote, nullptr);

    project.appendChild (juce::ValueTree (IDs::LANES), nullptr);
    return project;
}

void Project::resetToDefault()
{
    state.copyPropertiesAndChildrenFrom (createDefaultState(), nullptr);
    undoManager.clearUndoHistory();
    setFile ({});
    setChangedFlag (false);

    if (onHeldNoteChanged) onHeldNoteChanged();
}

//==============================================================================
void Project::setPluginDescription (const juce::PluginDescription& description)
{
    auto plugin = state.getChildWithName (IDs::PLUGIN);
    if (auto xml = description.createXml())
        plugin.setProperty (IDs::description, xml->toString (juce::XmlElement::TextFormat().singleLine()), nullptr);

    plugin.removeProperty (IDs::state, nullptr);   // a new plugin starts from its own default patch
    undoManager.clearUndoHistory();
}

std::unique_ptr<juce::PluginDescription> Project::getPluginDescription() const
{
    auto text = state.getChildWithName (IDs::PLUGIN)[IDs::description].toString();

    if (auto xml = juce::parseXML (text))
    {
        auto desc = std::make_unique<juce::PluginDescription>();
        if (desc->loadFromXml (*xml))
            return desc;
    }
    return nullptr;
}

void Project::storePluginState()
{
    auto* plugin = host.getPlugin();
    if (plugin == nullptr)
        return;

    if (beforeStoringPluginState) beforeStoringPluginState();

    juce::MemoryBlock data;
    plugin->getStateInformation (data);

    if (afterStoringPluginState) afterStoringPluginState();

    // Stored without undo and without marking the project as changed.
    state.removeListener (this);
    state.getChildWithName (IDs::PLUGIN).setProperty (IDs::state, data.toBase64Encoding(), nullptr);
    state.addListener (this);
}

void Project::restorePluginState()
{
    auto* plugin = host.getPlugin();
    auto text = state.getChildWithName (IDs::PLUGIN)[IDs::state].toString();

    if (plugin == nullptr || text.isEmpty())
        return;

    juce::MemoryBlock data;
    if (data.fromBase64Encoding (text) && data.getSize() > 0)
        plugin->setStateInformation (data.getData(), (int) data.getSize());
}

//==============================================================================
void Project::setHeldNote (bool on, int note, int velocity)
{
    auto heldNote = getHeldNote();
    heldNote.setProperty (IDs::on, on, &undoManager);
    heldNote.setProperty (IDs::note, juce::jlimit (0, 127, note), &undoManager);
    heldNote.setProperty (IDs::velocity, juce::jlimit (1, 127, velocity), &undoManager);
}

void Project::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    changed();

    if (tree.hasType (IDs::HELDNOTE) && onHeldNoteChanged)
        onHeldNoteChanged();
}

//==============================================================================
// Lanes

void Project::setLengthSeconds (double seconds)
{
    const double oldLength = getLengthSeconds();
    const double newLength = juce::jlimit (1.0, 7200.0, seconds);
    state.setProperty (IDs::lengthSeconds, newLength, &undoManager);

    // Items that spanned the whole project keep doing so (until step 4 brings real item editing).
    for (auto lane : getLanes())
        for (auto item : lane)
            if (item.hasType (IDs::ITEM) && std::abs ((double) item[IDs::start]) < 1.0e-9
                && std::abs ((double) item[IDs::length] - oldLength) < 1.0e-9)
                item.setProperty (IDs::length, newLength, &undoManager);
}

juce::ValueTree Project::addLane (int paramIndex, const juce::String& paramName)
{
    juce::ValueTree lane (IDs::LANE);
    lane.setProperty (IDs::laneId, juce::Uuid().toString(), nullptr);
    lane.setProperty (IDs::paramIndex, paramIndex, nullptr);
    lane.setProperty (IDs::paramName, paramName, nullptr);
    lane.setProperty (IDs::name, paramName, nullptr);
    lane.setProperty (IDs::colour, getLanes().getNumChildren() % paletteSize, nullptr);
    lane.setProperty (IDs::height, 72, nullptr);

    juce::ValueTree item (IDs::ITEM);
    item.setProperty (IDs::start, 0.0, nullptr);
    item.setProperty (IDs::length, getLengthSeconds(), nullptr);
    item.setProperty (IDs::timebase, "linear", nullptr);
    lane.appendChild (item, nullptr);

    getLanes().appendChild (lane, &undoManager);
    return lane;
}

void Project::removeLane (juce::ValueTree lane)
{
    getLanes().removeChild (lane, &undoManager);
}

juce::ValueTree Project::findLaneForParameter (int paramIndex)
{
    for (auto lane : getLanes())
        if ((int) lane[IDs::paramIndex] == paramIndex)
            return lane;
    return {};
}

juce::ValueTree Project::addPoint (juce::ValueTree item, double time, float value)
{
    juce::ValueTree point (IDs::POINT);
    point.setProperty (IDs::time, time, nullptr);
    point.setProperty (IDs::value, value, nullptr);
    point.setProperty (IDs::shape, (int) Automation::Shape::linear, nullptr);
    point.setProperty (IDs::tension, 0.0, nullptr);
    item.appendChild (point, &undoManager);
    return point;
}

juce::Array<juce::ValueTree> Project::getSortedItems (const juce::ValueTree& lane)
{
    juce::Array<juce::ValueTree> items;
    for (const auto& c : lane)
        if (c.hasType (IDs::ITEM))
            items.add (c);

    std::sort (items.begin(), items.end(),
               [] (const auto& a, const auto& b) { return (double) a[IDs::start] < (double) b[IDs::start]; });
    return items;
}

juce::Range<double> Project::getFreeRange (const juce::ValueTree& lane, const juce::ValueTree& item, double atTime) const
{
    double lo = 0.0, hi = juce::jmax (getLengthSeconds(), 0.0);
    const double ref = item.isValid() ? (double) item[IDs::start] : atTime;

    for (const auto& other : getSortedItems (lane))
    {
        if (other == item)
            continue;

        const double s = other[IDs::start];
        const double e = s + (double) other[IDs::length];

        if (e <= ref + 1.0e-9)       lo = juce::jmax (lo, e);
        else if (s >= ref - 1.0e-9)  { hi = juce::jmin (hi, s); }
        else                         { lo = hi = ref; break; }   // ref lies inside another item
    }

    return { lo, juce::jmax (lo, hi) };
}

bool Project::isFree (const juce::ValueTree& lane, double start, double length, const juce::ValueTree& exclude) const
{
    if (start < -1.0e-9 || start + length > getLengthSeconds() + 1.0e-9)
        return false;

    for (const auto& other : lane)
    {
        if (! other.hasType (IDs::ITEM) || other == exclude)
            continue;

        const double s = other[IDs::start];
        const double e = s + (double) other[IDs::length];
        if (start < e - 1.0e-9 && start + length > s + 1.0e-9)
            return false;
    }
    return true;
}

juce::ValueTree Project::addItem (juce::ValueTree lane, double start, double length)
{
    juce::ValueTree item (IDs::ITEM);
    item.setProperty (IDs::start, start, nullptr);
    item.setProperty (IDs::length, juce::jmax (minItemLength, length), nullptr);
    item.setProperty (IDs::timebase, "linear", nullptr);
    lane.appendChild (item, &undoManager);
    return item;
}

juce::Colour Project::paletteColour (int index)
{
    static const juce::uint32 colours[paletteSize] = {
        0xffe07b4f, 0xff3fb98a, 0xff5b9bd5, 0xffd4a73a, 0xffc0679f, 0xff4fb8c9, 0xff8f86e0, 0xff9ab54a,
        0xffe05a5a, 0xff2e9e6b, 0xff3d6fd1, 0xffb8862b, 0xffe39ac4, 0xff7fd6e0, 0xffb7a9f0, 0xffcfd96a };
    return juce::Colour (colours[(size_t) juce::jlimit (0, paletteSize - 1, index)]);
}

juce::Colour Project::laneColour (const juce::ValueTree& lane)
{
    // Lanes from older files have no colour yet: use their position.
    const int index = lane.hasProperty (IDs::colour) ? (int) lane[IDs::colour]
                                                     : juce::jmax (0, lane.getParent().indexOf (lane));
    return paletteColour ((index % paletteSize + paletteSize) % paletteSize);
}

std::vector<Automation::Point> Project::getPoints (const juce::ValueTree& item)
{
    std::vector<Automation::Point> points;
    points.reserve ((size_t) item.getNumChildren());

    for (const auto& p : item)
    {
        if (! p.hasType (IDs::POINT))
            continue;

        Automation::Point pt;
        pt.time    = p[IDs::time];
        pt.value   = (float) (double) p[IDs::value];
        pt.shape   = (Automation::Shape) juce::jlimit (0, 3, (int) p[IDs::shape]);
        pt.tension = (float) (double) p[IDs::tension];
        points.push_back (pt);
    }

    std::stable_sort (points.begin(), points.end(),
                      [] (const auto& a, const auto& b) { return a.time < b.time; });
    return points;
}

//==============================================================================
// JSON <-> ValueTree
//
// Each node becomes { "type": ..., "properties": { ... }, "children": [ ... ] }.

juce::var Project::toVar (const juce::ValueTree& tree)
{
    auto* node = new juce::DynamicObject();
    node->setProperty ("type", tree.getType().toString());

    auto* props = new juce::DynamicObject();
    for (int i = 0; i < tree.getNumProperties(); ++i)
    {
        auto name = tree.getPropertyName (i);
        props->setProperty (name, tree[name]);
    }
    node->setProperty ("properties", juce::var (props));

    juce::Array<juce::var> children;
    for (const auto& child : tree)
        children.add (toVar (child));
    node->setProperty ("children", children);

    return juce::var (node);
}

juce::ValueTree Project::fromVar (const juce::var& v)
{
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return {};

    auto type = obj->getProperty ("type").toString();
    if (type.isEmpty())
        return {};

    juce::ValueTree tree { juce::Identifier (type) };

    if (auto* props = obj->getProperty ("properties").getDynamicObject())
        for (auto& p : props->getProperties())
            tree.setProperty (p.name, p.value, nullptr);

    if (auto* children = obj->getProperty ("children").getArray())
        for (auto& c : *children)
        {
            auto child = fromVar (c);
            if (child.isValid())
                tree.appendChild (child, nullptr);
        }

    return tree;
}

//==============================================================================
// FileBasedDocument

juce::String Project::getDocumentTitle()
{
    return getFile().existsAsFile() ? getFile().getFileNameWithoutExtension() : juce::String ("Untitled");
}

juce::Result Project::loadDocument (const juce::File& file)
{
    auto parsed = juce::JSON::parse (file.loadFileAsString());

    if (parsed.getProperty ("format", {}).toString() != "trajet")
        return juce::Result::fail ("This is not a Trajet project file.");

    if ((int) parsed.getProperty ("version", 0) > currentFormatVersion)
        return juce::Result::fail ("This project was saved by a newer version of Trajet.");

    auto loaded = fromVar (parsed.getProperty ("project", {}));
    if (! loaded.hasType (IDs::PROJECT))
        return juce::Result::fail ("The project file is damaged.");

    // Fill in anything an older file might lack, then take over its contents.
    auto defaults = createDefaultState();
    for (const auto& child : defaults)
        if (! loaded.getChildWithName (child.getType()).isValid())
            loaded.appendChild (child.createCopy(), nullptr);

    state.removeListener (this);
    state.copyPropertiesAndChildrenFrom (loaded, nullptr);
    state.addListener (this);

    undoManager.clearUndoHistory();

    if (onHeldNoteChanged) onHeldNoteChanged();
    if (onProjectLoaded)   onProjectLoaded();

    return juce::Result::ok();
}

juce::Result Project::saveDocument (const juce::File& file)
{
    storePluginState();

    auto* root = new juce::DynamicObject();
    root->setProperty ("format", "trajet");
    root->setProperty ("version", currentFormatVersion);
    root->setProperty ("project", toVar (state));

    auto text = juce::JSON::toString (juce::var (root), juce::JSON::FormatOptions{});

    if (! file.replaceWithText (text))
        return juce::Result::fail ("Could not write to " + file.getFullPathName());

    return juce::Result::ok();
}

juce::File Project::getLastDocumentOpened()
{
    return juce::File (host.appProperties.getUserSettings()->getValue (keyLastProject));
}

void Project::setLastDocumentOpened (const juce::File& file)
{
    auto* settings = host.appProperties.getUserSettings();
    settings->setValue (keyLastProject, file.getFullPathName());
    settings->saveIfNeeded();
}
