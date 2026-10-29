#pragma once

#include <cstddef>
#include <memory>
#include <vector>

#include "tmac/FixedStepClock.hpp"
#include "tmac/MacroStore.hpp"
#include "tmac/Session.hpp"

// Process-wide TMac state. Touched only on the game's main thread.
struct TMacRuntime {
    tmac::Session session;
    tmac::FixedStepClock clock{240.0, 0.25};
    std::unique_ptr<tmac::MacroStore> store;  // created on first use
    bool dispatching = false;                 // true while TMac itself presses or releases buttons
    size_t nextLoadIndex = 0;
};

TMacRuntime& tmacRuntime();

// Returns the macro store, creating it on first use. Null if the save folder is unavailable.
tmac::MacroStore* tmacStore();

// Presses or releases player 1's buttons for each event. Player 2 is not wired yet.
void dispatchToGame(const std::vector<tmac::InputEvent>& events);
