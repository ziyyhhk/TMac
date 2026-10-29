#include "tmac/MacroStore.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iterator>
#include <system_error>

#include "tmac/Gdr2Codec.hpp"

namespace fs = std::filesystem;

namespace tmac {

const char* describe(StoreError error) {
    switch (error) {
        case StoreError::Ok: return "ok";
        case StoreError::InvalidName: return "invalid file name";
        case StoreError::NotFound: return "file not found";
        case StoreError::AlreadyExists: return "a macro with that name already exists";
        case StoreError::Io: return "file system error";
        case StoreError::Format: return "file is not a valid macro";
        case StoreError::UnsupportedFormat: return "unsupported file format";
    }
    return "unknown error";
}

namespace {

bool isReservedWindowsName(const std::string& name) {
    static const char* const reserved[] = {"CON", "PRN", "AUX", "NUL"};
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    for (const char* r : reserved) {
        if (upper == r) return true;
    }
    if (upper.size() == 4 && (upper.rfind("COM", 0) == 0 || upper.rfind("LPT", 0) == 0) &&
        upper[3] >= '1' && upper[3] <= '9') {
        return true;
    }
    return false;
}

StoreError readFile(const fs::path& path, std::vector<uint8_t>& bytes, std::string& message) {
    std::error_code ec;
    const auto size = fs::file_size(path, ec);
    if (ec) {
        message = "could not read the file size";
        return StoreError::Io;
    }
    if (size > limits::kMaxFileBytes) {
        message = "file is larger than the supported limit";
        return StoreError::Format;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        message = "could not open the file";
        return StoreError::Io;
    }
    bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (!in && !in.eof()) {
        message = "could not read the file";
        return StoreError::Io;
    }
    return StoreError::Ok;
}

}  // namespace

MacroStore::MacroStore(fs::path root) : root_(std::move(root)) {}

std::string MacroStore::sanitizeName(const std::string& input) {
    std::string out;
    for (unsigned char c : input) {
        if (out.size() >= 64) break;
        if (std::isalnum(c) || c == ' ' || c == '-' || c == '_') {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('_');
        }
    }
    // Trim spaces from both ends so names cannot start or end with whitespace.
    const auto first = out.find_first_not_of(' ');
    out = (first == std::string::npos) ? std::string() : out.substr(first, out.find_last_not_of(' ') - first + 1);
    if (out.empty()) out = "macro";
    if (isReservedWindowsName(out)) out = "_" + out;
    return out;
}

std::string MacroStore::uniqueName(const std::string& baseName) const {
    const std::string base = sanitizeName(baseName);
    std::string candidate = base;
    std::error_code ec;
    for (int n = 2; fs::exists(root_ / (candidate + kExtension), ec); ++n) {
        candidate = base + " (" + std::to_string(n) + ")";
    }
    return candidate;
}

StoreError MacroStore::resolve(const std::string& fileName, fs::path& out) const {
    if (fileName.empty() || fileName.find_first_of("/\\:") != std::string::npos ||
        fileName == "." || fileName == "..") {
        return StoreError::InvalidName;
    }
    const std::string ext = kExtension;
    if (fileName.size() <= ext.size() || fileName.compare(fileName.size() - ext.size(), ext.size(), ext) != 0) {
        return StoreError::InvalidName;
    }
    const fs::path candidate = (root_ / fileName).lexically_normal();
    if (candidate.parent_path() != root_.lexically_normal()) return StoreError::InvalidName;
    out = candidate;
    return StoreError::Ok;
}

StoreError MacroStore::save(const std::string& baseName, const Macro& macro, std::string& savedFileName,
                            bool overwrite, std::string& message) {
    const std::string fileName = sanitizeName(baseName) + kExtension;
    fs::path target;
    if (resolve(fileName, target) != StoreError::Ok) {
        message = "invalid file name";
        return StoreError::InvalidName;
    }

    std::error_code ec;
    if (fs::exists(target, ec) && !overwrite) {
        message = fileName + " already exists";
        return StoreError::AlreadyExists;
    }

    std::vector<uint8_t> bytes;
    const gdr2::Result encoded = gdr2::encode(macro, bytes);
    if (!encoded.ok()) {
        message = encoded.message;
        return StoreError::Format;
    }

    fs::create_directories(root_, ec);
    if (ec) {
        message = "could not create the macro folder";
        return StoreError::Io;
    }

    // Write to a temporary file, then rename, so a crash never leaves a half-written macro.
    fs::path temp = target;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            message = "could not create the file";
            return StoreError::Io;
        }
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        out.close();
        if (!out) {
            fs::remove(temp, ec);
            message = "could not write the file";
            return StoreError::Io;
        }
    }
    fs::rename(temp, target, ec);
    if (ec) {
        fs::remove(temp, ec);
        message = "could not replace the file";
        return StoreError::Io;
    }
    savedFileName = fileName;
    message = "Saved " + fileName + ".";
    return StoreError::Ok;
}

StoreError MacroStore::load(const std::string& fileName, Macro& out, std::string& message) const {
    fs::path path;
    if (resolve(fileName, path) != StoreError::Ok) {
        message = "invalid file name";
        return StoreError::InvalidName;
    }
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) {
        message = fileName + " was not found";
        return StoreError::NotFound;
    }
    std::vector<uint8_t> bytes;
    const StoreError read = readFile(path, bytes, message);
    if (read != StoreError::Ok) return read;

    Macro loaded;
    const gdr2::Result decoded = gdr2::decode(bytes, loaded);
    if (!decoded.ok()) {
        message = decoded.message;
        return StoreError::Format;
    }
    out = std::move(loaded);
    message = "Loaded " + fileName + ".";
    return StoreError::Ok;
}

StoreError MacroStore::importFile(const fs::path& source, Macro& out, std::string& message) const {
    const std::string ext = source.extension().string();
    if (ext == ".gdr") {
        message = "Legacy .gdr (GDR1) files cannot be read because their format is undocumented.";
        return StoreError::UnsupportedFormat;
    }
    if (ext != kExtension) {
        message = "Only .gdr2 files can be imported.";
        return StoreError::UnsupportedFormat;
    }
    std::vector<uint8_t> bytes;
    const StoreError read = readFile(source, bytes, message);
    if (read != StoreError::Ok) return read;

    Macro loaded;
    const gdr2::Result decoded = gdr2::decode(bytes, loaded);
    if (!decoded.ok()) {
        message = decoded.message;
        return StoreError::Format;
    }
    out = std::move(loaded);
    message = "Imported " + source.filename().string() + ".";
    return StoreError::Ok;
}

StoreError MacroStore::rename(const std::string& fileName, const std::string& newBaseName, std::string& newFileName) {
    fs::path from;
    if (resolve(fileName, from) != StoreError::Ok) return StoreError::InvalidName;
    const std::string target = sanitizeName(newBaseName) + kExtension;
    fs::path to;
    if (resolve(target, to) != StoreError::Ok) return StoreError::InvalidName;

    std::error_code ec;
    if (!fs::is_regular_file(from, ec)) return StoreError::NotFound;
    if (fs::exists(to, ec)) return StoreError::AlreadyExists;
    fs::rename(from, to, ec);
    if (ec) return StoreError::Io;
    newFileName = target;
    return StoreError::Ok;
}

StoreError MacroStore::remove(const std::string& fileName) {
    fs::path path;
    if (resolve(fileName, path) != StoreError::Ok) return StoreError::InvalidName;
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return StoreError::NotFound;
    fs::remove(path, ec);
    return ec ? StoreError::Io : StoreError::Ok;
}

std::vector<StoredMacro> MacroStore::list() const {
    std::vector<StoredMacro> result;
    std::error_code ec;
    fs::directory_iterator it(root_, ec);
    if (ec) return result;
    for (const fs::directory_iterator end; it != end; it.increment(ec)) {
        if (ec) break;
        const fs::path& path = it->path();
        std::error_code entryEc;
        if (!it->is_regular_file(entryEc) || path.extension().string() != kExtension) continue;
        StoredMacro entry;
        entry.fileName = path.filename().string();
        entry.name = path.stem().string();
        entry.bytes = fs::file_size(path, entryEc);
        result.push_back(std::move(entry));
    }
    std::sort(result.begin(), result.end(),
              [](const StoredMacro& a, const StoredMacro& b) { return a.fileName < b.fileName; });
    return result;
}

}  // namespace tmac
