// Identifiers.h — the names used in Trajet's data model (a juce::ValueTree).
//
// A ValueTree is a tree of named nodes with properties, a bit like a
// nested Max [dict]. Every change to it can be undone, and the whole tree is
// saved to disk as JSON.
//
// Model layout (grows step by step):
//
//   PROJECT  (formatVersion, lengthSeconds, outputGain dB, limiter, limiterCeiling dB)
//     PLUGIN   (description = XML text, state = base64 text)
//     HELDNOTE (on, note, velocity)
//     GROUPS
//       GROUP  (groupId, name, colour, folded)     -- a group's lanes always sit together
//     LANES
//       LANE   (laneId, paramIndex, paramName, name, colour, height, mute, solo, group = groupId or 0)
//         ITEM (start, length, timebase)          -- start/length always in seconds
//              musical items also: beatsPerBar, beatUnit, gridBeats, lengthBeats
//              generator items also: generator (lfo/random/formula), genShape, genRate,
//              genMin, genMax, genPhase, genSeed, formula -- used instead of the points;
//              pieces of a split generator item also: genOffset, genOffsetSeconds, genSpan
//           TEMPO (beat, bpm, ramp)               -- musical items: the tempo map
//           POINT (time, value, shape, tension)   -- time from the item start, in seconds
//                                                    (linear items) or beats (musical items)
//           NOTE  (time, length, pitch, velocity) -- in note lanes (LANE kind = "notes");
//                                                    time and length in the item's units

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
    TRAJET_ID (NOTE)
    TRAJET_ID (TEMPO)
    TRAJET_ID (GROUPS)
    TRAJET_ID (GROUP)

    // GROUP properties (also name, colour)
    TRAJET_ID (groupId)
    TRAJET_ID (folded)
    TRAJET_ID (group)

    // Note lanes
    TRAJET_ID (kind)
    TRAJET_ID (pitch)

    // PROJECT properties
    TRAJET_ID (formatVersion)
    TRAJET_ID (lengthSeconds)
    TRAJET_ID (outputGain)
    TRAJET_ID (limiter)
    TRAJET_ID (limiterCeiling)

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

    // Generator items
    TRAJET_ID (generator)
    TRAJET_ID (genShape)
    TRAJET_ID (genRate)
    TRAJET_ID (genMin)
    TRAJET_ID (genMax)
    TRAJET_ID (genPhase)
    TRAJET_ID (genSeed)
    TRAJET_ID (genOffset)          // pieces of a split generator item: where they start in the original,
    TRAJET_ID (genOffsetSeconds)   // the same in seconds,
    TRAJET_ID (genSpan)            // and the original's length (item units)
    TRAJET_ID (formula)

    // POINT properties
    TRAJET_ID (time)
    TRAJET_ID (value)
    TRAJET_ID (shape)
    TRAJET_ID (tension)

    #undef TRAJET_ID
}
