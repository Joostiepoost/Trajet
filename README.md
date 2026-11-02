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
| 2 | Projects: new, open, save (with the plugin's patch), undo/redo, menu bar | testing |

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
