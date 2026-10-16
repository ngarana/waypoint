// Regression test for wl_output size handling in WaylandCore.
//
// A wl_output emits two size-bearing events with DIFFERENT units: `mode` carries
// the resolution in PIXELS, while `geometry` carries the panel's physical size in
// MILLIMETRES. Only the pixel size is meaningful for layout. A prior bug stored
// the geometry millimetres in the same width/height fields, so on real hardware
// the launcher saw a 1200px-tall monitor as 190 and could neither clamp nor
// scroll its result list (the panel overflowed off the bottom of the screen).
//
// It stayed invisible in CI because it only manifests when the compositor emits
// `geometry` AFTER `mode` (as Hyprland does); replaying that order here locks the
// behaviour down. No compositor is needed: WaylandCore default-constructs without
// a connection, and the handlers match an injected OutputInfo by its wl_output*
// key, which is only ever compared, never dereferenced.

#include "waylaunch/wayland_core.h"

#include <cassert>
#include <cstdint>
#include <iostream>
#include <wayland-client.h>

using namespace waylaunch;

int main() {
    WaylandCore core;

    // A sentinel key; output_for() compares the pointer, it never reads through it.
    int sentinel = 0;
    auto* out = reinterpret_cast<wl_output*>(&sentinel);
    core.outputs_.push_back({.output = out, .width = 0, .height = 0, .scale = 1, .name = ""});

    // The active mode reports the resolution in pixels.
    core.handle_output_mode(out, WL_OUTPUT_MODE_CURRENT, 1920, 1200, 60000);
    assert(core.output_width() == 1920);
    assert(core.output_height() == 1200);

    // geometry carries physical millimetres (here a 300x190 mm panel) and must
    // NOT touch the pixel size, even though (under Hyprland) it arrives AFTER the
    // mode event.
    core.handle_output_geometry(out, 0, 0, 300, 190, 0, 0);
    assert(core.output_width() == 1920 && "geometry millimetres clobbered pixel width");
    assert(core.output_height() == 1200 && "geometry millimetres clobbered pixel height");

    // A non-current mode (an alternative resolution the panel advertises) must be
    // ignored so it cannot replace the active one.
    core.handle_output_mode(out, 0U, 800, 600, 60000);
    assert(core.output_width() == 1920 && "non-current mode overwrote the active mode");
    assert(core.output_height() == 1200 && "non-current mode overwrote the active mode");

    std::cout << "wayland_output_test: OK\n";
    return 0;
}
