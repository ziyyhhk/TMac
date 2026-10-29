#include "TMacRuntime.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

TMacRuntime& tmacRuntime() {
    static TMacRuntime runtime;
    return runtime;
}

tmac::MacroStore* tmacStore() {
    TMacRuntime& rt = tmacRuntime();
    if (!rt.store) {
        rt.store = std::make_unique<tmac::MacroStore>(geode::Mod::get()->getSaveDir() / "macros");
    }
    return rt.store.get();
}

uint64_t tmacCurrentFrame() {
    PlayLayer* level = PlayLayer::get();
    if (!level) return 0;
    // m_currentProgress counts half physics steps; the macro is indexed in
    // whole 240 Hz steps, which is the convention the 2.2 bots use.
    return static_cast<uint64_t>(level->m_gameState.m_currentProgress / 2);
}

void dispatchToGame(const std::vector<tmac::InputEvent>& events) {
    if (events.empty()) return;
    PlayLayer* level = PlayLayer::get();
    if (!level || !level->m_player1) return;

    TMacRuntime& rt = tmacRuntime();
    const bool dual = level->m_gameState.m_isDualMode;
    rt.dispatching = true;
    for (const tmac::InputEvent& event : events) {
        // A single-player run cannot drive player 2; drop those events rather
        // than pretending they were applied.
        if (event.player2 && !dual) continue;
        const int button = static_cast<int>(event.button);
        // Qualified call: dispatch exactly what the game does, regardless of any
        // other mod that also hooks handleButton.
        level->GJBaseGameLayer::handleButton(event.down, button, !event.player2);
    }
    rt.dispatching = false;
}
