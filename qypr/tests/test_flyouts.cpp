#include "test_framework.hpp"
#include "ui/statusbar/MenuPopover.hpp"

namespace {
struct FlyoutHost : qypr::Invalidator {
    void invalidate() override {}
};

struct FlyoutCanvas {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1280, 800);
    cairo_t* cr = cairo_create(surface);
    qypr::Painter painter{cr};
    FlyoutCanvas() = default;
    ~FlyoutCanvas() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    FlyoutCanvas(const FlyoutCanvas&) = delete;
    FlyoutCanvas& operator=(const FlyoutCanvas&) = delete;
    FlyoutCanvas(FlyoutCanvas&&) = delete;
    FlyoutCanvas& operator=(FlyoutCanvas&&) = delete;
};

void testScrollRoutesEveryZone() {
    qypr::EventLoop loop;
    FlyoutHost host;
    qypr::SystemBackends backends;
    qypr::IndicatorRegistry::ModuleSelection modules;
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);
    struct ScrollTarget : qypr::StatusIndicator {
        ScrollTarget() : StatusIndicator("scroll-test", qypr::Zone::Right, 0) {}
        [[nodiscard]] std::string icon() const override { return "S"; }
        [[nodiscard]] std::string tooltip() const override { return "Scroll target"; }
        bool onScroll(double, double, double, double) override { return true; }
    };
    for (auto* zone :
         {&bar.indicators().left(), &bar.indicators().center(), &bar.indicators().right()}) {
        auto target = std::make_unique<ScrollTarget>();
        target->bounds = {100, 0, 30, 30};
        zone->push_back(std::move(target));
        EXPECT_TRUE(bar.handleScroll(110, 15, 0, 1));
        zone->pop_back();
    }
}

void testTrayAliasesShareOneHost() {
    qypr::SystemBackends b;
    b.sessionSurface = true;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.left = {"notification-apps"};
    modules.right = {"sni", "notification-apps"};
    const auto indicators = qypr::IndicatorRegistry::instance().createAll(b, &modules);
    EXPECT_EQ(indicators.size(), 1U);
    EXPECT_EQ(indicators.front()->id(), "notification-apps");
    b.hasSession = false;
    EXPECT_TRUE(qypr::IndicatorRegistry::instance().createAll(b, &modules).empty());
}

void testStatusBarFlyoutSwitchToggleAndDismiss() {
    qypr::EventLoop loop;
    FlyoutHost host;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.left = {"clock"};
    modules.center = {"battery"};
    modules.right = {"notifications"};
    qypr::SystemBackends backends;
    int actions = 0;
    struct DirectAction : qypr::StatusIndicator {
        explicit DirectAction(int& actions)
            : StatusIndicator("direct", qypr::Zone::Right, 1),
              actions_(&actions) {}
        [[nodiscard]] std::string icon() const override { return "A"; }
        [[nodiscard]] std::string tooltip() const override { return "Action"; }
        bool onClick(double, double) override {
            ++(*actions_);
            return true;
        }

    private:
        int* actions_;
    };
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.indicators().right().push_back(std::make_unique<DirectAction>(actions));
    bar.setSessionContentVisible(true);
    bar.indicators().findById("notifications")->visible = true;
    FlyoutCanvas canvas;
    bar.layout(1280, 800);
    bar.draw(canvas.painter, qypr::nowMs());
    auto click = [&](const std::string& id) {
        const auto* ind = bar.indicators().findById(id);
        EXPECT_TRUE(ind != nullptr);
        const auto b = ind->bounds;
        EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, true, 0));
        bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, false, 0);
    };
    click("clock");
    EXPECT_EQ(bar.popovers().ownerId(), std::string("clock"));
    EXPECT_TRUE(bar.wantsKeyboard());
    bar.handlePointerLeave(0);
    EXPECT_TRUE(bar.popovers().active() != nullptr);
    click("battery");
    EXPECT_EQ(bar.popovers().ownerId(), std::string("battery"));
    click("battery");
    EXPECT_TRUE(bar.popovers().active() == nullptr);
    EXPECT_FALSE(bar.wantsKeyboard());
    click("notifications");
    EXPECT_TRUE(bar.handleKey(XKB_KEY_a));
    EXPECT_TRUE(bar.popovers().active() != nullptr);
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));
    EXPECT_TRUE(bar.popovers().active() == nullptr);
    click("clock");
    EXPECT_TRUE(bar.handlePointerButton(640, 790, 0x110, true, 0));
    EXPECT_TRUE(bar.popovers().active() == nullptr);
    click("clock");
    click("direct");  // dismiss must not trigger a direct action behind a flyout
    EXPECT_EQ(actions, 0);
    click("direct");
    EXPECT_EQ(actions, 1);
}

void testStatusBarFlyoutPlacementAllZonesAndEdges() {
    qypr::EventLoop loop;
    FlyoutHost host;
    FlyoutCanvas canvas;
    for (const auto zone : {qypr::Zone::Left, qypr::Zone::Center, qypr::Zone::Right}) {
        for (const bool bottom : {false, true}) {
            for (const int width : {360, 800, 1280}) {
                qypr::IndicatorRegistry::ModuleSelection modules;
                if (zone == qypr::Zone::Left) {
                    modules.left = {"clock"};
                } else if (zone == qypr::Zone::Center) {
                    modules.center = {"clock"};
                } else {
                    modules.right = {"clock"};
                }
                qypr::StatusBar bar(loop, host, {}, &modules);
                bar.setSessionContentVisible(true);
                qypr::BarGeometry geometry;
                geometry.bottom = bottom;
                bar.setGeometry(geometry);
                bar.layout(width, 600);
                auto* clock = bar.indicators().findById("clock");
                const auto trigger = clock->bounds;
                bar.handlePointerButton(trigger.x + 5, trigger.y + 5, 0x110, true, 0);
                bar.draw(canvas.painter, qypr::nowMs());
                auto* panel = bar.popovers().active();
                EXPECT_TRUE(panel != nullptr);
                const auto b = panel->visibleBounds();
                EXPECT_TRUE(b.x >= 8);
                EXPECT_TRUE(b.x + b.w <= width - 8 + 0.01);
                EXPECT_TRUE(b.y >= 8);
                EXPECT_TRUE(b.y + b.h <= 592.01);
                EXPECT_TRUE(bottom ? b.y + b.h <= bar.bounds.y - 6.0
                                   : b.y >= bar.bounds.y + bar.bounds.h + 6.0);
                const double x = b.x;
                bar.layout(width, 600);
                EXPECT_NEAR(panel->visibleBounds().x, x, 0.01);
                EXPECT_TRUE(bar.handleKey(XKB_KEY_Right));
                EXPECT_TRUE(bar.handleKey(XKB_KEY_Home));
            }
        }
    }
}

void testFlyoutOverflowScrollAndClip() {
    struct TallPanel : qypr::DetailedPopover {
        [[nodiscard]] double contentHeight() const override { return 900; }
        void draw(qypr::Painter& painter, int64_t) override {
            painter.fillRect(getBounds(), {1, 0, 0, 1});
        }
    };
    qypr::PopoverManager manager;
    auto panel = std::make_unique<TallPanel>();
    auto* raw = panel.get();
    panel->setViewport({8, 50, 304, 240});
    manager.open(std::move(panel), 1000, 50);
    EXPECT_NEAR(raw->visibleBounds().h, 240, 0.01);
    EXPECT_FALSE(raw->contains(100, 400));
    EXPECT_TRUE(manager.handleScroll(0, 100));
    EXPECT_NEAR(raw->getBounds().y, -50, 0.01);
    EXPECT_TRUE(manager.handleScroll(0, 5000));
    EXPECT_NEAR(raw->getBounds().y + raw->contentHeight(), 290, 0.01);
    FlyoutCanvas canvas;
    manager.draw(canvas.painter, qypr::nowMs() + 10000);
    EXPECT_EQ(cairo_status(canvas.cr), CAIRO_STATUS_SUCCESS);
    cairo_surface_flush(canvas.surface);
    const auto* pixels =
        reinterpret_cast<const uint32_t*>(cairo_image_surface_get_data(canvas.surface));
    EXPECT_EQ(pixels[400 * 1280 + 100], 0U);  // content below viewport was clipped
}

void testFlyoutSliderCaptureAndRelease() {
    qypr::EventLoop loop;
    FlyoutHost host;
    qypr::StatusBar bar(loop, host, {});
    bar.setSessionContentVisible(true);
    double level = 0.5;
    int muteClicks = 0;
    auto tile = std::make_unique<qypr::QSVolumeTile>(
        [&] { return level; }, [&](double value) { level = value; },
        [] { return std::string("V"); }, [&] { ++muteClicks; }, [] { return false; });
    auto* control = tile.get();
    bar.popovers().open(std::make_unique<qypr::SliderPopover>(std::move(tile)), 500, 60);
    FlyoutCanvas canvas;
    bar.draw(canvas.painter, qypr::nowMs());
    const auto b = control->bounds;
    bar.handlePointerButton(b.x + 5, b.y + 20, 0x110, true, 0);
    bar.handlePointerMotion(b.x + b.w - 10, b.y + 20, 0);
    EXPECT_EQ(muteClicks, 1);
    EXPECT_NEAR(level, 0.5, 0.001);
    bar.handlePointerButton(b.x, b.y, 0x110, false, 0);
    bar.handlePointerButton(b.x + b.w / 2, b.y + 20, 0x110, true, 0);
    bar.handlePointerMotion(1200, 700, 0);
    EXPECT_NEAR(level, 1.0, 0.001);  // capture survives leaving the panel
    bar.handlePointerButton(1200, 700, 0x110, false, 0);
    bar.handlePointerMotion(b.x + b.w / 2, b.y + 20, 0);
    EXPECT_NEAR(level, 1.0, 0.001);  // release outside ends capture
    bar.handlePointerButton(b.x + b.w / 2, b.y + 20, 0x112, true, 0);
    EXPECT_NEAR(level, 1.0, 0.001);  // middle click cannot adjust a slider
}

void testQuickSettingsHoverMoreKeyboardAndHeader() {
    qypr::QuickSettingsPanel panel;
    int toggles = 0;
    int details = 0;
    auto tile = std::make_unique<qypr::QSToggleTile>(
        "Bluetooth", "B", [] { return true; }, [&] { ++toggles; });
    auto* control = tile.get();
    tile->setOnSecondary([&] { ++details; });
    panel.addTile(std::move(tile));
    panel.anchorX = 640;
    panel.anchorY = 50;
    FlyoutCanvas canvas;
    panel.draw(canvas.painter, qypr::nowMs());
    const auto b = control->bounds;
    EXPECT_TRUE(b.y + b.h <= panel.getBounds().y + panel.contentHeight());
    panel.handleMotion(b.x + 5, b.y + 5);
    EXPECT_TRUE(control->hovered);
    panel.handleMotion(0, 0);
    EXPECT_FALSE(control->hovered);
    panel.handleClick(b.x + 5, b.y + 5);
    EXPECT_EQ(toggles, 1);
    panel.handleClick(b.x + b.w - 5, b.y + 5);
    EXPECT_EQ(details, 1);
    EXPECT_EQ(toggles, 1);
    EXPECT_TRUE(panel.handleKey(XKB_KEY_Tab));  // close
    EXPECT_TRUE(panel.handleKey(XKB_KEY_Tab));  // Bluetooth
    EXPECT_TRUE(panel.handleKey(XKB_KEY_space));
    EXPECT_EQ(toggles, 2);
    EXPECT_TRUE(panel.handleKey(XKB_KEY_Right));
    EXPECT_EQ(details, 2);
    EXPECT_TRUE(panel.handleKey(XKB_KEY_ISO_Left_Tab));
    EXPECT_TRUE(panel.handleKey(XKB_KEY_Return));
    EXPECT_TRUE(panel.consumeCloseRequest());
}

void testStatusBarReloadClearsBorrowedViews() {
    qypr::EventLoop loop;
    FlyoutHost host;
    qypr::StatusBar bar(loop, host, {});
    bar.setSessionContentVisible(true);
    bar.layout(800, 600);
    auto* clock = bar.indicators().findById("clock");
    bar.activateIndicator(*clock);
    bar.reloadModules({}, nullptr);
    EXPECT_TRUE(bar.popovers().active() == nullptr);
    EXPECT_FALSE(bar.popovers().isTransitioning());
    FlyoutCanvas canvas;
    bar.draw(canvas.painter, qypr::nowMs());
}

void testEveryRegisteredModuleRendersAndRoutesInput() {
    qypr::EventLoop loop;
    FlyoutHost host;
    FlyoutCanvas canvas;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(loop, bus);
    battery.seed({.percentage = 72, .state = qypr::BatterySnapshot::Discharging, .present = true});
    qypr::BrightnessBackend brightness(loop, bus);
    brightness.seed({.current = 55, .max = 100, .available = true});
    qypr::VolumeBackend volume(loop);
    volume.seed({.available = true, .level = 0.5, .sinkName = "Test speakers"});
    qypr::WifiBackend wifi(loop, bus);
    wifi.seed({.available = true, .enabled = true});
    qypr::BluetoothBackend bluetooth(loop, bus);
    qypr::BluetoothSnapshot bt;
    bt.available = true;
    bluetooth.seed(bt);
    qypr::NightLightBackend nightLight;
    qypr::SystemBackends backends;
    backends.sessionSurface = true;
    backends.battery = &battery;
    backends.brightness = &brightness;
    backends.volume = &volume;
    backends.wifi = &wifi;
    backends.bluetooth = &bluetooth;
    backends.nightLight = &nightLight;
    // No session loop, compositor or bus connection: application launch,
    // eject, network and power actions cannot affect the user's desktop.
    const std::vector<std::string> ids = {"active-window",
                                          "battery",
                                          "bluetooth",
                                          "brightness",
                                          "clock",
                                          "dnd",
                                          "email",
                                          "idle-inhibitor",
                                          "kde-connect",
                                          "keyboard-layout",
                                          "launcher",
                                          "mail",
                                          "media",
                                          "night-light",
                                          "notification-apps",
                                          "notifications",
                                          "pager",
                                          "phone-connect",
                                          "power",
                                          "sni",
                                          "system-monitor",
                                          "taskbar",
                                          "usb",
                                          "usb-eject",
                                          "volume",
                                          "whatsapp",
                                          "wifi",
                                          "workspaces"};
    for (const auto& id : ids) {
        qypr::DndState dnd;
        backends.dnd = &dnd;
        std::cout << "  Module: " << id << '\n';
        qypr::IndicatorRegistry::ModuleSelection modules;
        modules.right = {id};
        qypr::StatusBar bar(loop, host, backends, &modules);
        bar.setSessionContentVisible(true);
        EXPECT_EQ(bar.indicators().right().size(), 1U);
        auto* indicator = bar.indicators().right().front().get();
        EXPECT_TRUE(indicator != nullptr);
        indicator->onBackendUpdate();
        indicator->visible = true;  // exercise empty/unavailable device states too
        bar.layout(1280, 800);
        bar.draw(canvas.painter, qypr::nowMs());
        const auto b = indicator->bounds;
        EXPECT_TRUE(b.w >= 0);
        bar.handlePointerMotion(b.x + b.w / 2, b.y + b.h / 2, 0);
        if (!indicator->qsOnly() && b.w > 0) {
            EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, true, 0));
            bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, false, 0);
            if (indicator->hasDetailedView()) {
                EXPECT_TRUE(bar.popovers().active() != nullptr);
                EXPECT_EQ(bar.popovers().ownerId(), indicator->id());
                cairo_save(canvas.cr);
                cairo_set_operator(canvas.cr, CAIRO_OPERATOR_CLEAR);
                cairo_paint(canvas.cr);
                cairo_restore(canvas.cr);
                bar.setBackdrop(true, 0.96);
                bar.draw(canvas.painter, qypr::nowMs() + 10000);
                if (const char* dir = std::getenv("QYPR_TEST_ARTIFACT_DIR")) {
                    std::filesystem::create_directories(dir);
                    const auto path = std::filesystem::path(dir) / (id + ".png");
                    EXPECT_EQ(cairo_surface_write_to_png(canvas.surface, path.c_str()),
                              CAIRO_STATUS_SUCCESS);
                }
                auto* panel = bar.popovers().active();
                const auto panelBounds = panel->visibleBounds();
                EXPECT_TRUE(panelBounds.x >= 8);
                EXPECT_TRUE(panelBounds.x + panelBounds.w <= 1272.01);
                EXPECT_EQ(panel->autoDismissMs(), 0);
                bar.handlePointerMotion(panelBounds.x + 10, panelBounds.y + 10, 0);
                bar.handlePointerLeave(0);
                EXPECT_TRUE(bar.popovers().active() == panel);
                EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));
                EXPECT_TRUE(bar.popovers().active() == nullptr);
            }
        }
        if (indicator->qsOnly()) { EXPECT_TRUE(indicator->createTile() != nullptr); }
        EXPECT_EQ(cairo_status(canvas.cr), CAIRO_STATUS_SUCCESS);
    }
}

void testTrayMenuKeyboardAndDisabledRows() {
    qypr::MenuPopover menu(nullptr, "", "",
                           {{.id = 1, .label = "Unavailable", .enabled = false},
                            {.id = 2, .separator = true},
                            {.id = 3, .label = "Open"},
                            {.id = 4, .label = "Hidden", .visible = false}},
                           "Tray");
    menu.anchorX = 400;
    menu.anchorY = 60;
    FlyoutCanvas canvas;
    menu.draw(canvas.painter, qypr::nowMs());
    const auto b = menu.getBounds();
    EXPECT_FALSE(menu.handleClick(b.x + 40, b.y + 15));
    EXPECT_FALSE(menu.consumeCloseRequest());
    EXPECT_TRUE(menu.handleKey(XKB_KEY_Down));
    EXPECT_TRUE(menu.handleKey(XKB_KEY_Return));
    EXPECT_TRUE(menu.consumeCloseRequest());
    EXPECT_FALSE(menu.consumeCloseRequest());
}

void testQuickSettingsVolumeDetailsWithoutBarModule() {
    qypr::EventLoop loop;
    qypr::VolumeBackend volume(loop);
    volume.seed({.available = true, .level = 0.4, .sinkName = "Speakers"});
    qypr::SystemBackends backends;
    backends.sessionSurface = true;
    backends.volume = &volume;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"power"};
    FlyoutHost host;
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);
    bar.indicators().findById("power")->visible = true;
    bar.layout(1280, 800);
    bar.toggleQuickSettings();
    FlyoutCanvas canvas;
    bar.draw(canvas.painter, qypr::nowMs());
    const auto b = bar.quickSettings().boundsFor(qypr::QSTile::Role::Volume);
    const auto power = bar.quickSettings().powerBounds_;
    const auto close = bar.quickSettings().closeBounds_;
    EXPECT_TRUE(power.x + power.w < close.x);
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w - 10, b.y + b.h / 2, 0x110, true, 0));
    EXPECT_EQ(bar.popovers().ownerId(), std::string("volume"));
    EXPECT_TRUE(bar.popovers().active() != &bar.quickSettings());
    bar.draw(canvas.painter, qypr::nowMs());
    const auto popup = bar.popovers().active()->getBounds();
    bar.layout(800, 600, false);  // drawing another monitor must not move the popup
    EXPECT_NEAR(bar.popovers().active()->getBounds().x, popup.x, 0.01);
    EXPECT_NEAR(bar.popovers().active()->getBounds().y, popup.y, 0.01);
    bar.handleKey(XKB_KEY_Escape);
    EXPECT_FALSE(bar.wantsKeyboard());
}

}  // namespace

void runStatusBarFlyoutTests() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"TrayMenuKeyboardAndDisabledRows", testTrayMenuKeyboardAndDisabledRows},
        {"QuickSettingsVolumeDetailsWithoutBarModule",
         testQuickSettingsVolumeDetailsWithoutBarModule},
        {"StatusBarFlyoutSwitchToggleAndDismiss", testStatusBarFlyoutSwitchToggleAndDismiss},
        {"ScrollRoutesEveryZone", testScrollRoutesEveryZone},
        {"TrayAliasesShareOneHost", testTrayAliasesShareOneHost},
        {"StatusBarFlyoutPlacementAllZonesAndEdges", testStatusBarFlyoutPlacementAllZonesAndEdges},
        {"FlyoutOverflowScrollAndClip", testFlyoutOverflowScrollAndClip},
        {"FlyoutSliderCaptureAndRelease", testFlyoutSliderCaptureAndRelease},
        {"QuickSettingsHoverMoreKeyboardAndHeader", testQuickSettingsHoverMoreKeyboardAndHeader},
        {"StatusBarReloadClearsBorrowedViews", testStatusBarReloadClearsBorrowedViews},
        {"EveryRegisteredModuleRendersAndRoutesInput",
         testEveryRegisteredModuleRendersAndRoutesInput},
    };
    for (const auto& [name, run] : tests) {
        std::cout << "Running test: " << name << "...\n";
        ++g_tests_run;
        try {
            run();
            std::cout << "  PASS\n";
        } catch (const std::exception& e) {
            ++g_tests_failed;
            std::cout << "  FAIL: " << e.what() << '\n';
        }
    }
}
