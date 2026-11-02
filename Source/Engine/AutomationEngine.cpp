#include "AutomationEngine.h"

AutomationEngine::AutomationEngine()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    lastSent.reserve (4096);   // so the audio thread never has to allocate
}

AutomationEngine::~AutomationEngine() = default;

void AutomationEngine::setPlugin (juce::AudioPluginInstance* newPlugin)
{
    plugin = newPlugin;
    params = plugin != nullptr ? plugin->getParameters() : juce::Array<juce::AudioProcessorParameter*>();
    numParams = params.size();

    automatedFlags = std::make_unique<std::atomic<bool>[]> ((size_t) juce::jmax (1, numParams));
    for (int i = 0; i < numParams; ++i)
        automatedFlags[(size_t) i] = false;

    const juce::SpinLock::ScopedLockType sl (snapshotLock);
    pending = nullptr;
    current = nullptr;
    retired = nullptr;
    lastSent.clear();
    patchMode = true;
}

//==============================================================================
double AutomationEngine::getPositionSeconds() const noexcept
{
    return (double) position.load() / currentRate.load();
}

void AutomationEngine::setPositionSeconds (double seconds)
{
    position = (juce::int64) (juce::jmax (0.0, seconds) * currentRate.load());
    resendAll = true;
}

void AutomationEngine::setLoop (bool shouldLoop, double startSeconds, double endSeconds)
{
    loopStart = startSeconds;
    loopEnd = juce::jmax (startSeconds + 0.01, endSeconds);
    loopOn = shouldLoop;
}

bool AutomationEngine::isParameterAutomated (int index) const noexcept
{
    return automatedFlags != nullptr && juce::isPositiveAndBelow (index, numParams)
           && automatedFlags[(size_t) index].load();
}

void AutomationEngine::setSnapshot (std::shared_ptr<Automation::Snapshot> snapshot)
{
    for (int i = 0; i < numParams; ++i)
        automatedFlags[(size_t) i] = false;

    if (snapshot != nullptr)
        for (auto& lane : snapshot->lanes)
            if (juce::isPositiveAndBelow (lane.paramIndex, numParams))
                automatedFlags[(size_t) lane.paramIndex] = true;

    const juce::SpinLock::ScopedLockType sl (snapshotLock);
    pending = std::move (snapshot);
    if (pending == nullptr)
        pending = std::make_shared<Automation::Snapshot>();
}

void AutomationEngine::collectGarbage()
{
    std::shared_ptr<Automation::Snapshot> toFree;
    {
        const juce::SpinLock::ScopedLockType sl (snapshotLock);
        toFree = std::move (retired);
    }
    // toFree is deleted here, on the message thread.
}

//==============================================================================
void AutomationEngine::prepareToPlay (double sampleRate, int maxBlockSize)
{
    // Keep the play position (in seconds) when the sample rate changes.
    const auto seconds = getPositionSeconds();
    currentRate = sampleRate;
    position = (juce::int64) (seconds * sampleRate);

    preparedBlockSize = juce::jmax (16, maxBlockSize);
    chunkMidi.ensureSize (4096);

    if (plugin != nullptr)
    {
        plugin->setRateAndBufferSizeDetails (sampleRate, preparedBlockSize);
        plugin->prepareToPlay (sampleRate, preparedBlockSize);

        const int channels = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
        pluginBuffer.setSize (channels, preparedBlockSize, false, true, false);
    }

    limiter.prepare (sampleRate, 2);
    limiterLatency = limiter.getLatency();
    limiterWasOn = false;

    resendAll = true;
}

void AutomationEngine::releaseResources()
{
    if (plugin != nullptr)
        plugin->releaseResources();
}

bool AutomationEngine::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return layouts.getMainInputChannelSet().isDisabled()
           && (out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono());
}

void AutomationEngine::applyAutomation (double seconds)
{
    auto* snapshot = current.get();
    if (snapshot == nullptr)
        return;

    const bool sendAll = resendAll.exchange (false);
    const auto numLanes = juce::jmin (snapshot->lanes.size(), lastSent.size());

    for (size_t i = 0; i < numLanes; ++i)
    {
        const auto& lane = snapshot->lanes[i];
        if (! juce::isPositiveAndBelow (lane.paramIndex, params.size()))
            continue;

        const float v = juce::jlimit (0.0f, 1.0f, Automation::laneValueAt (lane, seconds));

        // Only send values that actually change: flat stretches cost nothing.
        if (sendAll || std::abs (v - lastSent[i]) > 1.0e-6f)
        {
            params.getUnchecked (lane.paramIndex)->setValue (v);
            lastSent[i] = v;
        }
    }
}

void AutomationEngine::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    // Pick up new automation if the UI has prepared some (never blocks).
    {
        const juce::SpinLock::ScopedTryLockType tl (snapshotLock);
        if (tl.isLocked() && pending != nullptr && retired == nullptr)
        {
            retired = std::move (current);
            current = std::move (pending);
            lastSent.assign (juce::jmin (current->lanes.size(), lastSent.capacity()), -1.0f);
            resendAll = true;
        }
    }

    const int numSamples = buffer.getNumSamples();

    if (plugin == nullptr)
    {
        buffer.clear();
        midi.clear();
        return;
    }

    const juce::ScopedLock pluginLock (plugin->getCallbackLock());

    if (plugin->isSuspended())
    {
        buffer.clear();
        midi.clear();
        return;
    }

    const double rate = currentRate.load();
    const int interval = liveInterval.load();
    const int pluginOuts = plugin->getTotalNumOutputChannels();
    const int pluginChannels = pluginBuffer.getNumChannels();

    for (int start = 0; start < numSamples;)
    {
        // How much to process before the parameters are updated again.
        int len = numSamples - start;
        if (interval > 0) len = juce::jmin (len, interval);
        len = juce::jmin (len, preparedBlockSize);

        auto pos = position.load();

        if (! patchMode.load() && ! writesPaused.load())
            applyAutomation ((double) pos / rate);

        // MIDI (the held note) for just this piece.
        chunkMidi.clear();
        for (const auto meta : midi)
            if (meta.samplePosition >= start && meta.samplePosition < start + len)
                chunkMidi.addEvent (meta.data, meta.numBytes, meta.samplePosition - start);

        juce::AudioBuffer<float> piece (pluginBuffer.getArrayOfWritePointers(), pluginChannels, 0, len);
        piece.clear();
        plugin->processBlock (piece, chunkMidi);

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        {
            if (ch < pluginOuts)
                buffer.copyFrom (ch, start, piece, ch, 0, len);
            else if (pluginOuts == 1)
                buffer.copyFrom (ch, start, piece, 0, 0, len);
            else
                buffer.clear (ch, start, len);
        }

        // Advance the transport.
        if (playing.load())
        {
            auto next = pos + len;
            const double nextSeconds = (double) next / rate;

            if (loopOn.load() && nextSeconds >= loopEnd.load())
            {
                next = (juce::int64) (loopStart.load() * rate);
            }
            else if (! loopOn.load() && nextSeconds >= projectLength.load())
            {
                next = (juce::int64) (projectLength.load() * rate);
                playing = false;
            }

            // If the UI moved the playhead meanwhile, its position wins.
            position.compare_exchange_strong (pos, next);
        }

        start += len;
    }

    // Output stage: gain, then the limiter (if on), then the meter.
    const float gain = outputGain.load();
    if (std::abs (gain - 1.0f) > 1.0e-6f)
        buffer.applyGain (0, numSamples, gain);

    const bool limiterOn = limiterEnabled.load();
    if (limiterOn != limiterWasOn)
    {
        limiter.reset();
        limiterWasOn = limiterOn;
    }
    if (limiterOn)
    {
        limiter.process (buffer, 0, numSamples, limiterCeiling.load());
        const float g = limiter.takeGainReduction();
        if (g < limiterGain.load()) limiterGain = g;
    }

    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));
    if (peak > peakLevel.load()) peakLevel = peak;

    midi.clear();
}
