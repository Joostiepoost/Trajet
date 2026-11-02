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
//     LANES    -- lanes, groups, items and points arrive in steps 3-6

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

    #undef TRAJET_ID
}
