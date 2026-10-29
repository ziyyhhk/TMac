#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "tmac/Macro.hpp"

namespace tmac {

enum class StoreError {
    Ok,
    InvalidName,
    NotFound,
    AlreadyExists,
    Io,
    Format,
    UnsupportedFormat,
};

const char* describe(StoreError error);

struct StoredMacro {
    std::string fileName;  // e.g. "Jump test.gdr2"
    std::string name;      // file name without the extension
    uintmax_t bytes = 0;
};

// Owns one directory of macro files. Names are sanitised and every operation
// resolves inside that directory, so a name can never reach a file outside it.
class MacroStore {
public:
    static constexpr const char* kExtension = ".gdr2";

    explicit MacroStore(std::filesystem::path root);

    // Keeps letters, digits, spaces, '-' and '_'. Everything else becomes '_'.
    // The result is never empty, never a Windows device name, and at most 64 bytes.
    static std::string sanitizeName(const std::string& input);

    // Returns `baseName` if free, otherwise "baseName (2)", "baseName (3)", ...
    std::string uniqueName(const std::string& baseName) const;

    // Writes atomically through a temporary file. Returns AlreadyExists unless
    // `overwrite` is true. The caller decides whether to ask the user first.
    StoreError save(const std::string& baseName, const Macro& macro, std::string& savedFileName,
                    bool overwrite, std::string& message);
    StoreError load(const std::string& fileName, Macro& out, std::string& message) const;
    // Reads a .gdr2 file from anywhere on disk. Legacy .gdr files are refused.
    StoreError importFile(const std::filesystem::path& source, Macro& out, std::string& message) const;
    StoreError rename(const std::string& fileName, const std::string& newBaseName, std::string& newFileName);
    StoreError remove(const std::string& fileName);
    std::vector<StoredMacro> list() const;

    const std::filesystem::path& root() const { return root_; }

private:
    StoreError resolve(const std::string& fileName, std::filesystem::path& out) const;

    std::filesystem::path root_;
};

}  // namespace tmac
