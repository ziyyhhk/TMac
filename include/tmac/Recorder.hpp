#pragma once

#include <cstddef>
#include <cstdint>

#include "tmac/Macro.hpp"

namespace tmac {

// Turns raw button transitions into a Macro. Macro frames are relative to the
// game frame passed to start(), so the first frame of every recording is 0.
class Recorder {
public:
    enum class State { Idle, Recording, Stopped };

    // Returns false if a recording is already in progress.
    bool start(uint64_t gameFrame, const MacroMeta& meta);
    // Returns false (and records nothing) for duplicate presses, releases of
    // inputs that are not held, out-of-order frames, invalid buttons, or when
    // not recording.
    bool onButton(uint64_t gameFrame, bool player2, Button button, bool down);
    // Records a death at this frame. Deaths are stored in the macro.
    bool recordDeath(uint64_t gameFrame);
    // Ends the recording. Inputs still held are released at this frame so that
    // nothing stays pressed in the saved macro.
    bool stop(uint64_t gameFrame);
    // Drops the current macro. Not allowed while recording.
    bool discard();
    // Frames elapsed since start() at `gameFrame`. Zero when not recording.
    uint64_t framesSinceStart(uint64_t gameFrame) const;

    State state() const { return state_; }
    const Macro& macro() const { return macro_; }

private:
    // Room kept free so that stop() can always append its releases.
    static constexpr size_t kReservedReleases = 6;

    State state_ = State::Idle;
    Macro macro_;
    uint64_t origin_ = 0;
    bool held_[2][4] = {};  // [player2][button]
};

}  // namespace tmac
