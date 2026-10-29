#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "tmac/Macro.hpp"

namespace tmac {

// Replays a validated Macro against a stream of game frame numbers.
//
// The caller invokes tick() once per game update with a non-decreasing frame
// counter. The Player does not touch the game itself: it returns the events
// that are due, in order, and the caller dispatches them. Redundant events are
// filtered so TMac never presses an input it is already holding or releases one
// it is not holding.
class Player {
public:
    enum class State { Idle, Playing, Paused, Finished };

    struct Stats {
        uint64_t dispatched = 0;     // events handed back to the caller
        uint64_t skipped = 0;        // redundant events removed
        uint64_t late = 0;           // events dispatched after their due frame
        uint64_t maxLateFrames = 0;  // worst lateness seen, in macro frames
    };

    // Replaces the loaded macro. Returns releases for inputs TMac was holding.
    // If validation fails, the current state is left untouched.
    MacroError load(Macro macro, std::vector<InputEvent>& releases);
    // Starts from the beginning. Returns false if already playing or paused.
    bool play();
    // Releases everything held and returns the releases. Playback position is kept.
    std::vector<InputEvent> pause();
    // Re-presses the inputs that were held when pause() was called.
    std::vector<InputEvent> resume();
    // Ends playback and returns releases for anything still held.
    std::vector<InputEvent> stop();
    // Releases held inputs, then starts again from the first event.
    std::vector<InputEvent> restart();
    // Advances the macro clock to `gameFrame` and returns the events now due.
    std::vector<InputEvent> tick(uint64_t gameFrame);

    State state() const { return state_; }
    uint64_t elapsedFrames() const { return elapsed_; }
    uint64_t durationFrames() const { return macro_.durationFrames; }
    size_t position() const { return index_; }
    size_t eventCount() const { return macro_.inputs.size(); }
    double progress() const;
    const Stats& stats() const { return stats_; }
    bool isHeld(bool player2, Button button) const;
    const Macro& macro() const { return macro_; }

private:
    std::vector<InputEvent> releaseHeld();
    void resetPlayback(bool clearStats);

    Macro macro_;
    State state_ = State::Idle;
    size_t index_ = 0;
    uint64_t elapsed_ = 0;
    uint64_t lastGameFrame_ = 0;
    bool haveLast_ = false;
    bool held_[2][4] = {};
    bool pausedHeld_[2][4] = {};
    Stats stats_;
};

}  // namespace tmac
