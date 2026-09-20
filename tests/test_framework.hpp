// test_framework.hpp - Shared harness for qypr's modular unit tests.
//
// Split from tests/unit_tests.cpp: one TEST registry per TU, counters
// defined once in test_main.cpp. TEST bodies run during static
// initialisation in link order (see test ordering note in test_bar.cpp).
#pragma once

// unit_tests.cpp - Headless C++ unit test suite for qypr-lock.
// Exercises 100% of core logic, UI states, animations, events, and auth.

#include <iostream>
#include <sstream>
#include <cassert>
#include <string>
#include <vector>
#include <memory>
#include <cmath>
#include <thread>
#include <chrono>
#include <fstream>
#include <cstdlib>
#include <functional>
#include <filesystem>
#include <sys/stat.h>

// Helper to convert any type to string for test diagnostics
template <typename T>
static std::string to_str(const T& val) {
    std::ostringstream oss;
    oss << val;
    return oss.str();
}

// Simple testing framework macros

#define TEST(name)                                                                                  \
    void test_##name();      /* NOLINT(misc-use-internal-linkage) */                                \
    struct Register_##name { /* NOLINT(misc-use-internal-linkage) */                                \
        Register_##name() {                                                                         \
            std::cout << "Running test: " << #name << "...\n";                                      \
            g_tests_run++;                                                                          \
            try {                                                                                   \
                test_##name();                                                                      \
                std::cout << "  PASS\n";                                                            \
            } catch (const std::exception& e) {                                                     \
                std::cout << "  FAIL: " << e.what() << '\n';                                        \
                g_tests_failed++;                                                                   \
            } catch (...) {                                                                         \
                std::cout << "  FAIL: Unknown exception\n";                                         \
                g_tests_failed++;                                                                   \
            }                                                                                       \
        }                                                                                           \
    } register_##name; /* NOLINT(misc-use-internal-linkage,bugprone-throwing-static-initialization) \
                        */                                                                          \
    void test_##name()

// Loop-free assertion backend. do { } while (0) is the usual macro guard, but
// cppcoreguidelines-avoid-do-while fires at every expansion site (and a NOLINT
// on the macro definition does not cover expansions), so the checks live in
// plain inline functions: single evaluation, if/else-safe, no loop involved.
namespace qypr_test {

template <typename T>
inline void expect_true(const T& cond, const char* expr, const char* file, int line) {
    if (!cond) {
        throw std::runtime_error(std::string("Assertion failed: ") + expr + " at " + file + ":" +
                                 std::to_string(line));
    }
}

template <typename A, typename B>
inline void expect_eq(const A& a, const B& b, const char* expr_a, const char* expr_b,
                      const char* file, int line) {
    if (!(a == b)) {
        throw std::runtime_error(std::string("Assertion failed: ") + expr_a + " == " + expr_b +
                                 " (value: " + to_str(a) + " vs " + to_str(b) + ") at " + file +
                                 ":" + std::to_string(line));
    }
}

template <typename A, typename B, typename Eps>
inline void expect_near(const A& a, const B& b, const Eps& eps, const char* expr_a,
                        const char* expr_b, const char* file, int line) {
    if (std::abs(a - b) > eps) {
        throw std::runtime_error(std::string("Assertion failed: ") + expr_a + " ≈ " + expr_b +
                                 " at " + file + ":" + std::to_string(line));
    }
}

}  // namespace qypr_test

#define EXPECT_TRUE(cond) ::qypr_test::expect_true((cond), #cond, __FILE__, __LINE__)
#define EXPECT_FALSE(cond) EXPECT_TRUE(!(cond))
#define EXPECT_EQ(val1, val2)                                                                      \
    ::qypr_test::expect_eq((val1), (val2), #val1, #val2, __FILE__, __LINE__)
#define EXPECT_NEAR(val1, val2, eps)                                                               \
    ::qypr_test::expect_near((val1), (val2), (eps), #val1, #val2, __FILE__, __LINE__)

// Counters live in test_main.cpp (extern here so every TU shares them).
extern int g_tests_run;
extern int g_tests_failed;

// Enable access to private members for unit testing
// NOTE: keep these lowercase — an automated fix once uppercased them to
// PRIVATE/PROTECTED, which defines no-op macros and breaks every test that
// touches internals.
#define private public
#define protected public

#include "core/Types.hpp"
#include "core/EventLoop.hpp"
#include "core/ConfigWatcher.hpp"
#include "auth/PamAuthenticator.hpp"
#include "power/PowerManager.hpp"
#include "mpris/MprisController.hpp"
#include "video/VideoPlayer.hpp"
#include "wayland/WaylandDisplay.hpp"
#include "wayland/LockSession.hpp"
#include "wayland/Output.hpp"
#include "wayland/Seat.hpp"
#include "wayland/ShmBuffer.hpp"
#include "wayland/Cursor.hpp"
#include "notifications/NotificationMonitor.hpp"
#include "notifications/NotificationLog.hpp"
#include "ui/Theme.hpp"
#include "ui/Widget.hpp"
#include "ui/Clock.hpp"
#include "ui/PasswordField.hpp"
#include "ui/Notification.hpp"
#include "ui/AudioController.hpp"
#include "ui/ActionButton.hpp"
#include "ui/StatusMessage.hpp"
#include "ui/LockScreen.hpp"
#include "render/IconResolver.hpp"
#include "ui/statusbar/StatusBar.hpp"
#include "ui/statusbar/StatusIndicator.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/QSTile.hpp"
#include "ui/statusbar/QuickSettingsPanel.hpp"
#include "ui/statusbar/PopoverManager.hpp"
#include "ui/statusbar/DetailedPopover.hpp"
#include "core/Config.hpp"
#include "core/Process.hpp"  // QL-5/QL-6 spawn regressions
#include "ui/indicators/ClockIndicator.hpp"
#include "ui/indicators/NotificationIndicator.hpp"
#include "ui/indicators/PowerMenuIndicator.hpp"
#include "ui/indicators/BatteryIndicator.hpp"
#include "system/BatteryBackend.hpp"
#include "system/BrightnessBackend.hpp"
#include "system/SystemBus.hpp"
#include "system/BluetoothBackend.hpp"
#include "system/WifiBackend.hpp"
#include "system/DndState.hpp"
#include "system/VolumeBackend.hpp"
#include "system/SNIBackend.hpp"
#include "system/WorkspaceBackend.hpp"
#include "system/ToplevelBackend.hpp"
#include "system/SessionMapper.hpp"
#include "system/SystemStats.hpp"
#include "system/IdleInhibitor.hpp"
#include "system/DesktopIndex.hpp"
#include "system/StateCache.hpp"
#include "system/KeyboardLayout.hpp"
#include "ui/indicators/IdleInhibitorIndicator.hpp"
#include "ui/indicators/BluetoothIndicator.hpp"
#include "ui/indicators/BrightnessIndicator.hpp"
#include "ui/indicators/DNDIndicator.hpp"
#include "ui/indicators/VolumeIndicator.hpp"
#include "ui/indicators/WifiIndicator.hpp"
#include "ui/indicators/SNITrayHost.hpp"
#include "ui/indicators/WorkspacesIndicator.hpp"
#include "ui/indicators/PagerIndicator.hpp"
#include "ui/indicators/ActiveWindowIndicator.hpp"
#include "ui/indicators/LauncherIndicator.hpp"
#include "ui/indicators/MediaIndicator.hpp"
#include "ui/indicators/KeyboardLayoutIndicator.hpp"
#include "core/App.hpp"
#include "core/BarApp.hpp"

#undef private
#undef protected

#include <pwd.h>
#include <unistd.h>
#include <cstdio>               // tmpfile/fileno for a mmap-able keymap fd
#include <security/pam_appl.h>  // PAM_USER_UNKNOWN and friends

// Programmable stub controls, implemented in mocks.cpp.
extern "C" {
void mock_pam_reset();
void mock_pam_set_expected_password(const char* p);
void mock_pam_force_rc(int rc);
const char* mock_pam_last_user();

void mock_xkb_reset();
void mock_xkb_set_sym(uint32_t s);
void mock_xkb_set_utf8(const char* s);
void mock_xkb_set_repeats(int r);
void mock_xkb_set_layouts(uint32_t count);
}

// -----------------------------------------------------------------------------
// Tests
// -----------------------------------------------------------------------------

// Temp-config helper shared by the config/theme suites.
inline std::string writeTempConfig(const std::string& body) {
    std::string path =
        "/tmp/qypr-test-" + std::to_string(::getpid()) + "-" + std::to_string(::rand()) + ".conf";
    std::ofstream f(path);
    f << body;
    f.close();
    return path;
}

// Concrete test indicator shared by the registry/bar suites.
// (Lives here so both test_core.cpp's testFactory and test_bar.cpp see it.)
class TestIndicator : public qypr::StatusIndicator {
public:
    TestIndicator(const std::string& id, qypr::Zone zone, int priority)
        : StatusIndicator(id, zone, priority) {}

    [[nodiscard]] std::string icon() const override { return icon_; }
    [[nodiscard]] std::string tooltip() const override { return tooltip_; }
    [[nodiscard]] qypr::Color iconColor() const override { return qypr::theme::color::primary; }

    std::string icon_ = "T";
    std::string tooltip_ = "Test Indicator";
};
