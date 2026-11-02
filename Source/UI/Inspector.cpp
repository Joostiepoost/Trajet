#include "Inspector.h"
#include "../Model/ItemTime.h"

Inspector::Inspector (Project& p, PluginHost& h, AutomationController& c, Selection& s)
    : project (p), host (h), controller (c), selection (s), generatorPanel (p)
{
    auto heading = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (l);
    };
    heading (title, "Inspector");
    title.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    heading (itemHeading, "Selected item");
    heading (pointHeading, "Selected point");
    heading (generatorHeading, "Generator");

    generatorPanel.parameterFor = [this] (const juce::ValueTree& lane) { return parameterFor (lane); };
    generatorPanel.showStatus = [this] (const juce::String& m) { if (showStatus) showStatus (m); };
    addChildComponent (generatorPanel);

    hint.setText ("Select an item or a point to see its details.", juce::dontSendNotification);
    hint.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.45f));
    hint.setFont (juce::FontOptions (12.0f));
    hint.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (hint);

    laneField   = &addField ("Lane", false);
    startField  = &addField ("Start", true);
    lengthField = &addField ("Length (s)", true);
    countField  = &addField ("Points", false);
    timebaseField = &addField ("Time base", false);
    tempoField  = &addField ("Tempo", true);
    timeField   = &addField ("Time", true);
    valueField  = &addField ("Value", true);

    startField->value.onTextChange = [this]
    {
        if (! currentItem.isValid()) return;
        const double length = currentItem[IDs::length];
        const double t = juce::jmax (0.0, TimeFormat::parse (startField->value.getText()));
        if (project.isFree (currentItem.getParent(), t, length, currentItem))
        {
            auto& um = project.getUndoManager();
            um.beginNewTransaction ("Item start");
            currentItem.setProperty (IDs::start, t, &um);
        }
        refresh();
    };

    lengthField->value.onTextChange = [this]
    {
        if (! currentItem.isValid()) return;
        const double start = currentItem[IDs::start];
        const double length = juce::jmax (Project::minItemLength, lengthField->value.getText().getDoubleValue());
        if (project.isFree (currentItem.getParent(), start, length, currentItem))
        {
            auto& um = project.getUndoManager();
            um.beginNewTransaction ("Item length");
            currentItem.setProperty (IDs::length, length, &um);
        }
        refresh();
    };

    tempoField->value.onTextChange = [this]
    {
        if (! currentItem.isValid() || currentItem[IDs::timebase].toString() != "musical") return;
        // Sets the first tempo point (the item's starting tempo).
        juce::ValueTree first;
        for (auto t : currentItem)
            if (t.hasType (IDs::TEMPO) && (! first.isValid() || (double) t[IDs::beat] < (double) first[IDs::beat]))
                first = t;
        if (! first.isValid()) return;

        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Change tempo");
        const double old = first[IDs::bpm];
        first.setProperty (IDs::bpm, juce::jlimit (10.0, 999.0, tempoField->value.getText().getDoubleValue()), &um);
        if (! project.refreshMusicalLength (currentItem))
            first.setProperty (IDs::bpm, old, &um);
        refresh();
    };

    timeField->value.onTextChange = [this]
    {
        if (! currentPoint.isValid()) return;
        const ItemTime it (currentPoint.getParent());
        const auto text = timeField->value.getText();
        const double barBeat = it.isMusical() ? it.parseBarBeat (text) : -1.0;
        const double t = juce::jlimit (0.0, it.localLength(), barBeat >= 0.0 ? barBeat : it.localAt (TimeFormat::parse (text)));
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Point time");
        currentPoint.setProperty (IDs::time, t, &um);
    };

    valueField->value.onTextChange = [this]
    {
        if (! currentPoint.isValid()) return;
        auto* param = parameterFor (currentPoint.getParent().getParent());
        const auto text = valueField->value.getText();
        const float v = param != nullptr ? param->getValueForText (text) : (float) text.getDoubleValue();
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Point value");
        currentPoint.setProperty (IDs::value, juce::jlimit (0.0f, 1.0f, v), &um);
    };

    shapeBox.addItem ("Linear", 1);
    shapeBox.addItem ("Curve: slow start", 2);
    shapeBox.addItem ("Curve: fast start", 3);
    shapeBox.addItem ("Smooth (S-curve)", 4);
    shapeBox.addItem ("Hold, then jump", 5);
    shapeBox.setTooltip ("Shape of the segment that starts at this point");
    shapeBox.onChange = [this]
    {
        if (! currentPoint.isValid()) return;
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Change segment shape");
        const int id = shapeBox.getSelectedId();
        const int shape = id <= 1 ? 0 : (id <= 3 ? 1 : id - 2);
        const float tension = id == 2 ? 0.6f : (id == 3 ? -0.6f : 0.0f);
        currentPoint.setProperty (IDs::shape, shape, &um);
        currentPoint.setProperty (IDs::tension, tension, &um);
    };
    addAndMakeVisible (shapeBox);

    selection.addChangeListener (this);
    project.getState().addListener (this);
    refresh();
}

Inspector::~Inspector()
{
    project.getState().removeListener (this);
    selection.removeChangeListener (this);
}

Inspector::Field& Inspector::addField (const juce::String& name, bool editable)
{
    auto* f = fields.add (new Field());
    f->name.setText (name, juce::dontSendNotification);
    f->name.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
    f->name.setFont (juce::FontOptions (12.0f));
    f->value.setFont (juce::FontOptions (12.5f));
    f->value.setEditable (editable, editable, false);
    if (editable)
    {
        f->value.setColour (juce::Label::outlineColourId, juce::Colours::white.withAlpha (0.12f));
        f->value.setTooltip ("Click to type a new value");
    }
    addAndMakeVisible (f->name);
    addAndMakeVisible (f->value);
    return *f;
}

juce::AudioProcessorParameter* Inspector::parameterFor (const juce::ValueTree& lane) const
{
    auto* plugin = host.getPlugin();
    const int index = controller.resolveParameterIndex (lane);
    return (plugin != nullptr && index >= 0) ? plugin->getParameters()[index] : nullptr;
}

void Inspector::refresh()
{
    selection.prune();
    currentPoint = selection.getPoints().size() == 1 ? selection.getPoints().getFirst() : juce::ValueTree();
    currentItem = currentPoint.isValid() ? currentPoint.getParent()
                                         : (selection.getItems().size() == 1 ? selection.getItems().getFirst() : juce::ValueTree());

    const bool hasItem = currentItem.isValid(), hasPoint = currentPoint.isValid();
    hint.setVisible (! hasItem);
    itemHeading.setVisible (hasItem);
    const bool musical = hasItem && currentItem[IDs::timebase].toString() == "musical";
    for (auto* f : { laneField, startField, lengthField, countField, timebaseField })
    {
        f->name.setVisible (hasItem);
        f->value.setVisible (hasItem);
    }
    tempoField->name.setVisible (musical);
    tempoField->value.setVisible (musical);
    lengthField->value.setEditable (! musical, ! musical, false);
    pointHeading.setVisible (hasPoint);
    for (auto* f : { timeField, valueField })
    {
        f->name.setVisible (hasPoint);
        f->value.setVisible (hasPoint);
    }
    shapeBox.setVisible (hasPoint);
    const bool noteItem = hasItem && Project::isNoteItem (currentItem);
    generatorPanel.setItem (hasPoint || noteItem ? juce::ValueTree() : currentItem);
    generatorPanel.setVisible (hasItem && ! hasPoint && ! noteItem);
    generatorHeading.setVisible (hasItem && ! hasPoint && ! noteItem);
    countField->name.setText (noteItem ? "Notes" : "Points", juce::dontSendNotification);

    if (! hasItem && selection.getItems().size() > 1)
    {
        hint.setText (juce::String (selection.getItems().size()) + " items selected.", juce::dontSendNotification);
        hint.setVisible (true);
    }
    else if (! hasItem)
    {
        hint.setText ("Select an item or a point to see its details.", juce::dontSendNotification);
    }

    if (hasItem)
    {
        auto lane = currentItem.getParent();
        int numPoints = 0;
        for (const auto& c : currentItem) if (c.hasType (IDs::POINT) || c.hasType (IDs::NOTE)) ++numPoints;

        laneField->value.setText (lane[IDs::name].toString(), juce::dontSendNotification);
        startField->value.setText (TimeFormat::format (currentItem[IDs::start]), juce::dontSendNotification);
        lengthField->value.setText (juce::String ((double) currentItem[IDs::length], 3), juce::dontSendNotification);
        countField->value.setText (ItemTime::isGenerator (currentItem) ? juce::String ("generated (") + juce::String (numPoints) + " kept)"
                                                                     : juce::String (numPoints), juce::dontSendNotification);

        const ItemTime it (currentItem);
        if (it.isMusical())
        {
            timebaseField->value.setText ("Musical " + juce::String (it.beatsPerBar) + "/" + juce::String (it.beatUnit)
                                              + ", " + juce::String (it.lengthBeats, 2) + " beats", juce::dontSendNotification);
            const double a = it.tempo.points.front().bpm, b = it.tempo.points.back().bpm;
            tempoField->value.setText (juce::String (a, 1) + (it.tempo.points.size() > 1 && std::abs (a - b) > 0.01
                                                                ? juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + juce::String (b, 1) : juce::String()),
                                       juce::dontSendNotification);
        }
        else
        {
            timebaseField->value.setText ("Linear (seconds)", juce::dontSendNotification);
        }
    }

    if (hasPoint)
    {
        auto lane = currentItem.getParent();
        const float v = (float) (double) currentPoint[IDs::value];
        const ItemTime it (currentItem);
        const double local = currentPoint[IDs::time];
        timeField->value.setText (it.isMusical() ? it.barBeat (local) + "  (" + TimeFormat::format (it.absolute (local)) + ")"
                                                 : TimeFormat::format (it.absolute (local)),
                                  juce::dontSendNotification);
        juce::String text = juce::String (v, 4);
        if (auto* p = parameterFor (lane))
        {
            text = p->getText (v, 32);
            if (p->getLabel().isNotEmpty() && ! text.endsWith (p->getLabel())) text << " " << p->getLabel();
        }
        valueField->value.setText (text, juce::dontSendNotification);

        const int shape = currentPoint[IDs::shape];
        const float tension = (float) (double) currentPoint[IDs::tension];
        const int id = shape == 0 ? 1 : shape == 1 ? (tension >= 0.0f ? 2 : 3) : shape + 2;
        shapeBox.setSelectedId (id, juce::dontSendNotification);
    }

    resized();
}

void Inspector::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff262a31));
    g.setColour (juce::Colours::black.withAlpha (0.5f));
    g.drawVerticalLine (0, 0.0f, (float) getHeight());
}

void Inspector::resized()
{
    auto r = getLocalBounds().reduced (12, 8);
    title.setBounds (r.removeFromTop (24));
    r.removeFromTop (8);

    auto placeField = [&r] (Field* f)
    {
        auto row = r.removeFromTop (24);
        f->name.setBounds (row.removeFromLeft (76));
        f->value.setBounds (row);
        r.removeFromTop (4);
    };

    if (! currentItem.isValid())
    {
        hint.setBounds (r.removeFromTop (60));
        return;
    }

    itemHeading.setBounds (r.removeFromTop (22));
    for (auto* f : { laneField, startField, lengthField, countField, timebaseField })
        placeField (f);
    if (tempoField->value.isVisible())
        placeField (tempoField);

    if (currentPoint.isValid())
    {
        r.removeFromTop (12);
        pointHeading.setBounds (r.removeFromTop (22));
        placeField (timeField);
        placeField (valueField);
        shapeBox.setBounds (r.removeFromTop (24));
    }
    else if (generatorPanel.isVisible())
    {
        r.removeFromTop (12);
        generatorHeading.setBounds (r.removeFromTop (22));
        generatorPanel.setBounds (r.removeFromTop (generatorPanel.getPreferredHeight()));
    }
}
