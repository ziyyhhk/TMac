// UNVERIFIED Geode integration. See TMacMod.cpp for the scope of this warning.
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
        // UNVERIFIED: Mod::getSaveDir() is assumed to be the per-mod save folder.
        rt.store = std::make_unique<tmac::MacroStore>(geode::Mod::get()->getSaveDir() / "macros");
    }
    return rt.store.get();
}

void dispatchToGame(const std::vector<tmac::InputEvent>& events) {
    if (events.empty()) return;
    PlayLayer* level = PlayLayer::get();
    if (!level || !level->m_player1) return;

    TMacRuntime& rt = tmacRuntime();
    rt.dispatching = true;
    for (const tmac::InputEvent& event : events) {
        if (event.player2) continue;
        const auto button = static_cast<PlayerButton>(static_cast<int>(event.button));
        if (event.down) {
            level->m_player1->pushButton(button);
        } else {
            level->m_player1->releaseButton(button);
        }
    }
    rt.dispatching = false;
}
