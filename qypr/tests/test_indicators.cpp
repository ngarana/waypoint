// test_indicators.cpp - Bar indicators, QS tiles, gating.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(ClockIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::ClockIndicator clock(backends);

    EXPECT_EQ(clock.id(), std::string("clock"));
    EXPECT_TRUE(static_cast<int>(clock.zone()) == static_cast<int>(qypr::Zone::Left));
    EXPECT_EQ(clock.priority(), 0);
    EXPECT_TRUE(clock.createTile() == nullptr);  // no Quick Settings tile
    // The clock drops a calendar popover (Phase 12).
    EXPECT_TRUE(clock.hasDetailedView());
    EXPECT_TRUE(clock.createDetailedView() != nullptr);
}
TEST(ClockIndicatorRenders) {
    qypr::SystemBackends const backends{};
    qypr::ClockIndicator clock(backends);

    // Poll to populate cached time
    clock.poll(qypr::nowMs());

    // Text-only indicator: label carries the formatted time, icon is empty
    std::string const t = clock.label();
    EXPECT_TRUE(!t.empty());
    EXPECT_TRUE(clock.icon().empty());

    // Tooltip returns date string
    std::string const d = clock.tooltip();
    EXPECT_TRUE(!d.empty());

    // Measure width against a real painter
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    double const w = clock.measureWidth(p);
    EXPECT_TRUE(w > 0);

    // Draw
    clock.bounds = {.x = 0, .y = 0, .w = w, .h = 36};
    clock.draw(p, qypr::nowMs());

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(BatteryIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::BatteryIndicator const batt(backends);

    EXPECT_EQ(batt.id(), std::string("battery"));
    EXPECT_TRUE(static_cast<int>(batt.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(batt.priority(), 500);
    EXPECT_TRUE(batt.hasDetailedView());
}
TEST(BatteryIndicatorCreatesInfoTile) {
    qypr::SystemBackends const backends{};
    qypr::BatteryIndicator batt(backends);

    auto tile = batt.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Info);

    // Draw the tile
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 80);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile->bounds = {.x = 0, .y = 0, .w = 180, .h = 64};
    tile->draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(BatteryIndicatorColorCoding) {
    qypr::SystemBackends const backends{};
    qypr::BatteryIndicator batt(backends);

    // Default state: 0%, Unknown
    qypr::Color const c = batt.iconColor();
    // Should be red for 0%
    EXPECT_TRUE(c.r > 0.5);  // red channel dominant

    // Backend update with no backend attached must be a no-op, not a crash
    batt.onBackendUpdate();

    // Measure should return positive width
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    double const w = batt.measureWidth(p);
    EXPECT_TRUE(w > 0);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(BatteryBackendConstruction) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend backend(bus);

    // start() fires an async D-Bus call and returns immediately.
    // On a machine with a bus, it returns true (the call was issued);
    // the snapshot will be populated by the callback.
    bool const started = backend.start();
    const auto& s = backend.snapshot();
    if (started) {
        // Async: snapshot may not be populated yet — just verify no crash.
    } else {
        EXPECT_FALSE(s.present);
    }
}

// =============================================================================
// Phase 3a: Brightness Indicator Tests
// =============================================================================
TEST(BrightnessIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::BrightnessIndicator bright(backends);

    EXPECT_EQ(bright.id(), std::string("brightness"));
    EXPECT_TRUE(static_cast<int>(bright.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(bright.priority(), 100);
    EXPECT_FALSE(bright.hasDetailedView());

    // Icon levels track the snapshot fraction
    bright.lastSnap_.max = 100;
    bright.lastSnap_.current = 80;
    EXPECT_EQ(bright.icon(), std::string("󰃠"));
    bright.lastSnap_.current = 50;
    EXPECT_EQ(bright.icon(), std::string("󰃟"));
    bright.lastSnap_.current = 10;
    EXPECT_EQ(bright.icon(), std::string("󰃞"));

    // Scroll without a backend must be a no-op, not a crash. (x,y are pointer
    // coords forwarded to multi-element indicators; unused here.)
    EXPECT_FALSE(bright.onScroll(0, -1.0, 0.0, 0.0));
}
TEST(BrightnessIndicatorCreatesSliderTile) {
    qypr::SystemBackends const backends{};
    qypr::BrightnessIndicator bright(backends);
    bright.lastSnap_.max = 100;
    bright.lastSnap_.current = 75;

    auto tile = bright.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Slider);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile->bounds = {.x = 0, .y = 0, .w = 348, .h = 40};
    tile->draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(BrightnessBackendConstruction) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BrightnessBackend backend(loop, bus);

    // start() must not crash whether or not a backlight exists; on failure
    // the snapshot stays unavailable so the indicator hides.
    bool const started = backend.start();
    const auto& s = backend.snapshot();
    if (started) {
        EXPECT_TRUE(s.available);
        EXPECT_TRUE(s.max > 0);
        EXPECT_TRUE(s.current >= 0 && s.current <= s.max);
    } else {
        EXPECT_FALSE(s.available);
    }
}

// =============================================================================
// Phase 3b: WiFi Indicator Tests
// =============================================================================
TEST(WifiIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::WifiIndicator wifi(backends);

    EXPECT_EQ(wifi.id(), std::string("wifi"));
    EXPECT_TRUE(static_cast<int>(wifi.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(wifi.priority(), 300);

    // Icon tracks radio/connection/strength states
    wifi.lastSnap_.enabled = false;
    EXPECT_EQ(wifi.icon(), std::string("󰤮"));
    wifi.lastSnap_.enabled = true;
    wifi.lastSnap_.connected = false;
    EXPECT_EQ(wifi.icon(), std::string("󰤭"));
    wifi.lastSnap_.connected = true;
    wifi.lastSnap_.strength = 80;
    EXPECT_EQ(wifi.icon(), std::string("󰤨"));
    wifi.lastSnap_.strength = 60;
    EXPECT_EQ(wifi.icon(), std::string("󰤥"));
    wifi.lastSnap_.strength = 30;
    EXPECT_EQ(wifi.icon(), std::string("󰤢"));
    wifi.lastSnap_.strength = 10;
    EXPECT_EQ(wifi.icon(), std::string("󰤯"));
}
TEST(WifiIndicatorCreatesToggleTile) {
    qypr::SystemBackends const backends{};
    qypr::WifiIndicator wifi(backends);
    wifi.lastSnap_.enabled = true;
    wifi.lastSnap_.connected = true;
    wifi.lastSnap_.ssid = "TestNet";

    auto tile = wifi.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Toggle);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 80);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile->bounds = {.x = 0, .y = 0, .w = 160, .h = 64};
    tile->draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    // Toggling without a backend must be a no-op, not a crash
    tile->onClick(10, 10);
}
TEST(WifiPopoverToggleSwitchAndScanning) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::WifiBackend backend(bus);
    // Seed a ready snapshot (as StateCache would): strongest-first, one saved
    // secured active, one open unsaved, one secured unsaved (needs a password).
    qypr::WifiSnapshot seeded;
    seeded.available = true;
    seeded.enabled = true;
    seeded.connected = true;
    seeded.ssid = "SavedNet";
    seeded.strength = 80;
    seeded.networks = {
        {.ssid = "SavedNet", .strength = 80, .secured = true, .active = true, .saved = true},
        {.ssid = "OpenNet", .strength = 60, .secured = false, .active = false, .saved = false},
        {.ssid = "SecNet", .strength = 40, .secured = true, .active = false, .saved = false},
    };
    backend.seed(seeded);
    // Make the backend's chain state agree with the seeded snapshot so
    // publish() reproduces it (enabled radio, activated device, live AP).
    backend.wirelessEnabled_ = true;
    backend.devState_ = 100;
    backend.apSsid_ = "SavedNet";
    backend.apStrength_ = 80;
    backend.device_ = "/org/freedesktop/NetworkManager/Devices/5";

    qypr::SystemBackends backends{};
    backends.wifi = &backend;
    qypr::WifiIndicator wifi(backends);
    wifi.onBackendUpdate();
    EXPECT_TRUE(wifi.visible);

    auto view = wifi.createDetailedView();
    EXPECT_TRUE(view != nullptr);
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 500);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    view->anchorX = 300;
    view->anchorY = 0;
    view->open();
    const double w = view->contentWidth();
    view->draw(p, qypr::nowMs() + 10000);  // past the open animation

    // The header switch sits in the top-right corner of the popover.
    const double swX = w - 12.0 - 17.0;  // centre of the switch pill
    const double swY = 12.0 + 1.0 + 9.0;
    EXPECT_TRUE(backend.snap_.enabled);
    view->handleClick(swX, swY);
    EXPECT_FALSE(backend.snap_.enabled);          // radio toggled off (optimistic write)
    EXPECT_TRUE(backend.snap_.networks.empty());  // off → picker empties
    view->handleClick(swX, swY);
    EXPECT_TRUE(backend.snap_.enabled);

    // Refresh button re-requests a scan (spinner state flips on; no live bus
    // round-trip happens in the test — the loop never dispatches).
    view->handleClick(w - 12.0 - 9.0, 12.0 + 30.0 + 12.0);
    EXPECT_TRUE(backend.snap_.scanning);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(WifiPopoverPasswordJoinsSecuredNetwork) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::WifiBackend backend(bus);
    qypr::WifiSnapshot seeded;
    seeded.available = true;
    seeded.enabled = true;
    seeded.networks = {
        {.ssid = "SecNet", .strength = 40, .secured = true, .active = false, .saved = false},
    };
    backend.seed(seeded);

    qypr::SystemBackends backends{};
    backends.wifi = &backend;
    qypr::WifiIndicator wifi(backends);
    wifi.onBackendUpdate();
    auto view = wifi.createDetailedView();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 500);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    view->anchorX = 300;
    view->anchorY = 0;
    view->open();
    view->draw(p, qypr::nowMs() + 10000);

    EXPECT_FALSE(view->wantsKeyboard());
    EXPECT_EQ(view->autoDismissMs(), 6000);

    // Click the secured unsaved row (first row below the header + subheader).
    const double rowY = 12.0 + 30.0 + 24.0 + 17.0;
    view->handleClick(150.0, rowY);

    // The passphrase editor opened: keyboard focus is requested, auto-dismiss
    // is disabled, and typing feeds the (masked) buffer.
    EXPECT_TRUE(view->wantsKeyboard());
    EXPECT_EQ(view->autoDismissMs(), 0);
    EXPECT_TRUE(view->handleText("hunter2"));
    EXPECT_TRUE(view->handleKey(XKB_KEY_BackSpace));
    EXPECT_TRUE(view->handleText("2!"));
    // Return commits: AddAndActivateConnection (no bus in tests → no-op, but
    // the editor must close and release the keyboard).
    EXPECT_TRUE(view->handleKey(XKB_KEY_Return));
    EXPECT_FALSE(view->wantsKeyboard());
    EXPECT_EQ(view->autoDismissMs(), 6000);

    // Escape also cancels the editor.
    view->draw(p, qypr::nowMs() + 10000);
    view->handleClick(150.0, rowY);
    EXPECT_TRUE(view->wantsKeyboard());
    EXPECT_TRUE(view->handleText("x"));
    EXPECT_TRUE(view->handleKey(XKB_KEY_Escape));
    EXPECT_FALSE(view->wantsKeyboard());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(WifiPopoverClickRoutingByNetworkKind) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::WifiBackend backend(bus);
    qypr::WifiSnapshot seeded;
    seeded.available = true;
    seeded.enabled = true;
    seeded.networks = {
        {.ssid = "Active", .strength = 90, .secured = true, .active = true, .saved = true},
        {.ssid = "Saved", .strength = 70, .secured = true, .active = false, .saved = true},
        {.ssid = "OpenNew", .strength = 50, .secured = false, .active = false, .saved = false},
    };
    backend.seed(seeded);

    qypr::SystemBackends backends{};
    backends.wifi = &backend;
    qypr::WifiIndicator wifi(backends);
    wifi.onBackendUpdate();
    auto view = wifi.createDetailedView();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 500);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    view->anchorX = 300;
    view->anchorY = 0;
    view->open();
    view->draw(p, qypr::nowMs() + 10000);

    const double row0 = 12.0 + 30.0 + 24.0 + 17.0;  // Active
    const double row1 = row0 + 34.0;                // Saved
    const double row2 = row0 + 68.0;                // OpenNew
    // Active row disconnects (stays open to watch the list update).
    view->handleClick(150.0, row0);
    // Saved row connects via its saved profile; open row joins directly. Both
    // must be consumed clicks (no crash without a bus).
    EXPECT_TRUE(view->handleClick(150.0, row1));
    EXPECT_TRUE(view->handleClick(150.0, row2));
    // The secured-new case is the password editor (covered in the test above).
    EXPECT_FALSE(view->wantsKeyboard());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(QSWifiComboTileZones) {
    bool toggled = false;
    bool pickerOpened = false;
    qypr::QSWifiComboTile tile(
        "MyNet", 80, true, true, qypr::Color::fromHex("#89b4fa"), [&toggled]() { toggled = true; },
        [&pickerOpened]() { pickerOpened = true; });
    tile.bounds = {.x = 0, .y = 0, .w = 160, .h = 64};

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 80);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile.setScanning(true);
    tile.draw(p, qypr::nowMs());  // spinner path must draw without a bus

    // Body → network picker; right power strip → radio toggle.
    tile.onClick(10.0, 32.0);
    EXPECT_TRUE(pickerOpened);
    EXPECT_FALSE(toggled);
    tile.onClick(150.0, 32.0);  // x >= bounds right - 40
    EXPECT_TRUE(toggled);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// -----------------------------------------------------------------------------
// Right-click (secondary) support: QS tiles and popovers
// -----------------------------------------------------------------------------
TEST(QSTileSecondaryClickCallback) {
    bool opened = false;
    qypr::QSToggleTile tile("Bluetooth", "󰂯", []() { return true; }, []() {});
    tile.setOnSecondary([&opened]() { opened = true; });
    tile.bounds = {.x = 0, .y = 0, .w = 100, .h = 40};

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 120, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile.draw(p, qypr::nowMs());

    // Right-click fires the attached context action; a tile without one is a
    // harmless no-op.
    tile.onSecondaryClick(50.0, 20.0);
    EXPECT_TRUE(opened);
    qypr::QSToggleTile plain("DND", "󰅛", []() { return false; }, []() {});
    plain.onSecondaryClick(50.0, 20.0);  // must not crash
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(WifiPopoverSecondaryForgetAndDisconnect) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::WifiBackend backend(bus);
    qypr::WifiSnapshot seeded;
    seeded.available = true;
    seeded.enabled = true;
    seeded.networks = {
        {.ssid = "Active", .strength = 90, .secured = true, .active = true, .saved = true},
        {.ssid = "Saved", .strength = 70, .secured = true, .active = false, .saved = true},
        {.ssid = "OpenNew", .strength = 50, .secured = false, .active = false, .saved = false},
    };
    backend.seed(seeded);

    qypr::SystemBackends backends{};
    backends.wifi = &backend;
    qypr::WifiIndicator wifi(backends);
    wifi.onBackendUpdate();
    auto view = wifi.createDetailedView();
    view->anchorX = 300;
    view->anchorY = 0;
    view->open();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 500);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    view->draw(p, qypr::nowMs() + 10000);

    const double row0 = 12.0 + 30.0 + 24.0 + 17.0;
    // Right-click the active row → disconnect; the saved row → forget; the
    // unsaved row → no action. All consumed, none crash (no live bus).
    EXPECT_TRUE(view->handleSecondaryClick(150.0, row0));
    EXPECT_TRUE(view->handleSecondaryClick(150.0, row0 + 34.0));
    EXPECT_TRUE(view->handleSecondaryClick(150.0, row0 + 68.0));
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(BluetoothIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::BluetoothIndicator bt(backends);

    EXPECT_EQ(bt.id(), std::string("bluetooth"));
    EXPECT_TRUE(static_cast<int>(bt.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(bt.priority(), 350);

    // Icon and accent track power/connection state
    bt.lastSnap_.powered = false;
    EXPECT_EQ(bt.icon(), std::string("󰂲"));
    bt.lastSnap_.powered = true;
    bt.lastSnap_.connectedCount = 0;
    EXPECT_EQ(bt.icon(), std::string("󰂯"));
    bt.lastSnap_.connectedCount = 1;
    bt.lastSnap_.firstDevice = "Headphones";
    EXPECT_EQ(bt.icon(), std::string("󰂱"));
    EXPECT_TRUE(bt.tooltip().find("Headphones") != std::string::npos);
    // Connected: blue accent, not the plain text color
    qypr::Color const accent = bt.iconColor();
    EXPECT_TRUE(accent.b > accent.r);
}
TEST(BluetoothIndicatorCreatesToggleTile) {
    qypr::SystemBackends const backends{};
    qypr::BluetoothIndicator bt(backends);
    bt.lastSnap_.powered = true;
    bt.lastSnap_.connectedCount = 2;

    auto tile = bt.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Toggle);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 80);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile->bounds = {.x = 0, .y = 0, .w = 160, .h = 64};
    tile->draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    // Toggling without a backend must be a no-op, not a crash
    tile->onClick(10, 10);
}

// =============================================================================
// Phase 3d: DND Tests
// =============================================================================
TEST(DndStateToggleNotifiesAllListeners) {
    qypr::DndState dnd;
    int a = 0;
    int b = 0;
    dnd.addListener([&] { ++a; });
    dnd.addListener([&] { ++b; });

    EXPECT_FALSE(dnd.enabled());
    dnd.toggle();
    EXPECT_TRUE(dnd.enabled());
    EXPECT_EQ(a, 1);
    EXPECT_EQ(b, 1);

    // Setting the same value must not re-notify
    dnd.setEnabled(true);
    EXPECT_EQ(a, 1);

    dnd.setEnabled(false);
    EXPECT_FALSE(dnd.enabled());
    EXPECT_EQ(a, 2);
}
TEST(DNDIndicatorVisibilityAndTile) {
    qypr::DndState dnd;
    qypr::SystemBackends backends{};
    backends.dnd = &dnd;
    qypr::DNDIndicator ind(backends);

    // Moon icon hidden until DND is active
    EXPECT_FALSE(ind.visible);
    dnd.setEnabled(true);
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);

    auto tile = ind.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Toggle);

    // The tile toggles the shared state directly
    tile->onClick(10, 10);
    EXPECT_FALSE(dnd.enabled());
}

// =============================================================================
// Phase 4: Volume Tests
// =============================================================================
TEST(VolumeIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::VolumeIndicator vol(backends);

    EXPECT_EQ(vol.id(), std::string("volume"));
    EXPECT_TRUE(static_cast<int>(vol.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(vol.priority(), 200);

    // Icon tracks level and mute
    vol.lastSnap_.level = 0.8;
    EXPECT_EQ(vol.icon(), std::string("󰕾"));
    vol.lastSnap_.level = 0.5;
    EXPECT_EQ(vol.icon(), std::string("󰖀"));
    vol.lastSnap_.level = 0.1;
    EXPECT_EQ(vol.icon(), std::string("󰕿"));
    vol.lastSnap_.muted = true;
    EXPECT_EQ(vol.icon(), std::string("󰝟"));

    // Scroll without a backend must be a no-op, not a crash. (x,y are pointer
    // coords forwarded to multi-element indicators; unused here.)
    EXPECT_FALSE(vol.onScroll(0, -1.0, 0.0, 0.0));
}
TEST(VolumeIndicatorCreatesSliderTile) {
    qypr::SystemBackends const backends{};
    qypr::VolumeIndicator vol(backends);
    vol.lastSnap_.level = 0.55;

    auto tile = vol.createTile();
    EXPECT_TRUE(tile != nullptr);
    EXPECT_TRUE(tile->type() == qypr::QSTile::Type::Slider);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile->bounds = {.x = 0, .y = 0, .w = 348, .h = 40};
    tile->draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(VolumeBackendConstructAndTeardown) {
    // Exercises the PulseLoop adapter lifecycle: async connect begins, then
    // the destructor disconnects with callbacks silenced. Must not crash or
    // touch freed memory whether or not a pulse server exists.
    qypr::EventLoop loop;
    qypr::VolumeBackend backend(loop);
    backend.setOnChange([] {});
    backend.start();
    // Snapshot stays unavailable until the (never-run) loop delivers READY.
    EXPECT_FALSE(backend.snapshot().available);
}

// -----------------------------------------------------------------------------
// SNI tray host (Phase 5)
// -----------------------------------------------------------------------------
TEST(SNIParseItemRef) {
    std::string service;
    std::string path;

    // "service/path" form (as reported by the watcher for real items).
    qypr::SNIBackend::parseItemRef(":1.51/org/blueman/sni", service, path);
    EXPECT_EQ(service, std::string(":1.51"));
    EXPECT_EQ(path, std::string("/org/blueman/sni"));

    qypr::SNIBackend::parseItemRef(":1.17/org/ayatana/NotificationItem/nm_applet", service, path);
    EXPECT_EQ(service, std::string(":1.17"));
    EXPECT_EQ(path, std::string("/org/ayatana/NotificationItem/nm_applet"));

    // Bare service name: default object path per the spec.
    qypr::SNIBackend::parseItemRef(":1.42", service, path);
    EXPECT_EQ(service, std::string(":1.42"));
    EXPECT_EQ(path, std::string("/StatusNotifierItem"));
}
TEST(SNITrayHostConstruction) {
    qypr::SystemBackends const backends{};  // no backend
    qypr::SNITrayHost host(backends);

    EXPECT_EQ(host.id(), std::string("sni"));
    EXPECT_TRUE(static_cast<int>(host.zone()) == static_cast<int>(qypr::Zone::Right));
    EXPECT_EQ(host.priority(), 600);
    EXPECT_EQ(host.icon(), std::string(""));  // custom multi-icon draw

    // No backend → hidden, zero width, click is a no-op (not a crash).
    host.onBackendUpdate();
    EXPECT_FALSE(host.visible);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    EXPECT_EQ(host.measureWidth(p), 0.0);
    EXPECT_FALSE(host.onClick(10, 10));
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    EXPECT_EQ(host.tooltip(), std::string("System tray"));
}
TEST(SNITrayHostVisibleWithItems) {
    // Drive the indicator from a backend whose item list we populate directly
    // (no bus needed): the tray host mirrors item count for visibility/width.
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::SNIBackend sni(session);
    sni.items_.push_back(qypr::SNIItem{.service = ":1.51",
                                       .path = "/org/blueman/sni",
                                       .iconName = "blueman",
                                       .title = "blueman",
                                       .status = "Active",
                                       .menuPath = "/org/blueman/sni/menu",
                                       .pixmap = nullptr});
    sni.items_.push_back(qypr::SNIItem{.service = ":1.17",
                                       .path = "/org/ayatana/NotificationItem/nm_applet",
                                       .iconName = "nm-signal-75",
                                       .title = "Network",
                                       .status = "Active",
                                       .menuPath = "",
                                       .pixmap = nullptr});

    qypr::SystemBackends backends{};
    backends.sni = &sni;
    qypr::SNITrayHost host(backends);

    host.onBackendUpdate();
    EXPECT_TRUE(host.visible);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    // Two 18px icons + one 6px gap + 2*8px side pad = 68px.
    EXPECT_NEAR(host.measureWidth(p), (2 * 18.0) + 6.0 + (2 * 8.0), 0.01);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);

    // Tooltip reflects the count when more than one item.
    EXPECT_EQ(host.tooltip(), std::string("2 tray items"));

    // A click within the first icon's slot resolves without a bus (the mock's
    // async call is a no-op) and is consumed.
    host.bounds = {.x = 100, .y = 0, .w = host.measureWidth(p), .h = 36};
    EXPECT_TRUE(host.onClick(100 + 8 + 2, 18));
}

// -----------------------------------------------------------------------------
// Workspaces + Active window (WM widgets — session-sensitive, hidden while locked)
// -----------------------------------------------------------------------------
TEST(WorkspacesIndicatorConstruction) {
    qypr::SystemBackends const backends{};  // no backend
    qypr::WorkspacesIndicator ws(backends);

    EXPECT_EQ(ws.id(), std::string("workspaces"));
    EXPECT_TRUE(static_cast<int>(ws.zone()) == static_cast<int>(qypr::Zone::Left));
    EXPECT_EQ(ws.priority(), -100);
    EXPECT_EQ(ws.icon(), std::string(""));
    EXPECT_TRUE(ws.sensitive());  // must be gated off while locked

    ws.onBackendUpdate();
    EXPECT_FALSE(ws.visible);  // no backend → hidden
    EXPECT_EQ(ws.tooltip(), std::string("Workspaces"));
}
TEST(WorkspacesIndicatorRendersAndActivates) {
    qypr::WorkspaceBackend backend;
    backend.snap_.available = true;
    backend.snap_.workspaces = {{.name = "1", .active = true, .urgent = false},
                                {.name = "2", .active = false, .urgent = false},
                                {.name = "3", .active = false, .urgent = true}};

    qypr::SystemBackends backends{};
    backends.workspace = &backend;
    qypr::WorkspacesIndicator ws(backends);

    ws.onBackendUpdate();
    EXPECT_TRUE(ws.visible);
    EXPECT_EQ(ws.tooltip(), std::string("Workspace 1"));

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 600, 40);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    EXPECT_TRUE(ws.measureWidth(p) > 0);

    // draw populates per-pill hit rects; a click inside a pill is consumed
    // (activate is a no-op without a live compositor, but must not crash).
    ws.bounds = {.x = 0, .y = 0, .w = ws.measureWidth(p), .h = 36};
    ws.draw(p, qypr::nowMs());
    EXPECT_TRUE(ws.onClick(ws.bounds.x + 10, 18));
    EXPECT_FALSE(ws.onClick(9000, 18));  // outside all pills
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(ActiveWindowIndicatorConstruction) {
    qypr::SystemBackends const backends{};
    qypr::ActiveWindowIndicator aw(backends);

    EXPECT_EQ(aw.id(), std::string("active-window"));
    EXPECT_TRUE(static_cast<int>(aw.zone()) == static_cast<int>(qypr::Zone::Center));
    EXPECT_TRUE(aw.sensitive());
    EXPECT_EQ(aw.icon(), std::string(""));

    aw.onBackendUpdate();
    EXPECT_FALSE(aw.visible);
    EXPECT_EQ(aw.label(), std::string(""));
}
TEST(ActiveWindowIndicatorShowsFocused) {
    qypr::ToplevelBackend backend;
    backend.snap_.available = true;
    backend.snap_.hasActive = true;
    backend.snap_.appId = "kitty";
    backend.snap_.title = "vim — file.cpp";

    qypr::SystemBackends backends{};
    backends.toplevel = &backend;
    qypr::ActiveWindowIndicator aw(backends);

    aw.onBackendUpdate();
    EXPECT_TRUE(aw.visible);
    EXPECT_EQ(aw.label(), std::string("vim — file.cpp"));  // title preferred
    EXPECT_EQ(aw.tooltip(), std::string("kitty — vim — file.cpp"));

    // Falls back to app id when the title is empty.
    backend.snap_.title = "";
    aw.onBackendUpdate();
    EXPECT_EQ(aw.label(), std::string("kitty"));
}
TEST(ActiveWindowIndicatorTruncatesUtf8) {
    qypr::ToplevelBackend backend;
    backend.snap_.available = true;
    backend.snap_.hasActive = true;
    // 70 multibyte codepoints (each "→" is 3 bytes): must cut on a codepoint
    // boundary and append the ellipsis — never split a character.
    std::string title;
    for (int i = 0; i < 70; ++i) {
        title += "\xE2\x86\x92";  // U+2192
    }
    backend.snap_.title = title;

    qypr::SystemBackends backends{};
    backends.toplevel = &backend;
    qypr::ActiveWindowIndicator aw(backends);
    aw.onBackendUpdate();

    std::string const shown = aw.label();
    // 60 codepoints kept (60*3 bytes) + "…" (3 bytes).
    EXPECT_EQ(shown.size(), static_cast<size_t>((60 * 3) + 3));
    EXPECT_TRUE(shown.size() < title.size());
}

// -----------------------------------------------------------------------------
// SessionMapper — focus-correlation window↔workspace inference
// -----------------------------------------------------------------------------
TEST(PagerIndicatorConstruction) {
    qypr::SystemBackends const backends{};  // no backends
    qypr::PagerIndicator pg(backends);

    EXPECT_EQ(pg.id(), std::string("pager"));
    EXPECT_EQ(pg.priority(), -100);
    EXPECT_TRUE(pg.sensitive());  // session content: gated off while locked

    pg.onBackendUpdate();
    EXPECT_FALSE(pg.visible);  // nothing anywhere → hidden
}
TEST(PagerIndicatorRendersClustersAndConsumesClicks) {
    qypr::WorkspaceBackend wsb;
    wsb.snap_.available = true;
    wsb.snap_.workspaces = {{.name = "1", .active = true, .urgent = false},
                            {.name = "2", .active = false, .urgent = false}};
    qypr::ToplevelBackend tlb;
    tlb.snap_.available = true;
    tlb.snap_.windows = {
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false},
        {.id = 2, .appId = "firefox", .title = "", .active = false, .minimized = false},
        {.id = 3, .appId = "org.kde.dolphin", .title = "", .active = false, .minimized = true}};

    qypr::SystemBackends backends{};
    backends.workspace = &wsb;
    backends.toplevel = &tlb;
    qypr::PagerIndicator pg(backends);
    pg.onBackendUpdate();
    EXPECT_TRUE(pg.visible);
    // Tooltip names apps via their pretty id.
    EXPECT_TRUE(pg.tooltip().find("dolphin") != std::string::npos);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 40);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    const double w = pg.measureWidth(p);
    EXPECT_TRUE(w > 0);
    pg.bounds = {.x = 0, .y = 0, .w = w, .h = 36};
    pg.draw(p, qypr::nowMs());

    // Clicks inside the module are consumed (activate calls are no-ops without
    // a live compositor but must route without crashing); outside is refused.
    EXPECT_TRUE(pg.onClick(pg.bounds.x + 6.0, 18));
    EXPECT_FALSE(pg.onClick(9000, 18));
    // Middle/right clicks only act on icons; over chip bodies they are no-ops.
    (void)pg.onMiddleClick(pg.bounds.x + 6.0, 18);
    (void)pg.onSecondaryClick(pg.bounds.x + 6.0, 18);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(PagerIndicatorDegradesToFlatTaskbar) {
    // No ext-workspace-v1: the pager must still render every open window as a
    // flat strip (the old taskbar behaviour), never hide.
    qypr::ToplevelBackend tlb;
    tlb.snap_.available = true;
    tlb.snap_.windows = {
        {.id = 1, .appId = "kitty", .title = "", .active = true, .minimized = false}};

    qypr::SystemBackends backends{};
    backends.toplevel = &tlb;
    qypr::PagerIndicator pg(backends);
    pg.onBackendUpdate();
    EXPECT_TRUE(pg.visible);
    EXPECT_FALSE(pg.tooltip().empty());

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 40);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    pg.bounds = {.x = 0, .y = 0, .w = pg.measureWidth(p), .h = 36};
    pg.draw(p, qypr::nowMs());
    EXPECT_TRUE(pg.onClick(pg.bounds.x + 5.0, 18));
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(PagerIndicatorOverflowCapsIconsPerCluster) {
    qypr::WorkspaceBackend wsb;
    wsb.snap_.available = true;
    wsb.snap_.workspaces = {{.name = "1", .active = true, .urgent = false}};
    qypr::ToplevelBackend tlb;
    tlb.snap_.available = true;
    for (uint64_t i = 1; i <= 10; ++i) {
        tlb.snap_.windows.push_back(
            {.id = i, .appId = "kitty", .title = "", .active = i == 1, .minimized = false});
    }

    qypr::SystemBackends backends{};
    backends.workspace = &wsb;
    backends.toplevel = &tlb;
    qypr::PagerIndicator pg(backends);
    pg.onBackendUpdate();
    EXPECT_TRUE(pg.visible);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 800, 40);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    pg.bounds = {.x = 0, .y = 0, .w = pg.measureWidth(p), .h = 36};
    // Collapsed caps at 3 icons (+badge); must stay narrow and draw cleanly.
    EXPECT_TRUE(pg.measureWidth(p) < 200.0);
    pg.draw(p, qypr::nowMs());
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
TEST(SensitiveIndicatorsGatedByDefault) {
    // The privacy contract: workspace/active-window are sensitive; the common
    // indicators are not. StatusBar hides sensitive ones unless session content
    // is explicitly enabled (never on the lock screen).
    qypr::SystemBackends const b{};
    qypr::WorkspacesIndicator const ws(b);
    qypr::ActiveWindowIndicator const aw(b);
    qypr::BatteryIndicator const bat(b);
    EXPECT_TRUE(ws.sensitive());
    EXPECT_TRUE(aw.sensitive());
    EXPECT_FALSE(bat.sensitive());  // default: not sensitive
}

TEST(SessionAppletsAbsentWithoutTheirBackends) {
    // The lock screen's App supplies no SystemActions and no NotificationMonitor.
    // Both applets must then not exist at all — belt-and-braces with sensitive().
    qypr::SystemBackends const none{};
    qypr::PowerMenuIndicator const power(none);
    qypr::NotificationIndicator const notes(none);
    EXPECT_FALSE(power.visible);
    EXPECT_FALSE(notes.visible);

    // And both are session-sensitive, so even a host that constructed them would
    // have to opt in explicitly (StatusBar::setSessionContentVisible).
    EXPECT_TRUE(power.sensitive());
    EXPECT_TRUE(notes.sensitive());
}
TEST(SessionAppletsAppearWithBackends) {
    qypr::EventLoop loop;
    qypr::SystemActions pm(loop);          // TESTING: actions are no-ops
    qypr::NotificationMonitor mon(loop);  // not started: empty, but present
    qypr::SystemBackends b{};
    b.power = &pm;
    b.notifications = &mon;

    qypr::PowerMenuIndicator power(b);
    qypr::NotificationIndicator notes(b);
    EXPECT_TRUE(power.visible);
    EXPECT_TRUE(notes.visible);

    // The power indicator is a trigger, not a menu: it stays visible and
    // routes into Quick Settings (StatusBar diverts "power" before any
    // detailed view is consulted), while the desktop's power UI lives in
    // `waylaunch --power`. The notification history keeps its own popover.
    EXPECT_TRUE(power.hasDetailedView());
    EXPECT_TRUE(notes.hasDetailedView());
    EXPECT_TRUE(power.createDetailedView() == nullptr);
    EXPECT_TRUE(notes.createDetailedView() != nullptr);
}
TEST(NotificationIndicatorCountAndDnd) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor mon(loop);
    qypr::DndState dnd;
    qypr::SystemBackends b{};
    b.notifications = &mon;
    b.dnd = &dnd;
    qypr::NotificationIndicator const ind(b);

    // Empty: no count label, muted colour, honest tooltip.
    EXPECT_EQ(ind.label(), std::string(""));
    EXPECT_EQ(ind.tooltip(), std::string("No notifications"));

    // With notifications the bell carries the count and the singular/plural is
    // correct. (notes_ is reachable via `#define private public`.)
    mon.notes_.push_back(qypr::Notification{});
    EXPECT_EQ(ind.label(), std::string("1"));
    EXPECT_EQ(ind.tooltip(), std::string("1 notification"));
    mon.notes_.push_back(qypr::Notification{});
    EXPECT_EQ(ind.label(), std::string("2"));
    EXPECT_EQ(ind.tooltip(), std::string("2 notifications"));

    // DND mutes the bell glyph but keeps the count: suppressed notifications are
    // still waiting for you.
    const std::string bell = ind.icon();
    dnd.setEnabled(true);
    EXPECT_TRUE(ind.icon() != bell);
    EXPECT_EQ(ind.label(), std::string("2"));
    dnd.setEnabled(false);
    EXPECT_EQ(ind.icon(), bell);
}
TEST(NotificationActionsAndInteractivity) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor mon(loop);
    qypr::SystemBackends b{};
    b.notifications = &mon;
    qypr::NotificationIndicator ind(b);

    qypr::Notification note;
    note.id = 1;
    note.daemonId = 100;
    note.app = "TestApp";
    note.title = "Test Title";
    note.body = "Test Body";
    note.actions = {{"default", "Open"}, {"reply", "Reply"}, {"dismiss", "Ignore"}};
    mon.notes_.push_back(note);

    auto popover = ind.createDetailedView();
    EXPECT_TRUE(popover != nullptr);
    EXPECT_TRUE(popover->contentHeight() > 54.0);
}

// -----------------------------------------------------------------------------
// System monitor (/proc parsing) — pure helpers, no real /proc needed
// -----------------------------------------------------------------------------
TEST(LauncherGating) {
    // No DesktopIndex (lock screen): the launcher never appears and offers no
    // detailed view — a locked machine can never spawn an app from the bar.
    qypr::SystemBackends const none{};
    qypr::LauncherIndicator locked(none);
    EXPECT_FALSE(locked.visible);
    EXPECT_FALSE(locked.hasDetailedView());
    EXPECT_TRUE(locked.createDetailedView() == nullptr);

    // With an index supplied (the unlocked bar): present as a trigger that
    // spawns waylaunch — no popover of its own (deleted as duplicate UI).
    qypr::DesktopIndex idx;  // empty is fine; presence is what gates
    qypr::SystemBackends bar{};
    bar.desktopIndex = &idx;
    qypr::LauncherIndicator unlocked(bar);
    EXPECT_TRUE(unlocked.visible);
    EXPECT_FALSE(unlocked.hasDetailedView());
    EXPECT_TRUE(unlocked.createDetailedView() == nullptr);
    // NOTE: onClick() is intentionally not invoked here — it spawns the real
    // launcher command via spawnDetached (side effect).
}

// (LauncherPopoverKeyboardAndEmptyState deleted with the popover: the in-bar
// .desktop search was duplicate UI — waylaunch is the launcher.)

// -----------------------------------------------------------------------------
// Keyboard layout — short-label derivation (pure) + gating
// -----------------------------------------------------------------------------
TEST(KeyboardLayoutShortLabel) {
    using qypr::KeyboardLayout;
    // Parenthetical country/variant code wins.
    EXPECT_EQ(KeyboardLayout::shortLabel("English (US)"), std::string("US"));
    EXPECT_EQ(KeyboardLayout::shortLabel("English (UK)"), std::string("UK"));
    // Fallback: first two letters of the description, uppercased.
    EXPECT_EQ(KeyboardLayout::shortLabel("Russian"), std::string("RU"));
    EXPECT_EQ(KeyboardLayout::shortLabel("French"), std::string("FR"));
    // Overlong parenthetical (a variant name) falls back to the description.
    EXPECT_EQ(KeyboardLayout::shortLabel("English (Dvorak)"), std::string("EN"));
    // Degenerate input never crashes.
    EXPECT_EQ(KeyboardLayout::shortLabel(""), std::string("??"));
}
TEST(KeyboardLayoutGating) {
    // No backend (lock screen): never visible.
    qypr::SystemBackends const none{};
    qypr::KeyboardLayoutIndicator locked(none);
    locked.onBackendUpdate();
    EXPECT_FALSE(locked.visible);
    EXPECT_EQ(locked.tooltip(), std::string("Keyboard layout"));

    // Backend present but a single layout: still hidden (nothing to switch).
    qypr::KeyboardLayout kb;
    qypr::SystemBackends b{};
    b.keyboardLayout = &kb;
    qypr::KeyboardLayoutIndicator ind(b);
    kb.update("English (US)", 0, 1);
    ind.onBackendUpdate();
    EXPECT_FALSE(ind.visible);

    // Two layouts: now visible, showing the active code + full-name tooltip.
    kb.update("English (US)", 0, 2);
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_EQ(ind.label(), std::string("US"));
    EXPECT_EQ(ind.tooltip(), std::string("Keyboard layout: English (US)"));

    // Switching group updates the label.
    kb.update("Russian", 1, 2);
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_EQ(ind.label(), std::string("RU"));
}

// -----------------------------------------------------------------------------
// StateCache — last-known indicator values, persisted so the first frame after a
// reboot carries real numbers instead of neutral "unknown" glyphs. The daemons
// that own this state (UPower especially) are usually not running yet when the
// bar starts, so this is the only thing standing between the user and a row of
// meaningless placeholders for the first few seconds.
//
// SystemBus is constructed against the unmocked sd_bus_open_system, which fails
// in the test environment and leaves the connection null — exactly what we want
// here, since none of this touches the bus.
// -----------------------------------------------------------------------------
