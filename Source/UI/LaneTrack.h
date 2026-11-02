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

class LaneTrack : public juce::Component
{
public:
    struct Context
    {
        Project& project;
        PluginHost& host;
        AutomationController& controller;
        TimeView& timeView;
        SnapSettings& snap;
        Selection& selection;
        std::function<void (const juce::String&)> showStatus;
        std::function<void (const juce::MouseEvent&, const juce::MouseWheelDetails&, juce::Component&)> handleWheel;
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

private:
    enum class Mode { none, moveItem, copyItem, trimLeft, trimRight, movePoints, draw, ramp, bend };
    enum class Zone { none, strip, leftEdge, rightEdge, curve };

    juce::ValueTree itemAt (float x) const;
    juce::Rectangle<float> itemBounds (const juce::ValueTree& item) const;
    Zone zoneFor (const juce::ValueTree& item, juce::Point<float> pos) const;
    juce::ValueTree pointAt (const juce::ValueTree& item, juce::Point<float> pos) const;

    juce::Rectangle<float> curveArea() const;
    float valueToY (float v) const;
    float yToValue (float y) const;
    double relativeTime (const juce::ValueTree& item, float x, bool bypassSnap) const;
    juce::AudioProcessorParameter* getParameter() const;
    juce::String valueText (float v) const;

    void beginPointDrag (const juce::MouseEvent&, const juce::ValueTree& item, const juce::ValueTree& hit);
    void dragPoints (const juce::MouseEvent&);
    void dragItem (const juce::MouseEvent&);
    void drawFreehand (const juce::MouseEvent&);
    void finishFreehand();
    void finishRamp();
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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LaneTrack)
};
