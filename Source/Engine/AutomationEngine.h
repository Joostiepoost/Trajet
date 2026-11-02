// AutomationEngine.h — sits between the audio device and the plugin.
//
// It runs the transport (play position, loop), and before handing each piece of
// audio to the plugin it sets the plugin's parameters from the automation.
// In Max terms: a [line~]-per-parameter driving [vst~], with its own clock.
//
// Threads: the audio thread calls processBlock(); everything else is called from
// the message (UI) thread. They share only atomics and an immutable Snapshot.

#pragma once

#include <JuceHeader.h>
#include "Automation.h"
#include "Limiter.h"

class AutomationEngine : public juce::AudioProcessor
{
public:
    AutomationEngine();
    ~AutomationEngine() override;

    /** Only call while the engine is not attached to the audio player. */
    void setPlugin (juce::AudioPluginInstance*);

    //==============================================================================
    // Transport (message thread)
    void play()                                 { playing = true; }
    void stop()                                 { playing = false; }
    bool isPlaying() const noexcept             { return playing; }

    double getPositionSeconds() const noexcept;
    void setPositionSeconds (double seconds);

    void setLoop (bool shouldLoop, double startSeconds, double endSeconds);
    bool isLooping() const noexcept             { return loopOn; }
    void setProjectLength (double seconds)      { projectLength = seconds; }

    //==============================================================================
    // Automation (message thread)
    void setSnapshot (std::shared_ptr<Automation::Snapshot>);

    /** In patch mode the engine writes nothing: the plugin shows the plain patch. */
    void setPatchMode (bool shouldBeInPatchMode)    { patchMode = shouldBeInPatchMode; resendAll = true; }
    bool isPatchMode() const noexcept               { return patchMode; }

    /** Temporarily stop writing (e.g. while the patch is being saved). */
    void setWritesPaused (bool paused)              { writesPaused = paused; }
    bool areWritesPaused() const noexcept           { return writesPaused; }

    /** Send every automated value again on the next block. */
    void forceResend()                              { resendAll = true; }

    /** Live update interval in samples; 0 = once per audio buffer. */
    void setLiveInterval (int samples)              { liveInterval = juce::jmax (0, samples); }
    int getLiveInterval() const noexcept            { return liveInterval; }

    /** True if this parameter currently has a lane (safe from any thread). */
    bool isParameterAutomated (int index) const noexcept;

    /** Offline rendering: tells the plugin it is not running in real time. */
    void setNonRealtime (bool isNonRealtime) noexcept override
    {
        AudioProcessor::setNonRealtime (isNonRealtime);
        if (plugin != nullptr) plugin->setNonRealtime (isNonRealtime);
    }

    /** Clears the plugin's sound (delay lines, envelopes) without touching its patch. */
    void resetPlugin()                              { if (plugin != nullptr) plugin->reset(); }

    //==============================================================================
    // Output stage: gain, optional look-ahead limiter, level meter.
    void setOutput (float gainDb, bool limiterOn, float ceilingDb)
    {
        outputGain = juce::Decibels::decibelsToGain (gainDb, -100.0f);
        limiterEnabled = limiterOn;
        limiterCeiling = juce::Decibels::decibelsToGain (juce::jlimit (-12.0f, 0.0f, ceilingDb));
    }

    /** Samples by which the output stage delays the sound (the limiter's look-ahead). */
    int getOutputLatency() const noexcept           { return limiterEnabled.load() ? limiterLatency.load() : 0; }

    /** Highest output level since the last call (1.0 = 0 dBFS), and the limiter's deepest gain. */
    float takePeak() noexcept                       { return peakLevel.exchange (0.0f); }
    float takeLimiterGain() noexcept                { return limiterGain.exchange (1.0f); }

    /** Frees snapshots the audio thread no longer uses (call regularly). */
    void collectGarbage();

    //==============================================================================
    // AudioProcessor
    const juce::String getName() const override            { return "Trajet engine"; }
    void prepareToPlay (double sampleRate, int maxBlockSize) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;

    double getTailLengthSeconds() const override            { return 0.0; }
    bool acceptsMidi() const override                       { return true; }
    bool producesMidi() const override                      { return false; }
    juce::AudioProcessorEditor* createEditor() override     { return nullptr; }
    bool hasEditor() const override                         { return false; }
    int getNumPrograms() override                           { return 1; }
    int getCurrentProgram() override                        { return 0; }
    void setCurrentProgram (int) override                   {}
    const juce::String getProgramName (int) override        { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override  {}
    void setStateInformation (const void*, int) override    {}

private:
    void applyAutomation (double seconds);

    juce::AudioPluginInstance* plugin = nullptr;
    juce::Array<juce::AudioProcessorParameter*> params;

    std::atomic<bool> playing { false }, loopOn { false }, patchMode { true },
                      writesPaused { false }, resendAll { true };
    std::atomic<juce::int64> position { 0 };
    std::atomic<double> currentRate { 48000.0 }, loopStart { 0.0 }, loopEnd { 60.0 }, projectLength { 60.0 };
    std::atomic<int> liveInterval { 0 };

    juce::SpinLock snapshotLock;
    std::shared_ptr<Automation::Snapshot> pending, current, retired;
    std::vector<float> lastSent;

    std::unique_ptr<std::atomic<bool>[]> automatedFlags;
    int numParams = 0;

    std::atomic<float> outputGain { 1.0f }, limiterCeiling { 0.966f }, peakLevel { 0.0f }, limiterGain { 1.0f };
    std::atomic<bool> limiterEnabled { false };
    std::atomic<int> limiterLatency { 0 };
    bool limiterWasOn = false;
    Limiter limiter;

    juce::AudioBuffer<float> pluginBuffer;
    juce::MidiBuffer chunkMidi;
    int preparedBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationEngine)
};
