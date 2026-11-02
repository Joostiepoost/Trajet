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
//         ITEM (start, length, timebase)          -- seconds; musical items in step 6
//           POINT (time, value, shape, tension)   -- time relative to the item start

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

    // POINT properties
    TRAJET_ID (time)
    TRAJET_ID (value)
    TRAJET_ID (shape)
    TRAJET_ID (tension)

    #undef TRAJET_ID
}
