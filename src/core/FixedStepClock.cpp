#include "tmac/FixedStepClock.hpp"

#include <cmath>

namespace tmac {

FixedStepClock::FixedStepClock(double framesPerSecond, double maxStepSeconds)
    : fps_(framesPerSecond > 0.0 && std::isfinite(framesPerSecond) ? framesPerSecond : 240.0),
      maxStep_(maxStepSeconds > 0.0 && std::isfinite(maxStepSeconds) ? maxStepSeconds : 0.25) {}

void FixedStepClock::reset() {
    residual_ = 0.0;
    frame_ = 0;
}

uint64_t FixedStepClock::advance(double delta) {
    // Rejects NaN, zero and negative deltas. Infinity is rejected by the isfinite check.
    if (!(delta > 0.0) || !std::isfinite(delta)) return frame_;
    if (delta > maxStep_) delta = maxStep_;

    residual_ += delta * fps_;
    const double whole = std::floor(residual_ + 1e-9);  // tolerance for 1/60-style deltas
    residual_ -= whole;
    if (residual_ < 0.0) residual_ = 0.0;
    frame_ += static_cast<uint64_t>(whole);
    return frame_;
}

}  // namespace tmac
