// test_wayland.cpp - Wayland display/session, seats, workspace mapping.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(WaylandDisplay) {
    qypr::EventLoop loop;
    qypr::WaylandDisplay disp(loop);

    // Should connect using Wayland mock
    EXPECT_TRUE(disp.connect());

    disp.invalidateAll();

    // Lock session
    qypr::LockSession lock(disp);
    EXPECT_TRUE(lock.lock());
    lock.unlock();
}
TEST(OutputHotplugWhileLocked) {
    qypr::EventLoop loop;
    qypr::WaylandDisplay disp(loop);
    auto* reg = reinterpret_cast<wl_registry*>(0x5550);

    // Bring up the globals a lock needs.
    qypr::WaylandDisplay::onGlobal(&disp, reg, 1, "wl_compositor", 4);
    qypr::WaylandDisplay::onGlobal(&disp, reg, 2, "wl_shm", 1);
    qypr::WaylandDisplay::onGlobal(&disp, reg, 3, "ext_session_lock_manager_v1", 1);
    qypr::WaylandDisplay::onGlobal(&disp, reg, 5, "wl_output", 4);
    EXPECT_EQ(static_cast<int>(disp.outputs_.size()), 1);
    EXPECT_TRUE(disp.outputs_.at(0)->surface() == nullptr);  // not locked yet

    qypr::LockSession lock(disp);
    EXPECT_TRUE(lock.lock());
    EXPECT_TRUE(disp.outputs_.at(0)->surface() != nullptr);  // existing output covered

    // Hotplug while locked: the new monitor is covered on arrival.
    qypr::WaylandDisplay::onGlobal(&disp, reg, 6, "wl_output", 4);
    EXPECT_EQ(static_cast<int>(disp.outputs_.size()), 2);
    EXPECT_TRUE(disp.outputs_.back()->surface() != nullptr);

    // Unplug: the output is dropped cleanly, others untouched.
    qypr::WaylandDisplay::onGlobalRemove(&disp, reg, 6);
    EXPECT_EQ(static_cast<int>(disp.outputs_.size()), 1);

    lock.unlock();

    // After unlock, a newly-appearing output is NOT given a lock surface.
    qypr::WaylandDisplay::onGlobal(&disp, reg, 7, "wl_output", 4);
    EXPECT_EQ(static_cast<int>(disp.outputs_.size()), 2);
    EXPECT_TRUE(disp.outputs_.back()->surface() == nullptr);
}

// The keyboard/pointer input path is the primary interactive attack surface at
// the lock screen. Exercise the real translation logic (special keys vs text,
// key repeat, modifiers, pointer routing) against a recording sink.
TEST(SeatInput) {
    qypr::EventLoop loop;

    struct RecSink : qypr::InputSink {
        std::string text;
        int specials = 0, motions = 0, buttons = 0, leaves = 0;
        uint32_t lastSym = 0, lastMods = 0;
        bool lastPressed = false;
        void onTextInput(const std::string& s) override { text += s; }
        void onSpecialKey(uint32_t sym, uint32_t mods) override {
            ++specials;
            lastSym = sym;
            lastMods = mods;
        }
        void onPointerMotion(int /*surfaceW*/, int /*surfaceH*/, double /*x*/,
                             double /*y*/) override {
            ++motions;
        }
        void onPointerButton(int /*surfaceW*/, int /*surfaceH*/, double /*x*/, double /*y*/,
                             uint32_t /*button*/, bool pressed) override {
            ++buttons;
            lastPressed = pressed;
        }
        void onPointerLeave() override { ++leaves; }
    } sink;

    qypr::OutputEnv env;  // null compositor/shm -> cursor build safely skipped
    auto* seatPtr = reinterpret_cast<wl_seat*>(0x5553);
    qypr::Seat seat(seatPtr, loop, &env);
    seat.setSink(&sink);

    // An output the pointer can focus, so motion/button carry a real size.
    qypr::Output out(reinterpret_cast<wl_output*>(0x5554), 42, &env);
    seat.setSurfaceSizer([&](wl_surface*, int& w, int& h) {
        w = out.logicalWidth();
        h = out.logicalHeight();
        return true;
    });

    mock_xkb_reset();

    // Unlinked temp fd of `size` zero bytes (like tmpfile(), but with
    // checked writes and no stdio owner for tidy to track).
    auto makeFd = [](size_t size) -> int {
        std::string path("/tmp/qypr-keymap-XXXXXX");
        int const fd = ::mkstemp(path.data());
        if (fd < 0) { return -1; }
        ::unlink(path.c_str());
        for (size_t i = 0; i < size; ++i) {
            if (::write(fd, "\0", 1) != 1) {
                ::close(fd);
                return -1;
            }
        }
        return fd;
    };

    // Capabilities: keyboard + pointer come online.
    qypr::Seat::onCapabilities(&seat, seatPtr,
                               WL_SEAT_CAPABILITY_KEYBOARD | WL_SEAT_CAPABILITY_POINTER);
    EXPECT_TRUE(seat.keyboard_ != nullptr);
    EXPECT_TRUE(seat.pointer_ != nullptr);

    // A non-XKB keymap format is rejected: no xkb state is built.
    qypr::Seat::onKeymap(&seat, nullptr, 0 /*bad format*/, makeFd(8), 8);
    EXPECT_TRUE(seat.xkbState_ == nullptr);

    // A valid XKB keymap builds the translation state.
    qypr::Seat::onKeymap(&seat, nullptr, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, makeFd(64), 64);
    EXPECT_TRUE(seat.xkbState_ != nullptr);

    qypr::Seat::onRepeatInfo(&seat, nullptr, 25 /*rate*/, 1 /*delay ms*/);

    // Printable key: delivered as text, and arms key-repeat.
    mock_xkb_set_utf8("k");
    mock_xkb_set_repeats(1);
    qypr::Seat::onKey(&seat, nullptr, 0, 0, 30, WL_KEYBOARD_KEY_STATE_PRESSED);
    EXPECT_EQ(sink.text, std::string("k"));
    EXPECT_TRUE(seat.repeatKeycode_ != 0);

    // Release stops repeat.
    qypr::Seat::onKey(&seat, nullptr, 0, 0, 30, WL_KEYBOARD_KEY_STATE_RELEASED);
    EXPECT_TRUE(seat.repeatKeycode_ == 0);

    // Special key (Return) routes to onSpecialKey, never as text.
    mock_xkb_set_utf8("");
    mock_xkb_set_repeats(0);
    mock_xkb_set_sym(XKB_KEY_Return);
    size_t const textBefore = sink.text.size();
    qypr::Seat::onKey(&seat, nullptr, 0, 0, 28, WL_KEYBOARD_KEY_STATE_PRESSED);
    EXPECT_EQ(sink.specials, 1);
    EXPECT_EQ(static_cast<int>(sink.lastSym), static_cast<int>(XKB_KEY_Return));
    EXPECT_EQ(static_cast<int>(sink.text.size()), static_cast<int>(textBefore));

    // Modifier update and keyboard-leave paths (leave must stop any repeat).
    qypr::Seat::onModifiers(&seat, nullptr, 0, 1, 0, 0, 0);
    qypr::Seat::onKbLeave(&seat, nullptr, 0, nullptr);

    // Pointer: enter focuses a surface (the sizer resolves it), motion + button
    // reach the sink, leave clears.
    qypr::Seat::onPtrEnter(&seat, reinterpret_cast<wl_pointer*>(0x5555), 1,
                           reinterpret_cast<wl_surface*>(0x5556), 0, 0);
    qypr::Seat::onPtrMotion(&seat, nullptr, 0, wl_fixed_from_int(100), wl_fixed_from_int(50));
    EXPECT_EQ(sink.motions, 1);
    qypr::Seat::onPtrButton(&seat, nullptr, 0, 0, 272 /*BTN_LEFT*/,
                            WL_POINTER_BUTTON_STATE_PRESSED);
    EXPECT_EQ(sink.buttons, 1);
    EXPECT_TRUE(sink.lastPressed);
    qypr::Seat::onPtrLeave(&seat, nullptr, 0, nullptr);
    EXPECT_EQ(sink.leaves, 1);

    // Capabilities dropped: keyboard + pointer are torn down.
    qypr::Seat::onCapabilities(&seat, seatPtr, 0);
    EXPECT_TRUE(seat.keyboard_ == nullptr);
    EXPECT_TRUE(seat.pointer_ == nullptr);

    mock_xkb_reset();  // restore defaults for any later test
}

// The Seat decodes the active xkb layout group and pushes it to the sink
// (backing the keyboard-layout indicator): initial report on sink-attach,
// re-report on group change, and coalescing of redundant modifier events.
TEST(SeatLayoutReport) {
    qypr::EventLoop loop;

    struct LayoutSink : qypr::InputSink {
        int changes = 0;
        std::string lastName;
        uint32_t lastIndex = 99, lastCount = 0;
        void onTextInput(const std::string& /*utf8*/) override {}
        void onSpecialKey(uint32_t /*keysym*/, uint32_t /*modifiers*/) override {}
        void onPointerMotion(int /*surfaceW*/, int /*surfaceH*/, double /*x*/,
                             double /*y*/) override {}
        void onPointerButton(int /*surfaceW*/, int /*surfaceH*/, double /*x*/, double /*y*/,
                             uint32_t /*button*/, bool /*pressed*/) override {}
        void onPointerLeave() override {}
        void onLayoutChanged(const std::string& n, uint32_t i, uint32_t c) override {
            ++changes;
            lastName = n;
            lastIndex = i;
            lastCount = c;
        }
    } sink;

    qypr::OutputEnv const env;
    auto* seatPtr = reinterpret_cast<wl_seat*>(0x6663);
    qypr::Seat seat(seatPtr, loop, &env);

    mock_xkb_reset();
    mock_xkb_set_layouts(2);

    // Unlinked temp fd of `size` zero bytes (like tmpfile(), but with
    // checked writes and no stdio owner for tidy to track).
    auto makeFd = [](size_t size) -> int {
        std::string path("/tmp/qypr-keymap-XXXXXX");
        int const fd = ::mkstemp(path.data());
        if (fd < 0) { return -1; }
        ::unlink(path.c_str());
        for (size_t i = 0; i < size; ++i) {
            if (::write(fd, "\0", 1) != 1) {
                ::close(fd);
                return -1;
            }
        }
        return fd;
    };

    qypr::Seat::onCapabilities(&seat, seatPtr, WL_SEAT_CAPABILITY_KEYBOARD);

    // Keymap arrives before any sink is attached: nothing is reported yet.
    qypr::Seat::onKeymap(&seat, nullptr, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, makeFd(64), 64);
    EXPECT_EQ(sink.changes, 0);

    // Attaching the sink re-reports the current layout immediately (group 0).
    seat.setSink(&sink);
    EXPECT_EQ(sink.changes, 1);
    EXPECT_EQ(sink.lastName, std::string("English (US)"));
    EXPECT_EQ(static_cast<int>(sink.lastIndex), 0);
    EXPECT_EQ(static_cast<int>(sink.lastCount), 2);

    // Switching to group 1 reports the new layout.
    qypr::Seat::onModifiers(&seat, nullptr, 0, 0, 0, 0, 1);
    EXPECT_EQ(sink.changes, 2);
    EXPECT_EQ(sink.lastName, std::string("Russian"));
    EXPECT_EQ(static_cast<int>(sink.lastIndex), 1);

    // A modifier event with the same group does not re-report (coalesced).
    qypr::Seat::onModifiers(&seat, nullptr, 0, 4, 0, 0, 1);
    EXPECT_EQ(sink.changes, 2);

    // Back to group 0 reports again.
    qypr::Seat::onModifiers(&seat, nullptr, 0, 0, 0, 0, 0);
    EXPECT_EQ(sink.changes, 3);
    EXPECT_EQ(sink.lastName, std::string("English (US)"));

    mock_xkb_reset();
}

// The compositor can refuse or revoke a lock via the `finished` event. When it
// does, the session must stop reporting itself as locked (it is no longer
// secure) and notify the app, and a later unlock() must not misuse the protocol
// by releasing a lock that was never granted.
TEST(LockSessionFinished) {
    qypr::EventLoop loop;
    qypr::WaylandDisplay disp(loop);
    auto* reg = reinterpret_cast<wl_registry*>(0x5550);
    qypr::WaylandDisplay::onGlobal(&disp, reg, 3, "ext_session_lock_manager_v1", 1);

    // --- Granted, then revoked mid-session ---
    {
        qypr::LockSession session(disp);
        bool lockedCb = false;
        bool finishedCb = false;
        session.setOnLocked([&] { lockedCb = true; });
        session.setOnFinished([&] { finishedCb = true; });

        EXPECT_TRUE(session.lock());
        EXPECT_FALSE(session.locked());  // not confirmed by the compositor yet

        qypr::LockSession::onLocked(&session, nullptr);
        EXPECT_TRUE(session.locked());
        EXPECT_TRUE(lockedCb);

        qypr::LockSession::onFinished(&session, nullptr);  // compositor revokes it
        EXPECT_FALSE(session.locked());                    // no longer secure
        EXPECT_TRUE(finishedCb);

        session.unlock();  // safe: destroy (not unlock_and_destroy), no crash
    }

    // --- Refused outright: `finished` before any `locked` ---
    {
        qypr::LockSession session(disp);
        bool finishedCb = false;
        session.setOnFinished([&] { finishedCb = true; });

        EXPECT_TRUE(session.lock());
        qypr::LockSession::onFinished(&session, nullptr);
        EXPECT_FALSE(session.locked());
        EXPECT_TRUE(finishedCb);
        session.unlock();  // never granted -> still safe
    }

    // --- Normal lifecycle: granted, then cleanly unlocked while locked ---
    {
        qypr::LockSession session(disp);
        EXPECT_TRUE(session.lock());
        qypr::LockSession::onLocked(&session, nullptr);
        EXPECT_TRUE(session.locked());
        session.unlock();  // locked -> unlock_and_destroy path
        EXPECT_FALSE(session.locked());
    }

    // --- Teardown while still holding a granted lock (no unlock() call):
    //     the destructor must release it cleanly rather than leak it. ---
    {
        qypr::LockSession session(disp);
        EXPECT_TRUE(session.lock());
        qypr::LockSession::onLocked(&session, nullptr);
        EXPECT_TRUE(session.locked());
    }  // ~LockSession runs here with lock_ still held
}

// The notification tile icon resolver: a base64 data: URI must decode to a real
// surface, repeated lookups must hit the cache, and empty/unknown names must
// resolve to nothing so the caller falls back to a letter glyph.
TEST(SessionMapperBirthAssignsActiveWorkspace) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot ws;
    ws.available = true;
    ws.workspaces = {{.name = "1", .active = true, .urgent = false},
                     {.name = "2", .active = false, .urgent = false}};
    qypr::ToplevelSnapshot tl;
    tl.available = true;

    m.ingest(ws, tl);
    EXPECT_EQ(m.view().clusters.size(), static_cast<size_t>(2));

    // kitty spawns (focused) while ws "1" is active → born there.
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("1"));

    // firefox spawns unfocused — still on the active workspace.
    tl.windows.push_back(
        {.id = 2, .appId = "firefox", .title = "", .active = false, .minimized = false});
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(2), std::string("1"));
    EXPECT_EQ(m.view().clusters.at(0).windows.size(), static_cast<size_t>(2));
}
TEST(SessionMapperCarriesFocusedWindowOnMove) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot ws;
    ws.available = true;
    ws.workspaces = {{.name = "1", .active = true, .urgent = false},
                     {.name = "2", .active = false, .urgent = false}};
    qypr::ToplevelSnapshot tl;
    tl.available = true;
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("1"));

    // Moving the focused window keeps it activated while the destination
    // workspace flips to active: the mapper must carry it across.
    ws.workspaces.at(0).active = false;
    ws.workspaces.at(1).active = true;
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("2"));
}
TEST(SessionMapperPlainSwitchDoesNotDragHomes) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot ws;
    ws.available = true;
    ws.workspaces = {{.name = "1", .active = true, .urgent = false},
                     {.name = "2", .active = false, .urgent = false}};
    qypr::ToplevelSnapshot tl;
    tl.available = true;
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});
    m.ingest(ws, tl);

    // A plain switch to an empty workspace deactivates the window in the same
    // batch — its home must not be dragged to the destination.
    ws.workspaces.at(0).active = false;
    ws.workspaces.at(1).active = true;
    tl.windows.at(0).active = false;
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("1"));
    EXPECT_EQ(m.view().unassigned.size(), static_cast<size_t>(0));
}
TEST(SessionMapperBindFollowsActivation) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot ws;
    ws.available = true;
    ws.workspaces = {{.name = "1", .active = true, .urgent = false},
                     {.name = "2", .active = false, .urgent = false}};
    qypr::ToplevelSnapshot tl;
    tl.available = true;
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});  // on 1
    tl.windows.push_back({.id = 2,
                          .appId = "code",
                          .title = "",
                          .active = false,
                          .minimized = false});  // on 1 too (born)
    m.ingest(ws, tl);

    // User focuses code while switching to ws 2 (clicking it on the other
    // workspace): activation + workspace flip in one batch → code re-homes.
    ws.workspaces.at(0).active = false;
    ws.workspaces.at(1).active = true;
    tl.windows.at(0).active = false;
    tl.windows.at(1).active = true;
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(2), std::string("2"));
    EXPECT_EQ(m.homeOf(1), std::string("1"));  // kitty stays put
}
TEST(SessionMapperAdoptsResidentsWhenWorkspaceVanishes) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot ws;
    ws.available = true;
    ws.workspaces = {{.name = "1", .active = true, .urgent = false},
                     {.name = "2", .active = false, .urgent = false},
                     {.name = "3", .active = false, .urgent = false}};
    qypr::ToplevelSnapshot tl;
    tl.available = true;
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("1"));
    EXPECT_EQ(m.homeOf(1), m.view().clusters.at(0).name);

    // ws "1" closes; its resident lands wherever becomes active next ("2").
    ws.workspaces.erase(ws.workspaces.begin());
    ws.workspaces.at(0).active = true;  // now "2"
    m.ingest(ws, tl);
    EXPECT_EQ(m.homeOf(1), std::string("2"));
}
TEST(SessionMapperWithoutWorkspacesEverythingUnassigned) {
    qypr::SessionMapper m;
    qypr::WorkspaceSnapshot const ws;  // protocol absent
    qypr::ToplevelSnapshot tl;
    tl.available = true;
    tl.windows.push_back(
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false});
    m.ingest(ws, tl);

    EXPECT_FALSE(m.view().hasWorkspaces);
    EXPECT_TRUE(m.view().clusters.empty());
    EXPECT_EQ(m.view().unassigned.size(), static_cast<size_t>(1));
    EXPECT_TRUE(m.view().window(1) != nullptr);
}

// -----------------------------------------------------------------------------
// PagerIndicator — the merged workspaces+taskbar module
// -----------------------------------------------------------------------------
