#include "tmac/PanelView.hpp"

#include <cstdio>

namespace tmac {

std::string formatFrames(uint64_t frames, double framesPerSecond) {
    if (!(framesPerSecond > 0.0)) return "--:--.---";
    const auto ms = static_cast<unsigned long long>(
        static_cast<double>(frames) * 1000.0 / framesPerSecond + 0.5);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%02llu:%02llu.%03llu", ms / 60000ULL,
                  (ms / 1000ULL) % 60ULL, ms % 1000ULL);
    return buffer;
}

PanelView buildPanelView(const Session& session, uint64_t nowGameFrame, bool saved) {
    PanelView v;
    const bool recording = session.isRecording();
    const bool active = session.isPlaybackActive();
    const Player::State playerState = session.playerState();
    const bool paused = playerState == Player::State::Paused;
    const bool finished = playerState == Player::State::Finished;
    const bool hasMacro = session.hasMacro();

    v.recordingActive = recording;
    v.playbackActive = active;
    v.paused = paused;
    v.statusDetail = session.message();

    if (recording) {
        const double fps = static_cast<double>(session.recorder().macro().meta.framerate);
        v.status = "Recording";
        v.elapsed = formatFrames(session.recordingFrames(nowGameFrame), fps);
        v.duration = "--:--.---";
        v.eventCount = session.recorder().macro().inputs.size();
    } else if (active || finished) {
        const Player& player = session.player();
        const double fps = static_cast<double>(player.macro().meta.framerate);
        v.status = active ? (paused ? "Paused" : "Playing") : "Completed";
        v.elapsed = formatFrames(player.elapsedFrames(), fps);
        v.duration = formatFrames(player.durationFrames(), fps);
        v.progress = player.progress();
        v.eventCount = player.eventCount();
    } else if (hasMacro) {
        const double fps = static_cast<double>(session.currentMacro().meta.framerate);
        v.status = "Ready";
        v.elapsed = formatFrames(0, fps);
        v.duration = formatFrames(session.currentMacro().durationFrames, fps);
        v.eventCount = session.currentMacro().inputs.size();
    } else {
        v.status = "Idle";
        v.elapsed = formatFrames(0, 240.0);
        v.duration = formatFrames(0, 240.0);
    }

    const bool idle = !recording && !active;
    v.canRecord = idle;
    v.canStopRecording = recording;
    v.canPlay = hasMacro && idle;
    v.canPause = playerState == Player::State::Playing;
    v.canResume = paused;
    v.canStopPlayback = active;
    v.canRestart = hasMacro && !recording;
    v.canLoad = idle;
    v.canSave = hasMacro && !saved && !recording;
    v.canClear = hasMacro && idle;
    return v;
}

}  // namespace tmac
