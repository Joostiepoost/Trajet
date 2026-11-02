# Trajet

An automation score app: load a synth (AU or VST3), turn its parameters into lanes, write automation as a score, play it and render it to WAV.

macOS, Apple Silicon only. Built with [JUCE](https://juce.com) 9 and CMake.

## Building

Requirements: Xcode (opened once, licence accepted), Homebrew, CMake (`brew install cmake`).

In Terminal, from this folder:

```
chmod +x build.sh   # only the first time
./build.sh
```

The first build downloads JUCE and takes a few minutes; later builds are much faster. The app opens when the build finishes, and lives at `build/Trajet_artefacts/Release/Trajet.app`.

Other options:

- `./build.sh debug` — Debug build (slower, more checks)
- `./build.sh xcode` — generate and open an Xcode project, for debugging

## Status

| Step | Content | State |
| --- | --- | --- |
| 0 | Toolchain: blank window | done |
| 1 | Plugin host: scan, load, open editor, audio settings, parameter list, held note, audio on/off | done |
| 2 | Projects: new, open, save (with the plugin's patch), undo/redo, menu bar | done |
| 3 | Playback engine: transport, lanes with points, automation driving the plugin, chase / patch | done |
| 4 | Main window: zoom/scroll, lane headers (reorder, resize, mute/solo, colour), items, point gestures, snap, inspector | done |
| 5 | Item editor, rubber-band selection, scale box, nudge, point copy/paste, smooth/thin/reverse/invert | done |
| 6 | Time bases: linear (seconds) and musical items (bars/beats, own metre, tempo map with ramps) | done |
| 7 | Generator items: LFO, random, formula; bake to points | testing |

## Using step 1

1. **Plugins...** opens the plugin list. Click *Options* at the bottom, then *Scan for new or updated AudioUnit plug-ins* (and VST3 if you like). Scanning can take a minute.
2. **Load plugin** shows the scanned instruments; pick Aalto.
3. **Edit patch** opens Aalto's own panel. Turn a dial and watch its value move in the parameter list.
4. **Audio...** chooses the output device and sample rate (up to 96 kHz, if the device supports it).
5. **Audio on / Audio off** pauses all audio processing to save CPU and battery; the plugin and its patch stay loaded.
6. **Held note** sends a sustained note for synths that need one; set its pitch and velocity next to it.

The last loaded plugin is reloaded automatically when Trajet starts.

## Using step 2

- **File menu**: New (⌘N), Open (⌘O), Save (⌘S), Save as (⇧⌘S). Projects are `.trajet` files: plain JSON you can open in a text editor.
- A saved project stores the plugin **and its patch**: open it again and Aalto comes back exactly as you left it.
- The window title shows the project name, with `*` when there are unsaved changes (turning a dial in Aalto counts as a change).
- **Edit menu**: Undo (⌘Z) and Redo (⇧⌘Z). For now only the held-note settings are undoable; lanes and points join from step 3 on.
- **Audio menu**: audio processing on/off (⇧⌘A) and the plugin's panel (⌘E).
- Trajet reopens the last project when it starts, and asks to save unsaved changes when you quit.

## Generator items (step 7)

An item can compute its curve instead of holding hand-drawn points.

- **Right-click an item → Generator → LFO / Random / Formula.** Choose *Points (by hand)* to switch back; the item's hand-drawn points are kept underneath and come back.
- Select the item: its settings appear in the **Inspector**. Double-click it to open the **item editor**, where the settings replace the point table.
- **Min / Max** are in the plugin's own units. The generator sweeps between them (Min above Max turns the curve upside down).
- **Rate** is in Hz for linear items and *cycles per beat* for musical items, so an LFO in a musical item speeds up with an accelerando.
- **LFO** shapes: sine (starts at the bottom), triangle, square, saw up, saw down; **Phase** shifts the start.
- **Random** modes: *stepped* (a new value each cycle), *smooth* (glides between them), *drift* (slow wandering). The **Seed** fixes the line, so it is the same each time you play or render; **New** picks another.
- **Formula**: any expression whose result 0..1 is mapped onto Min..Max. Variables: `t` (0 to 1 over the item), `s` (seconds since the item start), `b` (beats, musical items), `pi`, `e`. Functions: `sin cos tan asin acos atan abs sqrt exp log log10 floor ceil round frac sign min max pow clamp mod step mix tri saw sq noise rand`. Operators `+ - * / % ^` and comparisons (`t > 0.5` gives 1 or 0). Errors show in orange under the field; while the formula is broken the item sits at Min. Example: `0.5 + 0.5*sin(2*pi*4*t)*(1-t)` – four cycles that die away.
- **Bake to points** turns the generated curve into ordinary points (exact steps for stepped random and square LFOs, a thinned curve otherwise) so you can edit it by hand. Undo brings the generator back.

## Time bases (step 6)

Every item has its own time base:

- **Linear**: points are placed in seconds (and milliseconds). This is the default.
- **Musical**: points are placed in beats, with the item's own **metre** (e.g. 4/4, 5/8) and **tempo map**. Bar and beat lines show inside the item, and its title strip shows the metre and tempo (e.g. `4/4 ♩=90 → 120`).

How to use it:

- Right-click an item → **Make musical (bars and beats)** (or back to linear). Points keep their place in time; the item starts at 120 bpm.
- In the **item editor**: *Time base*, *Length* (in beats, or `bar|beat`), *Metre* and *Grid* (1 beat … 1/16 beat, triplets included) are in the second toolbar row.
- The **tempo strip** (musical items, in the item editor): click to add a tempo point, drag it (time and tempo), double-click to type a tempo, right-click → **Ramp to the next tempo** for an accelerando or ritardando, or delete.
- Changing the tempo stretches or squeezes the item; its start stays put. A change that would make it overlap the next item is refused.
- Inside musical items, snapping uses the item's beat grid; arrow keys nudge by one grid step; times can be typed as `bar|beat` (e.g. `3|2.5`).
- The maths is exact: a 64-beat accelerando from 90 to 120 bpm lasts 60 / (30/64) · ln (120/90) = 36.823 s, and its last beat lands there.

## Item editor and point tools (step 5)

- **Double-click inside an item** (not on a point) to open it in the **item editor**: a large view with a value axis in the plugin's units and a table of all points.
  - Tools: **Select** (1), **Pencil** (2), **Line** (3), **Bend** (4). The modifier gestures of the main window work too.
  - **Smooth**, **Thin**, **Reverse**, **Invert** act on the selected points, or on the whole item when none are selected.
  - In the table, click a row to select its point; double-click a time or value to type it, or the shape to change it.
  - ⌘Z / ⇧⌘Z undo, ⌘A select all, ⌘C/X/V points (paste at the playhead), Delete, arrows nudge. Click the small ruler to move the playhead.
- **Rubber band** (main window too): drag on empty space to select points, across lanes. ⌘ adds to the selection.
- **Scale**: with two or more points selected, a dashed box appears; drag its side handles to stretch in time or value (⌥: around the centre).
- **Nudge**: arrow keys move selected points by one grid step or 1%; with ⇧, 10×.
- **Copy points** (⌘C with points selected), click in a lane to put the playhead inside an item, ⌘V pastes them there.
- **Points menu**: smooth, thin, reverse, invert, select all points in the active lane (⌘A), open the item editor (⇧⌘E).

## Using the main window (step 4)

**Lanes**
- **+ Lane** opens the parameter list; double-click a parameter to add its lane (type in the filter first, e.g. `osc`).
- Drag a lane header up or down to reorder; drag its lower edge to resize (or Option-scroll over the lane).
- Double-click the name to rename. **M** mutes (the lane plays its patch value), **S** solos. Right-click the header for colour (16 choices) and *Remove lane*.

**Items**
- Double-click empty space in a lane: new item (up to 4 s, in the free space). Items never overlap.
- Drag the coloured title strip to move (also up or down into another lane); Option-drag it to copy; drag an edge to trim.
- Right-click an item: duplicate or delete. ⌘C / ⌘X / ⌘V / ⌘D (duplicate right after) / Delete.
- Click empty space in a lane to make it the paste target and move the playhead there; ⌘V pastes there. Items copied from several lanes keep their layout, starting at that lane.

**Points** (inside an item, below the title strip)
- ⌥-click: add a point · ⌥-drag: draw freehand (points are thinned automatically) · ⌥⇧-drag: straight ramp, replacing points underneath.
- Drag a point to move it (all selected points move together); ⇧ locks to time or value; ⌘ moves finely without snapping.
- ⌘-click adds a point to the selection · ⌘⌥-drag a segment bends it · double-click a point to type its time and value · right-click for the segment shape.

**View and snap**
- Scroll: lanes up/down · ⇧-scroll or trackpad sideways: time · ⌘-scroll: zoom around the mouse · **Fit** or ⌘0: whole project · ⌘= / ⌘-: zoom in/out.
- **Snap** (or S) with the grid size next to it; hold ⌘ while dragging to bypass.
- The **Inspector** on the right shows the selected item or point; click a value to type a new one.

## Using step 3

- **Add a lane**: double-click a parameter in the list at the bottom (filter first, e.g. `timbre`).
- **Points** (temporary editor, step 4 brings the real one): click in a lane to add a point and keep dragging to place it; drag a point to move it; double-click a point to delete it; right-click a point to choose the shape of the segment that starts there (linear, curve slow/fast start, smooth, hold). Undo works for all of it.
- **Transport**: Play/Stop (Space), back to start (Return), Loop (whole project). Click or drag in the time ruler to move the playhead.
- **Score vs patch**: when you move the playhead or press Play, the plugin follows the score: stopped, it sounds like the score at the playhead. **Patch** (or the P key) shows the plain patch instead, so you can edit the starting state; the ruler shows which one is active. Before a lane's first point the patch value applies. The project always saves the plain patch.
- **Length (s)** sets the project length. **Live update** sets how often parameters are updated during playback: *per buffer* (default) or finer, at more CPU.
