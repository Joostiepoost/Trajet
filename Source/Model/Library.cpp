#include "Library.h"
#include "ItemTime.h"
#include "../Engine/AutomationController.h"

namespace
{
    juce::File defaultFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Trajet Library");
    }

    juce::String kindOf (const juce::ValueTree& item)
    {
        for (const auto& c : item)
            if (c.hasType (IDs::NOTE)) return "notes";
        return item.getParent().isValid() && Project::isNoteLane (item.getParent()) ? "notes" : "automation";
    }
}

//==============================================================================
bool Library::Entry::matches (const juce::String& search) const
{
    const auto words = juce::StringArray::fromTokens (search.toLowerCase(), " ,", "\"");
    const auto haystack = (name + " " + tags.joinIntoString (" ") + " " + source + " " + kind).toLowerCase();
    for (auto& w : words)
        if (w.isNotEmpty() && ! haystack.contains (w))
            return false;
    return true;
}

juce::String Library::Entry::lengthText() const
{
    const ItemTime it (item);
    if (it.isMusical())
        return juce::String (it.lengthBeats, std::abs (it.lengthBeats - std::round (it.lengthBeats)) < 1.0e-6 ? 0 : 2) + " beats";
    return juce::String ((double) item[IDs::length], 2) + " s";
}

//==============================================================================
Library::Library (juce::PropertiesFile& s) : settings (s)
{
    const auto stored = settings.getValue ("libraryFolder");
    folder = stored.isNotEmpty() ? juce::File (stored) : defaultFolder();
}

void Library::setFolder (const juce::File& f)
{
    folder = f;
    settings.setValue ("libraryFolder", f.getFullPathName());
    settings.saveIfNeeded();
    scan();
}

void Library::scan()
{
    entries.clear();
    if (! folder.isDirectory())
        return;

    for (const auto& f : folder.findChildFiles (juce::File::findFiles, true, juce::String ("*") + fileExtension))
    {
        Entry e;
        if (load (f, e))
            entries.push_back (std::move (e));
    }
    std::sort (entries.begin(), entries.end(), [] (const Entry& a, const Entry& b)
               { return a.name.compareNatural (b.name) < 0; });
}

bool Library::load (const juce::File& f, Entry& e)
{
    auto v = juce::JSON::parse (f.loadFileAsString());
    if (v.getProperty ("format", {}).toString() != "trajet-item")
        return false;

    e.file = f;
    e.name = v.getProperty ("name", f.getFileNameWithoutExtension()).toString();
    e.kind = v.getProperty ("kind", "automation").toString();
    e.source = v.getProperty ("source", {}).toString();
    e.created = juce::Time::fromISO8601 (v.getProperty ("created", {}).toString());
    e.tags.clear();
    if (auto* arr = v.getProperty ("tags", {}).getArray())
        for (auto& t : *arr) e.tags.add (t.toString());
    e.item = Project::fromVar (v.getProperty ("item", {}));
    return e.item.hasType (IDs::ITEM);
}

static juce::var entryToVar (const Library::Entry& e)
{
    auto* obj = new juce::DynamicObject();
    obj->setProperty ("format", "trajet-item");
    obj->setProperty ("version", 1);
    obj->setProperty ("name", e.name);
    obj->setProperty ("kind", e.kind);
    obj->setProperty ("source", e.source);
    obj->setProperty ("created", e.created.toISO8601 (true));
    juce::Array<juce::var> tags;
    for (auto& t : e.tags) tags.add (t);
    obj->setProperty ("tags", tags);
    obj->setProperty ("item", Project::toVar (e.item));
    return juce::var (obj);
}

juce::File Library::save (const juce::ValueTree& item, const juce::String& name, const juce::StringArray& tags,
                          const juce::String& source, juce::String& error)
{
    if (! folder.isDirectory() && ! folder.createDirectory())
    {
        error = "Could not create the library folder " + folder.getFullPathName();
        return {};
    }

    Entry e;
    e.name = name.trim().isEmpty() ? juce::String ("Untitled") : name.trim();
    e.kind = kindOf (item);
    e.source = source;
    e.tags = tags;
    e.created = juce::Time::getCurrentTime();
    e.item = item.createCopy();
    e.item.setProperty (IDs::start, 0.0, nullptr);
    e.file = folder.getNonexistentChildFile (juce::File::createLegalFileName (e.name), fileExtension, false);

    if (! e.file.replaceWithText (juce::JSON::toString (entryToVar (e))))
    {
        error = "Could not write " + e.file.getFullPathName();
        return {};
    }
    scan();
    return e.file;
}

bool Library::rewrite (const Entry& e)
{
    const bool ok = e.file.replaceWithText (juce::JSON::toString (entryToVar (e)));
    scan();
    return ok;
}

bool Library::remove (const Entry& e)
{
    return e.file.moveToTrash() || e.file.deleteFile();
}

//==============================================================================
juce::String Library::insert (Project& project, const Entry& e, juce::ValueTree lane, double time, juce::ValueTree* inserted)
{
    if (! lane.isValid())
        return "Click a lane first: that is where the item goes.";

    const bool noteLane = Project::isNoteLane (lane);
    if (noteLane != (e.kind == "notes"))
        return noteLane ? "This is an automation item: it goes in a parameter lane, not a note lane."
                        : "This is a note item: it goes in a note lane.";

    const double length = e.item[IDs::length];
    time = juce::jmax (0.0, juce::jmin (time, project.getLengthSeconds() - length));
    if (! project.isFree (lane, time, length))
        return "No room there: the item (" + e.lengthText() + ") would overlap another item or pass the end.";

    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Insert from library");
    auto copy = e.item.createCopy();
    copy.setProperty (IDs::start, time, nullptr);
    lane.appendChild (copy, &um);
    if (inserted != nullptr) *inserted = copy;
    return {};
}

void Library::drawThumbnail (juce::Graphics& g, const juce::ValueTree& item, juce::Rectangle<float> r, juce::Colour colour)
{
    g.setColour (colour.withAlpha (0.12f));
    g.fillRoundedRectangle (r, 3.0f);
    auto area = r.reduced (3.0f);
    const ItemTime it (item);
    const double localLength = juce::jmax (1.0e-6, it.localLength());

    bool hasNotes = false;
    int lo = 128, hi = -1;
    for (const auto& n : item)
        if (n.hasType (IDs::NOTE)) { hasNotes = true; lo = juce::jmin (lo, (int) n[IDs::pitch]); hi = juce::jmax (hi, (int) n[IDs::pitch]); }

    if (hasNotes)
    {
        const int span = juce::jmax (12, hi - lo + 2);
        const float rowH = area.getHeight() / (float) span;
        for (const auto& n : item)
        {
            if (! n.hasType (IDs::NOTE)) continue;
            const float x0 = area.getX() + area.getWidth() * (float) ((double) n[IDs::time] / localLength);
            const float x1 = area.getX() + area.getWidth() * (float) (((double) n[IDs::time] + (double) n[IDs::length]) / localLength);
            const float y = area.getBottom() - (float) ((int) n[IDs::pitch] - lo + 1) * rowH;
            g.setColour (colour.withAlpha (0.4f + 0.6f * (float) (int) n[IDs::velocity] / 127.0f));
            g.fillRect (x0, y, juce::jmax (1.5f, x1 - x0), juce::jmax (1.5f, rowH));
        }
        return;
    }

    // A temporary lane holding a copy, so the curve is computed exactly as in playback.
    juce::ValueTree lane (IDs::LANE);
    auto copy = item.createCopy();
    copy.setProperty (IDs::start, 0.0, nullptr);
    lane.appendChild (copy, nullptr);
    Automation::Lane l;
    AutomationController::buildItems (lane, l);
    if (l.items.empty()) return;
    const auto& ai = l.items.front();
    if (ai.points.empty() && ai.generator.type == Automation::GeneratorType::none) return;

    juce::Path path;
    const int steps = juce::jmax (8, (int) area.getWidth());
    for (int i = 0; i <= steps; ++i)
    {
        const double local = localLength * i / steps;
        const float v = juce::jlimit (0.0f, 1.0f, ai.valueAtLocal (local));
        const float x = area.getX() + area.getWidth() * (float) i / (float) steps;
        const float y = area.getBottom() - v * area.getHeight();
        if (i == 0) path.startNewSubPath (x, y); else path.lineTo (x, y);
    }
    g.setColour (colour);
    g.strokePath (path, juce::PathStrokeType (1.3f));
}
