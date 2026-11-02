// PluginHost.h — the audio side of Trajet.
//
// Owns the audio device, the list of known plugins, and the one plugin that
// is currently loaded. In Max terms: this is [vst~] plus [dac~], plus the
// plugin browser.

#pragma once

#include <JuceHeader.h>

class PluginHost : private juce::ChangeListener,
                   private juce::AudioProcessorListener
{
public:
    PluginHost();
    ~PluginHost() override;

    //==============================================================================
    // Things the UI needs direct access to.
    juce::AudioDeviceManager     deviceManager;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList        knownPlugins;
    juce::ApplicationProperties  appProperties;

    /** File that records a plugin which crashed during a scan, so it is skipped next time. */
    juce::File getDeadMansPedalFile();

    //==============================================================================
    /** Loads a plugin asynchronously; onDone receives an empty string on success,
        or an error message. Any previous plugin is unloaded first. */
    void loadPlugin (const juce::PluginDescription& description,
                     std::function<void (const juce::String& error)> onDone);

    void unloadPlugin();

    juce::AudioPluginInstance* getPlugin() const noexcept   { return plugin.get(); }

    /** The plugin loaded last time the app ran, if any. */
    std::unique_ptr<juce::PluginDescription> getLastPluginDescription();

    //==============================================================================
    /** Held note: for synths that only sound while a note is held. */
    void setHeldNote (bool shouldBeOn, int noteNumber, int velocity);
    bool isHeldNoteOn() const noexcept      { return heldNoteOn; }

    //==============================================================================
    /** Audio processing on/off: off closes the audio device, so neither the device
        nor the plugin uses any CPU. The plugin and its settings stay loaded. */
    void setAudioEnabled (bool shouldBeEnabled);
    bool isAudioEnabled() const noexcept    { return audioEnabled; }

    /** True once if the plugin's patch changed since the last call (you turned a dial,
        picked a preset...). Used to mark the project as unsaved. */
    bool consumePatchChangedFlag() noexcept  { return patchChanged.exchange (false); }

    /** Called (on the message thread) whenever a plugin has been loaded or unloaded. */
    std::function<void()> onPluginChanged;

private:
    void changeListenerCallback (juce::ChangeBroadcaster*) override;

    // AudioProcessorListener: may be called from any thread, so it only sets a flag.
    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override   { patchChanged = true; }
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& d) override
    {
        if (d.parameterInfoChanged || d.programChanged || d.nonParameterStateChanged)
            patchChanged = true;
    }
    void sendNoteOn();
    void sendNoteOff();
    bool deviceIsRunning() const;

    juce::AudioProcessorPlayer player;
    std::unique_ptr<juce::AudioPluginInstance> plugin;

    bool audioEnabled = true;
    std::atomic<bool> patchChanged { false };
    bool heldNoteOn = false;
    int heldNote = 48, heldVelocity = 100, soundingNote = -1;

    // Lets an asynchronous plugin-creation callback know whether we still exist.
    std::shared_ptr<bool> alive = std::make_shared<bool> (true);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PluginHost)
};
