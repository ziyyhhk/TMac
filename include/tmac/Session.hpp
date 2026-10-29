#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tmac/Macro.hpp"
#include "tmac/Player.hpp"
#include "tmac/Recorder.hpp"

namespace tmac {

// Game events TMac reacts to. The Geode layer translates game hooks into these.
enum class LevelEvent { Died, Completed, Restarted, Exited, GamePaused, GameResumed };

// Single entry point for the UI and the Geode layer. It owns the recorder, the
// player and the current macro, so no caller keeps a separate copy of that state.
//
// Methods that return input events return the events the caller must dispatch to
// the game immediately. Session never touches the game itself.
class Session {
public:
    // ----- recording
    bool startRecording(uint64_t gameFrame, const MacroMeta& meta);
    bool recordButton(uint64_t gameFrame, bool player2, Button button, bool down);
    // Ends recording and makes the result the current macro, ready for playback.
    bool stopRecording(uint64_t gameFrame);

    // ----- current macro
    // Validates and installs a macro. Refused while recording or playing.
    bool setCurrentMacro(Macro macro);
    // Refused while recording or playing.
    bool clearCurrentMacro();
    bool hasMacro() const { return hasMacro_; }
    const Macro& currentMacro() const { return current_; }

    // ----- playback
    bool play();
    std::vector<InputEvent> pause();
    std::vector<InputEvent> resume();
    std::vector<InputEvent> stop();
    std::vector<InputEvent> restart();
    std::vector<InputEvent> tick(uint64_t gameFrame);

    // Death, completion and exit stop playback. Pause and resume are passed through.
    std::vector<InputEvent> onLevelEvent(LevelEvent event, uint64_t gameFrame);

    // ----- state (always read from the engine)
    bool isRecording() const { return recorder_.state() == Recorder::State::Recording; }
    bool isPlaybackActive() const;
    Recorder::State recorderState() const { return recorder_.state(); }
    Player::State playerState() const { return player_.state(); }
    uint64_t recordingFrames(uint64_t gameFrame) const { return recorder_.framesSinceStart(gameFrame); }
    const Player& player() const { return player_; }
    const Recorder& recorder() const { return recorder_; }

    // Latest user-facing message: why an action was refused, or what happened.
    const std::string& message() const { return message_; }
    void setMessage(std::string text) { message_ = std::move(text); }

private:
    MacroError loadForPlayback(Macro macro);

    Recorder recorder_;
    Player player_;
    Macro current_;
    bool hasMacro_ = false;
    std::string message_;
};

}  // namespace tmac
