// =============================================================================
// GEODE HOOKS.
//
// The macro frame number is Geometry Dash's own physics step counter
// (m_gameState.m_currentProgress / 2) rather than a clock built from real-time
// deltas. This is what frame-accurate Geode 2.2 bots index macros by: GD runs
// 240 physics steps per second, m_currentProgress counts half-steps, and it is
// identical for a recording and a replay at the same moment. A clock that
// integrates dt drifts away from the physics it is supposed to describe.
//
// Input capture and injection both go through GJBaseGameLayer::handleButton,
// the function GD itself calls for every press and release, whatever the
// device. PlayerObject::pushButton is only the inner half of that and misses
// bookkeeping the layer does.
//
// Injection happens at the start of each physics step, in
// processQueuedButtons, instead of once per rendered frame. At 60 fps and 240
// physics steps, once-per-frame injection can only place an input 4 steps
// late; per step it lands on the exact step the macro recorded.
//
// Scope limits that remain:
//   * No TPS bypass / frame stepper, so playback is step-accurate but depends
//     on the same physics delta the recording ran with.
//   * The pause-menu button placement is still unverified visually.
// =============================================================================
#include <Geode/Geode.hpp>
#include <Geode/modify/GJBaseGameLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

#include "TMacPanel.hpp"
#include "TMacRuntime.hpp"

using namespace geode::prelude;

namespace {

void notifyLevel(tmac::LevelEvent event) {
    TMacRuntime& rt = tmacRuntime();
    dispatchToGame(rt.session.onLevelEvent(event, tmacCurrentFrame()));
}

}  // namespace

class $modify(TMacPlayLayer, PlayLayer) {
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
        PlayLayer::onQuit();
    }
};

class $modify(TMacBaseGameLayer, GJBaseGameLayer) {
    // Every real press and release in the layer passes through here, for both
    // players and all input devices.
    void handleButton(bool down, int button, bool isPlayer1) {
        TMacRuntime& rt = tmacRuntime();
        if (rt.session.isRecording() && !rt.session.isPlaybackActive()) {
            const bool p2 = !isPlayer1;
            if (isPlayer1 || m_gameState.m_isDualMode) {
                rt.session.recordButton(tmacCurrentFrame(), p2,
                                        static_cast<tmac::Button>(button), down);
            }
        }
        GJBaseGameLayer::handleButton(down, button, isPlayer1);
    }

    // One call per physics step. Deliver the macro events that are due before
    // the step consumes its input queue, so they are felt on this exact step.
    void processQueuedButtons(float dt, bool clearInputQueue) {
        TMacRuntime& rt = tmacRuntime();
        dispatchToGame(rt.session.tick(tmacCurrentFrame()));
        GJBaseGameLayer::processQueuedButtons(dt, clearInputQueue);
    }
};
