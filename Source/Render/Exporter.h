// Exporter.h — writes lanes out for other software:
//   - a standard MIDI file: one controller (CC) per automation lane, 7- or 14-bit,
//     plus the notes of the note lanes;
//   - REAPER automation items (.ReaperAutoItem), one file per lane;
//   - a REAPER project made from a template project in which you saved the plugin
//     once: Trajet adds one envelope per lane and the notes as a MIDI item.

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"

struct ExportSettings
{
    enum class Format { midi, reaperItems, reaperProject };
    enum class Lanes { all, group, active };

    Format format = Format::midi;
    Lanes lanes = Lanes::all;
    double step = 0.01;            // seconds between samples of curves and generators
    bool exactCurves = false;      // REAPER: sample every curve (true) or keep linear/hold/smooth points as points
    int firstCC = 20;              // MIDI: the first lane gets this controller, the next one the next...
    bool fourteenBit = false;      // MIDI: CC n (coarse) + CC n+32 (fine)
    int midiChannel = 1;
    double reaperTempo = 120.0;    // automation items are measured in beats at this tempo
    juce::File output, reaperTemplate;

    void load (juce::PropertiesFile&);
    void save (juce::PropertiesFile&) const;
};

namespace Exporter
{
    /** Writes the chosen lanes; returns "" or an error. `summary` tells what was written. */
    juce::String run (const ExportSettings&, Project&, AutomationController&, const juce::ValueTree& activeLane,
                      juce::String& summary);

    /** One lane as REAPER envelope points: absolute seconds, value 0..1, REAPER shape (0 linear, 1 square, 2 slow start/end). */
    struct EnvPoint { double time; float value; int shape; };
    std::vector<EnvPoint> envelopePoints (const juce::ValueTree& lane, float baseValue, double length, double step, bool exact);
}
