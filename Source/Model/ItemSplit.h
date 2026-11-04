// ItemSplit.h — cutting an item into pieces that together play exactly as before.
//
// The same file is used in Trajet and Effet.
//
// What is kept across a cut:
//   - points: the curve is cut where it is; a point is added at each side of the
//     cut with the value there. A curved segment keeps its exact bend on both
//     sides (an exponential bend is self-similar); a smooth (S-shaped) segment is
//     traced with a few extra points; a held value stays held.
//   - generators: each piece remembers where in the original it starts (genOffset,
//     genOffsetSeconds) and the original length (genSpan), so an LFO, a random
//     curve or a formula carries on exactly instead of starting over.
//   - musical items: the tempo map is cut too (a ramp is split at its tempo at the
//     cut), so bars, beats and the length in beats stay right on both sides.
//   - notes (Trajet's note lanes): a note across the cut becomes two notes.
//
// Everything goes through the UndoManager: one beginNewTransaction around a
// split (or several) makes it one undo step.

#pragma once

#include <JuceHeader.h>

namespace ItemSplit
{
    /** Shortest piece a cut may leave, in seconds. */
    constexpr double minPiece = 0.01;

    /** Whether a cut at this absolute time would leave two pieces of at least minPiece. */
    bool canSplit (const juce::ValueTree& item, double absoluteSeconds);

    /** Cuts the item at an absolute time. The item itself becomes the left piece;
        the right piece is added to the same lane and returned (invalid if the cut
        is too close to an edge, then nothing changes). */
    juce::ValueTree split (juce::ValueTree item, double absoluteSeconds, juce::UndoManager*);

    /** Cuts at several absolute times (any order; times outside the item or too
        close to an edge or to each other are skipped). Returns the pieces from
        left to right, the original item first. */
    juce::Array<juce::ValueTree> splitAt (juce::ValueTree item, std::vector<double> absoluteSeconds, juce::UndoManager*);

    /** Cuts into n pieces of equal length in the item's own units (equal beats for
        musical items, equal seconds otherwise). */
    juce::Array<juce::ValueTree> splitEqually (juce::ValueTree item, int pieces, juce::UndoManager*);
}
