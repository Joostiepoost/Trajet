#include "NoteEditor.h"

std::vector<NoteEditor::CopiedNote> NoteEditor::clipboard;

namespace
{
    constexpr int toolbarHeight = 40, rulerHeight = 22, keyboardWidth = 56, velocityHeight = 90,
                  statusHeight = 24, scrollWidth = 12;
    const juce::Colour background (0xff1e2126), panel (0xff262a31), accent (0xffe8a33d);

    bool isBlackKey (int pitch)
    {
        const int n = pitch % 12;
        return n == 1 || n == 3 || n == 6 || n == 8 || n == 10;
    }

    juce::String noteName (int pitch)   { return juce::MidiMessage::getMidiNoteName (pitch, true, true, 3); }

    const double musicalGrids[] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 1.0 / 6.0, 0.125, 0.0625 };
    const char* musicalGridNames[] = { "1 beat", "1/2 beat", "1/3 beat (triplets)", "1/4 beat", "1/6 beat", "1/8 beat", "1/16 beat" };
    const double linearGrids[] = { 0.01, 0.025, 0.05, 0.1, 0.125, 0.25, 0.5, 1.0 };
    const char* linearGridNames[] = { "10 ms", "25 ms", "50 ms", "100 ms", "125 ms", "250 ms", "500 ms", "1 s" };
}

//==============================================================================
class NoteEditor::Ruler : public juce::Component
{
public:
    explicit Ruler (NoteEditor& e) : ed (e) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel.brighter (0.05f));
        const auto it = ed.itemTime();
        g.setFont (juce::FontOptions (11.0f));

        if (it.isMusical())
        {
            const double pxPerBeat = ed.timeView.getPixelsPerSecond() * 60.0 / juce::jmax (10.0, it.tempo.firstBpm());
            const int barStep = pxPerBeat * it.beatsPerBar >= 50.0 ? 1 : (pxPerBeat * it.beatsPerBar * 4 >= 50.0 ? 4 : 16);
            for (int bar = 0; bar * it.beatsPerBar <= (int) std::ceil (it.lengthBeats); bar += barStep)
            {
                const float x = ed.localToX ((double) (bar * it.beatsPerBar));
                if (x > (float) getWidth()) break;
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawVerticalLine ((int) x, (float) getHeight() - 7.0f, (float) getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawText (juce::String (bar + 1), (int) x + 3, 0, 40, getHeight() - 4, juce::Justification::centredLeft);
            }
        }
        else
        {
            double step = 0.01;
            for (double s : { 0.01, 0.05, 0.1, 0.25, 0.5, 1.0, 2.0, 5.0, 10.0, 30.0, 60.0 })
                if (s * ed.timeView.getPixelsPerSecond() >= 60.0) { step = s; break; }
            const int decimals = step >= 1.0 ? 0 : (std::abs (step * 10.0 - std::round (step * 10.0)) < 1.0e-9 ? 1 : 2);
            for (double t = std::floor (ed.timeView.getOffset() / step) * step; ed.timeView.toX (t) < (float) getWidth(); t += step)
            {
                const float x = ed.timeView.toX (t);
                g.setColour (juce::Colours::white.withAlpha (0.4f));
                g.drawVerticalLine ((int) x, (float) getHeight() - 7.0f, (float) getHeight());
                g.setColour (juce::Colours::white.withAlpha (0.7f));
                g.drawText (TimeFormat::format (t, decimals), (int) x + 3, 0, 80, getHeight() - 4, juce::Justification::centredLeft);
            }
        }

        const float px = ed.timeView.toX (ed.host.getEngine().getPositionSeconds());
        g.setColour (accent);
        g.drawVerticalLine ((int) px, 0.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override  { locate (e); }
    void mouseDrag (const juce::MouseEvent& e) override  { locate (e); }

private:
    void locate (const juce::MouseEvent& e)
    {
        auto local = ed.snapLocal (ed.xToLocal ((float) e.x), e.mods.isCommandDown());
        ed.controller.locate (ed.itemTime().absolute (juce::jmax (0.0, local)));
    }

    NoteEditor& ed;
};

//==============================================================================
class NoteEditor::Keyboard : public juce::Component
{
public:
    explicit Keyboard (NoteEditor& e) : ed (e) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colours::white.withAlpha (0.85f));
        g.setFont (juce::FontOptions (10.0f));
        const int lowest = ed.yToPitch ((float) getHeight());
        for (int p = ed.topPitch; p >= lowest - 1 && p >= 0; --p)
        {
            const float y = ed.pitchToY (p);
            if (isBlackKey (p))
            {
                g.setColour (juce::Colour (0xff22252a));
                g.fillRect (0.0f, y, (float) getWidth() * 0.62f, ed.rowHeight);
            }
            else if (p % 12 == 0 || p % 12 == 5)
            {
                g.setColour (juce::Colours::black.withAlpha (0.25f));
                g.drawHorizontalLine ((int) (y + ed.rowHeight), 0.0f, (float) getWidth());
            }
            if (p % 12 == 0 && ed.rowHeight >= 8.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.7f));
                g.drawText (noteName (p), 0, (int) y, getWidth() - 4, (int) ed.rowHeight, juce::Justification::centredRight);
            }
            if (p == pressed)
            {
                g.setColour (accent.withAlpha (0.6f));
                g.fillRect (0.0f, y, (float) getWidth(), ed.rowHeight);
            }
        }
        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawVerticalLine (getWidth() - 1, 0.0f, (float) getHeight());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        pressed = ed.yToPitch ((float) e.y);
        ed.host.previewNote (pressed, ed.newVelocity);
        repaint();
    }
    void mouseUp (const juce::MouseEvent&) override  { pressed = -1; repaint(); }
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override  { ed.handleWheel (e, w); }

private:
    NoteEditor& ed;
    int pressed = -1;
};

//==============================================================================
class NoteEditor::Roll : public juce::Component
{
public:
    explicit Roll (NoteEditor& e) : ed (e) {}

    void paint (juce::Graphics& g) override
    {
        const float w = (float) getWidth(), h = (float) getHeight();
        g.fillAll (background);

        // Pitch rows.
        const int lowest = ed.yToPitch (h);
        for (int p = ed.topPitch; p >= lowest - 1 && p >= 0; --p)
        {
            const float y = ed.pitchToY (p);
            if (isBlackKey (p))
            {
                g.setColour (juce::Colours::black.withAlpha (0.22f));
                g.fillRect (0.0f, y, w, ed.rowHeight);
            }
            if (p % 12 == 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.12f));
                g.drawHorizontalLine ((int) (y + ed.rowHeight), 0.0f, w);
            }
        }

        // Time grid.
        const auto it = ed.itemTime();
        if (it.isMusical())
        {
            const double grid = it.gridBeats;
            const double pxPerGrid = ed.timeView.getPixelsPerSecond() * 60.0 / juce::jmax (10.0, it.tempo.firstBpm()) * grid;
            const int every = pxPerGrid >= 6.0 ? 1 : (int) std::ceil (6.0 / juce::jmax (0.01, pxPerGrid));
            const int count = (int) std::ceil (it.lengthBeats / grid);
            for (int i = 0; i <= count; i += every)
            {
                const double beat = i * grid;
                const float x = ed.localToX (beat);
                if (x < 0.0f) continue;
                if (x > w) break;
                const bool bar = std::abs (std::fmod (beat, (double) it.beatsPerBar)) < 1.0e-6;
                const bool isBeat = std::abs (beat - std::round (beat)) < 1.0e-6;
                g.setColour (juce::Colours::white.withAlpha (bar ? 0.2f : (isBeat ? 0.09f : 0.035f)));
                g.drawVerticalLine ((int) x, 0.0f, h);
            }
        }
        else
        {
            const double grid = ed.gridLocal();
            double step = grid;
            while (step * ed.timeView.getPixelsPerSecond() < 6.0) step *= 2.0;
            for (double t = std::floor (ed.timeView.getOffset() / step) * step; ed.timeView.toX (t) < w; t += step)
            {
                const bool second = std::abs (t - std::round (t)) < 1.0e-6;
                g.setColour (juce::Colours::white.withAlpha (second ? 0.12f : 0.04f));
                g.drawVerticalLine ((int) ed.timeView.toX (t), 0.0f, h);
            }
        }

        // Outside the item.
        const float x0 = ed.localToX (0.0), x1 = ed.localToX (it.localLength());
        g.setColour (juce::Colours::black.withAlpha (0.45f));
        if (x0 > 0.0f) g.fillRect (0.0f, 0.0f, x0, h);
        if (x1 < w)    g.fillRect (x1, 0.0f, w - x1, h);

        // Notes.
        const auto colour = Project::laneColour (ed.lane);
        for (auto& n : ed.notes())
        {
            auto r = ed.noteBounds (n);
            if (r.getRight() < 0.0f || r.getX() > w || r.getBottom() < 0.0f || r.getY() > h) continue;
            const bool sel = ed.selected.contains (n);
            const float vel = (float) (int) n[IDs::velocity] / 127.0f;
            g.setColour ((sel ? accent : colour).withAlpha (0.45f + 0.55f * vel));
            g.fillRoundedRectangle (r, 2.0f);
            g.setColour (sel ? juce::Colours::white : colour.darker (0.6f));
            g.drawRoundedRectangle (r, 2.0f, sel ? 1.5f : 1.0f);
            if (r.getWidth() > 34.0f && ed.rowHeight >= 11.0f)
            {
                g.setColour (juce::Colours::black.withAlpha (0.75f));
                g.setFont (juce::FontOptions (juce::jmin (11.0f, ed.rowHeight - 2.0f)));
                g.drawText (noteName ((int) n[IDs::pitch]), r.reduced (3.0f, 0.0f).toNearestInt(), juce::Justification::centredLeft);
            }
        }

        if (mode == Mode::rubber)
        {
            g.setColour (accent.withAlpha (0.12f));
            g.fillRect (rubber);
            g.setColour (accent.withAlpha (0.7f));
            g.drawRect (rubber, 1.0f);
        }

        g.setColour (accent.withAlpha (0.9f));
        g.drawVerticalLine ((int) ed.timeView.toX (ed.host.getEngine().getPositionSeconds()), 0.0f, h);
    }

    void mouseMove (const juce::MouseEvent& e) override
    {
        bool edge = false;
        auto n = ed.noteAt (e.position, &edge);
        setMouseCursor (n.isValid() ? (edge ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::DraggingHandCursor)
                                    : juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        ed.grabKeyboardFocus();
        mode = Mode::none;
        auto& um = ed.project.getUndoManager();
        bool edge = false;
        auto hit = ed.noteAt (e.position, &edge);
        const bool add = e.mods.isShiftDown() || e.mods.isCommandDown();

        if (e.mods.isPopupMenu())
        {
            if (hit.isValid() && ! ed.selected.contains (hit)) ed.select (hit, false);
            showMenu();
            return;
        }

        if (hit.isValid())
        {
            if (add && ! edge)
            {
                if (ed.selected.contains (hit)) ed.selected.removeFirstMatchingValue (hit);
                else ed.selected.add (hit);
                ed.repaintAll();
                ed.refreshStatus();
                return;
            }
            if (! ed.selected.contains (hit))
                ed.select (hit, false);

            anchor = hit;
            originals.clear();
            for (auto& n : ed.selected)
                originals.push_back ({ n, (double) n[IDs::time], (double) n[IDs::length], (int) n[IDs::pitch] });

            if (edge)
            {
                um.beginNewTransaction ("Note length");
                mode = Mode::resize;
            }
            else
            {
                um.beginNewTransaction (e.mods.isAltDown() ? "Copy notes" : "Move notes");
                if (e.mods.isAltDown())
                {
                    // The copies stay behind; the originals move.
                    for (auto& o : originals)
                        ed.project.addNote (ed.item, o.time, o.length, o.pitch, (int) o.tree[IDs::velocity]);
                }
                mode = Mode::move;
                lastPreviewPitch = (int) hit[IDs::pitch];
            }
            return;
        }

        if (e.mods.isAltDown())
        {
            startDrawing (e.position);
            return;
        }

        if (! add) ed.selected.clear();
        mode = Mode::rubber;
        rubber = {};
        ed.repaintAll();
        ed.refreshStatus();
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        const bool bypass = e.mods.isCommandDown();
        const auto& um = ed.project.getUndoManager();
        juce::ignoreUnused (um);

        if (mode == Mode::move && anchor.isValid())
        {
            const auto& a = originalOf (anchor);
            const double wanted = a.time + (ed.xToLocal (e.position.x) - ed.xToLocal (e.mouseDownPosition.x));
            double dt = juce::jmax (-minTime(), ed.snapLocal (wanted, bypass) - a.time);
            int dp = juce::jlimit (-minPitch(), 127 - maxPitch(), ed.yToPitch (e.position.y) - ed.yToPitch (e.mouseDownPosition.y));

            // Shift: only along the direction you drag most.
            if (e.mods.isShiftDown())
            {
                if (std::abs (e.getDistanceFromDragStartX()) > std::abs (e.getDistanceFromDragStartY())) dp = 0;
                else dt = 0.0;
            }
            for (auto& o : originals)
            {
                o.tree.setProperty (IDs::time, o.time + dt, &ed.project.getUndoManager());
                o.tree.setProperty (IDs::pitch, o.pitch + dp, &ed.project.getUndoManager());
            }
            const int clampedDp = dp;
            const int newPitch = a.pitch + clampedDp;
            if (newPitch != lastPreviewPitch)
            {
                lastPreviewPitch = newPitch;
                ed.host.previewNote (newPitch, (int) anchor[IDs::velocity]);
            }
        }
        else if (mode == Mode::resize && anchor.isValid())
        {
            const auto& a = originalOf (anchor);
            const double wantedEnd = a.time + a.length + (ed.xToLocal (e.position.x) - ed.xToLocal (e.mouseDownPosition.x));
            const double dl = ed.snapLocal (wantedEnd, bypass) - (a.time + a.length);
            const double minLen = juce::jmax (0.001, ed.gridLocal() * 0.25);
            for (auto& o : originals)
                o.tree.setProperty (IDs::length, juce::jmax (minLen, o.length + dl), &ed.project.getUndoManager());
        }
        else if (mode == Mode::draw && anchor.isValid())
        {
            const double start = anchor[IDs::time];
            const double end = ed.snapLocal (ed.xToLocal (e.position.x), bypass);
            const double minLen = bypass ? 0.01 : ed.gridLocal();
            anchor.setProperty (IDs::length, juce::jmax (minLen, end - start), &ed.project.getUndoManager());
        }
        else if (mode == Mode::rubber)
        {
            rubber = juce::Rectangle<float> (e.mouseDownPosition, e.position);
            if (! (e.mods.isShiftDown() || e.mods.isCommandDown())) ed.selected.clear();
            for (auto& n : ed.notes())
                if (ed.noteBounds (n).intersects (rubber))
                    ed.selected.addIfNotAlreadyThere (n);
            ed.repaintAll();
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        mode = Mode::none;
        anchor = {};
        originals.clear();
        rubber = {};
        ed.refreshStatus();
        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        auto hit = ed.noteAt (e.position);
        auto& um = ed.project.getUndoManager();
        if (hit.isValid())
        {
            um.beginNewTransaction ("Delete note");
            ed.selected.removeFirstMatchingValue (hit);
            ed.item.removeChild (hit, &um);
        }
        else
        {
            startDrawing (e.position);
            mode = Mode::none;
        }
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override  { ed.handleWheel (e, w); }

private:
    enum class Mode { none, move, resize, draw, rubber };
    struct Original { juce::ValueTree tree; double time, length; int pitch; };

    const Original& originalOf (const juce::ValueTree& t) const
    {
        for (auto& o : originals) if (o.tree == t) return o;
        return originals.front();
    }
    double minTime() const   { double m = 1.0e9; for (auto& o : originals) m = juce::jmin (m, o.time); return m; }
    int minPitch() const     { int m = 127; for (auto& o : originals) m = juce::jmin (m, o.pitch); return m; }
    int maxPitch() const     { int m = 0; for (auto& o : originals) m = juce::jmax (m, o.pitch); return m; }

    void startDrawing (juce::Point<float> pos)
    {
        const double local = ed.xToLocal (pos.x);
        const double len = ed.itemTime().localLength();
        if (local < 0.0 || local >= len)
        {
            ed.statusLabel.setText ("Notes go inside the item (the light area).", juce::dontSendNotification);
            return;
        }
        const double grid = ed.gridLocal();
        const double start = juce::jlimit (0.0, len, std::floor (local / grid + 1.0e-9) * grid);
        const int pitch = ed.yToPitch (pos.y);
        ed.project.getUndoManager().beginNewTransaction ("Draw note");
        anchor = ed.project.addNote (ed.item, start, grid, pitch, ed.newVelocity);
        ed.select (anchor, false);
        ed.host.previewNote (pitch, ed.newVelocity);
        mode = Mode::draw;
    }

    void showMenu()
    {
        juce::PopupMenu m;
        const bool any = ! ed.selected.isEmpty();
        m.addItem (1, "Delete", any);
        m.addItem (2, "Copy", any);
        m.addItem (3, "Paste at the playhead", ! clipboard.empty());
        m.addSeparator();
        m.addItem (4, "Select all");
        juce::Component::SafePointer<Roll> safeThis (this);
        m.showMenuAsync (juce::PopupMenu::Options(), [safeThis] (int r)
        {
            if (safeThis == nullptr) return;
            auto& e = safeThis->ed;
            if (r == 1) e.deleteSelected();
            if (r == 2) e.copySelected();
            if (r == 3) e.pasteAtPlayhead();
            if (r == 4) { e.selected = e.notes(); e.repaintAll(); e.refreshStatus(); }
        });
    }

    NoteEditor& ed;
    Mode mode = Mode::none;
    juce::ValueTree anchor;
    std::vector<Original> originals;
    juce::Rectangle<float> rubber;
    int lastPreviewPitch = -1;
};

//==============================================================================
class NoteEditor::VelocityStrip : public juce::Component
{
public:
    explicit VelocityStrip (NoteEditor& e) : ed (e) {}

    void paint (juce::Graphics& g) override
    {
        g.fillAll (panel);
        const float h = (float) getHeight() - 8.0f;
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        for (float f : { 0.25f, 0.5f, 0.75f })
            g.drawHorizontalLine ((int) (4.0f + h * (1.0f - f)), 0.0f, (float) getWidth());

        const auto colour = Project::laneColour (ed.lane);
        for (auto& n : ed.notes())
        {
            const float x = ed.localToX ((double) n[IDs::time]);
            if (x < -4.0f || x > (float) getWidth() + 4.0f) continue;
            const float v = (float) (int) n[IDs::velocity] / 127.0f;
            const bool sel = ed.selected.contains (n);
            const float top = 4.0f + h * (1.0f - v);
            g.setColour (sel ? accent : colour);
            g.fillRect (x - 1.5f, top, 3.0f, 4.0f + h - top);
            g.fillEllipse (x - 4.0f, top - 4.0f, 8.0f, 8.0f);
        }

        if (lineActive)
        {
            g.setColour (accent);
            g.drawLine (juce::Line<float> (lineStart, lineEnd), 1.5f);
        }

        g.setColour (juce::Colours::black.withAlpha (0.5f));
        g.drawHorizontalLine (0, 0.0f, (float) getWidth());
    }

    void mouseDown (const juce::MouseEvent& e) override
    {
        ed.grabKeyboardFocus();
        auto& um = ed.project.getUndoManager();
        originals.clear();

        if (e.mods.isAltDown())
        {
            um.beginNewTransaction ("Draw velocities");
            lineActive = true;
            lineStart = lineEnd = e.position;
            for (auto& n : ed.notes())
                originals.push_back ({ n, (int) n[IDs::velocity] });
            return;
        }

        // The bar nearest the mouse (within a few pixels).
        juce::ValueTree hit;
        float best = 6.0f;
        for (auto& n : ed.notes())
        {
            const float d = std::abs (ed.localToX ((double) n[IDs::time]) - e.position.x);
            if (d < best || (std::abs (d - best) < 0.01f && ed.selected.contains (n))) { best = d; hit = n; }
        }
        if (! hit.isValid())
            return;

        if (! ed.selected.contains (hit))
            ed.select (hit, false);
        um.beginNewTransaction ("Velocity");
        anchorVelocity = (int) hit[IDs::velocity];
        for (auto& n : ed.selected)
            originals.push_back ({ n, (int) n[IDs::velocity] });
    }

    void mouseDrag (const juce::MouseEvent& e) override
    {
        auto& um = ed.project.getUndoManager();
        const float h = (float) getHeight() - 8.0f;

        if (lineActive)
        {
            lineEnd = e.position;
            const float xa = juce::jmin (lineStart.x, lineEnd.x), xb = juce::jmax (lineStart.x, lineEnd.x);
            for (auto& o : originals)
            {
                const float x = ed.localToX ((double) o.tree[IDs::time]);
                if (x < xa || x > xb || xb - xa < 1.0f)
                {
                    o.tree.setProperty (IDs::velocity, o.velocity, &um);
                    continue;
                }
                const float f = (x - lineStart.x) / (lineEnd.x - lineStart.x);
                const float y = lineStart.y + f * (lineEnd.y - lineStart.y);
                o.tree.setProperty (IDs::velocity, juce::jlimit (1, 127, juce::roundToInt ((1.0f - (y - 4.0f) / h) * 127.0f)), &um);
            }
            repaint();
            return;
        }

        if (originals.empty())
            return;

        const float scale = e.mods.isCommandDown() ? 0.25f : 1.0f;
        const float dv = -(float) e.getDistanceFromDragStartY() / h * 127.0f * scale;
        for (auto& o : originals)
        {
            int v;
            if (e.mods.isShiftDown())
                v = juce::roundToInt ((float) o.velocity * ((float) anchorVelocity + dv) / (float) juce::jmax (1, anchorVelocity));
            else
                v = juce::roundToInt ((float) o.velocity + dv);
            o.tree.setProperty (IDs::velocity, juce::jlimit (1, 127, v), &um);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        lineActive = false;
        originals.clear();
        repaint();
        ed.refreshStatus();
    }

    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) override  { ed.handleWheel (e, w); }

private:
    struct Original { juce::ValueTree tree; int velocity; };
    NoteEditor& ed;
    std::vector<Original> originals;
    int anchorVelocity = 100;
    bool lineActive = false;
    juce::Point<float> lineStart, lineEnd;
};

//==============================================================================
NoteEditor::NoteEditor (Project& p, PluginHost& h, AutomationController& c, SnapSettings& s, juce::ValueTree itemTree)
    : project (p), host (h), controller (c), snap (s), item (itemTree), lane (itemTree.getParent())
{
    ruler = std::make_unique<Ruler> (*this);
    keyboard = std::make_unique<Keyboard> (*this);
    roll = std::make_unique<Roll> (*this);
    velocityStrip = std::make_unique<VelocityStrip> (*this);
    for (juce::Component* comp : { (juce::Component*) ruler.get(), (juce::Component*) keyboard.get(),
                                   (juce::Component*) roll.get(), (juce::Component*) velocityStrip.get() })
        addAndMakeVisible (comp);

    vScroll.setAutoHide (false);
    vScroll.addListener (this);
    addAndMakeVisible (vScroll);

    titleLabel.setFont (juce::FontOptions (14.0f, juce::Font::bold));
    addAndMakeVisible (titleLabel);

    for (auto* l : { &gridLabel, &velocityLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
        addAndMakeVisible (l);
    }
    gridLabel.setText ("Grid", juce::dontSendNotification);
    velocityLabel.setText ("New notes", juce::dontSendNotification);

    gridBox.onChange = [this]
    {
        const int i = gridBox.getSelectedItemIndex();
        if (i < 0) return;
        if (itemTime().isMusical())
        {
            auto& um = project.getUndoManager();
            um.beginNewTransaction ("Grid");
            item.setProperty (IDs::gridBeats, musicalGrids[juce::jlimit (0, 6, i)], &um);
        }
        else
        {
            snap.setGrid (linearGrids[juce::jlimit (0, 7, i)]);
        }
        repaintAll();
    };
    addAndMakeVisible (gridBox);

    velocitySlider.setSliderStyle (juce::Slider::LinearBar);
    velocitySlider.setRange (1.0, 127.0, 1.0);
    velocitySlider.setValue (newVelocity, juce::dontSendNotification);
    velocitySlider.textFromValueFunction = [] (double v) { return "velocity " + juce::String ((int) v); };
    velocitySlider.setColour (juce::Slider::trackColourId, juce::Colour (0xff3b5f8f));
    velocitySlider.onValueChange = [this] { newVelocity = (int) velocitySlider.getValue(); };
    addAndMakeVisible (velocitySlider);

    fitButton.onClick = [this] { fitToItem(); };
    addAndMakeVisible (fitButton);

    statusLabel.setFont (juce::FontOptions (11.5f));
    statusLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.6f));
    addAndMakeVisible (statusLabel);

    timeView.setLength (project.getLengthSeconds());
    timeView.addChangeListener (this);
    project.getState().addListener (this);
    snap.addChangeListener (this);

    // Start around the notes there are (or middle C).
    int lo = 128, hi = -1;
    for (auto& n : notes()) { lo = juce::jmin (lo, (int) n[IDs::pitch]); hi = juce::jmax (hi, (int) n[IDs::pitch]); }
    topPitch = hi >= lo ? juce::jmin (127, hi + 6) : 72;

    refreshGridBox();
    refreshStatus();
    setWantsKeyboardFocus (true);
    startTimerHz (30);
    setSize (1100, 620);
    juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<NoteEditor> (this)]
    {
        if (safe != nullptr) { safe->fitToItem(); safe->grabKeyboardFocus(); }
    });
}

NoteEditor::~NoteEditor()
{
    snap.removeChangeListener (this);
    project.getState().removeListener (this);
    timeView.removeChangeListener (this);
    vScroll.removeListener (this);
}

//==============================================================================
float NoteEditor::localToX (double local) const     { return timeView.toX (itemTime().absolute (local)); }
double NoteEditor::xToLocal (float x) const          { return itemTime().localAt (timeView.toTime (x)); }

double NoteEditor::gridLocal() const
{
    const auto it = itemTime();
    if (it.isMusical()) return juce::jmax (0.01, it.gridBeats);
    return juce::jmax (0.001, snap.getGrid());
}

double NoteEditor::snapLocal (double local, bool bypass) const
{
    const auto it = itemTime();
    if (bypass) return local;
    if (it.isMusical())
        return std::round (local / gridLocal()) * gridLocal();
    if (! snap.isOn()) return local;
    const double start = item[IDs::start];
    return snap.snap (start + local) - start;
}

int NoteEditor::visibleRows() const
{
    return juce::jmax (1, (int) ((float) roll->getHeight() / rowHeight));
}

void NoteEditor::setTopPitch (int pitch)
{
    topPitch = juce::jlimit (juce::jmin (127, visibleRows() - 1), 127, pitch);
    vScroll.setRangeLimits (0.0, 128.0, juce::dontSendNotification);
    vScroll.setCurrentRange ((double) (127 - topPitch), (double) visibleRows(), juce::dontSendNotification);
    repaintAll();
}

void NoteEditor::scrollBarMoved (juce::ScrollBar*, double newRangeStart)
{
    setTopPitch (127 - (int) std::round (newRangeStart));
}

void NoteEditor::fitToItem()
{
    const double start = item[IDs::start], length = item[IDs::length];
    timeView.fitRange (start - length * 0.03, start + length * 1.03);

    int lo = 128, hi = -1;
    for (auto& n : notes()) { lo = juce::jmin (lo, (int) n[IDs::pitch]); hi = juce::jmax (hi, (int) n[IDs::pitch]); }
    if (hi >= lo)
        setTopPitch (juce::jmax (hi + 3, (hi + lo) / 2 + visibleRows() / 2));
    else
        setTopPitch (topPitch);
}

void NoteEditor::handleWheel (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    if (e.mods.isCommandDown())
    {
        if (wheel.isInertial) return;
        const float x = (float) e.getEventRelativeTo (roll.get()).x;
        timeView.zoomAround (juce::jlimit (0.5, 2.0, std::pow (2.0, (double) wheel.deltaY * 1.5)), juce::jmax (0.0f, x));
        return;
    }
    if (e.mods.isAltDown())
    {
        rowHeight = juce::jlimit (5.0f, 30.0f, rowHeight + wheel.deltaY * 8.0f);
        setTopPitch (topPitch);
        return;
    }
    if (e.mods.isShiftDown() || std::abs (wheel.deltaX) > std::abs (wheel.deltaY))
    {
        const float d = std::abs (wheel.deltaX) > std::abs (wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
        timeView.scrollBy (-(double) d * timeView.visibleSeconds() * 0.25);
        return;
    }
    const int rows = juce::roundToInt (wheel.deltaY * (wheel.isReversed ? -1.0f : 1.0f) * 30.0f);
    setTopPitch (topPitch + (rows == 0 ? (wheel.deltaY > 0 ? 1 : -1) : rows));
}

//==============================================================================
juce::Array<juce::ValueTree> NoteEditor::notes() const
{
    juce::Array<juce::ValueTree> result;
    for (auto n : item)
        if (n.hasType (IDs::NOTE))
            result.add (n);
    return result;
}

juce::Rectangle<float> NoteEditor::noteBounds (const juce::ValueTree& n) const
{
    const double t0 = n[IDs::time], t1 = t0 + (double) n[IDs::length];
    const float x0 = localToX (t0), x1 = localToX (t1);
    return { x0, pitchToY ((int) n[IDs::pitch]) + 1.0f, juce::jmax (3.0f, x1 - x0), juce::jmax (2.0f, rowHeight - 2.0f) };
}

juce::ValueTree NoteEditor::noteAt (juce::Point<float> pos, bool* onRightEdge) const
{
    auto all = notes();
    for (int i = all.size(); --i >= 0;)
    {
        auto r = noteBounds (all[i]);
        if (r.expanded (0.0f, 1.0f).contains (pos))
        {
            if (onRightEdge != nullptr)
                *onRightEdge = pos.x >= r.getRight() - juce::jmin (6.0f, r.getWidth() * 0.3f);
            return all[i];
        }
    }
    return {};
}

void NoteEditor::select (const juce::ValueTree& note, bool add)
{
    if (! add) selected.clear();
    selected.addIfNotAlreadyThere (note);
    repaintAll();
    refreshStatus();
}

void NoteEditor::deleteSelected()
{
    if (selected.isEmpty()) return;
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Delete notes");
    for (auto& n : selected)
        item.removeChild (n, &um);
    selected.clear();
    refreshStatus();
}

void NoteEditor::copySelected()
{
    if (selected.isEmpty()) return;
    double earliest = 1.0e12;
    for (auto& n : selected) earliest = juce::jmin (earliest, (double) n[IDs::time]);
    clipboard.clear();
    for (auto& n : selected)
        clipboard.push_back ({ itemTime().toSeconds ((double) n[IDs::time]) - itemTime().toSeconds (earliest),
                               itemTime().toSeconds ((double) n[IDs::time] + (double) n[IDs::length]) - itemTime().toSeconds ((double) n[IDs::time]),
                               (int) n[IDs::pitch], (int) n[IDs::velocity] });
    statusLabel.setText (juce::String (selected.size()) + " note(s) copied.", juce::dontSendNotification);
}

void NoteEditor::pasteAtPlayhead()
{
    if (clipboard.empty()) return;
    const auto it = itemTime();
    const double at = juce::jmax (0.0, snapLocal (it.localAt (host.getEngine().getPositionSeconds()), false));
    const double atSeconds = it.toSeconds (at);
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Paste notes");
    selected.clear();
    for (auto& c : clipboard)
    {
        // Copied in seconds: they keep their duration, also in a different tempo.
        const double t = it.toLocal (atSeconds + c.time);
        const double len = it.toLocal (atSeconds + c.time + c.length) - t;
        if (t >= it.localLength()) continue;
        selected.add (project.addNote (item, t, len, c.pitch, c.velocity));
    }
    refreshStatus();
}

void NoteEditor::moveSelected (double dLocal, int dPitch)
{
    if (selected.isEmpty()) return;
    auto& um = project.getUndoManager();
    um.beginNewTransaction ("Move notes");
    for (auto& n : selected)
    {
        n.setProperty (IDs::time, juce::jmax (0.0, (double) n[IDs::time] + dLocal), &um);
        n.setProperty (IDs::pitch, juce::jlimit (0, 127, (int) n[IDs::pitch] + dPitch), &um);
    }
    if (dPitch != 0 && selected.size() == 1)
        host.previewNote ((int) selected.getFirst()[IDs::pitch], (int) selected.getFirst()[IDs::velocity]);
}

void NoteEditor::valueTreeChildRemoved (juce::ValueTree& parent, juce::ValueTree& child, int)
{
    if (parent == item)
        selected.removeAllInstancesOf (child);
    repaintAll();
    refreshStatus();
}

void NoteEditor::refreshStatus()
{
    const auto it = itemTime();
    titleLabel.setText (lane[IDs::name].toString() + "   " + TimeFormat::format ((double) item[IDs::start]) + " - "
                            + TimeFormat::format ((double) item[IDs::start] + (double) item[IDs::length])
                            + (it.isMusical() ? "   (musical)" : ""),
                        juce::dontSendNotification);

    juce::String text;
    if (selected.size() == 1)
    {
        auto n = selected.getFirst();
        const double t = n[IDs::time], len = n[IDs::length];
        text << noteName ((int) n[IDs::pitch]) << "   velocity " << (int) n[IDs::velocity] << "   at "
             << (it.isMusical() ? it.barBeat (t) : TimeFormat::format (it.absolute (t)))
             << "   length " << (it.isMusical() ? juce::String (len, 3) + " beats" : juce::String (len, 3) + " s");
    }
    else if (selected.size() > 1)
    {
        text << selected.size() << " notes selected";
    }
    else
    {
        text = "Option-drag or double-click: new note.  Drag: move.  Right edge: length.  "
               "Velocity strip: drag a bar, Option-drag a line.  Keys: arrows, Delete, Cmd-C/V, Cmd-Z.";
    }
    statusLabel.setText (text, juce::dontSendNotification);
    refreshGridBox();
}

void NoteEditor::refreshGridBox()
{
    const auto it = itemTime();
    const int wanted = it.isMusical() ? 7 : 8;
    if (gridBox.getNumItems() != wanted || gridMusical != it.isMusical())
    {
        gridBox.clear (juce::dontSendNotification);
        for (int i = 0; i < wanted; ++i)
            gridBox.addItem (it.isMusical() ? musicalGridNames[i] : linearGridNames[i], i + 1);
    }
    gridMusical = it.isMusical();

    int best = 0;
    double bestDiff = 1.0e9;
    for (int i = 0; i < wanted; ++i)
    {
        const double d = std::abs ((it.isMusical() ? musicalGrids[i] : linearGrids[i]) - gridLocal());
        if (d < bestDiff) { bestDiff = d; best = i; }
    }
    gridBox.setSelectedItemIndex (best, juce::dontSendNotification);
}

void NoteEditor::repaintAll()
{
    ruler->repaint();
    keyboard->repaint();
    roll->repaint();
    velocityStrip->repaint();
}

void NoteEditor::timerCallback()
{
    if (! item.getParent().isValid() || ! item.getParent().getParent().isValid())
    {
        stopTimer();
        if (onItemGone) onItemGone();
        return;
    }
    lane = item.getParent();

    const double pos = host.getEngine().getPositionSeconds();
    if (! juce::approximatelyEqual (pos, lastPosition) || host.getEngine().isPlaying())
    {
        lastPosition = pos;
        ruler->repaint();
        roll->repaint();
    }
}

//==============================================================================
bool NoteEditor::keyPressed (const juce::KeyPress& key)
{
    const auto mods = key.getModifiers();
    const int code = key.getKeyCode();
    auto& um = project.getUndoManager();

    if (mods.isCommandDown() && (code == 'z' || code == 'Z'))
    {
        if (mods.isShiftDown()) um.redo(); else um.undo();
        return true;
    }
    if (mods.isCommandDown() && (code == 'a' || code == 'A'))  { selected = notes(); repaintAll(); refreshStatus(); return true; }
    if (mods.isCommandDown() && (code == 'c' || code == 'C'))  { copySelected(); return true; }
    if (mods.isCommandDown() && (code == 'x' || code == 'X'))  { copySelected(); deleteSelected(); return true; }
    if (mods.isCommandDown() && (code == 'v' || code == 'V'))  { pasteAtPlayhead(); return true; }
    if (code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) { deleteSelected(); return true; }
    if (code == juce::KeyPress::spaceKey) { controller.togglePlay(); return true; }
    if (code == juce::KeyPress::leftKey)  { moveSelected (-gridLocal(), 0); return true; }
    if (code == juce::KeyPress::rightKey) { moveSelected (gridLocal(), 0); return true; }
    if (code == juce::KeyPress::upKey)    { moveSelected (0.0, mods.isShiftDown() ? 12 : 1); return true; }
    if (code == juce::KeyPress::downKey)  { moveSelected (0.0, mods.isShiftDown() ? -12 : -1); return true; }
    return false;
}

//==============================================================================
void NoteEditor::paint (juce::Graphics& g)
{
    g.fillAll (background);
    g.setColour (panel);
    g.fillRect (0, 0, getWidth(), toolbarHeight);
    g.fillRect (0, getHeight() - statusHeight, getWidth(), statusHeight);
    g.fillRect (0, toolbarHeight, keyboardWidth, rulerHeight);

    // Label beside the velocity strip.
    auto r = velocityStrip->getBounds().withX (0).withWidth (keyboardWidth);
    g.fillRect (r);
    g.setColour (juce::Colours::white.withAlpha (0.5f));
    g.setFont (juce::FontOptions (10.5f));
    g.drawFittedText ("velocity", r.reduced (4), juce::Justification::centred, 2);
}

void NoteEditor::resized()
{
    auto r = getLocalBounds();
    auto toolbar = r.removeFromTop (toolbarHeight).reduced (8, 7);
    fitButton.setBounds (toolbar.removeFromRight (44));
    toolbar.removeFromRight (12);
    velocitySlider.setBounds (toolbar.removeFromRight (120));
    velocityLabel.setBounds (toolbar.removeFromRight (70));
    toolbar.removeFromRight (12);
    gridBox.setBounds (toolbar.removeFromRight (160));
    gridLabel.setBounds (toolbar.removeFromRight (34));
    toolbar.removeFromRight (12);
    titleLabel.setBounds (toolbar);

    statusLabel.setBounds (r.removeFromBottom (statusHeight).reduced (8, 0));

    auto strip = r.removeFromBottom (velocityHeight);
    velocityStrip->setBounds (strip.withTrimmedLeft (keyboardWidth).withTrimmedRight (scrollWidth));

    auto top = r.removeFromTop (rulerHeight);
    ruler->setBounds (top.withTrimmedLeft (keyboardWidth).withTrimmedRight (scrollWidth));

    vScroll.setBounds (r.removeFromRight (scrollWidth));
    keyboard->setBounds (r.removeFromLeft (keyboardWidth));
    roll->setBounds (r);

    timeView.setViewWidth (roll->getWidth());
    setTopPitch (topPitch);
}
