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
| 0 | Toolchain: blank window | done when it opens on your Mac |
