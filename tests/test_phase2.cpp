// Tests for the Phase 2 core: fixed-step clock, Session, MacroStore, PanelView.
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <fstream>
#include <string>
#include <vector>

#include "tmac/FixedStepClock.hpp"
#include "tmac/Gdr2Codec.hpp"
#include "tmac/Macro.hpp"
#include "tmac/MacroStore.hpp"
#include "tmac/PanelView.hpp"
#include "tmac/Session.hpp"
#include "tmac/ToggleController.hpp"

namespace tmac_phase2 {
namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                              \
    do {                                                                         \
        ++g_checks;                                                              \
        if (!(cond)) {                                                           \
            ++g_failures;                                                        \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        }                                                                        \
    } while (0)

using namespace tmac;
namespace fs = std::filesystem;

void testClock() {
    FixedStepClock c60(240.0);
    for (int i = 0; i < 60; ++i) c60.advance(1.0 / 60.0);
    CHECK(c60.frame() == 240);  // one second at 60 Hz -> 240 frames

    FixedStepClock c144(240.0);
    for (int i = 0; i < 144; ++i) c144.advance(1.0 / 144.0);
    CHECK(c144.frame() == 240);  // same one second at 144 Hz

    FixedStepClock mixed(240.0, 1.0);
    double total = 0.0;
    const double deltas[] = {0.0037, 0.0161, 0.0009, 0.0250, 0.0100, 0.0071};
    for (int i = 0; i < 1000; ++i) {
        const double d = deltas[i % 6];
        total += d;
        mixed.advance(d);
    }
    const auto expected = static_cast<uint64_t>(total * 240.0);
    CHECK(mixed.frame() + 1 >= expected && mixed.frame() <= expected + 1);

    FixedStepClock guard(240.0, 0.25);
    guard.advance(10.0);  // a 10 s stall is capped to 0.25 s
    CHECK(guard.frame() == 60);
    guard.advance(-1.0);
    guard.advance(0.0);
    guard.advance(std::nan(""));
    guard.advance(std::numeric_limits<double>::infinity());
    CHECK(guard.frame() == 60);
}

void testFormatFrames() {
    CHECK(formatFrames(0, 240.0) == "00:00.000");
    CHECK(formatFrames(240, 240.0) == "00:01.000");
    CHECK(formatFrames(240 * 61 + 120, 240.0) == "01:01.500");
    CHECK(formatFrames(10, 0.0) == "--:--.---");
}

MacroMeta meta() {
    MacroMeta m;
    m.author = "tester";
    return m;
}

void recordSample(Session& s) {
    CHECK(s.startRecording(1000, meta()));
    CHECK(s.recordButton(1000, false, Button::Jump, true));
    CHECK(s.recordButton(1010, false, Button::Jump, false));
    CHECK(s.onLevelEvent(LevelEvent::Died, 1015).empty());  // death while recording
    CHECK(s.recordButton(1020, false, Button::Jump, true));  // left held
    CHECK(s.stopRecording(1040));                            // stop releases it
}

void testSessionRecordingFlow() {
    Session s;
    CHECK(!s.hasMacro());
    CHECK(!s.play());                             // nothing to play
    recordSample(s);
    CHECK(s.recorderState() == Recorder::State::Stopped);
    CHECK(s.hasMacro());
    const Macro& m = s.currentMacro();
    CHECK(m.inputs.size() == 4);                  // press, release, press, auto release
    CHECK(m.inputs.back().frame == 40 && !m.inputs.back().down);
    CHECK(m.deaths.size() == 1 && m.deaths[0] == 15);  // death is stored in the macro
    CHECK(m.durationFrames == 40);
    CHECK(m.validate() == MacroError::Ok);
}

void testSessionRefusals() {
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(!s.startRecording(5, meta()));          // already recording
    CHECK(!s.play());                             // cannot play while recording
    CHECK(!s.setCurrentMacro(Macro{}));           // cannot load while recording
    CHECK(s.stopRecording(10));
    CHECK(s.play());
    CHECK(!s.startRecording(20, meta()));         // cannot record while playing
    CHECK(!s.setCurrentMacro(Macro{}));           // cannot load while playing
    CHECK(!s.clearCurrentMacro());
    s.stop();
}

void testSessionPlaybackAndLevelEvents() {
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(s.recordButton(0, false, Button::Jump, true));
    CHECK(s.recordButton(50, false, Button::Jump, false));
    CHECK(s.stopRecording(60));

    CHECK(s.play());
    auto out = s.tick(100);                       // baseline, press at macro 0
    CHECK(out.size() == 1 && out[0].down);
    CHECK(s.isPlaybackActive());

    out = s.onLevelEvent(LevelEvent::GamePaused, 110);
    CHECK(out.size() == 1 && !out[0].down);      // pausing releases held input
    CHECK(s.playerState() == Player::State::Paused);
    out = s.onLevelEvent(LevelEvent::GameResumed, 120);
    CHECK(out.size() == 1 && out[0].down);       // resuming re-presses it
    CHECK(s.playerState() == Player::State::Playing);

    out = s.onLevelEvent(LevelEvent::Died, 130);  // death stops playback and releases input
    CHECK(out.size() == 1 && !out[0].down);
    CHECK(s.playerState() == Player::State::Idle);
    CHECK(!s.isPlaybackActive());
    CHECK(s.message() == "Playback stopped: the player died.");
    CHECK(!s.player().isHeld(false, Button::Jump));
    CHECK(s.hasMacro());                          // the macro itself survives
}

void testSessionCompletionMessage() {
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(s.recordButton(0, false, Button::Jump, true));
    CHECK(s.recordButton(5, false, Button::Jump, false));
    CHECK(s.stopRecording(10));
    CHECK(s.play());
    s.tick(0);
    s.tick(20);                                   // macro time passes the end
    CHECK(s.playerState() == Player::State::Finished);
    CHECK(s.message() == "Playback completed.");
    CHECK(!s.isPlaybackActive());
}

void testSetCurrentMacroKeepsPreviousOnFailure() {
    Session s;
    Macro good;
    good.durationFrames = 5;
    CHECK(s.setCurrentMacro(good));

    Macro bad;
    bad.durationFrames = 1;
    bad.inputs.push_back(InputEvent{9, Button::Jump, false, true});  // beyond duration
    CHECK(!s.setCurrentMacro(bad));
    CHECK(s.hasMacro());
    CHECK(s.currentMacro().durationFrames == 5);  // previous macro intact
    CHECK(s.message().rfind("Macro rejected", 0) == 0);
}

void testPanelIdle() {
    Session s;
    PanelView v = buildPanelView(s, 0, false);
    CHECK(v.status == "Idle");
    CHECK(v.canRecord && !v.canPlay && !v.canSave && !v.canStopRecording);
    CHECK(!v.canPause && !v.canResume && !v.canStopPlayback && !v.canRestart);
}

void testPanelRecordingAndPlayback() {
    Session s;
    CHECK(s.startRecording(100, meta()));
    PanelView rec = buildPanelView(s, 340, false);
    CHECK(rec.status == "Recording" && rec.recordingActive);
    CHECK(rec.elapsed == "00:01.000");           // 240 frames at 240 fps
    CHECK(!rec.canRecord && !rec.canPlay && !rec.canLoad);
    CHECK(rec.canStopRecording);
    CHECK(s.recordButton(100, false, Button::Jump, true));
    CHECK(s.stopRecording(160));

    PanelView ready = buildPanelView(s, 0, false);
    CHECK(ready.status == "Ready" && ready.canPlay && ready.canSave && ready.canRestart);
    CHECK(ready.canClear && ready.canLoad);

    CHECK(s.play());
    s.tick(500);
    PanelView playing = buildPanelView(s, 0, false);
    CHECK(playing.status == "Playing" && playing.canPause && playing.canStopPlayback);
    CHECK(!playing.canPlay && !playing.canResume && !playing.canLoad);

    s.onLevelEvent(LevelEvent::GamePaused, 501);
    PanelView paused = buildPanelView(s, 0, false);
    CHECK(paused.status == "Paused" && paused.canResume && !paused.canPause);

    s.stop();
    CHECK(buildPanelView(s, 0, true).canSave == false);  // saved flag respected
}

void testPanelCompleted() {
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(s.stopRecording(10));
    CHECK(s.play());
    s.tick(0);
    s.tick(100);
    PanelView v = buildPanelView(s, 0, true);
    CHECK(v.status == "Completed");
    CHECK(v.progress == 1.0);
    CHECK(v.canPlay);                            // can replay after completion
}

// ----- MacroStore

fs::path freshDir(const std::string& name) {
    fs::path dir = fs::temp_directory_path() / ("tmac_phase2_" + name);
    std::error_code ec;
    fs::remove_all(dir, ec);
    return dir;
}

Macro simpleMacro() {
    Macro m;
    m.meta.author = "store test";
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{10, Button::Jump, false, false});
    m.durationFrames = 20;
    return m;
}

void testSanitizeName() {
    const std::string traversal = MacroStore::sanitizeName("../../etc/passwd");
    CHECK(traversal.find('/') == std::string::npos);
    CHECK(traversal.find('.') == std::string::npos);
    CHECK(MacroStore::sanitizeName("") == "macro");
    CHECK(MacroStore::sanitizeName("   ") == "macro");
    CHECK(MacroStore::sanitizeName("CON") == "_CON");
    CHECK(MacroStore::sanitizeName("com1") == "_com1");
    CHECK(MacroStore::sanitizeName("My Run!") == "My Run_");
    CHECK(MacroStore::sanitizeName(std::string(200, 'a')).size() == 64);
    CHECK(MacroStore::sanitizeName("  padded  ") == "padded");
}

void testStoreSaveLoadRoundTrip() {
    const fs::path dir = freshDir("roundtrip");
    MacroStore store(dir);
    std::string saved, msg;
    CHECK(store.save("First run", simpleMacro(), saved, false, msg) == StoreError::Ok);
    CHECK(saved == "First run.gdr2");

    Macro back;
    CHECK(store.load(saved, back, msg) == StoreError::Ok);
    CHECK(back.inputs == simpleMacro().inputs);
    CHECK(back.durationFrames == 20);

    const auto entries = store.list();
    CHECK(entries.size() == 1 && entries[0].name == "First run");
    fs::remove_all(dir);
}

void testStoreNoAccidentalOverwrite() {
    const fs::path dir = freshDir("overwrite");
    MacroStore store(dir);
    std::string saved, msg;
    CHECK(store.save("Run", simpleMacro(), saved, false, msg) == StoreError::Ok);
    CHECK(store.save("Run", simpleMacro(), saved, false, msg) == StoreError::AlreadyExists);
    CHECK(store.uniqueName("Run") == "Run (2)");
    CHECK(store.save("Run", simpleMacro(), saved, true, msg) == StoreError::Ok);  // explicit overwrite
    fs::remove_all(dir);
}

void testStoreRejectsTraversalAndBadFiles() {
    const fs::path dir = freshDir("reject");
    MacroStore store(dir);
    Macro out;
    std::string msg;
    CHECK(store.load("../escape.gdr2", out, msg) == StoreError::InvalidName);
    CHECK(store.load("sub/file.gdr2", out, msg) == StoreError::InvalidName);
    CHECK(store.load("notes.txt", out, msg) == StoreError::InvalidName);
    CHECK(store.load("missing.gdr2", out, msg) == StoreError::NotFound);

    fs::create_directories(dir);
    {
        std::ofstream junk(dir / "junk.gdr2", std::ios::binary);
        junk << "not a replay";
    }
    CHECK(store.load("junk.gdr2", out, msg) == StoreError::Format);
    CHECK(!msg.empty());
    CHECK(store.list().size() == 1);             // junk is listed but never loads silently
    fs::remove_all(dir);
}

void testStoreImportRules() {
    const fs::path dir = freshDir("import");
    fs::create_directories(dir);
    MacroStore store(dir);
    Macro out;
    std::string msg;

    const fs::path legacy = dir / "old.gdr";
    { std::ofstream f(legacy, std::ios::binary); f << "GDR1"; }
    CHECK(store.importFile(legacy, out, msg) == StoreError::UnsupportedFormat);

    const fs::path wrong = dir / "image.png";
    { std::ofstream f(wrong, std::ios::binary); f << "x"; }
    CHECK(store.importFile(wrong, out, msg) == StoreError::UnsupportedFormat);

    std::vector<uint8_t> bytes;
    CHECK(gdr2::encode(simpleMacro(), bytes).ok());
    const fs::path good = dir / "external.gdr2";
    { std::ofstream f(good, std::ios::binary); f.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())); }
    CHECK(store.importFile(good, out, msg) == StoreError::Ok);
    CHECK(out.inputs.size() == 2);
    fs::remove_all(dir);
}

void testStoreRenameRemove() {
    const fs::path dir = freshDir("renamedel");
    MacroStore store(dir);
    std::string saved, newName, msg;
    CHECK(store.save("Alpha", simpleMacro(), saved, false, msg) == StoreError::Ok);
    CHECK(store.save("Beta", simpleMacro(), saved, false, msg) == StoreError::Ok);
    CHECK(store.rename("Alpha.gdr2", "Beta", newName) == StoreError::AlreadyExists);
    CHECK(store.rename("Alpha.gdr2", "Gamma", newName) == StoreError::Ok);
    CHECK(newName == "Gamma.gdr2");
    CHECK(store.remove("Gamma.gdr2") == StoreError::Ok);
    CHECK(store.remove("Gamma.gdr2") == StoreError::NotFound);
    CHECK(store.list().size() == 1);
    // No temporary files should be left behind by saves.
    bool sawTemp = false;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() == ".tmp") sawTemp = true;
    }
    CHECK(!sawTemp);
    fs::remove_all(dir);
}


// ----- Phase 3: toggle rules and regressions

void testToggleController() {
    ToggleController t;
    CHECK(!t.isOpen());
    CHECK(t.onKey(false, false) == ToggleController::Action::None);  // release does nothing
    CHECK(t.onKey(true, true) == ToggleController::Action::None);    // key-repeat does nothing
    CHECK(!t.isOpen());
    CHECK(t.onKey(true, false) == ToggleController::Action::Open);
    CHECK(t.isOpen());
    CHECK(t.onKey(true, true) == ToggleController::Action::None);   // holding does not reopen
    CHECK(t.onKey(false, false) == ToggleController::Action::None);
    CHECK(t.onKey(true, false) == ToggleController::Action::Close);
    CHECK(!t.isOpen());
    // The window was closed by its own button, so the UI reports it.
    CHECK(t.onKey(true, false) == ToggleController::Action::Open);
    t.setOpen(false);
    CHECK(!t.isOpen());
    CHECK(t.onKey(true, false) == ToggleController::Action::Open);
}

void testPlayerRebasesOnBackwardsClock() {
    // Regression: a backwards game-frame counter (clock reset) used to stall macro time.
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{100, Button::Jump, false, false});
    m.durationFrames = 200;
    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok);
    CHECK(p.play());
    p.tick(1000);
    p.tick(1010);
    CHECK(p.elapsedFrames() == 10);
    p.tick(5);                        // counter went backwards
    CHECK(p.elapsedFrames() == 10);
    p.tick(15);                       // must advance from the rebased counter
    CHECK(p.elapsedFrames() == 20);
}

void testExitWhileRecordingKeepsRecording() {
    // Regression: closing the level mid-recording used to leave the recorder stuck.
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(s.recordButton(0, false, Button::Jump, true));
    CHECK(s.onLevelEvent(LevelEvent::Exited, 30).empty());
    CHECK(!s.isRecording());
    CHECK(s.recorderState() == Recorder::State::Stopped);
    CHECK(s.hasMacro());
    CHECK(s.currentMacro().inputs.size() == 2);        // press + release at the exit frame
    CHECK(s.currentMacro().durationFrames == 30);
    CHECK(s.message().find("level was closed") != std::string::npos);
    CHECK(s.startRecording(40, meta()));               // a new recording can start afterwards
}

void testNewRecordingHasNoStaleEvents() {
    Session s;
    CHECK(s.startRecording(0, meta()));
    CHECK(s.recordButton(0, false, Button::Jump, true));
    CHECK(s.recordButton(5, false, Button::Jump, false));
    CHECK(s.stopRecording(10));
    CHECK(s.startRecording(100, meta()));
    CHECK(s.recordButton(100, false, Button::Jump, true));
    CHECK(s.stopRecording(110));
    CHECK(s.currentMacro().inputs.size() == 2);        // only the second recording
    CHECK(s.currentMacro().inputs[0].frame == 0);
}

}  // namespace

int run() {
    testClock();
    testFormatFrames();
    testSessionRecordingFlow();
    testSessionRefusals();
    testSessionPlaybackAndLevelEvents();
    testSessionCompletionMessage();
    testSetCurrentMacroKeepsPreviousOnFailure();
    testPanelIdle();
    testPanelRecordingAndPlayback();
    testPanelCompleted();
    testSanitizeName();
    testStoreSaveLoadRoundTrip();
    testStoreNoAccidentalOverwrite();
    testStoreRejectsTraversalAndBadFiles();
    testStoreImportRules();
    testStoreRenameRemove();
    testToggleController();
    testPlayerRebasesOnBackwardsClock();
    testExitWhileRecordingKeepsRecording();
    testNewRecordingHasNoStaleEvents();
    std::printf("phase2: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures;
}

}  // namespace tmac_phase2
