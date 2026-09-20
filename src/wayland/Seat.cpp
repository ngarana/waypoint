#include "wayland/Seat.hpp"

#include <sys/mman.h>
#include <unistd.h>

#include <cstring>
#include <string>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "wayland/Cursor.hpp"
#include "wayland/Output.hpp"

namespace qypr {

namespace {
const wl_keyboard_listener kKeyboardListener = {
    .keymap = Seat::onKeymap,
    .enter = Seat::onKbEnter,
    .leave = Seat::onKbLeave,
    .key = Seat::onKey,
    .modifiers = Seat::onModifiers,
    .repeat_info = Seat::onRepeatInfo,
};

const wl_pointer_listener kPointerListener = {
    .enter = Seat::onPtrEnter,
    .leave = Seat::onPtrLeave,
    .motion = Seat::onPtrMotion,
    .button = Seat::onPtrButton,
    .axis = Seat::onPtrAxis,
    .frame = Seat::onPtrFrame,
    .axis_source = Seat::onPtrAxisSource,
    .axis_stop = Seat::onPtrAxisStop,
    .axis_discrete = Seat::onPtrAxisDiscrete,
};

const wl_seat_listener kSeatListener = {
    .capabilities = Seat::onCapabilities,
    .name = Seat::onSeatName,
};

bool isSpecial(xkb_keysym_t sym) {
    switch (sym) {
        case XKB_KEY_Escape:
        case XKB_KEY_Return:
        case XKB_KEY_KP_Enter:
        case XKB_KEY_BackSpace:
        case XKB_KEY_Delete:
        case XKB_KEY_Tab:
        case XKB_KEY_Left:
        case XKB_KEY_Right:
        case XKB_KEY_Up:
        case XKB_KEY_Down:
        case XKB_KEY_Home:
        case XKB_KEY_End:
            return true;
        default:
            return false;
    }
}
}  // namespace

Seat::Seat(wl_seat* seat, EventLoop& loop, const OutputEnv* env)
    : seat_(seat),
      loop_(loop),
      env_(env) {
    xkbContext_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
    wl_seat_add_listener(seat_, &kSeatListener, this);
}

Seat::~Seat() {
    stopRepeat();
    if (keyboard_) wl_keyboard_destroy(keyboard_);
    if (pointer_) wl_pointer_destroy(pointer_);
    if (xkbState_) xkb_state_unref(xkbState_);
    if (xkbKeymap_) xkb_keymap_unref(xkbKeymap_);
    if (xkbContext_) xkb_context_unref(xkbContext_);
}

// -----------------------------------------------------------------------------
// Seat capabilities
// -----------------------------------------------------------------------------
void Seat::onCapabilities(void* data, wl_seat* seat, uint32_t caps) {
    auto* self = static_cast<Seat*>(data);
    bool kb = caps & WL_SEAT_CAPABILITY_KEYBOARD;
    bool ptr = caps & WL_SEAT_CAPABILITY_POINTER;

    if (kb && !self->keyboard_) {
        self->keyboard_ = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(self->keyboard_, &kKeyboardListener, self);
    } else if (!kb && self->keyboard_) {
        wl_keyboard_destroy(self->keyboard_);
        self->keyboard_ = nullptr;
    }

    if (ptr && !self->pointer_) {
        self->pointer_ = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(self->pointer_, &kPointerListener, self);
    } else if (!ptr && self->pointer_) {
        wl_pointer_destroy(self->pointer_);
        self->pointer_ = nullptr;
    }
}
void Seat::onSeatName(void*, wl_seat*, const char*) {}

// -----------------------------------------------------------------------------
// Keyboard
// -----------------------------------------------------------------------------
void Seat::onKeymap(void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
    auto* self = static_cast<Seat*>(data);
    if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
        close(fd);
        return;
    }
    char* map = static_cast<char*>(mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0));
    if (map == MAP_FAILED) {
        close(fd);
        return;
    }
    xkb_keymap* keymap = xkb_keymap_new_from_string(
        self->xkbContext_, map, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
    munmap(map, size);
    close(fd);
    if (!keymap) return;

    if (self->xkbState_) xkb_state_unref(self->xkbState_);
    if (self->xkbKeymap_) xkb_keymap_unref(self->xkbKeymap_);
    self->xkbKeymap_ = keymap;
    self->xkbState_ = xkb_state_new(keymap);
    // A fresh keymap may carry a different layout set: force a re-report.
    self->lastReportedGroup_ = kNoGroup;
    self->reportLayout(self->currentGroup_);
}

void Seat::onKbEnter(void*, wl_keyboard*, uint32_t, wl_surface*, wl_array*) {}
void Seat::onKbLeave(void* data, wl_keyboard*, uint32_t, wl_surface*) {
    static_cast<Seat*>(data)->stopRepeat();
}

void Seat::onKey(void* data, wl_keyboard*, uint32_t, uint32_t, uint32_t key, uint32_t state) {
    auto* self = static_cast<Seat*>(data);
    if (!self->xkbState_ || !self->sink_) return;
    const uint32_t keycode = key + 8;  // evdev -> xkb

    if (state == WL_KEYBOARD_KEY_STATE_PRESSED) {
        self->handleKey(keycode);
        if (self->xkbKeymap_ && xkb_keymap_key_repeats(self->xkbKeymap_, keycode))
            self->startRepeat(keycode);
    } else {
        if (self->repeatKeycode_ == keycode) self->stopRepeat();
    }
}

void Seat::handleKey(uint32_t keycode) {
    xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState_, keycode);

    uint32_t mods = 0;
    if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_SHIFT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_SHIFT;
    if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_CTRL, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_CTRL;
    if (xkb_state_mod_name_is_active(xkbState_, XKB_MOD_NAME_ALT, XKB_STATE_MODS_EFFECTIVE) > 0)
        mods |= MOD_ALT;

    if (isSpecial(sym) || (mods & (MOD_CTRL | MOD_ALT))) {
        sink_->onSpecialKey(sym, mods);
        return;
    }

    char buf[64];
    int n = xkb_state_key_get_utf8(xkbState_, keycode, buf, sizeof(buf));
    if (n > 0 && static_cast<unsigned char>(buf[0]) >= 0x20 && buf[0] != 0x7f)
        sink_->onTextInput(std::string(buf, n));
    // The keystroke may be a character of the password: leave nothing readable
    // in this stack frame (QL-3 in docs/LOCK_SECURITY_REVIEW.md). The sink copies
    // what it needs into a SecureBuffer.
    explicit_bzero(buf, sizeof(buf));
}

void Seat::onModifiers(void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched,
                       uint32_t locked, uint32_t group) {
    auto* self = static_cast<Seat*>(data);
    if (self->xkbState_)
        xkb_state_update_mask(self->xkbState_, depressed, latched, locked, 0, 0, group);
    self->currentGroup_ = group;
    self->reportLayout(group);
}

void Seat::reportLayout(uint32_t group) {
    if (!xkbKeymap_ || !sink_) return;
    if (group == lastReportedGroup_) return;
    uint32_t count = xkb_keymap_num_layouts(xkbKeymap_);
    if (count == 0) return;
    if (group >= count) group = 0;  // stale group after a keymap swap
    lastReportedGroup_ = group;
    const char* name = xkb_keymap_layout_get_name(xkbKeymap_, group);
    sink_->onLayoutChanged(name ? name : "", group, count);
}

void Seat::onRepeatInfo(void* data, wl_keyboard*, int32_t rate, int32_t delay) {
    auto* self = static_cast<Seat*>(data);
    self->repeatRate_ = rate;
    self->repeatDelay_ = delay;
}

void Seat::startRepeat(uint32_t keycode) {
    stopRepeat();
    if (repeatRate_ <= 0) return;  // repeat disabled
    repeatKeycode_ = keycode;
    const int rateMs = 1000 / repeatRate_;
    repeatTimer_ = loop_.addTimer(repeatDelay_, false, [this, rateMs] {
        handleKey(repeatKeycode_);
        repeatTimer_ = loop_.addTimer(rateMs, true, [this] { handleKey(repeatKeycode_); });
    });
}

void Seat::stopRepeat() {
    if (repeatTimer_ >= 0) {
        loop_.removeTimer(repeatTimer_);
        repeatTimer_ = -1;
    }
    repeatKeycode_ = 0;
}

// -----------------------------------------------------------------------------
// Pointer
// -----------------------------------------------------------------------------
void Seat::applyCursor(wl_pointer* pointer, uint32_t serial) {
    if (!cursorInit_) {  // build once, lazily — globals are ready by first enter
        cursorInit_ = true;
        if (env_ && env_->compositor && env_->shm)
            cursor_ = std::make_unique<Cursor>(env_->compositor, env_->shm);
    }
    if (cursor_) cursor_->apply(pointer, serial);
}

void Seat::onPtrEnter(void* data, wl_pointer* pointer, uint32_t serial, wl_surface* surface,
                      wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Seat*>(data);
    self->pointerSurface_ = surface;
    self->ptrX_ = wl_fixed_to_double(sx);
    self->ptrY_ = wl_fixed_to_double(sy);
    self->applyCursor(pointer, serial);
}

void Seat::onPtrLeave(void* data, wl_pointer*, uint32_t, wl_surface*) {
    auto* self = static_cast<Seat*>(data);
    self->pointerSurface_ = nullptr;
    if (self->sink_) self->sink_->onPointerLeave();
}

bool Seat::pointerSize(int& w, int& h) const {
    return pointerSurface_ && surfaceSizer_ && surfaceSizer_(pointerSurface_, w, h);
}

void Seat::onPtrMotion(void* data, wl_pointer*, uint32_t, wl_fixed_t sx, wl_fixed_t sy) {
    auto* self = static_cast<Seat*>(data);
    self->ptrX_ = wl_fixed_to_double(sx);
    self->ptrY_ = wl_fixed_to_double(sy);
    int w = 0, h = 0;
    if (self->sink_ && self->pointerSize(w, h)) {
        self->sink_->onPointerMotion(w, h, self->ptrX_, self->ptrY_);
    }
}

void Seat::onPtrButton(void* data, wl_pointer*, uint32_t, uint32_t, uint32_t button,
                       uint32_t state) {
    auto* self = static_cast<Seat*>(data);
    int w = 0, h = 0;
    if (!self->sink_ || !self->pointerSize(w, h)) return;
    self->sink_->onPointerButton(w, h, self->ptrX_, self->ptrY_, button,
                                 state == WL_POINTER_BUTTON_STATE_PRESSED);
}

void Seat::onPtrAxis(void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
    auto* self = static_cast<Seat*>(data);
    int w = 0, h = 0;
    if (!self->sink_ || !self->pointerSize(w, h)) return;
    const double v = wl_fixed_to_double(value);
    const double dx = axis == WL_POINTER_AXIS_HORIZONTAL_SCROLL ? v : 0.0;
    const double dy = axis == WL_POINTER_AXIS_VERTICAL_SCROLL ? v : 0.0;
    self->sink_->onPointerScroll(w, h, self->ptrX_, self->ptrY_, dx, dy);
}

// Scroll-frame grouping events: unused, but must be handled (see header).
void Seat::onPtrFrame(void*, wl_pointer*) {}
void Seat::onPtrAxisSource(void*, wl_pointer*, uint32_t) {}
void Seat::onPtrAxisStop(void*, wl_pointer*, uint32_t, uint32_t) {}
void Seat::onPtrAxisDiscrete(void*, wl_pointer*, uint32_t, int32_t) {}

}  // namespace qypr
