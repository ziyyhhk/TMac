#include "tmac/Player.hpp"

#include <utility>

namespace tmac {

MacroError Player::load(Macro macro, std::vector<InputEvent>& releases) {
    releases.clear();
    const MacroError error = macro.validate();
    if (error != MacroError::Ok) return error;  // keep the current macro untouched
    releases = stop();
    macro_ = std::move(macro);
    resetPlayback(true);
    return MacroError::Ok;
}

bool Player::play() {
    if (state_ == State::Playing || state_ == State::Paused) return false;
    resetPlayback(true);
    state_ = State::Playing;
    return true;
}

std::vector<InputEvent> Player::pause() {
    if (state_ != State::Playing) return {};
    for (int s = 0; s < 2; ++s) {
        for (int b = 0; b < 4; ++b) pausedHeld_[s][b] = held_[s][b];
    }
    std::vector<InputEvent> releases = releaseHeld();
    state_ = State::Paused;
    haveLast_ = false;
    return releases;
}

std::vector<InputEvent> Player::resume() {
    if (state_ != State::Paused) return {};
    std::vector<InputEvent> presses;
    for (int s = 0; s < 2; ++s) {
        for (int b = 1; b <= 3; ++b) {
            if (!pausedHeld_[s][b]) continue;
            pausedHeld_[s][b] = false;
            held_[s][b] = true;
            presses.push_back(InputEvent{elapsed_, static_cast<Button>(b), s == 1, true});
        }
    }
    state_ = State::Playing;
    haveLast_ = false;
    return presses;
}

std::vector<InputEvent> Player::stop() {
    if (state_ == State::Idle) return {};
    std::vector<InputEvent> releases = releaseHeld();
    resetPlayback(false);
    state_ = State::Idle;
    return releases;
}

std::vector<InputEvent> Player::restart() {
    std::vector<InputEvent> releases = releaseHeld();
    resetPlayback(true);
    state_ = State::Playing;
    return releases;
}

std::vector<InputEvent> Player::tick(uint64_t gameFrame) {
    std::vector<InputEvent> out;
    if (state_ != State::Playing) return out;

    // The first tick after play/resume only establishes the baseline. Macro time
    // then advances by the game-frame delta, so a frame spike advances the
    // clock and dispatches every event that became due, in order.
    if (!haveLast_) {
        lastGameFrame_ = gameFrame;
        haveLast_ = true;
    }
    if (gameFrame < lastGameFrame_) {
        // The game's frame counter went backwards, for example after a clock reset.
        // Rebase now. Otherwise macro time would stall until the old counter was passed.
        lastGameFrame_ = gameFrame;
    } else if (gameFrame > lastGameFrame_) {
        elapsed_ += gameFrame - lastGameFrame_;
        lastGameFrame_ = gameFrame;
    }

    const auto& events = macro_.inputs;
    while (index_ < events.size() && events[index_].frame <= elapsed_) {
        const InputEvent& event = events[index_++];
        const uint64_t late = elapsed_ - event.frame;
        if (late > 0) {
            ++stats_.late;
            if (late > stats_.maxLateFrames) stats_.maxLateFrames = late;
        }
        bool& held = held_[event.player2 ? 1 : 0][static_cast<size_t>(event.button)];
        if (held == event.down) {  // redundant: would double-press or release nothing
            ++stats_.skipped;
            continue;
        }
        held = event.down;
        out.push_back(event);
        ++stats_.dispatched;
    }

    if (index_ >= events.size() && elapsed_ >= macro_.durationFrames) {
        std::vector<InputEvent> releases = releaseHeld();
        out.insert(out.end(), releases.begin(), releases.end());
        state_ = State::Finished;
    }
    return out;
}

double Player::progress() const {
    if (state_ == State::Finished) return 1.0;
    const uint64_t duration = macro_.durationFrames;
    if (duration == 0) return 0.0;
    const double ratio = static_cast<double>(elapsed_) / static_cast<double>(duration);
    return ratio > 1.0 ? 1.0 : ratio;
}

bool Player::isHeld(bool player2, Button button) const {
    return held_[player2 ? 1 : 0][static_cast<size_t>(button)];
}

std::vector<InputEvent> Player::releaseHeld() {
    std::vector<InputEvent> releases;
    for (int s = 0; s < 2; ++s) {
        for (int b = 1; b <= 3; ++b) {
            if (!held_[s][b]) continue;
            held_[s][b] = false;
            releases.push_back(InputEvent{elapsed_, static_cast<Button>(b), s == 1, false});
        }
    }
    return releases;
}

void Player::resetPlayback(bool clearStats) {
    index_ = 0;
    elapsed_ = 0;
    lastGameFrame_ = 0;
    haveLast_ = false;
    for (auto& side : pausedHeld_) {
        for (bool& h : side) h = false;
    }
    if (clearStats) stats_ = Stats{};
}

}  // namespace tmac
