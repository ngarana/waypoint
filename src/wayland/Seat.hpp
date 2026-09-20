// Seat.hpp - Keyboard (xkbcommon) and pointer input for the lock surfaces.
//
// Translates raw Wayland input into text / special keys / pointer events and
// forwards them to an InputSink. Owns key-repeat via the event loop's timers.

#pragma once

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace qypr {

class EventLoop;
class InputSink;
class Output;
struct OutputEnv;
class Cursor;

// Modifier bitmask passed with special keys.
enum Mod : uint32_t { MOD_SHIFT = 1u << 0, MOD_CTRL = 1u << 1, MOD_ALT = 1u << 2 };

class Seat {
public:
    Seat(wl_seat* seat, EventLoop& loop, const OutputEnv* env);
    ~Seat();

    void setSink(InputSink* sink) {
        sink_ = sink;
        // A sink can be attached after the keymap has already arrived (the bar
        // sets it post-connect). Re-report so the new sink learns the current
        // layout immediately instead of waiting for the next modifier event.
        lastReportedGroup_ = kNoGroup;
        reportLayout(currentGroup_);
    }
    // Resolve the focused wl_surface to its logical size (fills w/h, returns
    // false if unknown). Host-agnostic: the lock screen answers from its Output,
    // the bar from its BarWindow — Seat needs no concrete surface type.
    void setSurfaceSizer(std::function<bool(wl_surface*, int&, int&)> fn) {
        surfaceSizer_ = std::move(fn);
    }

    // Wayland C callbacks (public so listener tables at file scope can bind them).
    // wl_seat
    static void onCapabilities(void*, wl_seat*, uint32_t);
    static void onSeatName(void*, wl_seat*, const char*);

    // wl_keyboard
    static void onKeymap(void*, wl_keyboard*, uint32_t, int32_t, uint32_t);
    static void onKbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*);
    static void onKbLeave(void*, wl_keyboard*, uint32_t, wl_surface*);
    static void onKey(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onModifiers(void*, wl_keyboard*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onRepeatInfo(void*, wl_keyboard*, int32_t, int32_t);

    // wl_pointer
    static void onPtrEnter(void*, wl_pointer*, uint32_t, wl_surface*, wl_fixed_t, wl_fixed_t);
    static void onPtrLeave(void*, wl_pointer*, uint32_t, wl_surface*);
    static void onPtrMotion(void*, wl_pointer*, uint32_t, wl_fixed_t, wl_fixed_t);
    static void onPtrButton(void*, wl_pointer*, uint32_t, uint32_t, uint32_t, uint32_t);
    static void onPtrAxis(void*, wl_pointer*, uint32_t, uint32_t, wl_fixed_t);
    // No-op handlers required by wl_pointer >= v5 (bound at v7): every event of
    // the bound version must have a non-NULL listener slot or libwayland aborts.
    static void onPtrFrame(void*, wl_pointer*);
    static void onPtrAxisSource(void*, wl_pointer*, uint32_t);
    static void onPtrAxisStop(void*, wl_pointer*, uint32_t, uint32_t);
    static void onPtrAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t);

private:
    // Fill w/h with the current pointer surface's logical size; false if none.
    bool pointerSize(int& w, int& h) const;
    void handleKey(uint32_t keycode);
    // Push the active layout (group) to the sink if it changed since the last
    // report. Names/count come from the compiled keymap. Cheap no-op when the
    // group is unchanged or no keymap/sink is present.
    void reportLayout(uint32_t group);
    void startRepeat(uint32_t keycode);
    void stopRepeat();
    // Give the pointer a visible cursor (lazily built on first enter, once the
    // compositor and shm globals are available).
    void applyCursor(wl_pointer* pointer, uint32_t serial);

    wl_seat* seat_ = nullptr;
    EventLoop& loop_;
    const OutputEnv* env_ = nullptr;
    InputSink* sink_ = nullptr;
    std::function<bool(wl_surface*, int&, int&)> surfaceSizer_;
    std::unique_ptr<Cursor> cursor_;
    bool cursorInit_ = false;

    wl_keyboard* keyboard_ = nullptr;
    wl_pointer* pointer_ = nullptr;

    xkb_context* xkbContext_ = nullptr;
    xkb_keymap* xkbKeymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;

    // Active xkb layout group, and the last group actually reported to the sink
    // (so reportLayout() coalesces the frequent modifier events).
    static constexpr uint32_t kNoGroup = 0xffffffffu;
    uint32_t currentGroup_ = 0;
    uint32_t lastReportedGroup_ = kNoGroup;

    // Pointer focus
    wl_surface* pointerSurface_ = nullptr;
    double ptrX_ = 0, ptrY_ = 0;

    // Key repeat
    int repeatRate_ = 25;    // keys per second
    int repeatDelay_ = 600;  // ms before repeat begins
    uint32_t repeatKeycode_ = 0;
    int repeatTimer_ = -1;
};

}  // namespace qypr
