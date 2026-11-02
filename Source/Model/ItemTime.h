// ItemTime.h — converts between an item's own time units and the project's seconds.
//
// Linear items count in seconds from their start; musical items count in beats,
// through their tempo map (with ramps). Everything that places or reads points
// goes through this, so linear and musical items behave the same everywhere.

#pragma once

#include <JuceHeader.h>
#include "Identifiers.h"
#include "../Engine/Automation.h"

class ItemTime
{
public:
    explicit ItemTime (const juce::ValueTree& item)
        : start (item[IDs::start]),
          lengthSeconds (item[IDs::length]),
          musical (item[IDs::timebase].toString() == "musical")
    {
        if (musical)
        {
            beatsPerBar = juce::jmax (1, (int) item.getProperty (IDs::beatsPerBar, 4));
            beatUnit    = juce::jmax (1, (int) item.getProperty (IDs::beatUnit, 4));
            gridBeats   = juce::jmax (1.0 / 64.0, (double) item.getProperty (IDs::gridBeats, 0.25));
            lengthBeats = item.getProperty (IDs::lengthBeats, 0.0);
            tempo       = readTempo (item);
        }
    }

    static Automation::TempoMap readTempo (const juce::ValueTree& item)
    {
        Automation::TempoMap map;
        for (const auto& t : item)
            if (t.hasType (IDs::TEMPO))
                map.points.push_back ({ (double) t[IDs::beat], juce::jlimit (10.0, 999.0, (double) t[IDs::bpm]), (bool) t[IDs::ramp] });

        std::sort (map.points.begin(), map.points.end(), [] (const auto& a, const auto& b) { return a.beat < b.beat; });

        if (map.points.empty())
            map.points.push_back ({ 0.0, 120.0, false });
        else if (map.points.front().beat > 0.0)
            map.points.insert (map.points.begin(), { 0.0, map.points.front().bpm, false });

        return map;
    }

    bool isMusical() const noexcept         { return musical; }
    double getStart() const noexcept        { return start; }

    /** Seconds from the item start for a position in item units, and back. */
    double toSeconds (double local) const   { return musical ? tempo.secondsAt (local) : local; }
    double toLocal (double relSeconds) const { return musical ? tempo.beatAt (relSeconds) : relSeconds; }

    /** Absolute project seconds for a position in item units, and back. */
    double absolute (double local) const    { return start + toSeconds (local); }
    double localAt (double absSeconds) const { return toLocal (absSeconds - start); }

    /** Item length in its own units (seconds or beats). */
    double localLength() const              { return musical ? lengthBeats : lengthSeconds; }

    /** "bar|beat" for a beat position, bars and beats counted from 1. */
    juce::String barBeat (double beats) const
    {
        const int bar = (int) std::floor (beats / beatsPerBar);
        const double beatInBar = beats - bar * beatsPerBar;
        return juce::String (bar + 1) + "|" + juce::String (beatInBar + 1.0, 2);
    }

    /** Accepts "bar|beat" (from 1) and returns a beat position, or -1 if the text isn't one. */
    double parseBarBeat (const juce::String& text) const
    {
        if (! text.containsChar ('|'))
            return -1.0;
        const int bar = text.upToFirstOccurrenceOf ("|", false, false).trim().getIntValue();
        const double beat = text.fromFirstOccurrenceOf ("|", false, false).trim().getDoubleValue();
        return juce::jmax (0.0, (bar - 1) * beatsPerBar + (beat - 1.0));
    }

    /** The item's generator (type none for ordinary point items). Formula errors go to error. */
    static Automation::Generator readGenerator (const juce::ValueTree& item, juce::String* error = nullptr)
    {
        Automation::Generator g;
        const auto type = item[IDs::generator].toString();
        if (type == "lfo")          g.type = Automation::GeneratorType::lfo;
        else if (type == "random")  g.type = Automation::GeneratorType::random;
        else if (type == "formula") g.type = Automation::GeneratorType::formula;
        else                        return g;

        g.shape    = juce::jlimit (0, 4, (int) item.getProperty (IDs::genShape, 0));
        g.rate     = juce::jlimit (0.001, 1000.0, (double) item.getProperty (IDs::genRate, 1.0));
        g.minValue = (float) juce::jlimit (0.0, 1.0, (double) item.getProperty (IDs::genMin, 0.0));
        g.maxValue = (float) juce::jlimit (0.0, 1.0, (double) item.getProperty (IDs::genMax, 1.0));
        g.phase    = (double) item.getProperty (IDs::genPhase, 0.0);
        g.seed     = (int) item.getProperty (IDs::genSeed, 1);

        if (g.type == Automation::GeneratorType::formula)
        {
            juce::String err;
            g.formula = Formula::parseCached (item[IDs::formula].toString(), err);
            if (error != nullptr) *error = err;
        }
        return g;
    }

    static bool isGenerator (const juce::ValueTree& item)
    {
        const auto type = item[IDs::generator].toString();
        return type == "lfo" || type == "random" || type == "formula";
    }

    int beatsPerBar = 4, beatUnit = 4;
    double gridBeats = 0.25, lengthBeats = 0.0;
    Automation::TempoMap tempo;

private:
    double start = 0.0, lengthSeconds = 0.0;
    bool musical = false;
};
