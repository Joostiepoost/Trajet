// EditorState.h — view and editing state shared by the main window's parts:
// the visible time range (zoom/scroll), snap settings and the selection.
// None of this is part of the score itself.

#pragma once

#include <JuceHeader.h>
#include "../Model/Identifiers.h"

//==============================================================================
/** Which stretch of time is visible, and how wide a second is on screen. */
class TimeView : public juce::ChangeBroadcaster
{
public:
    double getPixelsPerSecond() const noexcept  { return pixelsPerSecond; }
    double getOffset() const noexcept           { return offset; }        // seconds at the left edge
    double getLength() const noexcept           { return length; }        // project length

    float toX (double seconds) const noexcept   { return (float) ((seconds - offset) * pixelsPerSecond); }
    double toTime (float x) const noexcept      { return offset + (double) x / pixelsPerSecond; }
    double visibleSeconds() const noexcept      { return (double) viewWidth / pixelsPerSecond; }

    void setViewWidth (int width)
    {
        viewWidth = juce::jmax (50, width);
        if (! hasZoomed) fitAll(); else clampAndNotify();
    }

    void setLength (double seconds)
    {
        length = juce::jmax (1.0, seconds);
        if (! hasZoomed) fitAll(); else clampAndNotify();
    }

    void setOffset (double seconds)             { offset = seconds; clampAndNotify(); }
    void scrollBy (double seconds)              { setOffset (offset + seconds); }

    /** Zoom in (factor > 1) or out, keeping the time under x in place. */
    void zoomAround (double factor, float x)
    {
        const double t = toTime (x);
        pixelsPerSecond = juce::jlimit (minPixelsPerSecond(), 4000.0, pixelsPerSecond * factor);
        offset = t - (double) x / pixelsPerSecond;
        hasZoomed = true;
        clampAndNotify();
    }

    /** Show exactly the stretch from t0 to t1. */
    void fitRange (double t0, double t1)
    {
        pixelsPerSecond = (double) viewWidth / juce::jmax (0.001, t1 - t0);
        offset = t0;
        hasZoomed = true;
        clampAndNotify();
    }

    void fitAll()
    {
        pixelsPerSecond = (double) viewWidth / length;
        offset = 0.0;
        hasZoomed = false;
        sendSynchronousChangeMessage();
    }

    /** Scroll so that t is visible (used to follow the playhead). */
    void keepVisible (double t)
    {
        if (t < offset || t > offset + visibleSeconds() * 0.98)
            setOffset (t - visibleSeconds() * 0.05);
    }

private:
    double minPixelsPerSecond() const   { return (double) viewWidth / (length * 1.05); }

    void clampAndNotify()
    {
        pixelsPerSecond = juce::jmax (pixelsPerSecond, minPixelsPerSecond());
        offset = juce::jlimit (0.0, juce::jmax (0.0, length - visibleSeconds()), offset);
        sendSynchronousChangeMessage();
    }

    double pixelsPerSecond = 20.0, offset = 0.0, length = 60.0;
    int viewWidth = 800;
    bool hasZoomed = false;
};

//==============================================================================
/** Snap-to-grid on/off and the grid size (seconds). */
class SnapSettings : public juce::ChangeBroadcaster
{
public:
    bool isOn() const noexcept                  { return on; }
    double getGrid() const noexcept             { return grid; }
    void setOn (bool shouldBeOn)                { on = shouldBeOn; sendChangeMessage(); }
    void setGrid (double seconds)               { grid = juce::jmax (0.001, seconds); sendChangeMessage(); }

    /** Snapped time, unless snapping is off or bypassed (Cmd held while dragging). */
    double snap (double seconds, bool bypass = false) const
    {
        if (! on || bypass) return std::round (seconds * 1000.0) / 1000.0;   // always on the ms grid
        return std::round (seconds / grid) * grid;
    }

private:
    bool on = true;
    double grid = 0.1;
};

//==============================================================================
/** Selected items and points. */
class Selection : public juce::ChangeBroadcaster
{
public:
    const juce::Array<juce::ValueTree>& getItems() const noexcept  { return items; }
    const juce::Array<juce::ValueTree>& getPoints() const noexcept { return points; }

    bool isSelected (const juce::ValueTree& t) const   { return items.contains (t) || points.contains (t); }

    /** The lane you last clicked in: where Paste goes. */
    juce::ValueTree getActiveLane() const               { return activeLane; }
    void setActiveLane (const juce::ValueTree& lane)
    {
        if (activeLane != lane) { activeLane = lane; sendChangeMessage(); }
    }

    void selectItem (const juce::ValueTree& item, bool addToSelection = false)
    {
        if (! addToSelection) { items.clearQuick(); points.clearQuick(); }
        items.addIfNotAlreadyThere (item);
        activeLane = item.getParent();
        sendChangeMessage();
    }

    void selectPoint (const juce::ValueTree& point, bool addToSelection = false)
    {
        if (! addToSelection) { points.clearQuick(); items.clearQuick(); }
        points.addIfNotAlreadyThere (point);
        items.addIfNotAlreadyThere (point.getParent());
        activeLane = point.getParent().getParent();
        sendChangeMessage();
    }

    void deselect (const juce::ValueTree& t)
    {
        items.removeAllInstancesOf (t);
        points.removeAllInstancesOf (t);
        sendChangeMessage();
    }

    void clear()
    {
        if (items.isEmpty() && points.isEmpty()) return;
        items.clearQuick();
        points.clearQuick();
        sendChangeMessage();
    }

    /** Drops everything in lanes that are not shown (e.g. in a folded group). */
    void keepOnlyLanes (const std::function<bool (const juce::ValueTree& lane)>& shown)
    {
        const int before = items.size() + points.size();
        items.removeIf ([&] (const juce::ValueTree& t) { return ! shown (t.getParent()); });
        points.removeIf ([&] (const juce::ValueTree& t) { return ! shown (t.getParent().getParent()); });
        const bool laneGone = activeLane.isValid() && ! shown (activeLane);
        if (laneGone) activeLane = {};
        if (laneGone || items.size() + points.size() != before)
            sendChangeMessage();
    }

    /** Removes anything that is no longer part of the project (after undo, delete...). */
    void prune()
    {
        auto gone = [] (const juce::ValueTree& t) { return ! t.getParent().isValid() || ! t.getParent().getParent().isValid(); };
        const int before = items.size() + points.size();
        items.removeIf (gone);
        points.removeIf (gone);
        if (activeLane.isValid() && ! activeLane.getParent().isValid())
            activeLane = {};
        if (items.size() + points.size() != before)
            sendChangeMessage();
    }

private:
    juce::Array<juce::ValueTree> items, points;
    juce::ValueTree activeLane;
};

//==============================================================================
namespace TimeFormat
{
    inline juce::String format (double seconds, int decimals = 3)
    {
        const bool negative = seconds < 0.0;
        seconds = std::abs (seconds);
        const int minutes = (int) (seconds / 60.0);
        const double rest = seconds - minutes * 60.0;
        return juce::String (negative ? "-" : "") + juce::String (minutes) + ":"
               + juce::String (rest, decimals).paddedLeft ('0', decimals > 0 ? 3 + decimals : 2);
    }

    /** Accepts "12.5", "0:12.500" or "1:02.25". */
    inline double parse (const juce::String& text)
    {
        auto t = text.trim();
        if (t.containsChar (':'))
            return t.upToFirstOccurrenceOf (":", false, false).getDoubleValue() * 60.0
                   + t.fromFirstOccurrenceOf (":", false, false).getDoubleValue();
        return t.getDoubleValue();
    }
}
