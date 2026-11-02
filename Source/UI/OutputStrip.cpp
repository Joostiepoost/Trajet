#include "OutputStrip.h"

namespace
{
    constexpr float meterFloorDb = -48.0f;

    float dbToProportion (float db)
    {
        return juce::jlimit (0.0f, 1.0f, (db - meterFloorDb) / -meterFloorDb);
    }
}

OutputStrip::OutputStrip (Project& p, PluginHost& h) : project (p), host (h)
{
    gainSlider.setSliderStyle (juce::Slider::LinearBar);
    gainSlider.setRange (-24.0, 12.0, 0.1);
    gainSlider.setDoubleClickReturnValue (true, 0.0);
    gainSlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3b5f8f));
    gainSlider.setTooltip ("Output gain, after the plugin (double-click: 0 dB). Applies to playback and render.");
    gainSlider.textFromValueFunction = [] (double v) { return (v > 0.0 ? "+" : "") + juce::String (v, 1) + " dB"; };
    gainSlider.onDragStart = [this] { project.getUndoManager().beginNewTransaction ("Output gain"); };
    gainSlider.onValueChange = [this]
    {
        auto& um = project.getUndoManager();
        if (! gainSlider.isMouseButtonDown()) um.beginNewTransaction ("Output gain");
        project.getState().setProperty (IDs::outputGain, gainSlider.getValue(), &um);
    };
    addAndMakeVisible (gainSlider);

    limiterButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffb86b2a));
    limiterButton.setTooltip ("Look-ahead limiter at the end of the chain: no peak goes above the ceiling. "
                              "Right-click to set the ceiling.");
    limiterButton.onClicked = [this] (bool popup)
    {
        if (popup) { showCeilingMenu(); return; }
        const bool on = ! (bool) project.getState().getProperty (IDs::limiter, false);
        auto& um = project.getUndoManager();
        um.beginNewTransaction (on ? "Limiter on" : "Limiter off");
        project.getState().setProperty (IDs::limiter, on, &um);
    };
    addAndMakeVisible (limiterButton);

    project.getState().addListener (this);
    refresh();
    startTimerHz (30);
}

OutputStrip::~OutputStrip()
{
    project.getState().removeListener (this);
}

void OutputStrip::refresh()
{
    const auto& st = project.getState();
    gainSlider.setValue (st.getProperty (IDs::outputGain, 0.0), juce::dontSendNotification);
    gainSlider.updateText();
    limiterButton.setToggleState ((bool) st.getProperty (IDs::limiter, false), juce::dontSendNotification);
    const double ceiling = st.getProperty (IDs::limiterCeiling, -0.3);
    limiterButton.setButtonText ("Limit " + juce::String (ceiling, 1));
}

juce::Rectangle<int> OutputStrip::meterArea() const
{
    return getLocalBounds().removeFromLeft (110).reduced (0, 6);
}

void OutputStrip::timerCallback()
{
    auto& engine = host.getEngine();
    const float peak = engine.takePeak();
    const float g = engine.takeLimiterGain();

    if (peak > 1.0001f)
        clipped = true;

    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    level = juce::jmax (db, level - 1.5f);          // falls about 45 dB per second
    if (db >= hold) { hold = db; holdFrames = 45; }
    else if (--holdFrames < 0) hold = juce::jmax (meterFloorDb, hold - 1.0f);

    limiterDb = juce::jmin (juce::Decibels::gainToDecibels (g, -100.0f), limiterDb * 0.85f);

    repaint (meterArea().expanded (2));
}

void OutputStrip::paint (juce::Graphics& g)
{
    auto r = meterArea();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.toFloat(), 3.0f);

    auto bar = r.reduced (3, 0);
    auto clipLight = bar.removeFromRight (12);
    bar.removeFromRight (3);
    auto top = bar.removeFromTop (bar.getHeight() / 2).reduced (0, 3);
    auto bottom = bar.reduced (0, 3);

    // Level (top): green, amber above -6 dB, red above 0 dB.
    const float w = dbToProportion (level) * (float) top.getWidth();
    juce::ColourGradient grad (juce::Colour (0xff3fae6a), (float) top.getX(), 0.0f,
                               juce::Colour (0xffd04545), (float) top.getRight(), 0.0f, false);
    grad.addColour (dbToProportion (-6.0f), juce::Colour (0xffd6b23f));
    g.setGradientFill (grad);
    g.fillRect (top.withWidth ((int) w));
    g.setColour (juce::Colours::white.withAlpha (0.8f));
    const int hx = top.getX() + (int) (dbToProportion (hold) * (float) top.getWidth());
    if (hold > meterFloorDb) g.drawVerticalLine (hx, (float) top.getY(), (float) top.getBottom());

    // Limiter gain reduction (bottom): orange, growing from the right.
    if (limiterDb < -0.05f)
    {
        const float gw = juce::jlimit (0.0f, 1.0f, -limiterDb / 12.0f) * (float) bottom.getWidth();
        g.setColour (juce::Colour (0xffe0883a));
        g.fillRect (bottom.withLeft (bottom.getRight() - (int) gw));
    }

    // Clip light: stays red until clicked.
    g.setColour (clipped ? juce::Colour (0xffe04040) : juce::Colours::white.withAlpha (0.12f));
    g.fillRoundedRectangle (clipLight.reduced (0, 4).toFloat(), 2.0f);
}

void OutputStrip::resized()
{
    auto r = getLocalBounds();
    r.removeFromLeft (110 + 6);
    limiterButton.setBounds (r.removeFromRight (78));
    r.removeFromRight (4);
    gainSlider.setBounds (r);
}

void OutputStrip::showCeilingMenu()
{
    {
        juce::PopupMenu m;
        const double current = project.getState().getProperty (IDs::limiterCeiling, -0.3);
        m.addSectionHeader ("Limiter ceiling");
        for (double c : { 0.0, -0.1, -0.3, -1.0, -3.0, -6.0 })
            m.addItem (juce::String (c, 1) + " dBFS", true, std::abs (c - current) < 0.001,
                       [this, c] { auto& um = project.getUndoManager();
                                   um.beginNewTransaction ("Limiter ceiling");
                                   project.getState().setProperty (IDs::limiterCeiling, c, &um); });
        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&limiterButton));
    }
}

void OutputStrip::mouseDown (const juce::MouseEvent& e)
{
    if (meterArea().contains (e.getPosition()))
    {
        clipped = false;
        hold = meterFloorDb;
        repaint();
    }
}
