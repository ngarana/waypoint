// DesktopIndex.cpp - .desktop application index implementation.
#include "system/DesktopIndex.hpp"

#include "core/EventLoop.hpp"
#include "core/Process.hpp"

#include <dirent.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <unordered_set>

namespace qypr {

namespace {

std::string lower(std::string s) {
    for (char& c : s) { c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); }
    return s;
}

bool truthy(const std::string& v) {
    return v == "true" || v == "1" || v == "yes";
}

std::string readFile(const std::string& path) {
    const std::ifstream f(path, std::ios::binary);
    if (!f) { return {}; }
    std::ostringstream out;
    out << f.rdbuf();
    return out.str();
}

// The application search path: XDG_DATA_HOME then XDG_DATA_DIRS, each +
// "/applications", with the spec's fallbacks. Reads process-startup
// environment only; nothing setenvs at runtime, so getenv is safe here.
// NOLINTBEGIN(concurrency-mt-unsafe)
std::vector<std::string> appDirs() {
    std::vector<std::string> dirs;
    const char* home = std::getenv("HOME");
    const char* dataHome = std::getenv("XDG_DATA_HOME");
    if (dataHome != nullptr && *dataHome != '\0') {
        dirs.push_back(std::string(dataHome) + "/applications");
    } else if (home != nullptr) {
        dirs.push_back(std::string(home) + "/.local/share/applications");
    }

    const char* dataDirs = std::getenv("XDG_DATA_DIRS");
    const std::string list =
        (dataDirs != nullptr && *dataDirs != '\0') ? dataDirs : "/usr/local/share:/usr/share";
    std::stringstream ss(list);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (!dir.empty()) { dirs.push_back(dir + "/applications"); }
    }
    return dirs;
}
// NOLINTEND(concurrency-mt-unsafe)

}  // namespace

std::string DesktopIndex::cleanExec(const std::string& exec) {
    std::string out;
    out.reserve(exec.size());
    for (size_t i = 0; i < exec.size(); ++i) {
        if (exec.at(i) == '%' && i + 1 < exec.size()) {
            const char c = exec.at(i + 1);
            // Freedesktop Exec field codes (incl. deprecated d D n N v m).
            if (std::strchr("fFuUickdDnNvm", c) != nullptr) {
                ++i;
                continue;
            }
            if (c == '%') {  // "%%" → literal "%"
                out += '%';
                ++i;
                continue;
            }
        }
        out += exec.at(i);
    }
    // Collapse doubled spaces the removals may have left, and trim.
    std::string tidy;
    bool prevSpace = false;
    for (const char c : out) {
        const bool sp = c == ' ' || c == '\t';
        if (sp && prevSpace) { continue; }
        tidy += c;
        prevSpace = sp;
    }
    const size_t a = tidy.find_first_not_of(" \t");
    const size_t b = tidy.find_last_not_of(" \t");
    return a == std::string::npos ? "" : tidy.substr(a, b - a + 1);
}

bool DesktopIndex::parseEntry(const std::string& body, DesktopEntry& out) {
    out = {};
    std::string type;
    std::string name;
    std::string exec;
    std::string icon;
    std::string categories;
    std::string comment;
    std::string genericName;
    bool noDisplay = false;
    bool hidden = false;
    bool terminal = false;
    bool inEntry = false;

    std::stringstream ss(body);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') { line.pop_back(); }
        if (line.empty() || line.at(0) == '#') { continue; }
        if (line.at(0) == '[') {
            // Only the main [Desktop Entry] group; stop at the first action group.
            inEntry = line == "[Desktop Entry]";
            continue;
        }
        if (!inEntry) { continue; }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) { continue; }
        const std::string key = line.substr(0, eq);
        const std::string val = line.substr(eq + 1);
        // Ignore localized keys (Name[de]=…): take the unlocalized value only.
        if (key == "Type") {
            type = val;
        } else if (key == "Name") {
            name = val;
        } else if (key == "Exec") {
            exec = val;
        } else if (key == "Icon") {
            icon = val;
        } else if (key == "Categories") {
            categories = val;
        } else if (key == "Comment") {
            comment = val;
        } else if (key == "GenericName") {
            genericName = val;
        } else if (key == "NoDisplay") {
            noDisplay = truthy(val);
        } else if (key == "Hidden") {
            hidden = truthy(val);
        } else if (key == "Terminal") {
            terminal = truthy(val);
        }
    }

    if (type != "Application") { return false; }
    if (noDisplay || hidden) { return false; }
    if (name.empty() || exec.empty()) { return false; }

    out.name = name;
    out.exec = cleanExec(exec);
    out.icon = icon;
    out.terminal = terminal;
    out.categories = categories;
    out.comment = comment;
    out.genericName = genericName;
    // Precomputed lowercase haystack: search() is one find() per entry.
    std::string key = name;
    key += ' ';
    key += genericName;
    key += ' ';
    key += comment;
    key += ' ';
    key += categories;
    out.searchKey = lower(key);
    return !out.exec.empty();
}

void DesktopIndex::load() {
    loaded_ = true;
    entries_.clear();
    std::unordered_set<std::string> seen;  // desktop-file id dedup (earlier dir wins)

    const std::vector<std::string> dirs = searchPaths_.empty() ? appDirs() : searchPaths_;
    for (const auto& dir : dirs) {
        DIR* d = opendir(dir.c_str());
        if (d == nullptr) { continue; }
        struct dirent* de = nullptr;
        // The DIR stream is local to this call; no thread shares it.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        while ((de = readdir(d)) != nullptr) {
            const std::string fname = de->d_name;
            if (fname.size() < 9 || !fname.ends_with(".desktop")) { continue; }
            if (!seen.insert(fname).second) { continue; }  // already provided by an earlier dir
            DesktopEntry e;
            std::string full = dir;
            full += '/';
            full += fname;
            if (parseEntry(readFile(full), e)) {
                e.id = fname.substr(0, fname.size() - 8);  // strip ".desktop"
                e.desktopPath = full;
                entries_.push_back(std::move(e));
            }
        }
        // Local DIR stream, see above.
        // NOLINTNEXTLINE(concurrency-mt-unsafe)
        closedir(d);
    }

    std::ranges::sort(entries_, [](const DesktopEntry& a, const DesktopEntry& b) {
        return lower(a.name) < lower(b.name);
    });
}

std::vector<const DesktopEntry*> DesktopIndex::search(const std::string& query) const {
    std::vector<const DesktopEntry*> prefix;
    std::vector<const DesktopEntry*> substr;
    const std::string q = lower(query);
    for (const auto& e : entries_) {
        if (q.empty()) {
            prefix.push_back(&e);
            continue;
        }
        const size_t pos = e.searchKey.find(q);
        if (pos == 0) {
            prefix.push_back(&e);
        } else if (pos != std::string::npos) {
            substr.push_back(&e);
        }
    }
    prefix.insert(prefix.end(), substr.begin(), substr.end());  // both already alphabetical
    return prefix;
}

const DesktopEntry* DesktopIndex::resolve(const std::string& key) const {
    if (key.empty()) { return nullptr; }
    const std::string q = lower(key);
    // The trailing dotted component, so an app-id like "org.telegram.desktop"
    // also matches an entry keyed "telegram", and vice-versa.
    auto tail = [](const std::string& s) {
        const size_t dot = s.rfind('.');
        return dot == std::string::npos ? s : s.substr(dot + 1);
    };
    const std::string qtail = tail(q);

    // 1. exact desktop-file id.
    for (const auto& e : entries_) {
        if (lower(e.id) == q) { return &e; }
    }
    // 2. trailing component of either side matches.
    for (const auto& e : entries_) {
        const std::string id = lower(e.id);
        if (tail(id) == qtail || id == qtail || tail(id) == q) { return &e; }
    }
    // 3. exact display name, then name prefix.
    for (const auto& e : entries_) {
        if (lower(e.name) == q) { return &e; }
    }
    for (const auto& e : entries_) {
        if (lower(e.name).starts_with(q)) { return &e; }
    }
    return nullptr;
}

bool launchDetached(EventLoop& loop, const std::string& cmd, bool terminal) {
    if (cmd.empty()) { return false; }
    std::string full = cmd;
    if (terminal) {
        // NOLINTNEXTLINE(concurrency-mt-unsafe): startup-path read; see appDirs.
        const char* term = std::getenv("TERMINAL");
        full = std::string(term != nullptr && *term != '\0' ? term : "xterm") + " -e " + cmd;
    }

    // New session so the app outlives the bar and has no controlling tty;
    // pidfd-reaped through the loop (no fork, no zombie, no SIGCHLD games).
    static constexpr const char* kShell = "/bin/sh";
    return spawnReaped(loop, kShell, {kShell, "-c", full}, /*newSession=*/true) >= 0;
}

}  // namespace qypr
