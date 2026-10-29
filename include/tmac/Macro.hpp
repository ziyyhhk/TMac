#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tmac {

// Button ids follow the GDR2 format (1 = Jump, 2 = Left, 3 = Right), which
// matches the numeric values of Geometry Dash's PlayerButton enum.
enum class Button : uint8_t { None = 0, Jump = 1, Left = 2, Right = 3 };

struct InputEvent {
    uint64_t frame = 0;  // macro frame, counted from 0
    Button button = Button::Jump;
    bool player2 = false;
    bool down = false;  // true = pressed, false = released

    friend bool operator==(const InputEvent&, const InputEvent&) = default;
};

struct MacroMeta {
    std::string author;
    std::string description;
    std::string botName = "TMac";
    uint32_t botVersion = 1;
    uint32_t gameVersion = 22081;
    uint32_t framerate = 240;
    uint32_t seed = 0;
    uint32_t coins = 0;
    bool ldm = false;
    bool platformer = false;
    uint32_t levelId = 0;
    std::string levelName;

    friend bool operator==(const MacroMeta&, const MacroMeta&) = default;
};

namespace limits {
inline constexpr uint64_t kMaxFrame = uint64_t{1} << 48;
inline constexpr size_t kMaxInputs = 10'000'000;
inline constexpr size_t kMaxDeaths = 1'000'000;
inline constexpr size_t kMaxStringBytes = 4096;
inline constexpr size_t kMaxFileBytes = size_t{256} * 1024 * 1024;
}  // namespace limits

enum class MacroError {
    Ok,
    Unsorted,
    FrameTooLarge,
    InvalidButton,
    TooManyInputs,
    TooManyDeaths,
    DurationTooShort,
    InvalidFramerate,
    StringTooLong,
    StringHasNull,
};

const char* describe(MacroError error);

struct Macro {
    MacroMeta meta;
    uint64_t durationFrames = 0;
    std::vector<uint64_t> deaths;    // non-decreasing frames
    std::vector<InputEvent> inputs;  // non-decreasing frames, stable order

    // Checks every invariant the file format and playback rely on.
    MacroError validate() const;
    // Appends one input. Refuses events that would break frame order or the
    // platformer/non-platformer button rules.
    MacroError append(const InputEvent& event);
    uint64_t lastFrame() const;
};

}  // namespace tmac
