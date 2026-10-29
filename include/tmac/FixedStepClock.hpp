#pragma once

#include <cstdint>

namespace tmac {

// Converts variable real-time deltas into whole macro frames at a fixed rate.
// Recording and playback both count these frames, so one input sequence maps to
// the same frame numbers whatever the display's refresh rate.
//
// This fixes TMac's own clock only. Geometry Dash still integrates its physics
// from the deltas it is given, so in-game determinism is NOT guaranteed by this.
class FixedStepClock {
public:
    // maxStepSeconds caps one delta so a single long stall cannot dump a large
    // burst of frames into the macro clock.
    explicit FixedStepClock(double framesPerSecond = 240.0, double maxStepSeconds = 0.25);

    void reset();
    // Adds one real-time delta in seconds and returns the total whole frames so far.
    uint64_t advance(double deltaSeconds);
    uint64_t frame() const { return frame_; }
    double framesPerSecond() const { return fps_; }

private:
    double fps_;
    double maxStep_;
    double residual_ = 0.0;
    uint64_t frame_ = 0;
};

}  // namespace tmac
