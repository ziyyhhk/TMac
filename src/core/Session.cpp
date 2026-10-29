#include "tmac/Session.hpp"

#include <utility>

namespace tmac {

namespace {

const char* stoppedReason(LevelEvent event) {
    switch (event) {
        case LevelEvent::Died: return "Playback stopped: the player died.";
        case LevelEvent::Completed: return "Playback stopped: the level was completed.";
        case LevelEvent::Restarted: return "Playback stopped: the level was restarted.";
        case LevelEvent::Exited: return "Playback stopped: the level was closed.";
        default: return "Playback stopped.";
    }
}

}  // namespace

bool Session::isPlaybackActive() const {
    return player_.state() == Player::State::Playing || player_.state() == Player::State::Paused;
}

// ----- recording

bool Session::startRecording(uint64_t gameFrame, const MacroMeta& meta) {
    if (isPlaybackActive()) {
        message_ = "Stop playback before recording.";
        return false;
    }
    if (!recorder_.start(gameFrame, meta)) {
        message_ = "A recording is already in progress.";
        return false;
    }
    message_ = "Recording started.";
    return true;
}

bool Session::recordButton(uint64_t gameFrame, bool player2, Button button, bool down) {
    // Rejected transitions (duplicates, invalid buttons) are expected and stay silent.
    return recorder_.onButton(gameFrame, player2, button, down);
}

bool Session::stopRecording(uint64_t gameFrame) {
    if (!recorder_.stop(gameFrame)) {
        message_ = "No recording is in progress.";
        return false;
    }
    const MacroError error = loadForPlayback(recorder_.macro());
    if (error != MacroError::Ok) {
        message_ = std::string("Recording could not be used: ") + describe(error);
        return false;
    }
    message_ = "Recording stopped: " + std::to_string(current_.inputs.size()) + " inputs captured.";
    return true;
}

// ----- current macro

bool Session::setCurrentMacro(Macro macro) {
    if (isRecording()) {
        message_ = "Stop recording before loading a macro.";
        return false;
    }
    if (isPlaybackActive()) {
        message_ = "Stop playback before loading a macro.";
        return false;
    }
    const MacroError error = loadForPlayback(std::move(macro));
    if (error != MacroError::Ok) {
        // The previous macro stays in place.
        message_ = std::string("Macro rejected: ") + describe(error);
        return false;
    }
    message_ = "Macro loaded: " + std::to_string(current_.inputs.size()) + " inputs.";
    return true;
}

bool Session::clearCurrentMacro() {
    if (isRecording() || isPlaybackActive()) {
        message_ = "Stop the current operation first.";
        return false;
    }
    current_ = Macro{};
    hasMacro_ = false;
    player_.stop();
    message_ = "Macro cleared.";
    return true;
}

MacroError Session::loadForPlayback(Macro macro) {
    std::vector<InputEvent> releases;  // empty: callers only load while playback is idle
    const MacroError error = player_.load(std::move(macro), releases);
    if (error != MacroError::Ok) return error;
    current_ = player_.macro();
    hasMacro_ = true;
    return MacroError::Ok;
}

// ----- playback

bool Session::play() {
    if (!hasMacro_) {
        message_ = "No macro is loaded.";
        return false;
    }
    if (isRecording()) {
        message_ = "Stop recording before playing.";
        return false;
    }
    if (!player_.play()) {
        message_ = "Playback is already running.";
        return false;
    }
    message_ = "Playback started.";
    return true;
}

std::vector<InputEvent> Session::pause() {
    if (player_.state() != Player::State::Playing) {
        message_ = "Nothing is playing.";
        return {};
    }
    message_ = "Playback paused.";
    return player_.pause();
}

std::vector<InputEvent> Session::resume() {
    if (player_.state() != Player::State::Paused) {
        message_ = "Playback is not paused.";
        return {};
    }
    message_ = "Playback resumed.";
    return player_.resume();
}

std::vector<InputEvent> Session::stop() {
    if (!isPlaybackActive()) {
        message_ = "Nothing is playing.";
        return {};
    }
    message_ = "Playback stopped.";
    return player_.stop();
}

std::vector<InputEvent> Session::restart() {
    if (!hasMacro_) {
        message_ = "No macro is loaded.";
        return {};
    }
    if (isRecording()) {
        message_ = "Stop recording before playing.";
        return {};
    }
    message_ = "Playback restarted.";
    return player_.restart();
}

std::vector<InputEvent> Session::tick(uint64_t gameFrame) {
    const Player::State before = player_.state();
    std::vector<InputEvent> out = player_.tick(gameFrame);
    if (before == Player::State::Playing && player_.state() == Player::State::Finished) {
        message_ = "Playback completed.";
    }
    return out;
}

std::vector<InputEvent> Session::onLevelEvent(LevelEvent event, uint64_t gameFrame) {
    switch (event) {
        case LevelEvent::GamePaused:
            if (player_.state() == Player::State::Playing) return pause();
            return {};
        case LevelEvent::GameResumed:
            if (player_.state() == Player::State::Paused) return resume();
            return {};
        case LevelEvent::Died:
            recorder_.recordDeath(gameFrame);  // deaths are part of the recording
            break;
        case LevelEvent::Exited:
            if (isRecording()) {
                // Keep what was captured. Leaving it recording would strand the
                // recorder once the game's frame counter resets.
                stopRecording(gameFrame);
                message_ = "Recording stopped because the level was closed. The recording was kept.";
            }
            break;
        default:
            break;
    }
    if (!isPlaybackActive()) return {};
    message_ = stoppedReason(event);
    return player_.stop();
}

}  // namespace tmac
