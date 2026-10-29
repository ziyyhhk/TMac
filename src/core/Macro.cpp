#include "tmac/Macro.hpp"

#include <initializer_list>

namespace tmac {

const char* describe(MacroError error) {
    switch (error) {
        case MacroError::Ok: return "ok";
        case MacroError::Unsorted: return "events are not in frame order";
        case MacroError::FrameTooLarge: return "frame exceeds the supported range";
        case MacroError::InvalidButton: return "button is not valid for this macro type";
        case MacroError::TooManyInputs: return "too many inputs";
        case MacroError::TooManyDeaths: return "too many deaths";
        case MacroError::DurationTooShort: return "duration ends before the last input";
        case MacroError::InvalidFramerate: return "framerate must be greater than zero";
        case MacroError::StringTooLong: return "a text field exceeds the length limit";
        case MacroError::StringHasNull: return "a text field contains a NUL character";
    }
    return "unknown error";
}

namespace {

bool buttonAllowed(const MacroMeta& meta, Button button) {
    if (meta.platformer) {
        return button == Button::Jump || button == Button::Left || button == Button::Right;
    }
    // The non-platformer GDR2 encoding stores no button id; every input is a jump.
    return button == Button::Jump;
}

MacroError checkText(const std::string& text) {
    if (text.size() > limits::kMaxStringBytes) return MacroError::StringTooLong;
    if (text.find('\0') != std::string::npos) return MacroError::StringHasNull;
    return MacroError::Ok;
}

}  // namespace

MacroError Macro::validate() const {
    if (inputs.size() > limits::kMaxInputs) return MacroError::TooManyInputs;
    if (deaths.size() > limits::kMaxDeaths) return MacroError::TooManyDeaths;
    if (meta.framerate == 0) return MacroError::InvalidFramerate;
    for (const std::string* text : {&meta.author, &meta.description, &meta.botName, &meta.levelName}) {
        if (const MacroError e = checkText(*text); e != MacroError::Ok) return e;
    }
    if (durationFrames > limits::kMaxFrame) return MacroError::FrameTooLarge;

    uint64_t previous = 0;
    for (const InputEvent& event : inputs) {
        if (event.frame > limits::kMaxFrame) return MacroError::FrameTooLarge;
        if (event.frame < previous) return MacroError::Unsorted;
        if (!buttonAllowed(meta, event.button)) return MacroError::InvalidButton;
        previous = event.frame;
    }

    previous = 0;
    for (uint64_t death : deaths) {
        if (death > limits::kMaxFrame) return MacroError::FrameTooLarge;
        if (death < previous) return MacroError::Unsorted;
        previous = death;
    }

    if (!inputs.empty() && durationFrames < inputs.back().frame) {
        return MacroError::DurationTooShort;
    }
    return MacroError::Ok;
}

MacroError Macro::append(const InputEvent& event) {
    if (inputs.size() >= limits::kMaxInputs) return MacroError::TooManyInputs;
    if (event.frame > limits::kMaxFrame) return MacroError::FrameTooLarge;
    if (!inputs.empty() && event.frame < inputs.back().frame) return MacroError::Unsorted;
    if (!buttonAllowed(meta, event.button)) return MacroError::InvalidButton;
    inputs.push_back(event);
    return MacroError::Ok;
}

uint64_t Macro::lastFrame() const {
    return inputs.empty() ? 0 : inputs.back().frame;
}

}  // namespace tmac
