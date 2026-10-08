// Held-key autorepeat in WaylandCore. Wayland delivers one event per physical
// press; the client must synthesise repeats itself. These tests drive
// handle_key() with a real xkb keymap and a real EventLoop (no compositor):
// the timer/state logic is what regressed, not the protocol plumbing.
#include "waylaunch/wayland_core.h"

#include "core/EventLoop.hpp"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/mman.h>
#include <unistd.h>
#include <wayland-client-protocol.h>
#include <xkbcommon/xkbcommon-keysyms.h>

using namespace waylaunch;

namespace {

// evdev codes (handle_key adds the +8 xkb offset itself).
constexpr uint32_t kBackspace = 14;
constexpr uint32_t kKeyA = 30;
constexpr uint32_t kLeftShift = 42;

constexpr int kRate = 100; // keys/s  -> 10 ms between repeats
constexpr int kDelay = 30; // ms before the first repeat

struct Fixture {
    qypr::EventLoop loop;
    WaylandCore core;
    int backspace = 0;
    int keyA = 0;
    int shift = 0;
    int releases = 0;

    explicit Fixture(bool attach_loop = true, int rate = kRate) {
        xkb_rule_names names{};
        names.rules = "evdev";
        names.model = "pc105";
        names.layout = "us";
        xkb_keymap* km =
            xkb_keymap_new_from_names(core.kbd_.xkb_ctx, &names, XKB_KEYMAP_COMPILE_NO_FLAGS);
        assert(km && "xkb data (evdev/pc105/us) must be installed to run this test");
        char* text = xkb_keymap_get_as_string(km, XKB_KEYMAP_FORMAT_TEXT_V1);
        xkb_keymap_unref(km);
        const size_t size = std::strlen(text) + 1;
        int fd = memfd_create("keymap", 0);
        assert(fd >= 0 && write(fd, text, size) == static_cast<ssize_t>(size));
        std::free(text);
        core.handle_keymap(WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, fd, static_cast<uint32_t>(size));

        core.kbd_.repeat_rate = rate;
        core.kbd_.repeat_delay = kDelay;
        if (attach_loop) core.set_event_loop(&loop);
        core.set_key_handler([this](uint32_t sym, uint32_t, bool pressed) {
            if (!pressed) {
                ++releases;
                return;
            }
            if (sym == XKB_KEY_BackSpace) ++backspace;
            else if (sym == XKB_KEY_a) ++keyA;
            else if (sym == XKB_KEY_Shift_L) ++shift;
        });
    }

    void press(uint32_t key) { core.handle_key(0, 0, key, WL_KEYBOARD_KEY_STATE_PRESSED); }
    void release(uint32_t key) { core.handle_key(0, 0, key, WL_KEYBOARD_KEY_STATE_RELEASED); }

    // Spin the loop for `ms`, delivering whatever timers come due.
    void run_for(int ms) {
        loop.addTimer(ms, false, [this] { loop.quit(); });
        loop.run();
    }
};

void held_key_repeats_until_release() {
    Fixture f;
    f.press(kBackspace);
    assert(f.backspace == 1 && "the press itself is delivered immediately, once");

    f.run_for(200);
    // 30 ms delay + 10 ms interval: ~17 repeats in 200 ms. Wide floor for slow CI.
    assert(f.backspace >= 5 && "a held Backspace must keep firing");

    f.release(kBackspace);
    const int at_release = f.backspace;
    f.run_for(100);
    assert(f.backspace == at_release && "no repeats after release");
}

void quick_tap_does_not_repeat() {
    Fixture f;
    f.press(kBackspace);
    f.release(kBackspace);
    f.run_for(150);
    assert(f.backspace == 1 && "released inside the delay: exactly one event");
}

void releasing_another_key_keeps_repeating() {
    Fixture f;
    f.press(kBackspace);
    f.press(kKeyA); // newest key takes over repeat, like every other toolkit
    f.release(kBackspace);
    const int before = f.keyA;
    f.run_for(150);
    assert(f.keyA > before + 3 && "releasing a different key must not cancel the held one");

    f.release(kKeyA);
    const int at_release = f.keyA;
    f.run_for(80);
    assert(f.keyA == at_release);
}

void newest_press_replaces_old_repeat() {
    Fixture f;
    f.press(kBackspace);
    f.press(kKeyA);
    const int bs = f.backspace;
    f.run_for(150);
    assert(f.backspace == bs && "old key stops repeating once a new one is pressed");
    assert(f.keyA > 3);
}

void modifiers_do_not_repeat() {
    Fixture f;
    f.press(kLeftShift);
    f.run_for(150);
    assert(f.shift == 1 && "xkb marks Shift as non-repeating");
}

void focus_loss_stops_repeat() {
    Fixture f;
    f.press(kBackspace);
    f.run_for(100);
    f.core.stop_repeat(); // what the wl_keyboard.leave trampoline calls
    const int at_leave = f.backspace;
    f.run_for(100);
    assert(f.backspace == at_leave && "the release never arrives after focus loss");
}

void rate_zero_disables_repeat() {
    Fixture f(/*attach_loop=*/true, /*rate=*/0);
    f.press(kBackspace);
    f.run_for(150);
    assert(f.backspace == 1 && "compositor rate 0 means repeat is off");
}

void no_event_loop_means_no_repeat() {
    Fixture f(/*attach_loop=*/false);
    f.press(kBackspace);
    f.run_for(150);
    assert(f.backspace == 1 && "overlays that never set a loop keep one event per press");
}

void teardown_while_repeating_is_safe() {
    // Destroying the core mid-repeat must remove its timer from the loop, or
    // the loop would later fire a callback into a dead object (ASan/valgrind
    // catches that; without a sanitizer this at least must not crash).
    qypr::EventLoop loop;
    {
        WaylandCore core;
        core.set_event_loop(&loop);
        core.kbd_.repeat_rate = kRate;
        core.kbd_.repeat_delay = 10;
        core.set_key_handler([](uint32_t, uint32_t, bool) {});
    } // no keymap loaded: nothing armed, destructor path still exercised
    loop.addTimer(40, false, [&] { loop.quit(); });
    loop.run();
}

} // namespace

int main() {
    held_key_repeats_until_release();
    quick_tap_does_not_repeat();
    releasing_another_key_keeps_repeating();
    newest_press_replaces_old_repeat();
    modifiers_do_not_repeat();
    focus_loss_stops_repeat();
    rate_zero_disables_repeat();
    no_event_loop_means_no_repeat();
    teardown_while_repeating_is_safe();
    std::puts("key_repeat_test: all passed");
    return 0;
}
