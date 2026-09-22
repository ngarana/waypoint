cmake_minimum_required(VERSION 3.20)
project(qypr VERSION 0.1.0 LANGUAGES C CXX)

# Pre-release / build-metadata suffix appended to the semantic version string
# (e.g. "-rc1", "-dev+gabcdef0"). Leave empty for stable releases.
set(QYPR_VERSION_EXTRA "" CACHE STRING "Version pre-release / build-metadata suffix")

# -----------------------------------------------------------------------------
# Standards & defaults
# -----------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Coverage is a test-only opt-in (configure with -DENABLE_COVERAGE=ON). It is
# scoped to the qypr-test target below, so the shipped qypr-lock / qypr-record
# binaries are never instrumented (no -O0, no .gcda writes at runtime).
option(ENABLE_COVERAGE "Instrument the qypr-test target for coverage" OFF)

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -DNDEBUG")
add_compile_options(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

# -----------------------------------------------------------------------------
# Dependencies
# -----------------------------------------------------------------------------
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)

pkg_check_modules(WAYLAND REQUIRED IMPORTED_TARGET wayland-client)
pkg_check_modules(WLCURSOR REQUIRED IMPORTED_TARGET wayland-cursor)
pkg_check_modules(XKB      REQUIRED IMPORTED_TARGET xkbcommon)
pkg_check_modules(CAIRO    REQUIRED IMPORTED_TARGET cairo)
pkg_check_modules(PANGO    REQUIRED IMPORTED_TARGET pangocairo)
pkg_check_modules(SDBUS    REQUIRED IMPORTED_TARGET sdbus-c++)
pkg_check_modules(SYSTEMD  REQUIRED IMPORTED_TARGET libsystemd)  # sd-bus (notification monitor)
pkg_check_modules(UDEV     REQUIRED IMPORTED_TARGET libudev)     # backlight change events
pkg_check_modules(PULSE    REQUIRED IMPORTED_TARGET libpulse)    # volume (pipewire-pulse)
pkg_check_modules(MPV      REQUIRED IMPORTED_TARGET mpv)
pkg_check_modules(RSVG     REQUIRED IMPORTED_TARGET librsvg-2.0)  # SVG icon rasterization

find_library(PAM_LIBRARY pam REQUIRED)
find_path(PAM_INCLUDE_DIR security/pam_appl.h REQUIRED)

# -----------------------------------------------------------------------------
# Wayland protocol code generation (ext-session-lock-v1)
# -----------------------------------------------------------------------------
find_program(WAYLAND_SCANNER wayland-scanner REQUIRED)
pkg_get_variable(WAYLAND_PROTOCOLS_DIR wayland-protocols pkgdatadir)

set(GEN_DIR "${CMAKE_BINARY_DIR}/wayland-generated")
file(MAKE_DIRECTORY "${GEN_DIR}")

# Generate client-header + private-code for a protocol XML, appending the two
# outputs to PROTO_SOURCES. <name> is the file stem (without -v1.xml).
set(PROTO_SOURCES "")
function(qypr_gen_protocol name xml)
    set(hdr "${GEN_DIR}/${name}-client-protocol.h")
    set(src "${GEN_DIR}/${name}-protocol.c")
    # wlr-layer-shell's get_layer_surface has an argument literally named
    # `namespace` — a C++ keyword that breaks the generated header when included
    # from C++. Rename the identifier (whole-word; a no-op for other protocols).
    add_custom_command(OUTPUT "${hdr}"
        COMMAND "${WAYLAND_SCANNER}" client-header "${xml}" "${hdr}"
        COMMAND sed -i "s/\\bnamespace\\b/wl_namespace/g" "${hdr}"
        DEPENDS "${xml}" VERBATIM)
    add_custom_command(OUTPUT "${src}"
        COMMAND "${WAYLAND_SCANNER}" private-code "${xml}" "${src}"
        DEPENDS "${xml}" VERBATIM)
    set(PROTO_SOURCES ${PROTO_SOURCES} "${hdr}" "${src}" PARENT_SCOPE)
endfunction()

qypr_gen_protocol(ext-session-lock-v1
    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-session-lock/ext-session-lock-v1.xml")
# General-purpose bar widgets — compositor-agnostic protocols only:
#   workspaces  → ext-workspace-v1 (staging, standard)
#   active window / taskbar → wlr-foreign-toplevel-management (vendored; the only
#     widely-supported protocol carrying per-window *focus* state — ext's
#     foreign-toplevel-list is list-only).
qypr_gen_protocol(ext-workspace-v1
    "${WAYLAND_PROTOCOLS_DIR}/staging/ext-workspace/ext-workspace-v1.xml")
qypr_gen_protocol(wlr-foreign-toplevel-management-unstable-v1
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/protocols/wlr-foreign-toplevel-management-unstable-v1.xml")
# Standalone qypr-bar host surface:
#   wlr-layer-shell (vendored) anchors the bar as a desktop panel with an
#   exclusive zone. xdg-shell is generated only to satisfy the xdg_popup symbol
#   that layer-shell's get_popup request references — qypr-bar never uses popups.
qypr_gen_protocol(xdg-shell
    "${WAYLAND_PROTOCOLS_DIR}/stable/xdg-shell/xdg-shell.xml")
qypr_gen_protocol(wlr-layer-shell-unstable-v1
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/protocols/wlr-layer-shell-unstable-v1.xml")
# Idle inhibitor ("keep awake" toggle, Phase 15) — standard unstable protocol,
# respected by swayidle/hypridle and any compositor implementing it.
qypr_gen_protocol(idle-inhibit-unstable-v1
    "${WAYLAND_PROTOCOLS_DIR}/unstable/idle-inhibit/idle-inhibit-unstable-v1.xml")
# Night Light (wlr-gamma-control, vendored wlroots protocol) — lets the bar
# shift the screen colour temperature without a D-Bus daemon (Redshift) or
# any compositor-specific API. Per-output gamma ramps pushed once per toggle.
qypr_gen_protocol(wlr-gamma-control-unstable-v1
    "${CMAKE_CURRENT_SOURCE_DIR}/../common/protocols/wlr-gamma-control-unstable-v1.xml")

# -----------------------------------------------------------------------------
# Version header (generated)
# -----------------------------------------------------------------------------
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/Version.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/Version.hpp"
    @ONLY)

# sdbus-c++ v1 vs v2 API: v2 introduced the ServiceName/ObjectPath strong
# types and renamed processPendingRequest() to processPendingEvent(). CI
# images ship v1, developer machines v2 — detect, don't assume.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_LIBRARIES PkgConfig::SDBUS)
check_cxx_source_compiles(
    "#include <sdbus-c++/sdbus-c++.h>
     int main() { sdbus::ServiceName s{\"x\"}; (void)s; return 0; }"
    QYPR_SDBUS_HAS_SERVICE_NAME)
unset(CMAKE_REQUIRED_LIBRARIES)
configure_file(
    "${CMAKE_SOURCE_DIR}/src/core/SdbusApi.hpp.in"
    "${CMAKE_BINARY_DIR}/generated/SdbusApi.hpp"
    @ONLY)

# -----------------------------------------------------------------------------
# Targets
# -----------------------------------------------------------------------------
# Explicit per-target sources (ARCHITECTURE_REVIEW finding 1): every .cpp
# lives in exactly one list below (shared, lock-only, or entry point), so a
# newly added lock-only file can never silently leak into the bar through a
# glob again. Keep each list sorted; entry points carry their own main() and
# are added per-target, so no executable ever links two of them.
set(QYPR_SHARED_SOURCES
    src/core/BarApp.cpp
    src/core/BarBackendLifecycle.cpp
    src/core/BarRuntime.cpp
    src/core/BarSurfaceController.cpp
    src/core/Config.cpp
    src/core/ConfigRuntime.cpp
    src/core/ConfigWatcher.cpp
    src/core/SecureBuffer.cpp
    src/core/ThemeRuntime.cpp
    src/mpris/MprisController.cpp
    src/notifications/NotificationActions.cpp
    src/notifications/NotificationMonitor.cpp
    src/power/SystemActions.cpp
    src/system/BatteryBackend.cpp
    src/system/BluetoothAgent.cpp
    src/system/BluetoothBackend.cpp
    src/system/BrightnessBackend.cpp
    src/system/DbusMenuBackend.cpp
    src/system/GeoClueBackend.cpp
    src/system/IdleInhibitor.cpp
    src/system/KeyboardLayout.cpp
    src/system/NightLightBackend.cpp
    src/system/PowerProfilesBackend.cpp
    src/system/PulseLoop.cpp
    src/system/SessionMapper.cpp
    src/system/SNIBackend.cpp
    src/system/StateCache.cpp
    src/system/StateCacheCodec.cpp
    src/system/StateCacheCoordinator.cpp
    src/system/StateCacheStore.cpp
    src/system/SystemBus.cpp
    src/system/SystemStats.cpp
    src/system/ToplevelBackend.cpp
    src/system/VolumeBackend.cpp
    src/system/WifiBackend.cpp
    src/system/WorkspaceBackend.cpp
    src/ui/indicators/ActiveWindowIndicator.cpp
    src/ui/indicators/BatteryIndicator.cpp
    src/ui/indicators/BluetoothIndicator.cpp
    src/ui/indicators/BrightnessIndicator.cpp
    src/ui/indicators/ClockIndicator.cpp
    src/ui/indicators/DNDIndicator.cpp
    src/ui/indicators/IdleInhibitorIndicator.cpp
    src/ui/indicators/KeyboardLayoutIndicator.cpp
    src/ui/indicators/LauncherIndicator.cpp
    src/ui/indicators/MediaIndicator.cpp
    src/ui/indicators/NightLightIndicator.cpp
    src/ui/indicators/NotificationIndicator.cpp
    src/ui/indicators/PagerIndicator.cpp
    src/ui/indicators/PowerMenuIndicator.cpp
    src/ui/indicators/SNITrayHost.cpp
    src/ui/indicators/SystemMonitorIndicator.cpp
    src/ui/indicators/TaskbarIndicator.cpp
    src/ui/indicators/VolumeIndicator.cpp
    src/ui/indicators/WifiIndicator.cpp
    src/ui/indicators/WorkspacesIndicator.cpp
    src/ui/Notification.cpp
    src/ui/PaletteReader.cpp
    src/ui/PaletteSource.cpp
    src/ui/Theme.cpp
    src/ui/statusbar/IndicatorHost.cpp
    src/ui/statusbar/IndicatorRegistry.cpp
    src/ui/statusbar/MenuPopover.cpp
    src/ui/statusbar/PopoverManager.cpp
    src/ui/statusbar/QSTile.cpp
    src/ui/statusbar/QSTileFactory.cpp
    src/ui/statusbar/QuickSettingsInput.cpp
    src/ui/statusbar/QuickSettingsLayout.cpp
    src/ui/statusbar/QuickSettingsPanel.cpp
    src/ui/statusbar/StatusBar.cpp
    src/ui/statusbar/StatusBarInput.cpp
    src/ui/statusbar/StatusBarLayout.cpp
    src/ui/statusbar/StatusIndicator.cpp
    src/ui/statusbar/TileRenderer.cpp
    src/ui/statusbar/TooltipController.cpp
    src/wayland/BarDisplay.cpp
    src/wayland/BarWindow.cpp
    src/wayland/Cursor.cpp
    src/wayland/Output.cpp
    src/wayland/Seat.cpp
)
# NotificationLog is recorder-only (never in the shared set).
set(QYPR_RECORDER_SOURCES
    src/notifications/NotificationLog.cpp
)

# Lock-only sources: the session-lock app and everything reachable only from it
# (its UI, the video wallpaper, PAM). qypr-bar links none of these.
#
# This split is a startup-latency guard, not just tidiness. VideoPlayer.cpp is
# the sole reason libmpv appears in a link line, and libmpv drags in the whole
# ffmpeg stack — measured at 201 extra shared objects and 213 MB of mapped
# closure that the bar never calls into. On a cold boot the dynamic loader must
# open and fault every one of them before main() runs, which is what made the
# bar appear seconds after the desktop. Keep this list in sync when a new
# lock-only file lands: the include graph must never take the bar back to mpv
# or PAM.
set(QYPR_LOCK_ONLY_SOURCES
    src/core/App.cpp
    src/core/LockRuntime.cpp
    src/ui/Shell.cpp
    src/ui/ShellInputRouter.cpp
    src/ui/LockScreen.cpp
    src/ui/AudioController.cpp
    src/ui/ActionButton.cpp
    src/ui/PasswordField.cpp
    src/ui/Clock.cpp
    src/ui/ConfirmPopover.cpp
    src/ui/StatusMessage.cpp
    src/wayland/LockSession.cpp
    src/wayland/WaylandDisplay.cpp
    src/video/VideoPlayer.cpp
    src/auth/PamAuthenticator.cpp
)

# qypr-bar is exactly the shared set: lock-only sources were never in it,
# so no denylist step remains. (The B1 structural gate in
# scripts/check-invariants.sh guards this from the link-line side too.)

# Shared core (libwl-common subtree): canonical protocol XML (referenced by
# qypr_gen_protocol above) + the EventLoop reactor. The subtree root goes on
# every target's include path so `#include "core/EventLoop.hpp"` resolves
# identically in both repos; WL_COMMON_SOURCES joins each binary explicitly
# (third-party/ is outside the src/ glob).
set(WL_COMMON_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common")
set(WL_COMMON_SOURCES
    "${WL_COMMON_DIR}/core/EventLoop.cpp"
    "${WL_COMMON_DIR}/core/Process.cpp"
    "${WL_COMMON_DIR}/core/SolarCalc.cpp"
    "${WL_COMMON_DIR}/system/DesktopIndex.cpp"
    "${WL_COMMON_DIR}/toplevel/ToplevelStates.cpp")
# Render helpers: bar/lock/test only. qypr-record never draws (linking cairo
# into it would drag the graphics stack into a daemon).
set(WL_COMMON_RENDER_SOURCES
    "${WL_COMMON_DIR}/render/Painter.cpp"
    "${WL_COMMON_DIR}/render/IconResolver.cpp"
    "${WL_COMMON_DIR}/render/MatugenTokens.cpp")
# CPU shm buffers (bar/lock/test; recorder never draws).
set(WL_COMMON_WAYLAND_SOURCES "${WL_COMMON_DIR}/wayland/ShmBuffer.cpp")

# 1. Main Lock Screen target (links all graphics, mpv, PAM, sdbus-c++, etc.)
add_executable(qypr-lock
    ${QYPR_SHARED_SOURCES}
    ${QYPR_LOCK_ONLY_SOURCES}
    ${WL_COMMON_SOURCES}
    ${WL_COMMON_RENDER_SOURCES}
    ${WL_COMMON_WAYLAND_SOURCES}
    src/main.cpp
    ${PROTO_SOURCES})

target_include_directories(qypr-lock PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${WL_COMMON_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
    "${GEN_DIR}"
    "${PAM_INCLUDE_DIR}")

target_link_libraries(qypr-lock PRIVATE
    PkgConfig::WAYLAND
    PkgConfig::WLCURSOR
    PkgConfig::XKB
    PkgConfig::CAIRO
    PkgConfig::PANGO
    PkgConfig::SDBUS
    PkgConfig::SYSTEMD
    PkgConfig::UDEV
    PkgConfig::PULSE
    PkgConfig::MPV
    PkgConfig::RSVG
    "${PAM_LIBRARY}"
    Threads::Threads)

# 1b. Standalone status-bar target (qypr-bar): the unlocked-desktop bar on
# wlr-layer-shell. Shares the *bar* object set with qypr-lock — the lock app,
# its UI, the video wallpaper and PAM are excluded (QYPR_LOCK_ONLY_SOURCES
# above), so neither libmpv nor libpam is on the bar's startup path.
add_executable(qypr-bar
    ${QYPR_SHARED_SOURCES}
    ${WL_COMMON_SOURCES}
    ${WL_COMMON_RENDER_SOURCES}
    ${WL_COMMON_WAYLAND_SOURCES}
    src/bar_main.cpp
    ${PROTO_SOURCES})

target_include_directories(qypr-bar PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${WL_COMMON_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
    "${GEN_DIR}")

target_link_libraries(qypr-bar PRIVATE
    PkgConfig::WAYLAND
    PkgConfig::WLCURSOR
    PkgConfig::XKB
    PkgConfig::CAIRO
    PkgConfig::PANGO
    PkgConfig::SDBUS
    PkgConfig::SYSTEMD
    PkgConfig::UDEV
    PkgConfig::PULSE
    PkgConfig::RSVG
    Threads::Threads)

# 2. Standalone Recorder target (only needs systemd for sd-bus)
add_executable(qypr-record
    src/core/Config.cpp          # NotificationMonitor resolves its XDG config dir
    "${WL_COMMON_DIR}/core/EventLoop.cpp"
    src/notifications/NotificationMonitor.cpp
    ${QYPR_RECORDER_SOURCES}
    src/recorder_main.cpp
)

target_include_directories(qypr-record PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${WL_COMMON_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(qypr-record PRIVATE
    PkgConfig::SYSTEMD
)

# 3. Unit Test suite target (mock/headless environment). Modular TUs
# (tests/test_*.cpp) share tests/test_framework.hpp; TEST bodies run during
# static initialisation in link order — keep test_bar.cpp's internal order
# (registry test before the focus-cycling test that reuses its indicator).
add_executable(qypr-test
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_theme.cpp
    tests/test_auth.cpp
    tests/test_system.cpp
    tests/test_wayland.cpp
    tests/test_lock.cpp
    tests/test_bar.cpp
    tests/test_indicators.cpp
    tests/test_notifications.cpp
    tests/test_desktop.cpp
    tests/mocks.cpp
    ${QYPR_SHARED_SOURCES}
    ${QYPR_LOCK_ONLY_SOURCES}
    ${WL_COMMON_SOURCES}
    ${WL_COMMON_RENDER_SOURCES}
    ${WL_COMMON_WAYLAND_SOURCES}
    src/main.cpp
    src/notifications/NotificationLog.cpp
    src/recorder_main.cpp
    ${PROTO_SOURCES})

# Registered with CTest (ARCHITECTURE_REVIEW drift fix): `ctest --test-dir
# <build>` runs the suite; the bare binary still works for direct runs.
enable_testing()
add_test(NAME qypr-test COMMAND qypr-test)

target_compile_definitions(qypr-test PRIVATE TESTING)

if(ENABLE_COVERAGE)
    target_compile_options(qypr-test PRIVATE --coverage -O0 -g)
    target_link_options(qypr-test PRIVATE --coverage)
endif()

target_include_directories(qypr-test PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${WL_COMMON_DIR}"
    "${CMAKE_BINARY_DIR}/generated"
    "${GEN_DIR}"
    "${PAM_INCLUDE_DIR}")

target_link_libraries(qypr-test PRIVATE
    PkgConfig::WAYLAND
    PkgConfig::WLCURSOR
    PkgConfig::XKB
    PkgConfig::CAIRO
    PkgConfig::PANGO
    PkgConfig::SYSTEMD
    PkgConfig::UDEV
    PkgConfig::PULSE
    PkgConfig::MPV
    PkgConfig::RSVG
    Threads::Threads)

# qypr-test is a headless, mock-linked binary — build it, but never install it.
install(TARGETS qypr-lock qypr-bar qypr-record RUNTIME DESTINATION bin)

# Man pages.
install(FILES man/man1/qypr-lock.1 man/man1/qypr-bar.1 man/man1/qypr-record.1
        DESTINATION share/man/man1)

# Example configuration (copy to $XDG_CONFIG_HOME/qypr/bar.conf to use).
install(FILES examples/bar.conf DESTINATION share/qypr/examples)
