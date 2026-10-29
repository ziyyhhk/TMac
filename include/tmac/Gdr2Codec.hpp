#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "tmac/Macro.hpp"

namespace tmac::gdr2 {

// Format version written and accepted by TMac.
// UNVERIFIED: the value is taken from the GDR2 spec's header description and
// must be checked against maxnut/GDReplayFormat (gdr2 branch) before release.
inline constexpr uint64_t kFormatVersion = 1;

enum class Status {
    Ok,
    NotGdr2,              // no "GDR" signature (includes legacy .gdr v1 files)
    UnsupportedVersion,
    UnsupportedInputTag,  // replay uses per-input custom extensions
    Truncated,
    Malformed,
    TooLarge,
    InvalidMacro,  // export only: the Macro failed validation
};

struct Result {
    Status status = Status::Ok;
    std::string message;

    bool ok() const { return status == Status::Ok; }
};

// Serialises a validated macro to GDR2 bytes.
Result encode(const Macro& macro, std::vector<uint8_t>& out);
// Parses GDR2 bytes. `out` is only modified on success.
Result decode(const std::vector<uint8_t>& bytes, Macro& out);

}  // namespace tmac::gdr2
