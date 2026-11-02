#include "PluginHost.h"

namespace
{
    juce::PropertiesFile::Options makeSettingsOptions()
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Trajet";
        o.filenameSuffix      = "settings";
        o.folderName          = "Trajet";
        o.osxLibrarySubFolder = "Application Support";
        return o;
    }

    const char* const keyDeviceState = "audioDeviceState";
    const char* const keyPluginList  = "knownPlugins";
    const char* const keyLastPlugin  = "lastPlugin";
}

PluginHost::PluginHost()
{
    appProperties.setStorageParameters (makeSettingsOptions());
    auto* settings = appProperties.getUserSettings();

    // Which plugin formats we can host: AU and VST3 on macOS.
    juce::addDefaultFormatsToManager (formatManager);

    // Restore the plugin list from the last scan.
    if (auto xml = settings->getXmlValue (keyPluginList))
        knownPlugins.recreateFromXml (*xml);

    knownPlugins.addChangeListener (this);

    // Audio: no inputs, stereo out; restore the last chosen device and sample rate.
    auto savedState = settings->getXmlValue (keyDeviceState);
    deviceManager.initialise (0, 2, savedState.get(), true);
    deviceManager.addChangeListener (this);
    deviceManager.addAudioCallback (&player);
}

PluginHost::~PluginHost()
{
    *alive = false;
    knownPlugins.removeChangeListener (this);
    deviceManager.removeChangeListener (this);
    unloadPlugin();
    deviceManager.removeAudioCallback (&player);
    deviceManager.closeAudioDevice();
}

juce::File PluginHost::getDeadMansPedalFile()
{
    return appProperties.getUserSettings()->getFile().getSiblingFile ("RecentlyCrashedPluginsList");
}

//==============================================================================
void PluginHost::loadPlugin (const juce::PluginDescription& description,
                             std::function<void (const juce::String&)> onDone)
{
    unloadPlugin();

    double sampleRate = 48000.0;
    int blockSize = 512;

    if (auto* device = deviceManager.getCurrentAudioDevice())
    {
        sampleRate = device->getCurrentSampleRate();
        blockSize  = device->getCurrentBufferSizeSamples();
    }

    std::weak_ptr<bool> stillAlive = alive;

    formatManager.createPluginInstanceAsync (description, sampleRate, blockSize,
        [this, stillAlive, description, onDone] (std::unique_ptr<juce::AudioPluginInstance> instance,
                                                 const juce::String& error)
        {
            if (stillAlive.expired())
                return;

            if (instance == nullptr)
            {
                if (onDone) onDone (error.isNotEmpty() ? error : juce::String ("The plugin could not be loaded."));
                return;
            }

            plugin = std::move (instance);
            player.setProcessor (plugin.get());

            if (auto xml = description.createXml())
                appProperties.getUserSettings()->setValue (keyLastPlugin, xml.get());

            if (heldNoteOn)
                sendNoteOn();

            if (onPluginChanged) onPluginChanged();
            if (onDone) onDone ({});
        });
}

void PluginHost::unloadPlugin()
{
    if (plugin == nullptr)
        return;

    soundingNote = -1;
    player.setProcessor (nullptr);
    plugin.reset();

    if (onPluginChanged) onPluginChanged();
}

std::unique_ptr<juce::PluginDescription> PluginHost::getLastPluginDescription()
{
    if (auto xml = appProperties.getUserSettings()->getXmlValue (keyLastPlugin))
    {
        auto desc = std::make_unique<juce::PluginDescription>();
        if (desc->loadFromXml (*xml))
            return desc;
    }
    return nullptr;
}

//==============================================================================
void PluginHost::setHeldNote (bool shouldBeOn, int noteNumber, int velocity)
{
    const bool noteChanged = (noteNumber != heldNote || velocity != heldVelocity);
    heldNote = juce::jlimit (0, 127, noteNumber);
    heldVelocity = juce::jlimit (1, 127, velocity);

    if (shouldBeOn && (! heldNoteOn || noteChanged))
    {
        sendNoteOff();
        sendNoteOn();
    }
    else if (! shouldBeOn && heldNoteOn)
    {
        sendNoteOff();
    }

    heldNoteOn = shouldBeOn;
}

void PluginHost::setAudioEnabled (bool shouldBeEnabled)
{
    if (shouldBeEnabled == audioEnabled)
        return;

    if (! shouldBeEnabled)
    {
        sendNoteOff();
        audioEnabled = false;
        deviceManager.closeAudioDevice();
    }
    else
    {
        audioEnabled = true;
        deviceManager.restartLastAudioDevice();

        if (heldNoteOn)
            sendNoteOn();
    }
}

bool PluginHost::deviceIsRunning() const
{
    auto* device = deviceManager.getCurrentAudioDevice();
    return device != nullptr && device->isPlaying();
}

void PluginHost::sendNoteOn()
{
    if (plugin == nullptr || ! deviceIsRunning())
        return;

    auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    player.getMidiMessageCollector().addMessageToQueue (
        juce::MidiMessage::noteOn (1, heldNote, (juce::uint8) heldVelocity).withTimeStamp (now));
    soundingNote = heldNote;
}

void PluginHost::sendNoteOff()
{
    if (soundingNote < 0 || plugin == nullptr || ! deviceIsRunning())
    {
        soundingNote = -1;
        return;
    }

    auto now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    player.getMidiMessageCollector().addMessageToQueue (
        juce::MidiMessage::noteOff (1, soundingNote).withTimeStamp (now));
    soundingNote = -1;
}

//==============================================================================
void PluginHost::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    auto* settings = appProperties.getUserSettings();

    if (source == &knownPlugins)
    {
        if (auto xml = knownPlugins.createXml())
            settings->setValue (keyPluginList, xml.get());
    }
    else if (source == &deviceManager && audioEnabled)
    {
        if (auto xml = deviceManager.createStateXml())
            settings->setValue (keyDeviceState, xml.get());
    }

    settings->saveIfNeeded();
}
