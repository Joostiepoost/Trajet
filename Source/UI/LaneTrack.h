// LaneTrack.h — the timeline part of one lane: its items, curves and points,
// and all mouse gestures for editing them.
//
// Items                        Points (inside an item, below its title strip)
//   double-click empty space     Option-click            add a point
//     = new item                 Option-drag             freehand draw
//   drag title strip = move      Option-Shift-drag       straight ramp
//   Option-drag strip = copy     drag a point            move (Shift: one axis only,
//   drag an edge = trim                                    Cmd: fine, no snap)
//   right-click = item menu      Cmd-click               add to / remove from selection
//                                Control-drag a segment  bend the curve
//                                double-click a point    type time and value
//                                right-click a point     segment shape

#pragma once

#include <JuceHeader.h>
#include "../Model/Project.h"
#include "../PluginHost.h"
#include "../Engine/AutomationController.h"
#include "EditorState.h"
#include "../Model/ItemTime.h"

class LaneTrack : public juce::Component
{
public:
    /** Tools for the item editor; in the main window gestures use modifier keys instead. */
    enum class Tool { select, pencil, line, bend };

    struct Context
    {
        Context (Project& p, PluginHost& h, AutomationController& c, TimeView& tv, SnapSettings& s, Selection& sel)
            : project (p), host (h), controller (c), timeView (tv), snap (s), selection (sel) {}

        Project& project;
        PluginHost& host;
        AutomationController& controller;
        TimeView& timeView;
        SnapSettings& snap;
        Selection& selection;
        Tool* tool = nullptr;

        std::function<void (const juce::String&)> showStatus;
        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&, juce::Component&)> handleWheel;
        /** The lane under a screen position (for dragging items between lanes). */
        std::function<juce::ValueTree (juce::Point<int>)> laneAt;
        /** Rubber-band selection across lanes: screen rectangle, add to selection, gesture finished. */
        std::function<void (juce::Rectangle<int>, bool, bool)> rubberBand;
        std::function<void (juce::ValueTree item)> openItemEditor;
    };

    LaneTrack (Context&, juce::ValueTree lane);

    void paint (juce::Graphics&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    static constexpr int stripHeight = 14;

    /** Points of this lane whose dots lie inside a rectangle (local coordinates). */
    void collectPointsIn (juce::Rectangle<float> localArea, juce::Array<juce::ValueTree>& result) const;

    juce::ValueTree getLane() const     { return lane; }

    float valueToY (float v) const;
    float yToValue (float y) const;

private:
    enum class Mode { none, moveItem, copyItem, trimLeft, trimRight, movePoints, draw, ramp, bend, rubberBand, scale };
    enum class Handle { none, left, right, top, bottom };
    enum class Zone { none, strip, leftEdge, rightEdge, curve };

    juce::ValueTree itemAt (float x) const;
    juce::Rectangle<float> itemBounds (const juce::ValueTree& item) const;
    Zone zoneFor (const juce::ValueTree& item, juce::Point<float> pos) const;
    juce::ValueTree pointAt (const juce::ValueTree& item, juce::Point<float> pos) const;

    juce::Rectangle<float> curveArea() const;
    double relativeTime (const juce::ValueTree& item, float x, bool bypassSnap) const;
    double snapLocal (const ItemTime&, double absSeconds, bool bypassSnap) const;
    juce::AudioProcessorParameter* getParameter() const;
    juce::String valueText (float v) const;

    void beginPointDrag (const juce::MouseEvent&, const juce::ValueTree& item, const juce::ValueTree& hit);
    void dragPoints (const juce::MouseEvent&);
    void dragItem (const juce::MouseEvent&);
    void drawFreehand (const juce::MouseEvent&);
    void finishFreehand();
    void finishRamp();
    juce::Array<juce::ValueTree> selectedPointsHere() const;
    juce::Rectangle<float> selectionBox() const;
    Handle handleAt (juce::Point<float>) const;
    void dragScale (const juce::MouseEvent&);
    bool startBend (const juce::ValueTree& item, float x);
    void showPointMenu (juce::ValueTree point);
    void showItemMenu (juce::ValueTree item);
    void editPointNumerically (juce::ValueTree point);

    Context& ctx;
    juce::ValueTree lane;

    // Gesture state
    Mode mode = Mode::none;
    juce::ValueTree dragItemTree, dragAnchor, rampEnd, bendPoint;
    double origStart = 0.0, origLength = 0.0, origTension = 0.0;
    juce::Range<double> freeRange;
    struct Orig { juce::ValueTree tree; double time; float value; };
    std::vector<Orig> origPoints;
    int constrainAxis = 0;                  // 0 = undecided, 1 = time only, 2 = value only
    std::vector<juce::ValueTree> drawnPoints;
    float lastDrawX = 0.0f;
    double lastDrawTime = 0.0;
    bool bendRising = true, copyCreated = false;
    Handle scaleHandle = Handle::none;
    double boxT0 = 0.0, boxT1 = 0.0;          // absolute times of the selection box
    float boxV0 = 0.0f, boxV1 = 0.0f;         // its value range
    juce::Rectangle<float> rubberRect;
    std::vector<Automation::TempoPoint> origTempo;
    double origLengthBeats = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneTrack)
};
