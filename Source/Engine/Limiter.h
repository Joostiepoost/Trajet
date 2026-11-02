// Limiter.h — a look-ahead peak limiter for the end of the signal chain.
//
// The sound is delayed by a few milliseconds so the limiter can see every peak
// coming and turn the level down smoothly *before* it arrives: no sample ever
// exceeds the ceiling, and there is no clipping distortion.
//
// How: for each sample, the gain needed to keep it under the ceiling is computed;
// the lowest such gain over the look-ahead window is held, then smoothed with a
// moving average of the same length (so the gain ramps down in time), and lets go
// slowly (release). All memory is allocated in prepare(), none while processing.

#pragma once

#include <JuceHeader.h>

class Limiter
{
public:
    void prepare (double sampleRate, int numChannels)
    {
        lookahead = juce::jmax (2, (int) std::round (sampleRate * 0.005));     // 5 ms
        releaseCoef = (float) (1.0 - std::exp (-1.0 / (sampleRate * 0.12)));   // ~120 ms
        channels = juce::jmax (1, numChannels);

        delay.setSize (channels, lookahead, false, true, false);
        minValues.assign ((size_t) lookahead, 1.0f);
        minIndices.assign ((size_t) lookahead, 0);
        boxRing.assign ((size_t) lookahead, 1.0f);
        reset();
    }

    void reset()
    {
        delay.clear();
        dqHead = dqSize = 0;
        std::fill (boxRing.begin(), boxRing.end(), 1.0f);
        boxSum = (double) lookahead;
        writePos = 0;
        counter = 0;
        gain = 1.0f;
        reduction = 1.0f;
    }

    /** Delay the limiter adds, in samples. */
    int getLatency() const noexcept         { return lookahead - 1; }

    /** Lowest gain since the last call (1 = no limiting). */
    float takeGainReduction() noexcept      { const float r = reduction; reduction = 1.0f; return r; }

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples, float ceiling)
    {
        const int numCh = juce::jmin (channels, buffer.getNumChannels());
        if (numCh == 0 || lookahead < 2) return;

        auto* const* data = buffer.getArrayOfWritePointers();

        // Rebuild the running sum once per block, so rounding errors cannot pile up.
        boxSum = 0.0;
        for (auto v : boxRing) boxSum += v;

        for (int i = startSample; i < startSample + numSamples; ++i)
        {
            // 1. The gain this sample needs.
            float peak = 0.0f;
            for (int ch = 0; ch < numCh; ++ch)
                peak = juce::jmax (peak, std::abs (data[ch][i]));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;

            // 2. Lowest needed gain over the last `lookahead` samples (sliding minimum).
            const auto now = counter++;
            while (dqSize > 0 && minIndices[(size_t) dqHead] <= now - lookahead)
            {
                dqHead = (dqHead + 1) % lookahead;
                --dqSize;
            }
            while (dqSize > 0 && minValues[(size_t) back()] >= needed)
                --dqSize;
            const int slot = (dqHead + dqSize) % lookahead;
            minValues[(size_t) slot] = needed;
            minIndices[(size_t) slot] = now;
            ++dqSize;
            const float held = minValues[(size_t) dqHead];

            // 3. Moving average of the held gain: a smooth ramp down that is complete in time.
            boxSum += (double) held - (double) boxRing[(size_t) writePos];
            boxRing[(size_t) writePos] = held;
            const float smoothed = (float) (boxSum / (double) lookahead);

            // 4. Slow release; never above what the ramp asks for.
            gain = smoothed < gain ? smoothed : gain + (smoothed - gain) * releaseCoef;
            gain = juce::jmin (gain, smoothed);
            reduction = juce::jmin (reduction, gain);

            // 5. Delay the sound by lookahead - 1 samples and apply the gain.
            const int readPos = (writePos + 1) % lookahead;
            for (int ch = 0; ch < numCh; ++ch)
            {
                auto* d = delay.getWritePointer (ch);
                d[writePos] = data[ch][i];
                data[ch][i] = juce::jlimit (-ceiling, ceiling, d[readPos] * gain);
            }
            writePos = readPos;
        }
    }

private:
    int back() const noexcept   { return (dqHead + dqSize - 1) % lookahead; }

    int lookahead = 240, channels = 2;
    float releaseCoef = 0.001f, gain = 1.0f, reduction = 1.0f;

    juce::AudioBuffer<float> delay;
    std::vector<float> minValues;       // ring-buffer deque for the sliding minimum
    std::vector<juce::int64> minIndices;
    int dqHead = 0, dqSize = 0;

    std::vector<float> boxRing;
    double boxSum = 0.0;
    int writePos = 0;
    juce::int64 counter = 0;
};
