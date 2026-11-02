// Renderer.h — offline render of the score to a WAV file, and export of the
// automation as a CSV table.
//
// The render runs on its own thread, faster (or slower) than real time, with the
// automation updated every few samples. The audio device plays silence meanwhile.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"

struct RenderSettings
{
    juce::File file;
    double sampleRate = 48000.0;
    int bitDepth = 24;                  // 16, 24, or 32 (= 32-bit float)
    double start = 0.0, end = 60.0;     // seconds
    double tail = 2.0;                  // seconds of sound after the end
    int interval = 16;                  // automation update every n samples

    // Automation CSV
    bool writeCsv = false;
    double csvStep = 0.01;              // seconds between rows
    bool csvPluginUnits = false;        // false: normalised 0..1

    void load (juce::PropertiesFile&);
    void save (juce::PropertiesFile&) const;
};

//==============================================================================
namespace AutomationCsv
{
    /** Writes one row per step: time, then one column per lane. Returns an error or "". */
    juce::String write (const juce::File&, Project&, PluginHost&, AutomationController&, const RenderSettings&);
}

//==============================================================================
/** Starts a render. Shows a progress window; calls onFinished with a message for the
    user when done (or cancelled). Deletes itself afterwards. */
class RenderJob : private juce::ThreadWithProgressWindow
{
public:
    static void start (Project&, PluginHost&, AutomationController&, const RenderSettings&,
                       juce::Component* centreAround, std::function<void (const juce::String&)> onFinished);

private:
    RenderJob (Project&, PluginHost&, AutomationController&, const RenderSettings&, juce::Component*);
    void run() override;
    void threadComplete (bool userPressedCancel) override;

    void prepare();
    void restore();

    Project& project;
    PluginHost& host;
    AutomationController& controller;
    RenderSettings settings;
    std::function<void (const juce::String&)> onFinished;

    // State to come back to after the render.
    bool wasPatchMode = true, wasLooping = false;
    double oldPosition = 0.0;
    bool heldNoteOn = false;
    int heldNote = 48, heldVelocity = 100;

    void log (const juce::String&);
    juce::String logText;
    juce::CriticalSection logLock;

    // Results (written by the render thread).
    juce::String error;
    float peak = 0.0f;
    juce::int64 samplesWritten = 0;
    double seconds = 0.0;
};
