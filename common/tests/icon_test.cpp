// icon_test.cpp - Unit tests for shared render/IconResolver.
// Hermetic: builds a fake $HOME/$XDG_DATA_DIRS icon tree in mkdtemp, so no
// system theme (or its absence) affects the outcome. Env is save/restored.
#include "render/IconResolver.hpp"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace {

std::string g_tmp;
std::string g_oldHome;
std::string g_oldDataDirs;
std::string g_oldDataHome;
std::string g_oldIconTheme;
std::string g_oldWayTheme;

void save_env(const char* k, std::string& slot) {
    const char* v = ::getenv(k);
    slot = v ? v : "";
}

void setup_env() {
    save_env("HOME", g_oldHome);
    save_env("XDG_DATA_DIRS", g_oldDataDirs);
    save_env("XDG_DATA_HOME", g_oldDataHome);
    save_env("XDG_ICON_THEME", g_oldIconTheme);
    save_env("WAYLAUNCH_ICON_THEME", g_oldWayTheme);
    char tmpl[] = "/tmp/wl-icon-test-XXXXXX";
    if (::mkdtemp(tmpl) == nullptr) {
        std::abort();
    }
    g_tmp = tmpl;
    ::setenv("HOME", (g_tmp + "/home").c_str(), 1);
    ::setenv("XDG_DATA_DIRS", (g_tmp + "/share").c_str(), 1);
    ::setenv("XDG_DATA_HOME", (g_tmp + "/data-home").c_str(), 1);
    ::setenv("WAYLAUNCH_ICON_THEME", "testtheme", 1);
    ::unsetenv("XDG_ICON_THEME");
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
    if (g_oldIconTheme.empty()) {
        ::unsetenv("XDG_ICON_THEME");
    } else {
        ::setenv("XDG_ICON_THEME", g_oldIconTheme.c_str(), 1);
    }
    if (g_oldWayTheme.empty()) {
        ::unsetenv("WAYLAUNCH_ICON_THEME");
    } else {
        ::setenv("WAYLAUNCH_ICON_THEME", g_oldWayTheme.c_str(), 1);
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

// 16x16 solid-red PNG via cairo (no fixture binaries in the repo).
void write_png(const std::string& p, int size) {
    cairo_surface_t* s = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, size, size);
    assert(cairo_surface_status(s) == CAIRO_STATUS_SUCCESS);
    cairo_t* cr = cairo_create(s);
    cairo_set_source_rgb(cr, 1, 0, 0);
    cairo_paint(cr);
    assert(cairo_surface_write_to_png(s, p.c_str()) == CAIRO_STATUS_SUCCESS);
    cairo_destroy(cr);
    cairo_surface_destroy(s);
}

void build_theme_tree() {
    // $XDG_DATA_DIRS/icons/testtheme: index with 16 + 48 Fixed dirs,
    // Inherits hicolor; hicolor holds the fallback icon.
    const std::string t = g_tmp + "/share/icons/testtheme";
    mkdir_p(t + "/16x16/apps");
    mkdir_p(t + "/48x48/apps");
    mkdir_p(g_tmp + "/share/icons/hicolor/48x48/apps");
    write_file(t + "/index.theme",
               "[Icon Theme]\n"
               "Name=Test\n"
               "Directories=16x16/apps,48x48/apps\n"
               "Inherits=hicolor\n"
               "\n"
               "[16x16/apps]\n"
               "Size=16\n"
               "Type=Fixed\n"
               "\n"
               "[48x48/apps]\n"
               "Size=48\n"
               "Type=Fixed\n");
    write_file(g_tmp + "/share/icons/hicolor/index.theme",
               "[Icon Theme]\nName=Hicolor\nDirectories=48x48/apps\n");
    write_png(t + "/16x16/apps/sized.png", 16);
    write_png(t + "/48x48/apps/sized.png", 48);
    write_png(g_tmp + "/share/icons/hicolor/48x48/apps/fallback.png", 48);
}

int surf_w(cairo_surface_t* s) {
    return cairo_image_surface_get_width(s);
}

void test_size_distance_ordering() {
    qypr::IconResolver r;
    // 16px request resolves the 16px file, not the nearer-in-path 48px one.
    cairo_surface_t* s16 = r.get("sized", 16);
    assert(s16);
    assert(surf_w(s16) == 16);
    // 48px request resolves the 48px file (cache key includes size).
    cairo_surface_t* s48 = r.get("sized", 48);
    assert(s48);
    assert(surf_w(s48) == 48);
    assert(s48 != s16);
    // Repeated lookup hits the cache (same pointer, no rescan).
    assert(r.get("sized", 16) == s16);
    printf("[PASS] size-distance ordering + sized cache\n");
}

void test_inheritance_fallback_and_miss() {
    qypr::IconResolver r;
    // Only in hicolor (inherited): found through the Inherits chain.
    cairo_surface_t* fb = r.get("fallback", 48);
    assert(fb);
    // Unknown name: miss (nullptr), cached so the chain is not rescanned.
    assert(r.get("no-such-icon-xyz", 48) == nullptr);
    assert(r.get("no-such-icon-xyz", 48) == nullptr);
    // Empty name: miss without touching the filesystem.
    assert(r.get("", 48) == nullptr);
    printf("[PASS] inheritance fallback + miss caching\n");
}

void test_absolute_path_passthrough() {
    qypr::IconResolver r;
    cairo_surface_t* s =
        r.get(g_tmp + "/share/icons/testtheme/48x48/apps/sized.png", 48);
    assert(s);
    assert(surf_w(s) == 48);
    printf("[PASS] absolute path passthrough\n");
}

}  // namespace

int main() {
    setup_env();
    build_theme_tree();
    test_size_distance_ordering();
    test_inheritance_fallback_and_miss();
    test_absolute_path_passthrough();
    restore_env();
    printf("All IconResolver unit tests passed successfully!\n");
    return 0;
}
