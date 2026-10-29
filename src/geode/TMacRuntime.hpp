#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "tmac/MacroStore.hpp"
#include "tmac/Session.hpp"
#include "tmac/ToggleController.hpp"

// Process-wide TMac state. Touched only on the game's main thread.
struct TMacRuntime {
    tmac::Session session;
    tmac::ToggleController toggle;            // TAB window state
    std::unique_ptr<tmac::MacroStore> store;  // created on first use
    bool dispatching = false;                 // true while TMac itself presses or releases buttons
    size_t nextLoadIndex = 0;
};

TMacRuntime& tmacRuntime();

// Returns the macro store, creating it on first use. Null if the save folder is unavailable.
tmac::MacroStore* tmacStore();

// The current macro frame: Geometry Dash's own physics step counter, not a
// wall clock. Returns 0 outside a level.
uint64_t tmacCurrentFrame();

// Presses or releases each event's button through the game layer, so recorded
// and injected input take the same path. Player 2 is rejected outside dual mode.
void dispatchToGame(const std::vector<tmac::InputEvent>& events);
