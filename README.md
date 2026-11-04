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
| 7 | Generator items: LFO, random, formula; bake to points | done |
| 8 | Render to WAV (offline, 44.1–96 kHz, 16/24/32-float), automation CSV, output limiter | done |
| 9 | Lane groups (fold, colour, move, group mute/solo), recent files | done |
| 10 | Note lanes: MIDI notes with a piano roll and velocity strip | done |
| 11 | Library: save items with name and tags, search, drag onto lanes | testing |
| 12 | Export: MIDI file (CC 7/14-bit + notes), REAPER automation items, REAPER project from a template | testing |
| 13 | Splitting items: at the playhead, at selected points, here, into equal parts (shared with Effet) | testing |

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

## Splitting items (step 13)

Cut a roughly edited item into segments, for instance to keep the good parts or to save pieces to the library. The pieces together play exactly as the original did.

- **Split at playhead (⌘T)**: cuts the selected items at the playhead; with nothing selected, every item under the playhead (in all lanes).
- **Split at selected points (⇧⌘T)**: select points (⌘-click, or drag a box) and cut at each of them.
- **Right-click an item → Split**: *Split here* (where you clicked, snapped to the grid), *Split at the selected points*, or *Into 2 / 3 / 4 / 6 / 8 / 12 / 16 equal parts* (equal beats for musical items). This also works in the item editor.
- After a split the pieces are selected, so **⌘L** can save one to the library right away. One **⌘Z** undoes a whole split.
- What carries over:
  - **points**: a point is added on each side of the cut. Straight lines, bends and held values stay exactly the same. A smooth (S-shaped) segment across a cut is traced with a few extra points, within a fraction of a percent.
  - **generators** (LFO, random, formula) carry on where the cut was instead of starting over. Each piece remembers its place in the original. *Bake* takes that into account.
  - **musical items**: the tempo map is cut too (a ramp is split at the tempo at the cut), so each piece keeps its bars, beats and length in beats.
  - **notes**: a note across the cut becomes two notes.
- Cuts closer than 10 ms to an item's edge are skipped.

## Library (step 11)

Keep items you like (point curves, generators, note items) and reuse them in any project.

- **Save**: select an item and press **⌘L**, or right-click it → *Save to library…*. Give it a name and tags (words separated by spaces).
- The **Library** tab (beside the Inspector; **⇧⌘L**) lists the entries with a small picture, their length and tags. Type in the search box to filter by name, tag or source lane.
- **Use**: drag an entry onto a lane (a ghost shows where it lands; red = no room or wrong kind of lane), or double-click it to put it at the playhead in the active lane. Automation items go in parameter lanes, note items in note lanes.
- Right-click an entry: rename, edit tags, show in Finder, delete (to the bin).
- Entries are plain files (`.trajetitem`, JSON) in `~/Music/Trajet Library`; **Folder…** picks another folder (a shared one, a project folder…).

## Export (step 12)

**File → Export lanes… (⌥⌘E)**. Choose the lanes (all, the active lane's group, or only the active lane) and a format:

- **MIDI file**: every automation lane becomes one controller, in lane order from the first CC you choose (7-bit, or 14-bit as CC n + CC n+32 for CC 0-31), on the channel you choose. Curves are sampled at the resolution and written when the value changes; note lanes become notes. The file is at 120 bpm, so its times match the score in any DAW. The CC numbers are listed as text in the file.
- **REAPER automation items**: one `.ReaperAutoItem` per lane in a folder. In REAPER show the plugin parameter's envelope, right-click it → *Insert automation item*, then load the file. Positions are in beats: set the tempo of the REAPER project they go into. (Put them in `~/Library/Application Support/REAPER/AutomationItems` to see them in REAPER's load menu.)
- **REAPER project**: needs a template, made once: in REAPER, put the plugin (with the patch you want) on the first track of an empty project and save it. Trajet copies that project, adds one envelope per lane to the plugin and the notes as a MIDI item. The envelopes refer to parameters by number, as Trajet sees them; check in REAPER that each envelope sits on the right parameter.
- **Points**: *Keep points editable* writes Trajet's linear, hold and smooth points as REAPER points (smooth becomes REAPER's "slow start/end", very close), and samples curves and generators. *Sample everything* samples every curve exactly. Before the first item a lane holds its patch value, and between items the last value holds, as in Trajet.

## Note lanes (step 10)

For synths that need notes (and for melodies on top of the automation). A note lane holds items like an automation lane: move, trim, copy, make musical, delete them the same way. The notes are sent to the plugin on MIDI channel 1 while playing and rendering. Notes are not chased: starting in the middle of a note does not sound it.

- **Add one**: **+ Notes** in the transport bar, or Edit → Add note lane (**⇧⌘N**). It goes below the lane you clicked last, in the same group.
- **Double-click an item** (or right-click → *Open in piano roll*) to edit its notes:
  - **Option-drag** on empty space draws a note (the drag sets its length); **double-click** empty space adds a note, double-click a note deletes it.
  - **Drag a note** to move it (time snaps to the grid, up/down changes pitch; Shift keeps one direction; Cmd: no snap). **Option-drag** copies.
  - **Drag the right edge** of a note to change its length (all selected notes together).
  - **Drag on empty space** for a rubber-band selection; Shift/Cmd-click adds or removes notes.
  - **Velocity strip** (bottom): drag a bar up or down (all selected notes together; **Shift** scales them proportionally; **Cmd** for fine steps). **Option-drag** draws a line across the strip: the notes under it get velocities along the line (crescendo, diminuendo).
  - **Keys**: arrows move by the grid / a semitone (Shift-up/down: an octave), Delete, Cmd-A, Cmd-C/X/V (paste at the playhead), Cmd-Z, Space plays.
  - **Grid**: in musical items 1 to 1/16 beat (triplets included); in linear items 10 ms to 1 s. New notes are one grid step long, with the velocity set in *New notes*.
  - Click a key of the keyboard to hear the pitch; drawn and moved notes sound too (when the audio is on).
- The main window shows the notes small, in the lane's colour (brighter = louder).

## Groups (step 9)

Groups gather lanes that belong together (for Aalto: oscillator, waveguide, filter...). A group's lanes always sit together under its bar.

- **Make a group**: right-click a lane header → *Group → New group with this lane*, or click a lane and press **⌘G**.
- **Put lanes in a group**: drag a lane header to just below the group's bar, or between two of its lanes. Or right-click the lane → *Group → Move to "…"*. To take a lane out: *Group → Take out of "…"*, or drag it outside the group.
- **Fold / open**: click the triangle, or double-click the bar's right side. A folded group is one compact row showing all its lanes' curves on top of each other. **⌘[** folds all groups, **⌘]** opens them all (View menu).
- **Move a group**: drag its bar; all its lanes move along.
- **M / S** on the bar mute or solo all lanes of the group.
- **Right-click the bar**: rename (or double-click the name), colour, *Give the lanes the group colour*, *Ungroup* (keeps the lanes), *Remove group and its lanes*.
- A new lane (+ Lane) goes right below the lane you clicked last, in the same group.
- Paste works in the lanes as shown, so it never lands in a folded (hidden) lane.

**File → Open recent** lists the last twelve projects.

## Rendering and CSV (step 8)

**File → Render to WAV… (⌘R)** writes the score to an audio file, faster than real time.

- **Range**: the whole score, or *From… to…* (type times like `0:12.5`).
- **Sample rate** 44.1 / 48 / 88.2 / 96 kHz and **format** WAV 16-bit, 24-bit (default) or 32-bit float.
- **Tail**: seconds of sound recorded after the end (delay and reverb tails, release). The automation holds its last values during the tail; a held note is released at the end.
- **Automation update**: how often the parameters are set while rendering (every 16 samples by default, every sample for the finest result). This is independent of the *Live update* setting for playback.
- Muted and solo lanes work as in playback. Before the render the plugin gets a short silent warm-up at the start values, so the file does not begin with a glide from the patch.
- The audio output is silent during the render; afterwards Trajet returns to where you were (patch or chase, playhead).
- The report gives the peak level and warns if a 16- or 24-bit file clipped. If a file comes out silent, the report says so; a log of the last renders is kept in `~/Library/Application Support/Trajet/render.log`.
- The file is written directly under the name you chose (no temporary or hidden files); a cancelled or failed render deletes it.

**Output stage (top bar)**: a level meter, the output gain and the limiter. They are saved with the project and apply to playback *and* render, so what you hear is what you get.
- **Meter**: the upper bar shows the output level with a peak marker; the light on the right turns red when the output went over 0 dBFS and stays red until you click the meter. The lower orange bar shows how much the limiter is turning the sound down.
- **Gain**: −24 to +12 dB after the plugin (double-click for 0 dB).
- **Limit**: a look-ahead peak limiter (5 ms look-ahead, smooth release). No sample goes above the ceiling and there is no clipping distortion. Right-click the button to choose the ceiling (0 to −6 dBFS; default −0.3). The look-ahead delay is compensated in renders, so the file stays sample-aligned with the score.

**File → Export automation as CSV… (⇧⌘R)**, or tick *Also write the automation as CSV* in the render dialog, writes a table: `time_s` and one column per lane, one row every 1–100 ms. Values are either normalised (0 to 1, as Trajet stores them) or in the plugin's own units (the number the plugin shows, without its unit label). Handy for reading the score into Max (`[coll]`, `[text]`) or Python.

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
