// mocks.cpp - Link-time stubs for C libraries (Wayland, XKB, PAM, mpv, sd-bus)
// used to test qypr-lock in a headless/mocked environment.

#include <sys/epoll.h>
#include <unistd.h>

#include <array>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

// Use the real PAM types/constants so the stubs exercise the exact same code
// path (conversation callback, return-code mapping) as production. Only the
// PAM *functions* are replaced at link time; qypr-test links no real libpam.
#include <security/pam_appl.h>

#define WAYLAND_EXPORT __attribute__((visibility("default")))

// NOLINTBEGIN(readability-identifier-naming, cppcoreguidelines-use-enum-class)
// Every symbol below mirrors a C library ABI (PAM, libmpv, Wayland, xkbcommon,
// sd-bus) or a mock_* control in matching snake_case; renaming would break the
// mirror and the link-time interposition. C enum mirrors stay unscoped so the
// mock constants compare equal to the real headers' values.

// -----------------------------------------------------------------------------
// Programmable PAM mock state
//
// The real PamAuthenticator drives pam_start -> pam_authenticate -> pam_acct_mgmt
// and maps the return codes to Success/Failure/Error. To test that mapping (and
// the conversation callback that carries the password) honestly, the stubs here
// are programmable rather than always returning success:
//   - pam_start records the username it was handed (regression guard: it must be
//     the real login user, never a hardcoded name).
//   - pam_authenticate invokes the stored conversation callback to retrieve the
//     password and compares it against the expected one.
//   - mock_pam_force_rc() lets a test simulate a PAM subsystem error.
// -----------------------------------------------------------------------------
namespace {
struct pam_conv g_pam_conv{};
std::array<char, 256> g_pam_user{};
// NOLINTNEXTLINE(bugprone-throwing-static-initialization) // strcpy is noexcept
std::array<char, 256> g_pam_expected_password = [] {
    std::array<char, 256> buf{};
    std::strcpy(buf.data(), "correct");
    return buf;
}();
int g_pam_forced_rc = -1;  // when >= 0, pam_authenticate returns this verbatim
}  // namespace

extern "C" {

// Test controls (declared in the test translation unit).
WAYLAND_EXPORT void mock_pam_reset() {
    g_pam_user[0] = '\0';
    std::strcpy(g_pam_expected_password.data(), "correct");
    g_pam_forced_rc = -1;
}
WAYLAND_EXPORT void mock_pam_set_expected_password(const char* p) {
    std::strncpy(g_pam_expected_password.data(), p ? p : "", g_pam_expected_password.size() - 1);
    g_pam_expected_password[g_pam_expected_password.size() - 1] = '\0';
}
WAYLAND_EXPORT void mock_pam_force_rc(int rc) {
    g_pam_forced_rc = rc;
}
WAYLAND_EXPORT const char* mock_pam_last_user() {
    return g_pam_user.data();
}

WAYLAND_EXPORT int pam_start(const char* service_name, const char* user,
                             const struct pam_conv* pam_conversation, pam_handle_t** pamh) {
    if (user) {
        std::strncpy(g_pam_user.data(), user, g_pam_user.size() - 1);
        g_pam_user[g_pam_user.size() - 1] = '\0';
    } else {
        g_pam_user[0] = '\0';
    }
    g_pam_conv = *pam_conversation;
    *pamh = (pam_handle_t*)0x1111;
    return PAM_SUCCESS;
}

WAYLAND_EXPORT int pam_authenticate(pam_handle_t* pamh, int flags) {
    if (g_pam_forced_rc >= 0) return g_pam_forced_rc;
    if (!g_pam_conv.conv) return PAM_AUTH_ERR;

    // Drive the real conversation callback the way libpam would: a realistic
    // stack emits an informational banner (which the callback must ignore)
    // ahead of the password prompt. Exercising both message styles guards the
    // callback against mishandling non-password PAM messages.
    struct pam_message info{PAM_TEXT_INFO, "qypr lock"};
    struct pam_message prompt{PAM_PROMPT_ECHO_OFF, "Password: "};
    const struct pam_message* msgs[2] = {&info, &prompt};
    struct pam_response* resp = nullptr;
    int rc = g_pam_conv.conv(2, msgs, &resp, g_pam_conv.appdata_ptr);
    // Mock owns the PAM response memory across the conversation callback.
    // NOLINTBEGIN(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
    if (rc != PAM_SUCCESS) {
        if (resp) free(resp);
        return PAM_AUTH_ERR;
    }
    // The password reply sits at the prompt's index (1); the info reply is null.
    bool ok =
        resp && resp[1].resp && std::strcmp(resp[1].resp, g_pam_expected_password.data()) == 0;
    if (resp) {
        free(resp[0].resp);  // ownership passes to the PAM layer (us)
        free(resp[1].resp);
        free(resp);
    }
    // NOLINTEND(cppcoreguidelines-owning-memory,cppcoreguidelines-no-malloc,hicpp-no-malloc)
    return ok ? PAM_SUCCESS : PAM_AUTH_ERR;
}

WAYLAND_EXPORT int pam_acct_mgmt(pam_handle_t* pamh, int flags) {
    return PAM_SUCCESS;
}

WAYLAND_EXPORT const char* pam_strerror(pam_handle_t* pamh, int errnum) {
    return errnum == PAM_SUCCESS ? "Success" : "Authentication failure";
}

WAYLAND_EXPORT int pam_end(pam_handle_t* pamh, int pam_status) {
    return PAM_SUCCESS;
}

// -----------------------------------------------------------------------------
// mpv Mocks
// -----------------------------------------------------------------------------
using mpv_handle = struct mpv_handle;
using mpv_render_context = struct mpv_render_context;

using mpv_format = enum {
    MPV_FORMAT_NONE = 0,
    MPV_FORMAT_STRING = 1,
    MPV_FORMAT_OSD_STRING = 2,
    MPV_FORMAT_FLAG = 3,
    MPV_FORMAT_INT64 = 4,
    MPV_FORMAT_DOUBLE = 5,
    MPV_FORMAT_NODE = 6,
    MPV_FORMAT_NODE_ARRAY = 7,
    MPV_FORMAT_NODE_MAP = 8,
    MPV_FORMAT_BYTE_ARRAY = 9
};

using mpv_event_id = enum {
    MPV_EVENT_NONE = 0,
    MPV_EVENT_SHUTDOWN = 1,
    MPV_EVENT_LOG_MESSAGE = 2,
    MPV_EVENT_GET_PROPERTY_REPLY = 3,
    MPV_EVENT_SET_PROPERTY_REPLY = 4,
    MPV_EVENT_COMMAND_REPLY = 5,
    MPV_EVENT_START_FILE = 6,
    MPV_EVENT_END_FILE = 7,
    MPV_EVENT_FILE_LOADED = 8,
    MPV_EVENT_IDLE = 11,
    MPV_EVENT_TICK = 14,
    MPV_EVENT_CLIENT_MESSAGE = 16,
    MPV_EVENT_VIDEO_RECONFIG = 17,
    MPV_EVENT_AUDIO_RECONFIG = 18,
    MPV_EVENT_SEEK = 20,
    MPV_EVENT_PLAYBACK_RESTART = 21,
    MPV_EVENT_PROPERTY_CHANGE = 22,
    MPV_EVENT_QUEUE_OVERFLOW = 24,
    MPV_EVENT_HOOK = 25
};

struct mpv_event {
    mpv_event_id event_id;
    int error;
    uint64_t reply_userdata;
    void* data;
};

using mpv_render_param = struct mpv_render_param {
    int type;
    void* data;
};

WAYLAND_EXPORT mpv_handle* mpv_create() {
    return (mpv_handle*)0x2222;
}

WAYLAND_EXPORT int mpv_initialize(mpv_handle* ctx) {
    return 0;
}

WAYLAND_EXPORT void mpv_destroy(mpv_handle* ctx) {}

WAYLAND_EXPORT int mpv_set_option_string(mpv_handle* ctx, const char* name, const char* value) {
    return 0;
}

WAYLAND_EXPORT int mpv_set_property(mpv_handle* ctx, const char* name, mpv_format format,
                                    void* data) {
    return 0;
}

WAYLAND_EXPORT int mpv_get_property(mpv_handle* ctx, const char* name, mpv_format format,
                                    void* data) {
    if (format == MPV_FORMAT_INT64 && data) {
        *(int64_t*)data = 1920;  // width/height mock
    }
    return 0;
}

WAYLAND_EXPORT int mpv_command_async(mpv_handle* ctx, uint64_t reply_userdata, const char** args) {
    return 0;
}

WAYLAND_EXPORT void mpv_set_wakeup_callback(mpv_handle* ctx, void (*cb)(void* d), void* d) {}

WAYLAND_EXPORT int mpv_request_log_messages(mpv_handle* ctx, const char* min_level) {
    return 0;
}

WAYLAND_EXPORT mpv_event* mpv_wait_event(mpv_handle* ctx, double timeout) {
    static mpv_event ev;
    ev.event_id = MPV_EVENT_NONE;
    return &ev;
}

WAYLAND_EXPORT int mpv_render_context_create(mpv_render_context** res, mpv_handle* mpv,
                                             mpv_render_param* params) {
    if (res) { *res = (mpv_render_context*)0x3333; }
    return 0;
}

WAYLAND_EXPORT void mpv_render_context_free(mpv_render_context* ctx) {}

WAYLAND_EXPORT int mpv_render_context_render(mpv_render_context* ctx, mpv_render_param* params) {
    return 0;
}

// -----------------------------------------------------------------------------
// Wayland Client Mocks
// -----------------------------------------------------------------------------
struct wl_display;
struct wl_registry;
struct wl_proxy;
struct wl_cursor_theme;
struct wl_cursor;
struct wl_buffer;

namespace {
// Listener slots recorded by wl_proxy_add_listener for the Seat test.
void (**g_registry_listener)(void) = nullptr;
void* g_registry_data = nullptr;

void (**g_seat_listener)(void) = nullptr;
void* g_seat_data = nullptr;
}  // namespace

WAYLAND_EXPORT struct wl_display* wl_display_connect(const char* name) {
    return (struct wl_display*)0x4444;
}

WAYLAND_EXPORT void wl_display_disconnect(struct wl_display* display) {}

WAYLAND_EXPORT int wl_display_dispatch(struct wl_display* display) {
    return 0;
}

WAYLAND_EXPORT int wl_display_dispatch_pending(struct wl_display* display) {
    return 0;
}

WAYLAND_EXPORT int wl_display_flush(struct wl_display* display) {
    return 0;
}

WAYLAND_EXPORT int wl_display_get_fd(struct wl_display* display) {
    static int pipe_fds[2] = {-1, -1};
    if (pipe_fds[0] < 0) {
        if (pipe(pipe_fds) < 0) return 0;
    }
    return pipe_fds[0];
}

WAYLAND_EXPORT int wl_display_roundtrip(struct wl_display* display) {
    static int roundtrip_count = 0;
    roundtrip_count++;

    // First roundtrip triggers globals on registry listener
    if (roundtrip_count == 1 && g_registry_listener && g_registry_data) {
        using global_cb_t = void (*)(void* data, void* registry, uint32_t name,
                                     const char* interface, uint32_t version);
        auto global_cb = (global_cb_t)g_registry_listener[0];
        if (global_cb) {
            global_cb(g_registry_data, (void*)0x5550, 1, "wl_compositor", 4);
            global_cb(g_registry_data, (void*)0x5551, 2, "wl_shm", 1);
            global_cb(g_registry_data, (void*)0x5552, 3, "ext_session_lock_manager_v1", 1);
            global_cb(g_registry_data, (void*)0x5553, 4, "wl_seat", 7);
            global_cb(g_registry_data, (void*)0x5554, 5, "wl_output", 4);
        }
    }
    // Second roundtrip triggers seat capabilities (keyboard=1, pointer=2) on seat listener
    else if (roundtrip_count == 2 && g_seat_listener && g_seat_data) {
        using caps_cb_t = void (*)(void* data, void* seat, uint32_t caps);
        auto caps_cb = (caps_cb_t)g_seat_listener[0];
        if (caps_cb) {
            caps_cb(g_seat_data, (void*)0x5553, 3);  // both keyboard + pointer
        }
    }
    return 0;
}

WAYLAND_EXPORT int wl_proxy_add_listener(struct wl_proxy* proxy, void (**implementation)(void),
                                         void* data) {
    // Save registry listener and seat listener sequentially
    if (!g_registry_listener) {
        g_registry_listener = implementation;
        g_registry_data = data;
    } else if (!g_seat_listener) {
        g_seat_listener = implementation;
        g_seat_data = data;
    }
    return 0;
}

WAYLAND_EXPORT void wl_proxy_destroy(struct wl_proxy* proxy) {}

WAYLAND_EXPORT uint32_t wl_proxy_get_version(struct wl_proxy* proxy) {
    return 1;
}

WAYLAND_EXPORT struct wl_proxy* wl_proxy_marshal_flags(struct wl_proxy* proxy, uint32_t opcode,
                                                       const void* interface, uint32_t version,
                                                       uint32_t flags, ...) {
    return (struct wl_proxy*)0x5555;
}

WAYLAND_EXPORT struct wl_cursor_theme* wl_cursor_theme_load(const char* name, int size,
                                                            struct wl_shm* shm) {
    return (struct wl_cursor_theme*)0x6666;
}

WAYLAND_EXPORT struct wl_cursor* wl_cursor_theme_get_cursor(struct wl_cursor_theme* theme,
                                                            const char* name) {
    return (struct wl_cursor*)0x7777;
}

WAYLAND_EXPORT struct wl_buffer* wl_cursor_image_get_buffer(void* image) {
    return (struct wl_buffer*)0x8888;
}

WAYLAND_EXPORT void wl_cursor_theme_destroy(struct wl_cursor_theme* theme) {}

// -----------------------------------------------------------------------------
// XKB Common Mocks
// -----------------------------------------------------------------------------
struct xkb_context;
struct xkb_keymap;
struct xkb_state;

// Programmable xkb translation state. Defaults (no keysym, empty text, no
// repeat) match the historical hardcoded stubs, so tests that don't touch
// these see identical behaviour; the Seat test sets them to drive the real
// key-translation branches (special keys, text delivery, key repeat).
namespace {
uint32_t g_xkb_sym = 0;
std::array<char, 64> g_xkb_utf8{};
int g_xkb_repeats = 0;
// Layout set the Seat reports from (default: a single "English (US)").
uint32_t g_xkb_num_layouts = 1;
}  // namespace

WAYLAND_EXPORT void mock_xkb_reset() {
    g_xkb_sym = 0;
    g_xkb_utf8[0] = '\0';
    g_xkb_repeats = 0;
    g_xkb_num_layouts = 1;
}
WAYLAND_EXPORT void mock_xkb_set_layouts(uint32_t count) {
    g_xkb_num_layouts = count;
}
WAYLAND_EXPORT void mock_xkb_set_sym(uint32_t s) {
    g_xkb_sym = s;
}
WAYLAND_EXPORT void mock_xkb_set_utf8(const char* s) {
    std::strncpy(g_xkb_utf8.data(), s ? s : "", g_xkb_utf8.size() - 1);
    g_xkb_utf8[g_xkb_utf8.size() - 1] = '\0';
}
WAYLAND_EXPORT void mock_xkb_set_repeats(int r) {
    g_xkb_repeats = r;
}

WAYLAND_EXPORT struct xkb_context* xkb_context_new(int flags) {
    return (struct xkb_context*)0x9999;
}

WAYLAND_EXPORT void xkb_context_unref(struct xkb_context* context) {}

WAYLAND_EXPORT int xkb_keymap_key_repeats(struct xkb_keymap* keymap, uint32_t key) {
    return g_xkb_repeats;
}

WAYLAND_EXPORT struct xkb_keymap* xkb_keymap_new_from_string(struct xkb_context* context,
                                                             const char* string, int format,
                                                             int flags) {
    return (struct xkb_keymap*)0xaaaa;
}

WAYLAND_EXPORT void xkb_keymap_unref(struct xkb_keymap* keymap) {}

WAYLAND_EXPORT uint32_t xkb_state_key_get_one_sym(struct xkb_state* state, uint32_t key) {
    return g_xkb_sym;
}

WAYLAND_EXPORT int xkb_state_key_get_utf8(struct xkb_state* state, uint32_t key, char* buffer,
                                          size_t size) {
    size_t n = std::strlen(g_xkb_utf8.data());
    if (buffer && size > 0) {
        if (n >= size) n = size - 1;
        std::memcpy(buffer, g_xkb_utf8.data(), n);
        buffer[n] = '\0';
    }
    return static_cast<int>(n);
}

WAYLAND_EXPORT int xkb_state_mod_name_is_active(struct xkb_state* state, const char* name,
                                                int type) {
    return 0;
}

WAYLAND_EXPORT struct xkb_state* xkb_state_new(struct xkb_keymap* keymap) {
    return (struct xkb_state*)0xbbbb;
}

WAYLAND_EXPORT uint32_t xkb_keymap_num_layouts(struct xkb_keymap* keymap) {
    return g_xkb_num_layouts;
}

WAYLAND_EXPORT const char* xkb_keymap_layout_get_name(struct xkb_keymap* keymap, uint32_t idx) {
    // Deterministic per-group names so Seat layout tests can distinguish groups.
    if (idx == 0) return "English (US)";
    if (idx == 1) return "Russian";
    return "Other";
}

WAYLAND_EXPORT void xkb_state_unref(struct xkb_state* state) {}

WAYLAND_EXPORT int xkb_state_update_mask(struct xkb_state* state, uint32_t depressed_mods,
                                         uint32_t latched_mods, uint32_t locked_mods,
                                         uint32_t depressed_layout, uint32_t latched_layout,
                                         uint32_t locked_layout) {
    return 0;
}

// -----------------------------------------------------------------------------
// systemd sd-bus Mocks
// -----------------------------------------------------------------------------
#include <poll.h>

struct sd_bus;
struct sd_bus_message;
struct sd_bus_slot;
struct sd_bus_error {
    const char* name;
    const char* message;
    int _need_free;
};
using sd_bus_vtable = struct {
    const char* element;
    int type;
    int (*handler)();
    size_t offset;
    unsigned long flags;
};

WAYLAND_EXPORT bool g_mock_sdbus_fail = false;

WAYLAND_EXPORT int sd_bus_open_user(struct sd_bus** ret) {
    if (g_mock_sdbus_fail) return -1;
    if (ret) *ret = (struct sd_bus*)0xcccc;
    return 0;
}

WAYLAND_EXPORT int sd_bus_call_method(struct sd_bus* bus, const char* destination, const char* path,
                                      const char* interface, const char* member,
                                      struct sd_bus_error* ret_error, struct sd_bus_message** reply,
                                      const char* types, ...) {
    if (member && std::strcmp(member, "BecomeMonitor") == 0) {
        return 0;  // success for BecomeMonitor to allow NotificationMonitor::start
    }
    return -1;  // simulate failure or no service for other calls
}

WAYLAND_EXPORT void sd_bus_error_free(struct sd_bus_error* e) {}

WAYLAND_EXPORT int sd_bus_get_fd(struct sd_bus* bus) {
    static int pipe_fds[2] = {-1, -1};
    if (pipe_fds[0] < 0) {
        if (pipe(pipe_fds) < 0) return 0;
    }
    return pipe_fds[0];
}

WAYLAND_EXPORT int sd_bus_add_filter(struct sd_bus* bus, struct sd_bus_slot** slot,
                                     int (*callback)(struct sd_bus_message* m, void* userdata,
                                                     struct sd_bus_error* ret_error),
                                     void* userdata) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_process(struct sd_bus* bus, struct sd_bus_message** r) {
    return 0;
}

WAYLAND_EXPORT struct sd_bus_slot* sd_bus_slot_unref(struct sd_bus_slot* slot) {
    return nullptr;
}

WAYLAND_EXPORT struct sd_bus* sd_bus_unref(struct sd_bus* bus) {
    return nullptr;
}

WAYLAND_EXPORT int sd_bus_message_get_type(struct sd_bus_message* m, uint8_t* type) {
    if (type) *type = 0;
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_is_method_call(struct sd_bus_message* m, const char* interface,
                                                 const char* member) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_read(struct sd_bus_message* m, const char* types, ...) {
    return -1;
}

WAYLAND_EXPORT int sd_bus_message_skip(struct sd_bus_message* m, const char* types) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_enter_container(struct sd_bus_message* m, char type,
                                                  const char* contents) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_exit_container(struct sd_bus_message* m) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_get_cookie(struct sd_bus_message* m, uint64_t* cookie) {
    return -1;
}

WAYLAND_EXPORT const char* sd_bus_message_get_sender(struct sd_bus_message* m) {
    return nullptr;
}

WAYLAND_EXPORT int sd_bus_message_get_reply_cookie(struct sd_bus_message* m, uint64_t* cookie) {
    return -1;
}

WAYLAND_EXPORT const char* sd_bus_message_get_destination(struct sd_bus_message* m) {
    return nullptr;
}

WAYLAND_EXPORT const char* sd_bus_message_get_signature(struct sd_bus_message* m, int complete) {
    return nullptr;
}

WAYLAND_EXPORT int sd_bus_message_is_signal(struct sd_bus_message* m, const char* interface,
                                            const char* member) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_peek_type(struct sd_bus_message* m, char* type,
                                            const char** contents) {
    return -1;
}

WAYLAND_EXPORT struct sd_bus_message* sd_bus_message_unref(struct sd_bus_message* m) {
    return nullptr;
}

WAYLAND_EXPORT int sd_bus_add_object_vtable(struct sd_bus* bus, struct sd_bus_slot** slot,
                                            const char* path, const char* interface,
                                            const sd_bus_vtable* vtable, void* userdata) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_request_name(struct sd_bus* bus, const char* name, uint64_t flags) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_new_method_return(struct sd_bus_message* call,
                                                    struct sd_bus_message** reply) {
    return -1;
}

WAYLAND_EXPORT int sd_bus_message_open_container(struct sd_bus_message* m, char type,
                                                 const char* contents) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_close_container(struct sd_bus_message* m) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_message_append(struct sd_bus_message* m, const char* types, ...) {
    return 0;
}

WAYLAND_EXPORT int sd_bus_send(struct sd_bus* bus, struct sd_bus_message* m, uint64_t* cookie) {
    return 0;
}

// The session bus is mocked to "succeed" (open_user returns a fake pointer),
// so any session-bus backend (e.g. SNIBackend) must find every sd-bus call it
// makes mocked here — otherwise it reaches real libsystemd on the fake pointer
// and crashes. The system-bus backends never hit these because open_system is
// left unmocked and genuinely fails, leaving their bus null.
WAYLAND_EXPORT int sd_bus_add_match(
    struct sd_bus* bus, struct sd_bus_slot** slot, const char* match,
    int (*callback)(struct sd_bus_message*, void*, struct sd_bus_error*), void* userdata) {
    if (slot) *slot = nullptr;
    return 0;
}

WAYLAND_EXPORT int sd_bus_get_property(struct sd_bus* bus, const char* destination,
                                       const char* path, const char* interface, const char* member,
                                       struct sd_bus_error* error, struct sd_bus_message** reply,
                                       const char* type) {
    return -1;  // no property available in the mock environment
}

// Bounding the per-connection call timeout (SystemBus's constructor, and the
// notification monitor's own connection) has to be mocked for the same reason as
// everything above: it is called on the fake session-bus pointer.
//
// Note for anyone tempted to mock sd_bus_message_new_method_call / sd_bus_call
// to get finer-grained timeouts: don't. libsystemd calls those symbols
// internally while opening a connection, and a definition here interposes on
// *its* use of them too — sd_bus_open_system() then hands a fake message to the
// real sd_bus_message_ref() and segfaults. Connection-level timeouts avoid the
// problem entirely, which is why the production code sets them that way.
WAYLAND_EXPORT int sd_bus_set_method_call_timeout(struct sd_bus* bus, uint64_t usec) {
    return 0;
}

// SystemBus drives sd-bus properly now: it asks which events to wait for and
// when the next method call expires, before every epoll_wait. Both take a real
// sd_bus*, so without stubs they receive this file's fake pointer and crash the
// whole suite. "Readable, nothing pending" keeps the loop's behaviour identical
// to what the tests saw before.
WAYLAND_EXPORT int sd_bus_get_events(struct sd_bus* bus) {
    return POLLIN;
}

WAYLAND_EXPORT int sd_bus_get_timeout(struct sd_bus* bus, uint64_t* usec) {
    if (usec) *usec = UINT64_MAX;  // no deadline: the timer stays disarmed
    return 0;
}

WAYLAND_EXPORT int sd_bus_call_method_async(struct sd_bus* bus, struct sd_bus_slot** slot,
                                            const char* destination, const char* path,
                                            const char* interface, const char* member,
                                            void* callback, void* userdata, const char* types,
                                            ...) {
    return 0;  // fire-and-forget no-op
}

}  // extern "C"

// NOLINTEND(readability-identifier-naming, cppcoreguidelines-use-enum-class)
