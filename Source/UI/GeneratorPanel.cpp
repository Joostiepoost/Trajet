#include "GeneratorPanel.h"

namespace
{
    constexpr int rowHeight = 24, gap = 4, labelWidth = 62;
}

GeneratorPanel::GeneratorPanel (Project& p) : project (p)
{
    auto label = [this] (juce::Label& l, const juce::String& text)
    {
        l.setText (text, juce::dontSendNotification);
        l.setFont (juce::FontOptions (12.0f));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.55f));
        addAndMakeVisible (l);
    };
    label (typeLabel, "Type");
    label (shapeLabel, "Shape");
    label (rateLabel, "Rate");
    label (minLabel, "Min");
    label (maxLabel, "Max");
    label (phaseLabel, "Phase");
    label (seedLabel, "Seed");
    label (formulaLabel, "Formula");

    errorLabel.setColour (juce::Label::textColourId, juce::Colours::orange);
    errorLabel.setFont (juce::FontOptions (11.0f));
    addAndMakeVisible (errorLabel);
    helpLabel.setText ("t: 0 to 1 over the item, s: seconds, b: beats. Result 0..1 maps onto min..max. "
                       "Functions: sin cos abs pow min max clamp tri saw sq noise rand mix step...",
                       juce::dontSendNotification);
    helpLabel.setFont (juce::FontOptions (10.5f));
    helpLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.4f));
    helpLabel.setJustificationType (juce::Justification::topLeft);
    addAndMakeVisible (helpLabel);

    typeBox.addItem ("Points (by hand)", 1);
    typeBox.addItem ("LFO", 2);
    typeBox.addItem ("Random", 3);
    typeBox.addItem ("Formula", 4);
    typeBox.onChange = [this]
    {
        if (updating || ! item.isValid()) return;
        const char* types[] = { "", "lfo", "random", "formula" };
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Generator");
        project.setGenerator (item, types[juce::jlimit (0, 3, typeBox.getSelectedId() - 1)]);
    };
    addAndMakeVisible (typeBox);

    shapeBox.onChange = [this]
    {
        if (updating) return;
        setProperty (IDs::genShape, shapeBox.getSelectedId() - 1, "Generator shape");
    };
    addAndMakeVisible (shapeBox);

    auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double interval)
    {
        s.setSliderStyle (juce::Slider::LinearBar);
        s.setRange (lo, hi, interval);
        s.setColour (juce::Slider::trackColourId, juce::Colour (0xff3b5f8f));
        addAndMakeVisible (s);
    };

    setupSlider (rateSlider, 0.01, 64.0, 0.0);
    rateSlider.setSkewFactorFromMidPoint (2.0);
    rateSlider.textFromValueFunction = [this] (double v)
    {
        const bool musical = item.isValid() && ItemTime (item).isMusical();
        return juce::String (v, v < 1.0 ? 3 : 2) + (musical ? " per beat" : " Hz");
    };
    rateSlider.valueFromTextFunction = [] (const juce::String& t) { return t.getDoubleValue(); };
    rateSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Generator rate"); };
    rateSlider.onValueChange = [this] { if (! updating) setProperty (IDs::genRate, rateSlider.getValue(), "Generator rate", ! rateSlider.isMouseButtonDown()); };

    for (auto* s : { &minSlider, &maxSlider })
    {
        setupSlider (*s, 0.0, 1.0, 0.0);
        s->textFromValueFunction = [this] (double v) { return valueText (v); };
        s->valueFromTextFunction = [this] (const juce::String& t)
        {
            if (parameterFor && item.isValid())
                if (auto* param = parameterFor (item.getParent()))
                    return (double) param->getValueForText (t);
            return t.getDoubleValue();
        };
    }
    minSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Generator range"); };
    maxSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Generator range"); };
    minSlider.onValueChange = [this] { if (! updating) setProperty (IDs::genMin, minSlider.getValue(), "Generator range", ! minSlider.isMouseButtonDown()); };
    maxSlider.onValueChange = [this] { if (! updating) setProperty (IDs::genMax, maxSlider.getValue(), "Generator range", ! maxSlider.isMouseButtonDown()); };

    setupSlider (phaseSlider, 0.0, 360.0, 1.0);
    phaseSlider.textFromValueFunction = [] (double v) { return juce::String ((int) v) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb0")); };
    phaseSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Generator phase"); };
    phaseSlider.onValueChange = [this] { if (! updating) setProperty (IDs::genPhase, phaseSlider.getValue() / 360.0, "Generator phase", ! phaseSlider.isMouseButtonDown()); };

    seedSlider.setSliderStyle (juce::Slider::IncDecButtons);
    seedSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 60, rowHeight);
    seedSlider.setRange (1.0, 99999.0, 1.0);
    seedSlider.onValueChange = [this] { if (! updating) setProperty (IDs::genSeed, (int) seedSlider.getValue(), "Random seed"); };
    addAndMakeVisible (seedSlider);

    newSeedButton.setTooltip ("A different random line");
    newSeedButton.onClick = [this] { setProperty (IDs::genSeed, juce::Random::getSystemRandom().nextInt ({ 1, 99999 }), "New random seed"); };
    addAndMakeVisible (newSeedButton);

    formulaEditor.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 12.5f, juce::Font::plain));
    formulaEditor.setTextToShowWhenEmpty ("e.g. 0.5 + 0.5*sin(2*pi*4*t)", juce::Colours::grey);
    formulaEditor.onReturnKey = [this] { applyFormula(); };
    formulaEditor.onFocusLost = [this] { applyFormula(); };
    addAndMakeVisible (formulaEditor);

    bakeButton.setTooltip ("Turn the generator into ordinary points that you can edit by hand");
    bakeButton.onClick = [this]
    {
        if (! item.isValid()) return;
        auto& um = project.getUndoManager();
        um.beginNewTransaction ("Bake generator");
        const int n = project.bakeGenerator (item);
        if (showStatus) showStatus ("Baked to " + juce::String (n) + " points.");
    };
    addAndMakeVisible (bakeButton);

    project.getState().addListener (this);
    refresh();
}

GeneratorPanel::~GeneratorPanel()
{
    project.getState().removeListener (this);
}

void GeneratorPanel::setItem (juce::ValueTree newItem)
{
    if (item == newItem) return;
    item = newItem;
    refresh();
}

juce::String GeneratorPanel::valueText (double v) const
{
    if (parameterFor && item.isValid())
        if (auto* p = parameterFor (item.getParent()))
        {
            auto text = p->getText ((float) v, 32);
            if (p->getLabel().isNotEmpty() && ! text.endsWith (p->getLabel())) text << " " << p->getLabel();
            return text;
        }
    return juce::String (v, 3);
}

void GeneratorPanel::setProperty (const juce::Identifier& id, const juce::var& value, const juce::String& action, bool newTransaction)
{
    if (! item.isValid()) return;
    auto& um = project.getUndoManager();
    if (newTransaction) um.beginNewTransaction (action);
    item.setProperty (id, value, &um);
}

void GeneratorPanel::applyFormula()
{
    if (! item.isValid() || item[IDs::formula].toString() == formulaEditor.getText()) return;
    juce::String error;
    Formula::parseCached (formulaEditor.getText(), error);
    errorLabel.setText (error, juce::dontSendNotification);
    setProperty (IDs::formula, formulaEditor.getText(), "Formula");
}

void GeneratorPanel::valueTreePropertyChanged (juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree == item || tree == item.getParent())
        refresh();
}

void GeneratorPanel::refresh()
{
    const juce::ScopedValueSetter<bool> svs (updating, true);
    const auto type = item.isValid() ? item[IDs::generator].toString() : juce::String();
    const bool isLfo = type == "lfo", isRandom = type == "random", isFormula = type == "formula";
    const bool isGen = isLfo || isRandom || isFormula;

    typeBox.setSelectedId (isLfo ? 2 : isRandom ? 3 : isFormula ? 4 : 1, juce::dontSendNotification);

    shapeBox.clear (juce::dontSendNotification);
    if (isLfo)
        for (auto* n : { "Sine", "Triangle", "Square", "Saw up", "Saw down" })
            shapeBox.addItem (n, shapeBox.getNumItems() + 1);
    else if (isRandom)
        for (auto* n : { "Stepped", "Smooth", "Drift" })
            shapeBox.addItem (n, shapeBox.getNumItems() + 1);

    shapeLabel.setText (isRandom ? "Mode" : "Shape", juce::dontSendNotification);

    if (item.isValid())
    {
        shapeBox.setSelectedId ((int) item.getProperty (IDs::genShape, 0) + 1, juce::dontSendNotification);
        rateSlider.setValue (item.getProperty (IDs::genRate, 1.0), juce::dontSendNotification);
        minSlider.setValue (item.getProperty (IDs::genMin, 0.0), juce::dontSendNotification);
        maxSlider.setValue (item.getProperty (IDs::genMax, 1.0), juce::dontSendNotification);
        phaseSlider.setValue ((double) item.getProperty (IDs::genPhase, 0.0) * 360.0, juce::dontSendNotification);
        seedSlider.setValue (item.getProperty (IDs::genSeed, 1), juce::dontSendNotification);
        if (! formulaEditor.hasKeyboardFocus (true))
            formulaEditor.setText (item[IDs::formula].toString(), juce::dontSendNotification);
        for (auto* s : { &rateSlider, &minSlider, &maxSlider, &phaseSlider })
            s->updateText();

        juce::String error;
        if (isFormula) Formula::parseCached (item[IDs::formula].toString(), error);
        errorLabel.setText (error, juce::dontSendNotification);
    }

    auto show = [] (bool visible, std::initializer_list<juce::Component*> cs) { for (auto* c : cs) c->setVisible (visible); };
    show (item.isValid(), { &typeLabel, &typeBox });
    show (isLfo || isRandom, { &shapeLabel, &shapeBox, &rateLabel, &rateSlider });
    show (isGen, { &minLabel, &minSlider, &maxLabel, &maxSlider, &bakeButton });
    show (isLfo, { &phaseLabel, &phaseSlider });
    show (isRandom, { &seedLabel, &seedSlider, &newSeedButton });
    show (isFormula, { &formulaLabel, &formulaEditor, &errorLabel, &helpLabel });

    resized();
}

int GeneratorPanel::getPreferredHeight() const
{
    if (! item.isValid()) return 0;
    const auto type = item[IDs::generator].toString();
    int rows = 1;
    if (type == "lfo")     rows += 6;      // shape, rate, min, max, phase, bake
    if (type == "random")  rows += 6;      // mode, rate, min, max, seed, bake
    if (type == "formula") rows += 4 + 3;  // formula, error, help (2 rows), min, max, bake
    return rows * (rowHeight + gap);
}

void GeneratorPanel::resized()
{
    auto r = getLocalBounds();
    auto row = [&r] { auto x = r.removeFromTop (rowHeight); r.removeFromTop (gap); return x; };
    auto place = [&] (juce::Label& l, juce::Component& c)
    {
        if (! c.isVisible()) return;
        auto x = row();
        l.setBounds (x.removeFromLeft (labelWidth));
        c.setBounds (x);
    };

    place (typeLabel, typeBox);
    place (shapeLabel, shapeBox);
    place (rateLabel, rateSlider);

    if (formulaEditor.isVisible())
    {
        auto x = row();
        formulaLabel.setBounds (x.removeFromLeft (labelWidth));
        formulaEditor.setBounds (x);
        errorLabel.setBounds (row());
        helpLabel.setBounds (r.removeFromTop (2 * rowHeight + gap));
        r.removeFromTop (gap);
    }

    place (minLabel, minSlider);
    place (maxLabel, maxSlider);
    place (phaseLabel, phaseSlider);

    if (seedSlider.isVisible())
    {
        auto x = row();
        seedLabel.setBounds (x.removeFromLeft (labelWidth));
        newSeedButton.setBounds (x.removeFromRight (46));
        x.removeFromRight (4);
        seedSlider.setBounds (x);
    }

    if (bakeButton.isVisible())
        bakeButton.setBounds (row().withWidth (juce::jmin (140, r.getWidth())));
}
