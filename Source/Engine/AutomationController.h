// AutomationController.h — the link between the project (lanes in the model)
// and the audio engine. Runs on the message thread.
//
// It keeps the engine's copy of the automation up to date, and manages the
// two stopped states:
//   - chase: the plugin sounds like the score at the playhead;
//   - patch: the plugin shows its plain patch, so you can edit the starting state.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"

class AutomationController : private juce::ValueTree::Listener,
                             private juce::Timer
{
public:
    AutomationController (Project&, PluginHost&);
    ~AutomationController() override;

    /** Call after a plugin was loaded (and its patch restored) or unloaded. */
    void pluginReady();

    // Transport
    void play();
    void stop();
    void togglePlay();
    void locate (double seconds);       // also leaves patch mode, so you hear that spot
    void toStart()                      { locate (0.0); }
    void setLooping (bool);

    // Patch / chase
    void setPatchMode (bool);
    bool isPatchMode() const;

    // Live update interval (samples, 0 = per buffer); stored in the app settings.
    void setLiveInterval (int samples);
    int getLiveInterval() const;

    /** Resolves a lane to the plugin's parameter index (by index, checked by name). */
    int resolveParameterIndex (const juce::ValueTree& lane) const;

private:
    void rebuild();
    void restoreBaseValues();
    void captureBaseValues();
    void applyLoopRange();

    void valueTreePropertyChanged (juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded (juce::ValueTree&, juce::ValueTree&) override        { triggerRebuild(); }
    void valueTreeChildRemoved (juce::ValueTree&, juce::ValueTree&, int) override { triggerRebuild(); }
    void valueTreeChildOrderChanged (juce::ValueTree&, int, int) override         { triggerRebuild(); }
    void valueTreeRedirected (juce::ValueTree&) override                          { triggerRebuild(); }

    void triggerRebuild()   { rebuildPending = true; }
    void timerCallback() override;

    Project& project;
    PluginHost& host;
    std::map<int, float> baseValues;    // parameter index -> patch value
    bool rebuildPending = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AutomationController)
};
