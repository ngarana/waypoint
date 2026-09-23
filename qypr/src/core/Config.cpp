#include "core/Config.hpp"

#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace qypr {

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) { return ""; }
    const size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string lower(std::string s) {
    for (char& c : s) {
        if (c >= 'A' && c <= 'Z') { c += 32; }
    }
    return s;
}

bool isComment(const std::string& line) {
    return line.empty() || line[0] == '#' || line.starts_with("//");
}

std::string resolveImportPath(const std::string& importingFile, const std::string& importArg) {
    namespace fs = std::filesystem;
    fs::path const parent = fs::path(importingFile).parent_path();
    fs::path const resolved = parent / importArg;
    return resolved.string();
}

}  // namespace

std::string Config::configDir() {
    if (const char* xdg = std::getenv("XDG_CONFIG_HOME"); (xdg != nullptr) && ((*xdg) != 0)) {
        return std::string(xdg) + "/qypr";
    }
    if (const char* home = std::getenv("HOME"); (home != nullptr) && ((*home) != 0)) {
        return std::string(home) + "/.config/qypr";
    }
    return ".";  // last resort: cwd, so a missing HOME cannot crash startup
}

std::string Config::defaultPath() {
    return configDir() + "/bar.conf";
}

std::string Config::makeKey(const std::string& section, const std::string& key) {
    return section + "." + key;
}

// NOLINTNEXTLINE(misc-no-recursion) // bounded recursion over include chain
bool Config::loadFile(const std::string& path, std::set<std::string>& imported,
                      bool processImports) {
    // Resolve the real (canonical) path for cycle detection.
    std::string real;
    {
        std::error_code ec;
        real = std::filesystem::canonical(path, ec);
        if (ec) {
            real = path;  // file may not exist — still track literal
        }
    }
    if (static_cast<unsigned int>(imported.contains(real)) != 0U) {
        return false;  // circular import, silently skip
    }
    imported.insert(real);

    std::ifstream f(path);
    if (!f.is_open()) { return false; }

    std::string line;
    std::string section;
    std::vector<std::pair<std::string, std::string>> localPairs;  // ordered imports
    std::vector<std::string> imports;

    while (std::getline(f, line)) {
        line = trim(line);
        if (isComment(line)) { continue; }

        if (line.front() == '[') {
            const size_t close = line.find(']');
            if (close != std::string::npos) { section = trim(line.substr(1, close - 1)); }
            continue;
        }

        const size_t eq = line.find('=');
        if (eq != std::string::npos) {
            const std::string key = trim(line.substr(0, eq));
            if (!key.empty()) {
                localPairs.emplace_back(makeKey(section, key), trim(line.substr(eq + 1)));
            }
            continue;
        }

        // `import` directive: only honoured in the main/top-level file, not in
        // imported ones (imports inside imports would complicate order guarantees).
        if (processImports && line.starts_with("import") && line.size() > 6) {
            const std::string arg = trim(line.substr(6));
            if (!arg.empty()) { imports.push_back(resolveImportPath(path, arg)); }
        }
    }

    // Load imports first so the local file's keys override them.
    for (const auto& imp : imports) { loadFile(imp, imported, false); }

    // Write pairs in file order (last-wins within the same file).
    for (const auto& [k, v] : localPairs) { values_[k] = v; }

    return true;
}

bool Config::load(const std::string& path) {
    path_ = path.empty() ? defaultPath() : path;
    values_.clear();
    loaded_ = false;

    std::set<std::string> imported;
    const bool ok = loadFile(path_, imported, true);

    loaded_ = ok;
    return ok;
}

bool Config::has(const std::string& section, const std::string& key) const {
    return values_.contains(makeKey(section, key));
}

std::string Config::getString(const std::string& section, const std::string& key,
                              const std::string& def) const {
    auto it = values_.find(makeKey(section, key));
    return it == values_.end() ? def : it->second;
}

int Config::getInt(const std::string& section, const std::string& key, int def) const {
    auto it = values_.find(makeKey(section, key));
    if (it == values_.end()) { return def; }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return def;  // malformed value: keep the default rather than abort
    }
}

double Config::getDouble(const std::string& section, const std::string& key, double def) const {
    auto it = values_.find(makeKey(section, key));
    if (it == values_.end()) { return def; }
    try {
        return std::stod(it->second);
    } catch (...) { return def; }
}

bool Config::getBool(const std::string& section, const std::string& key, bool def) const {
    auto it = values_.find(makeKey(section, key));
    if (it == values_.end()) { return def; }
    const std::string v = lower(it->second);
    if (v == "true" || v == "yes" || v == "on" || v == "1") { return true; }
    if (v == "false" || v == "no" || v == "off" || v == "0") { return false; }
    return def;
}

std::vector<std::string> Config::getList(const std::string& section, const std::string& key,
                                         const std::vector<std::string>& def) const {
    auto it = values_.find(makeKey(section, key));
    if (it == values_.end()) { return def; }

    // An explicitly empty value means "this zone is empty" — not "use defaults".
    std::vector<std::string> out;
    const std::string& v = it->second;
    size_t start = 0;
    while (start <= v.size()) {
        const size_t comma = v.find(',', start);
        const std::string item =
            trim(v.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
        if (!item.empty()) { out.push_back(item); }
        if (comma == std::string::npos) { break; }
        start = comma + 1;
    }
    return out;
}

}  // namespace qypr
