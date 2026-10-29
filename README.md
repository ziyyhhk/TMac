# TMac

A Geometry Dash macro mod for the Geode loader, targeting Geometry Dash **2.2081** and Geode **5.11.1**.

Press **TAB** in a level to open or close the TMac window.

## Status

| Area | Status |
|---|---|
| Macro engine: recorder, playback scheduler, session | Implemented. Unit tested. |
| TAB toggle rules (`ToggleController`) | Implemented. Unit tested. |
| TAB key binding and window in Geometry Dash | Written against the documented Geode keybind API. **Not compiled or run in the game.** |
| Build configuration | Follows the official Geode example CMake. The mod target links with the plain `target_link_libraries` signature that `setup_geode_mod()` uses. CMake configuration is confirmed by your CI log, not by a local run. |
| GitHub Actions build | Uses the official `geode-sdk/build-geode-mod` action. **Not yet run.** |
| `.gdr2` read and write | Implemented and unit tested. Compatibility with real bot files is **not verified**. |
| `.gdr` (legacy v1) | **Not supported.** Its layout is undocumented, so TMac refuses these files. |
| Playback accuracy in Geometry Dash | **Unverified.** |

## Build the core and run its tests

No Geode SDK is needed.

```sh
cmake -S . -B build -DTMAC_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build --output-on-failure -C Release
```

## Build the Geode mod

The mod target is configured only when `GEODE_SDK` is set. Set it to a Geode SDK installed by the official tooling, not a hand-made clone. Then configure and build:

```sh
cmake -S . -B build
cmake --build build --config RelWithDebInfo
```

In GitHub Actions the official `geode-sdk/build-geode-mod` action installs the SDK and sets `GEODE_SDK`. See `.github/workflows/build.yml`.

If CMake reports that `GEODE_SDK` has no `CMakeLists.txt`, the variable points at the wrong folder.

## Using TAB

- Press TAB inside a level to open the window. Press TAB again to close it.
- Holding TAB does not toggle repeatedly.
- The window can only be opened inside a level, because recording and playback need one.
- Closing the window with its own button keeps the TAB state in sync.
- Closing the level closes the window and keeps any recording that was in progress. That recording is stopped and kept.
- The window has no text fields, so TAB never competes with typing.

## How the pieces fit

- `Session` owns the recorder, the player and the current macro. The UI and the game hooks call only `Session`.
- `Session` returns input events. The Geode layer dispatches them to the game.
- `FixedStepClock` turns frame deltas into whole macro frames.
- `buildPanelView` turns the session into plain data for the window.
- `ToggleController` decides what each TAB press means.
- `MacroStore` keeps macro files in one folder. It sanitises names, refuses traversal and overwrites, and writes through a temporary file.

## `.gdr2` format notes

The codec follows the GDR2 description in the maxnut/GDReplayFormat README. These details are **not confirmed** against real files:

- The format version written and accepted (`kFormatVersion`).
- The continuation encoding of multi-byte packed frame deltas.
- Frame deltas are relative to the previous event in the same player's list.
- Same-frame events from different players are reordered on import (P1 first).
- The framerate field is treated as an integer.

A replay with a non-empty input tag is refused.

## Known limitations

- The first build of this version still needs compilation to confirm the Geode layer. The Geode calls were checked against the Geode documentation, not a compiled SDK.

- Geometry Dash's physics uses its own frame deltas. Playback timing can drift until a fixed physics step is wired in.
- Player 2 is not recorded or played back.
- There is no name entry. Saves use generated names (`Macro`, `Macro (2)`, ...).
- Load cycles through saved macros in order. There is no file picker or list view.
- Overwrite confirmation is not implemented. Saves never overwrite.
- Settings other than the TAB key are not implemented.
- The Geode layer has not been compiled. Expect small fixes against the real headers on the first build.
