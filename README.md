# TMac

A Geometry Dash macro mod for the Geode loader, targeting Geometry Dash **2.2081** and Geode **5.11.1**.

## Status

Read this before relying on anything below. Only the items marked "tested" were run.

| Area | Status |
|---|---|
| Macro engine: recorder, playback scheduler, session | Implemented. Covered by unit tests (tested). |
| Fixed-step macro clock (`FixedStepClock`) | Implemented and tested. It fixes TMac's clock, not Geometry Dash's physics. |
| `.gdr2` read and write | Implemented and tested. Compatibility with real bot files is **not verified**. |
| `.gdr` (legacy v1) | **Not supported.** The layout is undocumented, so TMac refuses these files with a message. |
| Macro store (save, load, list, rename, delete, import) | Implemented and tested. Names are sanitised and kept inside one folder. |
| UI state model (`buildPanelView`) | Implemented and tested. Every enabled flag and label comes from the engine. |
| Geode hooks and popup (`src/geode/`) | **Written but not compiled.** Checked only against stub headers. The real Geode API is unverified. |
| Playback accuracy in Geometry Dash | **Unverified.** |
| In-game testing | **Not performed.** |

## Build and test the core

No Geode SDK is needed for the core.

```sh
cmake -S . -B build -DTMAC_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure
```

Without CMake, `g++ -std=c++20 -Iinclude src/core/*.cpp tests/test_core.cpp tests/test_phase2.cpp` builds the same tests.

## Build the Geode mod

1. Install Geode SDK v5.11.1 and set `GEODE_SDK` to its folder.
2. Configure and build. CMake adds the mod target only when `GEODE_SDK` is set.

## How the pieces fit

- `Session` owns the recorder, the player and the current macro. The UI and the game hooks call only `Session`.
- `Session` returns input events. The Geode layer dispatches them to the game, so the core never touches game objects.
- `FixedStepClock` turns frame deltas into whole macro frames, so a macro maps to the same frame numbers at any refresh rate.
- `buildPanelView` turns the session into plain data (status, labels, enabled buttons). The popup renders that data and keeps no state of its own.
- `MacroStore` keeps macro files in one folder. It sanitises names, refuses traversal and overwrites, and writes through a temporary file.

## Behaviour

The game-side items below depend on Geode hooks that have not been compiled or run yet.

- Recording captures button presses and releases, and deaths. Inputs still held when recording stops are released at that frame.
- Playback replays events in order. Redundant events are skipped so TMac never double-presses or releases an input it is not holding.
- Pausing releases held inputs. Resuming presses them again.
- Death, level completion, restart and exit stop playback and release held inputs.
- Opening the pause menu pauses playback. Closing it resumes playback.
- Recording and playback cannot run at the same time, and loading is refused while either is active.

## `.gdr2` format notes

The codec follows the GDR2 description in the maxnut/GDReplayFormat README. These details are **not confirmed** against real files:

- The format version written and accepted (`kFormatVersion`).
- The continuation encoding of multi-byte packed frame deltas.
- Frame deltas are relative to the previous event in the same player's list.
- Same-frame events from different players are reordered on import (P1 first).
- The framerate field is treated as an integer.

A replay with a non-empty input tag is refused because its per-input extensions cannot be read safely.

## Known limitations

- Geometry Dash's physics still uses its own deltas. Timing can drift until a fixed physics step is wired in.
- Player 2 is not recorded or played back.
- There is no name entry. Saves use generated names (`Macro`, `Macro (2)`, ...).
- Load cycles through saved macros in order. There is no file picker or list view.
- Overwrite confirmation is not implemented. Saves never overwrite.
- Settings are not implemented.
- The Geode layer has not been compiled. Expect to fix hook signatures and popup calls against the real headers on the first build.
