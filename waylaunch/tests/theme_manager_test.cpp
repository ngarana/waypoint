#include "waylaunch/theme_manager.h"

#include "waylaunch/config.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

using namespace waylaunch;

namespace {

// Distinct dark/light schemes so the resolved mode is observable.
const char* kColorsJson = R"({"colors": {
    "dark": {"background": "#111111", "primary": "#222222"},
    "light": {"background": "#eeeeee", "primary": "#dddddd"}
}})";

std::filesystem::path fresh_dir(const std::string& tag) {
    auto dir = std::filesystem::temp_directory_path() /
               ("waylaunch-theme-" + tag + "-" +
                std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(dir);
    return dir;
}

void write_file(const std::filesystem::path& path, const std::string& body) {
    std::ofstream file(path);
    file << body;
    file.close();
}

// Full [theme] section on disk: the manager re-reads the file when it moves,
// so everything it needs must live there (nothing assigned in code). The
// mtime is pushed forward on every write so consecutive rewrites in one test
// are always observable regardless of filesystem timestamp granularity.
std::string write_config(const std::filesystem::path& dir, const std::string& colors_path,
                         const std::string& source, const std::string& mode) {
    auto conf = dir / "config.toml";
    std::ofstream file(conf);
    file << "[theme]\n"
         << "source = \"" << source << "\"\n"
         << "matugen_path = \"" << colors_path << "\"\n"
         << "mode = \"" << mode << "\"\n"
         << "[theme.colors]\n"
         << "background = \"#000000\"\n";
    file.close();
    auto later = std::filesystem::last_write_time(conf) + std::chrono::seconds(5);
    std::filesystem::last_write_time(conf, later);
    return conf.string();
}

} // namespace

void test_static_modes_pass_through() {
    auto dir = fresh_dir("static");
    std::string colors = (dir / "colors.json").string();
    write_file(dir / "colors.json", kColorsJson);
    std::string conf_path = write_config(dir, colors, "static", "dark");

    Config repo_config;
    assert(repo_config.load(conf_path));

    ThemeManager themes;
    // Static source ignores mode entirely.
    assert(themes.colors(repo_config, conf_path).background == "#000000");
    repo_config.get().theme.mode = "light"; // same-file edit path below covers disk
    assert(themes.colors(repo_config, conf_path).background == "#000000");

    // Matugen source resolves per scheme (mtime push makes the rewrite
    // observable even within one filesystem tick).
    write_config(dir, colors, "matugen", "dark");
    assert(themes.colors(repo_config, conf_path).background == "#111111");
    std::cout << "[PASS] static modes pass through\n";
}

void test_poll_detects_config_edit() {
    auto dir = fresh_dir("poll");
    std::string colors = (dir / "colors.json").string();
    write_file(dir / "colors.json", kColorsJson);
    std::string conf_path = write_config(dir, colors, "matugen", "dark");

    Config repo_config;
    assert(repo_config.load(conf_path));

    ThemeManager themes;
    assert(themes.poll(repo_config, conf_path));  // first call always true
    assert(!themes.poll(repo_config, conf_path)); // steady state: no change
    // Flip the mode on disk; the next poll notices (write_config already
    // pushes the mtime forward).
    write_config(dir, colors, "matugen", "light");
    assert(themes.poll(repo_config, conf_path));
    assert(repo_config.get().theme.mode == "light");
    assert(themes.colors(repo_config, conf_path).background == "#eeeeee");
    std::cout << "[PASS] poll detects config edit\n";
}

void test_poll_detects_effective_mode_change_for_static_theme() {
    auto dir = fresh_dir("static-poll");
    std::string colors = (dir / "colors.json").string();
    write_file(dir / "colors.json", kColorsJson);
    std::string conf_path = write_config(dir, colors, "static", "dark");

    Config repo_config;
    assert(repo_config.load(conf_path));

    ThemeManager themes;
    assert(themes.poll(repo_config, conf_path));
    assert(!themes.poll(repo_config, conf_path));

    write_config(dir, colors, "static", "light");
    assert(themes.poll(repo_config, conf_path));
    assert(repo_config.get().theme.mode == "light");
    // Static themes intentionally keep their configured colors; the mode edge
    // still invalidates all overlay surfaces so auto-capable sources repaint.
    assert(themes.colors(repo_config, conf_path).background == "#000000");
    std::cout << "[PASS] poll detects static effective mode change\n";
}

void test_auto_resolves_either_scheme() {
    // No GeoClue stub here (live bus, any timezone): auto must resolve to
    // one of the two real schemes — membership, not the sun's position.
    auto dir = fresh_dir("auto");
    std::string colors = (dir / "colors.json").string();
    write_file(dir / "colors.json", kColorsJson);
    std::string conf_path = write_config(dir, colors, "matugen", "auto");

    Config repo_config;
    assert(repo_config.load(conf_path));

    ThemeManager themes;
    const std::string bg = themes.colors(repo_config, conf_path).background;
    assert(bg == "#111111" || bg == "#eeeeee");
    std::cout << "[PASS] auto resolves either scheme\n";
}

int main() {
    test_static_modes_pass_through();
    test_poll_detects_config_edit();
    test_poll_detects_effective_mode_change_for_static_theme();
    test_auto_resolves_either_scheme();
    std::cout << "theme_manager_test: all passed\n";
    return 0;
}
