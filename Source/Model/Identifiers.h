// Identifiers.h — the names used in Trajet's data model (a juce::ValueTree).
//
// A ValueTree is a tree of named nodes with properties, a bit like a
// nested Max [dict]. Every change to it can be undone, and the whole tree is
// saved to disk as JSON.
//
// Model layout (grows step by step):
//
//   PROJECT  (formatVersion, lengthSeconds)
//     PLUGIN   (description = XML text, state = base64 text)
//     HELDNOTE (on, note, velocity)
//     LANES
//       LANE   (laneId, paramIndex, paramName, name, colour, height, mute, solo)
//         ITEM (start, length, timebase)          -- start/length always in seconds
//              musical items also: beatsPerBar, beatUnit, gridBeats, lengthBeats
//           TEMPO (beat, bpm, ramp)               -- musical items: the tempo map
//           POINT (time, value, shape, tension)   -- time from the item start, in seconds
//                                                    (linear items) or beats (musical items)

#pragma once

#include <JuceHeader.h>

namespace IDs
{
    #define TRAJET_ID(name) inline const juce::Identifier name { #name };

    // Node types
    TRAJET_ID (PROJECT)
    TRAJET_ID (PLUGIN)
    TRAJET_ID (HELDNOTE)
    TRAJET_ID (LANES)
    TRAJET_ID (LANE)
    TRAJET_ID (ITEM)
    TRAJET_ID (POINT)
    TRAJET_ID (TEMPO)

    // PROJECT properties
    TRAJET_ID (formatVersion)
    TRAJET_ID (lengthSeconds)

    // PLUGIN properties
    TRAJET_ID (description)
    TRAJET_ID (state)

    // HELDNOTE properties
    TRAJET_ID (on)
    TRAJET_ID (note)
    TRAJET_ID (velocity)

    // LANE properties
    TRAJET_ID (laneId)
    TRAJET_ID (paramIndex)
    TRAJET_ID (paramName)
    TRAJET_ID (name)
    TRAJET_ID (colour)
    TRAJET_ID (height)
    TRAJET_ID (mute)
    TRAJET_ID (solo)

    // ITEM properties
    TRAJET_ID (start)
    TRAJET_ID (length)
    TRAJET_ID (timebase)

    // Musical items and their tempo points
    TRAJET_ID (beatsPerBar)
    TRAJET_ID (beatUnit)
    TRAJET_ID (gridBeats)
    TRAJET_ID (lengthBeats)
    TRAJET_ID (beat)
    TRAJET_ID (bpm)
    TRAJET_ID (ramp)

    // POINT properties
    TRAJET_ID (time)
    TRAJET_ID (value)
    TRAJET_ID (shape)
    TRAJET_ID (tension)

    #undef TRAJET_ID
}
