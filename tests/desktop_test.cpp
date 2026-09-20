// desktop_test.cpp - Unit tests for shared system/DesktopIndex.
// Hermetic: fake XDG applications tree in mkdtemp; env save/restored.
#include "system/DesktopIndex.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_tmp;
std::string g_oldHome;
std::string g_oldDataDirs;
std::string g_oldDataHome;

void save_env(const char* k, std::string& slot) {
    const char* v = ::getenv(k);
    slot = v ? v : "";
}

void setup_env() {
    save_env("HOME", g_oldHome);
    save_env("XDG_DATA_DIRS", g_oldDataDirs);
    save_env("XDG_DATA_HOME", g_oldDataHome);
    char tmpl[] = "/tmp/wl-desktop-test-XXXXXX";
    if (::mkdtemp(tmpl) == nullptr) {
        std::abort();
    }
    g_tmp = tmpl;
    ::setenv("HOME", (g_tmp + "/home").c_str(), 1);
    ::setenv("XDG_DATA_HOME", (g_tmp + "/data-home").c_str(), 1);
    ::setenv("XDG_DATA_DIRS", (g_tmp + "/share").c_str(), 1);
}

void restore_env() {
    ::setenv("HOME", g_oldHome.c_str(), 1);
    if (g_oldDataDirs.empty()) {
        ::unsetenv("XDG_DATA_DIRS");
    } else {
        ::setenv("XDG_DATA_DIRS", g_oldDataDirs.c_str(), 1);
    }
    if (g_oldDataHome.empty()) {
        ::unsetenv("XDG_DATA_HOME");
    } else {
        ::setenv("XDG_DATA_HOME", g_oldDataHome.c_str(), 1);
    }
}

void mkdir_p(const std::string& p) {
    std::string cur;
    for (size_t i = 1; i <= p.size(); ++i) {
        if (i == p.size() || p[i] == '/') {
            cur = p.substr(0, i);
            ::mkdir(cur.c_str(), 0755);
        }
    }
}

void write_file(const std::string& p, const std::string& body) {
    FILE* f = std::fopen(p.c_str(), "w");
    assert(f);
    assert(std::fwrite(body.data(), 1, body.size(), f) == body.size());
    std::fclose(f);
}

void build_app_tree() {
    const std::string apps = g_tmp + "/share/applications";
    mkdir_p(apps);
    write_file(apps + "/firefox.desktop",
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=Firefox Web Browser\n"
               "GenericName=Web Browser\n"
               "Comment=Browse the World Wide Web\n"
               "Exec=firefox %u\n"
               "Icon=firefox\n"
               "Categories=Network;WebBrowser;\n");
    write_file(apps + "/hidden.desktop",
               "[Desktop Entry]\n"
               "Type=Application\n"
               "Name=Hidden App\n"
               "Exec=hidden\n"
               "NoDisplay=true\n");
    write_file(apps + "/nondesk.desktop",
               "[Desktop Entry]\n"
               "Type=Link\n"
               "Name=Link App\n"
               "Exec=linkapp\n");
}

bool has_name(const std::vector<const qypr::DesktopEntry*>& hits, const std::string& name) {
    for (const auto* e : hits) {
        if (e->name == name) { return true; }
    }
    return false;
}

void test_scan_filters_and_fields() {
    qypr::DesktopIndex idx;
    idx.load();
    assert(idx.loaded());
    // NoDisplay + non-Application filtered: exactly one entry survives.
    assert(idx.entries().size() == 1);
    const qypr::DesktopEntry& e = idx.entries().front();
    assert(e.name == "Firefox Web Browser");
    assert(e.exec == "firefox");  // field codes stripped
    assert(e.icon == "firefox");
    assert(e.genericName == "Web Browser");
    assert(e.comment == "Browse the World Wide Web");
    assert(e.categories == "Network;WebBrowser;");
    assert(e.id == "firefox");
    assert(!e.desktopPath.empty());
    assert(!e.searchKey.empty());
    printf("[PASS] scan filters + fields\n");
}

void test_search_prefix_ranking_and_haystack() {
    qypr::DesktopIndex idx;
    idx.load();
    // Prefix on the name ranks (single hit here either way).
    auto hits = idx.search("fire");
    assert(hits.size() == 1);
    // Comment-only match: found via the precomputed haystack, one find().
    hits = idx.search("world wide");
    assert(hits.size() == 1 && has_name(hits, "Firefox Web Browser"));
    // Category-only match.
    hits = idx.search("webbrowser");
    assert(has_name(hits, "Firefox Web Browser"));
    // Miss.
    assert(idx.search("no-such-app-xyz").empty());
    // Empty query returns everything scanned.
    assert(idx.search("").size() == 1);
    printf("[PASS] search ranking + haystack\n");
}

void test_search_paths_override() {
    // A second tree with one app; override picks exactly it.
    const std::string other = g_tmp + "/other-apps";
    mkdir_p(other);
    write_file(other + "/other.desktop",
               "[Desktop Entry]\nType=Application\nName=Other App\nExec=other\n");
    qypr::DesktopIndex idx;
    idx.setSearchPaths({other});
    idx.load();
    assert(idx.entries().size() == 1);
    assert(idx.entries().front().name == "Other App");
    printf("[PASS] search-paths override\n");
}

void test_resolve_tiers() {
    qypr::DesktopIndex idx;
    idx.load();
    assert(idx.resolve("firefox") != nullptr);          // exact id
    assert(idx.resolve("Firefox Web Browser") != nullptr);  // exact name
    assert(idx.resolve("missing-xyz") == nullptr);
    printf("[PASS] resolve tiers\n");
}

}  // namespace

int main() {
    setup_env();
    build_app_tree();
    test_scan_filters_and_fields();
    test_search_prefix_ranking_and_haystack();
    test_search_paths_override();
    test_resolve_tiers();
    restore_env();
    printf("All DesktopIndex unit tests passed successfully!\n");
    return 0;
}
