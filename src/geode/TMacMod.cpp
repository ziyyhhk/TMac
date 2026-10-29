// =============================================================================
// GEODE HOOKS: NOT COMPILED OR RUN IN THIS ENVIRONMENT.
// Hook targets and member names come from memory of the Geode and Geometry Dash
// 2.2081 APIs. Check each against the Geode v5.11.1 headers before relying on it.
// Known scope limits:
//   * Player 1 only.
//   * Clock is driven by PlayLayer::update deltas through FixedStepClock.
//     Geometry Dash's own physics still uses the raw deltas, so in-game timing
//     accuracy is NOT verified.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/modify/PlayLayer.hpp>
#include <Geode/modify/PlayerObject.hpp>

#include "TMacPanel.hpp"
#include "TMacRuntime.hpp"

using namespace geode::prelude;

namespace {

void notifyLevel(tmac::LevelEvent event) {
    TMacRuntime& rt = tmacRuntime();
    dispatchToGame(rt.session.onLevelEvent(event, rt.clock.frame()));
}

// Records a button transition made by the player. TMac's own dispatches are
// excluded, so playback never records itself.
void noteTransition(PlayerObject* player, PlayerButton button, bool down) {
    TMacRuntime& rt = tmacRuntime();
    if (rt.dispatching || !rt.session.isRecording()) return;
    PlayLayer* level = PlayLayer::get();
    if (!level || player != level->m_player1) return;
    rt.session.recordButton(rt.clock.frame(), false, static_cast<tmac::Button>(static_cast<int>(button)), down);
}

}  // namespace

class $modify(TMacPlayLayer, PlayLayer) {
    void update(float dt) {
        PlayLayer::update(dt);
        TMacRuntime& rt = tmacRuntime();
        const uint64_t frame = rt.clock.advance(dt);
        dispatchToGame(rt.session.tick(frame));
    }

    void destroyPlayer(PlayerObject* player, GameObject* object) {
        if (player == m_player1) notifyLevel(tmac::LevelEvent::Died);
        PlayLayer::destroyPlayer(player, object);
    }

    void levelComplete() {
        notifyLevel(tmac::LevelEvent::Completed);
        PlayLayer::levelComplete();
    }

    void resetLevel() {
        notifyLevel(tmac::LevelEvent::Restarted);
        PlayLayer::resetLevel();
    }

    void onQuit() {
        notifyLevel(tmac::LevelEvent::Exited);
        tmacCloseOpenPanel();
        tmacRuntime().clock.reset();
        PlayLayer::onQuit();
    }
};

class $modify(TMacPlayerObject, PlayerObject) {
    bool pushButton(PlayerButton button) {
        noteTransition(this, button, true);
        return PlayerObject::pushButton(button);
    }

    bool releaseButton(PlayerButton button) {
        noteTransition(this, button, false);
        return PlayerObject::releaseButton(button);
    }
};
