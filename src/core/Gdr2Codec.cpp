#include "tmac/Gdr2Codec.hpp"

#include <algorithm>
#include <cstring>
#include <exception>
#include <iterator>
#include <limits>
#include <string>

namespace tmac::gdr2 {
namespace {

// Internal error channel. Converted to Result at the public boundary.
struct Fail {
    Status status;
    std::string message;
};

class Writer {
public:
    explicit Writer(std::vector<uint8_t>& out) : out_(out) {}

    void raw(const char* data, size_t size) {
        const auto* begin = reinterpret_cast<const uint8_t*>(data);
        out_.insert(out_.end(), begin, begin + size);
    }
    void byte(uint8_t value) { out_.push_back(value); }
    void boolean(bool value) { out_.push_back(value ? 1 : 0); }
    void varint(uint64_t value) {  // unsigned LEB128
        do {
            uint8_t b = static_cast<uint8_t>(value & 0x7f);
            value >>= 7;
            if (value != 0) b |= 0x80;
            out_.push_back(b);
        } while (value != 0);
    }
    void cstring(const std::string& text) {
        raw(text.data(), text.size());
        out_.push_back(0);
    }

private:
    std::vector<uint8_t>& out_;
};

class Reader {
public:
    explicit Reader(const std::vector<uint8_t>& data) : data_(data) {}

    size_t remaining() const { return data_.size() - pos_; }

    uint8_t byte() {
        if (pos_ >= data_.size()) throw Fail{Status::Truncated, "unexpected end of file"};
        return data_[pos_++];
    }

    bool boolean() {
        const uint8_t b = byte();
        if (b > 1) throw Fail{Status::Malformed, "boolean field is not 0 or 1"};
        return b == 1;
    }

    uint64_t varint() {
        uint64_t result = 0;
        for (unsigned shift = 0; shift < 64; shift += 7) {
            const uint8_t b = byte();
            const uint64_t payload = b & 0x7f;
            if (shift == 63 && payload > 1) throw Fail{Status::Malformed, "varint overflows 64 bits"};
            result |= payload << shift;
            if ((b & 0x80) == 0) return result;
        }
        throw Fail{Status::Malformed, "varint is too long"};
    }

    std::string cstring() {
        const size_t start = pos_;
        for (;;) {
            if (byte() == 0) break;
            if (pos_ - start > limits::kMaxStringBytes) {
                throw Fail{Status::Malformed, "string exceeds the length limit"};
            }
        }
        return std::string(reinterpret_cast<const char*>(data_.data() + start), pos_ - start - 1);
    }

    void skip(uint64_t count) {
        if (count > remaining()) throw Fail{Status::Truncated, "unexpected end of file"};
        pos_ += static_cast<size_t>(count);
    }

private:
    const std::vector<uint8_t>& data_;
    size_t pos_ = 0;
};

uint32_t toU32(uint64_t value, const char* field) {
    if (value > std::numeric_limits<uint32_t>::max()) {
        throw Fail{Status::Malformed, std::string(field) + " is out of range"};
    }
    return static_cast<uint32_t>(value);
}

// Packed input record.
//   Non-platformer: bit0 = more bytes follow, bit1 = down, bits2-7 = delta (6 bits).
//   Platformer:     bit0 = more bytes follow, bit1 = down, bits2-3 = button id,
//                   bits4-7 = delta (4 bits).
// When bit0 is set, the remaining delta bits follow as an unsigned varint.
// UNVERIFIED: this follows the README's description. Confirm against real
// bot-recorded files before claiming interoperability.
void writeInput(Writer& w, uint64_t delta, const InputEvent& event, bool platformer) {
    const unsigned lowBits = platformer ? 4u : 6u;
    const uint64_t low = delta & ((uint64_t{1} << lowBits) - 1);
    const uint64_t rest = delta >> lowBits;

    uint8_t first = static_cast<uint8_t>(low << (platformer ? 4u : 2u));
    if (event.down) first |= 0x02;
    if (platformer) first |= static_cast<uint8_t>((static_cast<uint8_t>(event.button) & 0x3) << 2);
    if (rest != 0) {
        first |= 0x01;
        w.byte(first);
        w.varint(rest);
    } else {
        w.byte(first);
    }
}

void writeInputList(Writer& w, const std::vector<InputEvent>& list, bool platformer) {
    uint64_t previous = 0;
    for (const InputEvent& event : list) {
        writeInput(w, event.frame - previous, event, platformer);
        previous = event.frame;
    }
}

void readInputs(Reader& r, uint64_t count, bool player2, bool platformer, std::vector<InputEvent>& out) {
    const unsigned lowBits = platformer ? 4u : 6u;
    uint64_t frame = 0;
    for (uint64_t i = 0; i < count; ++i) {
        const uint8_t first = r.byte();
        const bool down = (first & 0x02) != 0;
        const bool more = (first & 0x01) != 0;

        Button button = Button::Jump;
        if (platformer) {
            button = static_cast<Button>((first >> 2) & 0x3);
            if (button == Button::None) throw Fail{Status::Malformed, "platformer input has no button"};
        }

        uint64_t delta = static_cast<uint64_t>(first >> (platformer ? 4 : 2));
        if (more) {
            const uint64_t rest = r.varint();
            if (rest > (std::numeric_limits<uint64_t>::max() >> lowBits)) {
                throw Fail{Status::Malformed, "input frame delta overflows"};
            }
            delta |= rest << lowBits;
        }
        if (delta > limits::kMaxFrame - frame) {
            throw Fail{Status::Malformed, "input frame exceeds the supported range"};
        }
        frame += delta;
        out.push_back(InputEvent{frame, button, player2, down});
    }
}

}  // namespace

Result encode(const Macro& macro, std::vector<uint8_t>& out) {
    out.clear();
    const MacroError error = macro.validate();
    if (error != MacroError::Ok) {
        return {Status::InvalidMacro, std::string("macro cannot be exported: ") + describe(error)};
    }

    const MacroMeta& meta = macro.meta;
    Writer w(out);
    w.raw("GDR", 3);
    w.varint(kFormatVersion);
    w.cstring("");  // input tag: standard inputs only, no per-input extensions

    // Replay metadata
    w.cstring(meta.author);
    w.cstring(meta.description);
    w.varint(macro.durationFrames);
    w.varint(meta.gameVersion);
    w.varint(meta.framerate);
    w.varint(meta.seed);
    w.varint(meta.coins);
    w.boolean(meta.ldm);
    w.boolean(meta.platformer);
    w.cstring(meta.botName);
    w.varint(meta.botVersion);
    w.varint(meta.levelId);
    w.cstring(meta.levelName);
    w.varint(0);  // replay-level extension block: none

    // Death frames, delta encoded
    w.varint(macro.deaths.size());
    uint64_t previous = 0;
    for (uint64_t death : macro.deaths) {
        w.varint(death - previous);
        previous = death;
    }

    // Inputs: all P1 then all P2, each list delta encoded.
    std::vector<InputEvent> p1;
    std::vector<InputEvent> p2;
    for (const InputEvent& event : macro.inputs) {
        (event.player2 ? p2 : p1).push_back(event);
    }
    w.varint(macro.inputs.size());
    w.varint(p1.size());
    writeInputList(w, p1, meta.platformer);
    writeInputList(w, p2, meta.platformer);
    return {Status::Ok, ""};
}

Result decode(const std::vector<uint8_t>& bytes, Macro& out) {
    try {
        if (bytes.size() > limits::kMaxFileBytes) {
            throw Fail{Status::TooLarge, "file is larger than the supported limit"};
        }
        if (bytes.size() < 3 || std::memcmp(bytes.data(), "GDR", 3) != 0) {
            throw Fail{Status::NotGdr2,
                       "not a GDR2 replay (no 'GDR' signature). Legacy .gdr (GDR1) files are not supported"};
        }

        Reader r(bytes);
        r.skip(3);
        const uint64_t version = r.varint();
        if (version != kFormatVersion) {
            throw Fail{Status::UnsupportedVersion, "unsupported GDR2 version " + std::to_string(version)};
        }
        const std::string tag = r.cstring();
        if (!tag.empty()) {
            throw Fail{Status::UnsupportedInputTag,
                       "replay uses custom input extensions ('" + tag + "') that TMac cannot read"};
        }

        Macro m;
        m.meta.author = r.cstring();
        m.meta.description = r.cstring();
        m.durationFrames = r.varint();
        m.meta.gameVersion = toU32(r.varint(), "game version");
        m.meta.framerate = toU32(r.varint(), "framerate");
        m.meta.seed = toU32(r.varint(), "seed");
        m.meta.coins = toU32(r.varint(), "coins");
        m.meta.ldm = r.boolean();
        m.meta.platformer = r.boolean();
        m.meta.botName = r.cstring();
        m.meta.botVersion = toU32(r.varint(), "bot version");
        m.meta.levelId = toU32(r.varint(), "level id");
        m.meta.levelName = r.cstring();
        r.skip(r.varint());  // replay-level extension block: skipped, not interpreted

        const uint64_t deathCount = r.varint();
        if (deathCount > limits::kMaxDeaths) throw Fail{Status::TooLarge, "too many deaths"};
        if (deathCount > r.remaining()) throw Fail{Status::Truncated, "unexpected end of file"};
        m.deaths.reserve(static_cast<size_t>(deathCount));
        uint64_t frame = 0;
        for (uint64_t i = 0; i < deathCount; ++i) {
            const uint64_t delta = r.varint();
            if (delta > limits::kMaxFrame - frame) {
                throw Fail{Status::Malformed, "death frame exceeds the supported range"};
            }
            frame += delta;
            m.deaths.push_back(frame);
        }

        const uint64_t inputCount = r.varint();
        const uint64_t p1Count = r.varint();
        if (inputCount > limits::kMaxInputs) throw Fail{Status::TooLarge, "too many inputs"};
        if (p1Count > inputCount) throw Fail{Status::Malformed, "P1 input count exceeds total input count"};
        if (inputCount > r.remaining()) throw Fail{Status::Truncated, "unexpected end of file"};

        std::vector<InputEvent> p1;
        std::vector<InputEvent> p2;
        p1.reserve(static_cast<size_t>(p1Count));
        p2.reserve(static_cast<size_t>(inputCount - p1Count));
        readInputs(r, p1Count, false, m.meta.platformer, p1);
        readInputs(r, inputCount - p1Count, true, m.meta.platformer, p2);
        if (r.remaining() != 0) throw Fail{Status::Malformed, "unexpected data after the input block"};

        // Both lists are already sorted. std::merge is stable, so on equal
        // frames P1 events come first. Same-frame cross-player order is
        // therefore canonicalised on import.
        m.inputs.reserve(p1.size() + p2.size());
        std::merge(p1.begin(), p1.end(), p2.begin(), p2.end(), std::back_inserter(m.inputs),
                   [](const InputEvent& a, const InputEvent& b) { return a.frame < b.frame; });

        const MacroError error = m.validate();
        if (error != MacroError::Ok) {
            throw Fail{Status::Malformed, std::string("invalid replay data: ") + describe(error)};
        }
        out = std::move(m);
        return {Status::Ok, ""};
    } catch (const Fail& fail) {
        return {fail.status, fail.message};
    } catch (const std::exception& ex) {
        return {Status::Malformed, ex.what()};
    }
}

}  // namespace tmac::gdr2
