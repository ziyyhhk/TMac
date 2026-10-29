#pragma once

#include <cstdint>
#include <string>

#include "tmac/Session.hpp"

namespace tmac {

// Plain data the UI renders. Every enabled flag and label is derived from the
// Session, so the window cannot show a state the engine is not in.
struct PanelView {
    std::string status;        // Idle, Ready, Recording, Playing, Paused, Completed
    std::string statusDetail;  // Session::message()
    std::string elapsed;       // mm:ss.mmm
    std::string duration;      // mm:ss.mmm, or a placeholder while recording
    double progress = 0.0;     // 0..1, playback only
    uint64_t eventCount = 0;

    bool recordingActive = false;
    bool playbackActive = false;
    bool paused = false;

    bool canRecord = false;
    bool canStopRecording = false;
    bool canPlay = false;
    bool canPause = false;
    bool canResume = false;
    bool canStopPlayback = false;
    bool canRestart = false;
    bool canLoad = false;
    bool canSave = false;
    bool canClear = false;
};

// Formats a frame count as mm:ss.mmm at `framesPerSecond`.
std::string formatFrames(uint64_t frames, double framesPerSecond);

// `saved` tells whether the current macro has been written to disk. The UI
// tracks that, since the engine does not know about files.
PanelView buildPanelView(const Session& session, uint64_t nowGameFrame, bool saved);

}  // namespace tmac
