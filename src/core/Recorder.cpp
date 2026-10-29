#include "tmac/Recorder.hpp"

namespace tmac {

bool Recorder::start(uint64_t gameFrame, const MacroMeta& meta) {
    if (state_ == State::Recording) return false;
    macro_ = Macro{};
    macro_.meta = meta;
    origin_ = gameFrame;
    for (auto& side : held_) {
        for (bool& h : side) h = false;
    }
    state_ = State::Recording;
    return true;
}

bool Recorder::onButton(uint64_t gameFrame, bool player2, Button button, bool down) {
    if (state_ != State::Recording || gameFrame < origin_ || button == Button::None) return false;

    bool& held = held_[player2 ? 1 : 0][static_cast<size_t>(button)];
    if (held == down) return false;  // duplicate press, or release of an input not held
    if (macro_.inputs.size() + kReservedReleases >= limits::kMaxInputs) return false;

    const InputEvent event{gameFrame - origin_, button, player2, down};
    if (macro_.append(event) != MacroError::Ok) return false;
    held = down;
    return true;
}

bool Recorder::recordDeath(uint64_t gameFrame) {
    if (state_ != State::Recording || gameFrame < origin_) return false;
    const uint64_t relative = gameFrame - origin_;
    if (!macro_.deaths.empty() && relative < macro_.deaths.back()) return false;
    if (macro_.deaths.size() >= limits::kMaxDeaths) return false;
    macro_.deaths.push_back(relative);
    return true;
}

bool Recorder::stop(uint64_t gameFrame) {
    if (state_ != State::Recording || gameFrame < origin_) return false;

    uint64_t relative = gameFrame - origin_;
    if (relative < macro_.lastFrame()) relative = macro_.lastFrame();
    if (!macro_.deaths.empty() && relative < macro_.deaths.back()) relative = macro_.deaths.back();

    for (int side = 0; side < 2; ++side) {
        for (int b = 1; b <= 3; ++b) {
            bool& held = held_[side][b];
            if (!held) continue;
            held = false;
            // Room for these was reserved in onButton(), so append cannot fail.
            macro_.append(InputEvent{relative, static_cast<Button>(b), side == 1, false});
        }
    }
    macro_.durationFrames = relative;
    state_ = State::Stopped;
    return true;
}

bool Recorder::discard() {
    if (state_ == State::Recording) return false;
    macro_ = Macro{};
    state_ = State::Idle;
    return true;
}

uint64_t Recorder::framesSinceStart(uint64_t gameFrame) const {
    if (state_ != State::Recording || gameFrame < origin_) return 0;
    return gameFrame - origin_;
}

}  // namespace tmac
