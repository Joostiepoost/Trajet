// PointOps.h — operations on a set of selected points (all undoable):
// nudge, smooth, thin, reverse, invert, and copy/paste of points.
// Used by both the main window and the item editor.

#pragma once

#include <JuceHeader.h>
#include "Identifiers.h"

namespace PointOps
{
    /** Move points by dt seconds and dv (0..1), each kept inside its item. */
    void nudge (const juce::Array<juce::ValueTree>& points, double dt, float dv, juce::UndoManager&);

    /** Average each inner point's value with its neighbours (per item, in time order). */
    void smooth (const juce::Array<juce::ValueTree>& points, juce::UndoManager&);

    /** Remove points that hardly change the curve. Returns how many were removed. */
    int thin (const juce::Array<juce::ValueTree>& points, juce::UndoManager&, float tolerance = 0.01f);

    /** Mirror the points in time within their span (per item). */
    void reverse (const juce::Array<juce::ValueTree>& points, juce::UndoManager&);

    /** Mirror the values within their range (lowest becomes highest). */
    void invert (const juce::Array<juce::ValueTree>& points, juce::UndoManager&);

    /** Copy points (times kept relative to the earliest one). */
    void copy (const juce::Array<juce::ValueTree>& points);
    bool hasCopiedPoints();

    /** Paste copied points into an item, the earliest at absolute time `at`.
        Points that fall outside the item are left out. Returns the new points. */
    juce::Array<juce::ValueTree> paste (juce::ValueTree item, double at, juce::UndoManager&);

    /** Group points by their item. */
    std::vector<std::pair<juce::ValueTree, juce::Array<juce::ValueTree>>> byItem (const juce::Array<juce::ValueTree>& points);
}
