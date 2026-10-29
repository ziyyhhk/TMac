// Dependency-free unit tests for the TMac core. Run with: tmac_tests
#include <cstdio>
#include <random>
#include <vector>

#include "tmac/Gdr2Codec.hpp"
#include "tmac/Macro.hpp"
#include "tmac/Player.hpp"
#include "tmac/Recorder.hpp"

namespace tmac_phase2 { int run(); }

namespace {

int g_checks = 0;
int g_failures = 0;

#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_checks;                                                                 \
        if (!(cond)) {                                                              \
            ++g_failures;                                                           \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);    \
        }                                                                           \
    } while (0)

using namespace tmac;

Macro sampleMacro(bool platformer) {
    Macro m;
    m.meta.author = "Tester";
    m.meta.description = "round trip";
    m.meta.levelName = "Stereo Madness";
    m.meta.levelId = 1;
    m.meta.seed = 42;
    m.meta.coins = 3;
    m.meta.ldm = true;
    m.meta.platformer = platformer;
    m.deaths = {10, 10, 500};

    // Gaps of 0, 1, 63, 64, 127 and 2^40 exercise single-byte and multi-byte packed deltas.
    const uint64_t frames[] = {0, 0, 1, 64, 127, 128, 1000, 100000, uint64_t{1} << 40};
    const Button cycle[] = {Button::Jump, Button::Left, Button::Right};
    bool down = true;
    for (size_t i = 0; i < std::size(frames); ++i) {
        const Button button = platformer ? cycle[i % 3] : Button::Jump;
        const bool player2 = (i == 3);  // one P2 event in the middle
        CHECK(m.append(InputEvent{frames[i], button, player2, down}) == MacroError::Ok);
        down = !down;
    }
    m.durationFrames = m.lastFrame() + 7;
    return m;
}

bool sameMacro(const Macro& a, const Macro& b) {
    return a.meta == b.meta && a.durationFrames == b.durationFrames && a.deaths == b.deaths &&
           a.inputs == b.inputs;
}

void testMacroValidation() {
    Macro m;
    CHECK(m.append(InputEvent{5, Button::Jump, false, true}) == MacroError::Ok);
    CHECK(m.append(InputEvent{3, Button::Jump, false, false}) == MacroError::Unsorted);
    CHECK(m.append(InputEvent{6, Button::Left, false, true}) == MacroError::InvalidButton);
    m.durationFrames = 2;
    CHECK(m.validate() == MacroError::DurationTooShort);
    m.durationFrames = 10;
    CHECK(m.validate() == MacroError::Ok);
    Macro zeroRate = m;
    zeroRate.meta.framerate = 0;
    CHECK(zeroRate.validate() == MacroError::InvalidFramerate);

    Macro plat;
    plat.meta.platformer = true;
    CHECK(plat.append(InputEvent{1, Button::Left, false, true}) == MacroError::Ok);
    CHECK(plat.append(InputEvent{2, Button::None, false, true}) == MacroError::InvalidButton);
}

void testRoundTrip(bool platformer) {
    const Macro original = sampleMacro(platformer);
    CHECK(original.validate() == MacroError::Ok);

    std::vector<uint8_t> bytes;
    const gdr2::Result enc = gdr2::encode(original, bytes);
    CHECK(enc.ok());
    CHECK(bytes.size() > 3 && bytes[0] == 'G' && bytes[1] == 'D' && bytes[2] == 'R');

    Macro decoded;
    const gdr2::Result dec = gdr2::decode(bytes, decoded);
    if (!dec.ok()) std::fprintf(stderr, "decode failed: %s\n", dec.message.c_str());
    CHECK(dec.ok());
    CHECK(sameMacro(original, decoded));

    // Re-encoding the decoded macro must give identical bytes.
    std::vector<uint8_t> again;
    CHECK(gdr2::encode(decoded, again).ok());
    CHECK(again == bytes);
}

void testRejects() {
    Macro out;
    std::vector<uint8_t> bytes;
    CHECK(gdr2::decode({}, out).status == gdr2::Status::NotGdr2);
    CHECK(gdr2::decode({'X', 'Y', 'Z', 1}, out).status == gdr2::Status::NotGdr2);

    // The valid encoding has version byte 1 at index 3. Change it to 99.
    CHECK(gdr2::encode(sampleMacro(false), bytes).ok());
    CHECK(bytes[3] == 1);
    bytes[3] = 99;
    CHECK(gdr2::decode(bytes, out).status == gdr2::Status::UnsupportedVersion);

    // Non-empty input tag means per-input extensions we cannot read.
    const std::vector<uint8_t> tagged = {'G', 'D', 'R', 1, 'a', 0};
    CHECK(gdr2::decode(tagged, out).status == gdr2::Status::UnsupportedInputTag);

    // Unsorted macros must not be exported.
    Macro bad = sampleMacro(false);
    bad.inputs[1].frame = 0;
    bad.inputs[2].frame = 0;
    bad.inputs[3].frame = 5000;
    bad.inputs[4].frame = 1;  // out of order
    CHECK(gdr2::encode(bad, bytes).status == gdr2::Status::InvalidMacro);
    CHECK(bytes.empty());
}

void testTruncationNeverDecodes() {
    std::vector<uint8_t> full;
    CHECK(gdr2::encode(sampleMacro(false), full).ok());
    for (size_t len = 0; len < full.size(); ++len) {
        std::vector<uint8_t> prefix(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(len));
        Macro out;
        CHECK(!gdr2::decode(prefix, out).ok());
    }
}

void testMutationNeverCrashes() {
    std::vector<uint8_t> base;
    CHECK(gdr2::encode(sampleMacro(false), base).ok());
    std::mt19937 rng(12345);
    for (int i = 0; i < 20000; ++i) {
        std::vector<uint8_t> data = base;
        switch (rng() % 3) {
            case 0:
                data.resize(rng() % (data.size() + 1));
                break;
            case 1:
                for (int k = 0; k < 3; ++k) data[rng() % data.size()] = static_cast<uint8_t>(rng());
                break;
            default:
                data.insert(data.begin() + static_cast<std::ptrdiff_t>(rng() % (data.size() + 1)),
                            static_cast<uint8_t>(rng()));
                break;
        }
        Macro out;
        const gdr2::Result result = gdr2::decode(data, out);
        if (result.ok()) CHECK(out.validate() == MacroError::Ok);
    }
}

void testRecorder() {
    Recorder r;
    CHECK(!r.onButton(0, false, Button::Jump, true));  // not recording yet
    CHECK(r.start(1000, MacroMeta{}));
    CHECK(!r.start(1001, MacroMeta{}));                 // already recording
    CHECK(!r.onButton(999, false, Button::Jump, true)); // before origin
    CHECK(r.onButton(1000, false, Button::Jump, true)); // relative frame 0
    CHECK(!r.onButton(1001, false, Button::Jump, true)); // duplicate press
    CHECK(r.onButton(1010, false, Button::Jump, false)); // relative frame 10
    CHECK(!r.onButton(1011, false, Button::Jump, false)); // release while not held
    CHECK(!r.onButton(1012, false, Button::Left, true));  // invalid for non-platformer
    CHECK(r.onButton(1020, true, Button::Jump, true));    // P2 press, still held at stop
    CHECK(r.stop(1030));
    CHECK(r.state() == Recorder::State::Stopped);
    CHECK(!r.onButton(1031, false, Button::Jump, true));  // no input after stop

    const Macro& m = r.macro();
    CHECK(m.inputs.size() == 4);
    CHECK(m.inputs.back().frame == 30 && m.inputs.back().player2 && !m.inputs.back().down);
    CHECK(m.durationFrames == 30);
    CHECK(m.validate() == MacroError::Ok);
}

void testPlayerTiming() {
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{10, Button::Jump, false, false});
    m.append(InputEvent{20, Button::Jump, false, true});
    m.append(InputEvent{25, Button::Jump, false, false});
    m.durationFrames = 30;

    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok && releases.empty());
    CHECK(p.state() == Player::State::Idle);
    CHECK(p.play());

    auto e = p.tick(1000);  // baseline frame: macro time 0
    CHECK(e.size() == 1 && e[0].down && e[0].frame == 0);
    CHECK(p.tick(1005).empty());
    e = p.tick(1010);       // macro time 10
    CHECK(e.size() == 1 && !e[0].down);
    e = p.tick(1100);       // one large spike: macro time jumps to 100
    CHECK(e.size() == 2 && e[0].down && e[0].frame == 20 && !e[1].down && e[1].frame == 25);
    CHECK(p.state() == Player::State::Finished);
    CHECK(p.progress() == 1.0);
    CHECK(p.stats().dispatched == 4);
    CHECK(p.stats().late == 2);
    CHECK(p.stats().maxLateFrames == 80);
}

void testPlayerSkipsRedundantEvents() {
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{5, Button::Jump, false, true});  // duplicate press
    m.append(InputEvent{10, Button::Jump, false, false});
    m.durationFrames = 10;

    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok);
    CHECK(p.play());
    CHECK(p.tick(0).size() == 1);
    CHECK(p.tick(5).empty());
    CHECK(p.stats().skipped == 1);
    CHECK(p.tick(10).size() == 1);
    CHECK(p.state() == Player::State::Finished);
}

void testPauseResume() {
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{50, Button::Jump, false, false});
    m.durationFrames = 60;

    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok);
    CHECK(p.play());
    CHECK(p.tick(0).size() == 1);      // press dispatched
    CHECK(p.tick(10).empty());         // macro time 10
    CHECK(p.isHeld(false, Button::Jump));

    auto pr = p.pause();              // pausing must not leave an input held
    CHECK(pr.size() == 1 && !pr[0].down);
    CHECK(!p.isHeld(false, Button::Jump));
    CHECK(p.tick(500).empty());       // no progress while paused
    CHECK(p.elapsedFrames() == 10);

    auto rs = p.resume();             // held input is restored
    CHECK(rs.size() == 1 && rs[0].down);
    CHECK(p.isHeld(false, Button::Jump));
    CHECK(p.tick(505).empty());       // first tick after resume is the baseline
    CHECK(p.elapsedFrames() == 10);
    CHECK(p.tick(545).size() == 1);   // release at macro time 50
    CHECK(p.tick(560).empty());
    CHECK(p.state() == Player::State::Finished);
}

void testStopAndRestart() {
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.append(InputEvent{40, Button::Jump, false, false});
    m.durationFrames = 50;

    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok);
    CHECK(p.play());
    CHECK(p.tick(0).size() == 1);
    CHECK(p.tick(20).empty());

    auto stopped = p.stop();          // stopping mid-hold must release the input
    CHECK(stopped.size() == 1 && !stopped[0].down);
    CHECK(p.state() == Player::State::Idle);
    CHECK(p.tick(100).empty());
    CHECK(!p.isHeld(false, Button::Jump));

    CHECK(p.play());
    CHECK(p.tick(0).size() == 1);     // play restarts from the beginning
    CHECK(p.tick(20).empty());
    auto restarted = p.restart();     // restart releases the held input first
    CHECK(restarted.size() == 1 && !restarted[0].down);
    CHECK(p.position() == 0);
    CHECK(p.elapsedFrames() == 0);
    CHECK(p.state() == Player::State::Playing);
}

void testFinishReleasesHeldInput() {
    Macro m;
    m.append(InputEvent{0, Button::Jump, false, true});
    m.durationFrames = 5;             // macro ends while the input is still down

    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(m, releases) == MacroError::Ok);
    CHECK(p.play());
    auto out = p.tick(0);
    CHECK(out.size() == 1 && out[0].down);
    out = p.tick(10);
    CHECK(out.size() == 1 && !out[0].down);
    CHECK(p.state() == Player::State::Finished);
    CHECK(!p.isHeld(false, Button::Jump));
}

void testLoadFailureKeepsCurrentMacro() {
    Macro good;
    good.append(InputEvent{0, Button::Jump, false, true});
    good.durationFrames = 5;
    Player p;
    std::vector<InputEvent> releases;
    CHECK(p.load(good, releases) == MacroError::Ok);
    CHECK(p.play());
    CHECK(p.tick(0).size() == 1);

    Macro bad = good;
    bad.durationFrames = 0;
    bad.inputs.push_back(InputEvent{9, Button::Jump, false, false});  // beyond duration
    CHECK(p.load(bad, releases) == MacroError::DurationTooShort);
    CHECK(releases.empty());
    CHECK(p.state() == Player::State::Playing);  // playback was not disturbed
    CHECK(p.isHeld(false, Button::Jump));
}

}  // namespace

int main() {
    testMacroValidation();
    testRoundTrip(false);
    testRoundTrip(true);
    testRejects();
    testTruncationNeverDecodes();
    testMutationNeverCrashes();
    testRecorder();
    testPlayerTiming();
    testPlayerSkipsRedundantEvents();
    testPauseResume();
    testStopAndRestart();
    testFinishReleasesHeldInput();
    testLoadFailureKeepsCurrentMacro();

    std::printf("core: %d checks, %d failures\n", g_checks, g_failures);
    const int phase2Failures = tmac_phase2::run();
    return (g_failures + phase2Failures) == 0 ? 0 : 1;
}
