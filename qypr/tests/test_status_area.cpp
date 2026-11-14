// test_status_area.cpp - Tray claims, menu sources, context-menu routing.
#include "test_framework.hpp"

#include <xkbcommon/xkbcommon-keysyms.h>

#include "ui/indicators/TrayAppBinding.hpp"
#include "ui/statusbar/MenuPopover.hpp"
#include "ui/statusbar/MenuSource.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace {

struct AreaHost : qypr::Invalidator {
    void invalidate() override {}
};

struct AreaCanvas {
    cairo_surface_t* surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1280, 800);
    cairo_t* cr = cairo_create(surface);
    qypr::Painter painter{cr};
    AreaCanvas() = default;
    ~AreaCanvas() {
        cairo_destroy(cr);
        cairo_surface_destroy(surface);
    }
    AreaCanvas(const AreaCanvas&) = delete;
    AreaCanvas& operator=(const AreaCanvas&) = delete;
    AreaCanvas(AreaCanvas&&) = delete;
    AreaCanvas& operator=(AreaCanvas&&) = delete;
};

qypr::SNIItem trayItem(const std::string& service, const std::string& id,
                       const std::string& title) {
    qypr::SNIItem item;
    item.service = service;
    item.path = "/StatusNotifierItem";
    item.title = title;
    item.status = "Active";
    item.id = id;
    return item;
}

}  // namespace

TEST(StatusAreaClaimsFollowLayout) {
    qypr::StatusArea area;
    // Nothing in the layout: nothing is claimed, so nothing can go missing.
    EXPECT_EQ(area.claimant("nm-applet"), std::string());

    area.setLayoutModules({"wifi", "clock"});
    EXPECT_TRUE(area.claimedBy("wifi", "nm-applet"));
    EXPECT_EQ(area.claimant("NM-Applet"), std::string("wifi"));  // case-insensitive
    // bluetooth is not in the layout: blueman stays an ordinary tray app.
    EXPECT_EQ(area.claimant("blueman"), std::string());

    area.setLayoutModules({"wifi", "bluetooth", "phone-link"});
    EXPECT_EQ(area.claimant("blueman"), std::string("bluetooth"));
    EXPECT_EQ(area.claimant("kdeconnect.indicator"), std::string("phone-link"));  // prefix
    EXPECT_EQ(area.claimant("KDE Connect"), std::string("phone-link"));
    EXPECT_EQ(area.claimant("TelegramDesktop"), std::string());

    EXPECT_TRUE(qypr::StatusArea::patternMatches("blueman-*", "blueman-tray"));
    EXPECT_FALSE(qypr::StatusArea::patternMatches("blueman", "blueman-tray"));
    EXPECT_FALSE(qypr::StatusArea::patternMatches("", "x"));
}

TEST(StatusAreaConfigExtendsClaimsAndPlacement) {
    qypr::Config config;
    EXPECT_TRUE(config.load(writeTempConfig("[tray]\n"
                                            "claims-wifi = iwgtk\n"
                                            "pinned = TelegramDesktop, org.gnome.Nautilus\n"
                                            "hidden = steam\n")));
    qypr::StatusArea area;
    area.configure(&config);
    area.setLayoutModules({"wifi"});
    EXPECT_EQ(area.claimant("iwgtk"), std::string("wifi"));
    EXPECT_EQ(area.claimant("nm-applet"), std::string("wifi"));  // defaults kept
    EXPECT_TRUE(area.pinned("telegramdesktop"));
    EXPECT_TRUE(area.pinned("org.gnome.Nautilus"));
    EXPECT_TRUE(area.hidden("Steam"));
    EXPECT_FALSE(area.hidden("telegramdesktop"));
    area.configure(nullptr);
    EXPECT_EQ(area.claimant("iwgtk"), std::string());
}

TEST(CompositeMenuStacksSectionsAndRoutesActivation) {
    int fired = 0;
    auto native = std::make_unique<qypr::NativeMenu>();
    native->add("Ring phone", [&] { fired += 1; });
    const int32_t more = native->submenu("More");
    native->add("Ping", [&] { fired += 10; }, true, -1, more);
    auto second = std::make_unique<qypr::NativeMenu>();
    second->add("Disabled", [&] { fired += 100; }, false);
    second->add("Toggle", [&] { fired += 1000; }, true, 1);
    auto empty = std::make_unique<qypr::NativeMenu>();

    qypr::CompositeMenu menu;
    menu.addSection(std::move(native));
    menu.addSection(std::move(empty));  // empty sections add no separator
    menu.addSection(std::move(second));

    const auto root = menu.fetch(0);
    EXPECT_EQ(root.size(), 5U);  // Ring, More, ─, Disabled, Toggle
    EXPECT_EQ(root[0].label, std::string("Ring phone"));
    EXPECT_TRUE(root[1].hasSubmenu);
    EXPECT_TRUE(root[2].separator);
    EXPECT_EQ(root[4].toggleState, 1);
    EXPECT_EQ(root[4].toggleType, std::string("checkmark"));

    const auto sub = menu.fetch(root[1].id);
    EXPECT_EQ(sub.size(), 1U);
    EXPECT_EQ(sub[0].label, std::string("Ping"));
    // Fetching the root again keeps ids stable.
    EXPECT_EQ(menu.fetch(0)[0].id, root[0].id);

    menu.activate(root[0].id);
    menu.activate(sub[0].id);
    menu.activate(root[4].id);
    menu.activate(root[2].id);  // separator: no-op
    menu.activate(9999);        // unknown: no-op
    EXPECT_EQ(fired, 1011);

    // The popover drills into a native submenu and fires leaves.
    auto popMenu = std::make_unique<qypr::NativeMenu>();
    int leaf = 0;
    const int32_t group = popMenu->submenu("Group");
    popMenu->add("Leaf", [&] { ++leaf; }, true, -1, group);
    qypr::MenuPopover pop(std::move(popMenu), "Title");
    EXPECT_TRUE(pop.hasItems());
    pop.anchorX = 400;
    AreaCanvas canvas;
    pop.draw(canvas.painter, qypr::nowMs());
    EXPECT_TRUE(pop.handleKey(XKB_KEY_Down));
    EXPECT_TRUE(pop.handleKey(XKB_KEY_Return));  // into "Group"
    pop.draw(canvas.painter, qypr::nowMs());
    EXPECT_TRUE(pop.handleKey(XKB_KEY_Down));  // back header
    EXPECT_TRUE(pop.handleKey(XKB_KEY_Down));  // "Leaf"
    EXPECT_TRUE(pop.handleKey(XKB_KEY_Return));
    EXPECT_EQ(leaf, 1);
    EXPECT_TRUE(pop.consumeCloseRequest());

    qypr::MenuPopover none(std::make_unique<qypr::NativeMenu>(), "Empty");
    EXPECT_FALSE(none.hasItems());
}

TEST(TrayHostSkipsItemsClaimedByLayoutModules) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::SNIBackend sni(session);
    sni.items_.push_back(trayItem(":1.22", "nm-applet", "Network"));
    sni.items_.push_back(trayItem(":1.80", "TelegramDesktop", "Telegram"));

    qypr::StatusArea area;
    qypr::SystemBackends backends{};
    backends.sni = &sni;
    backends.statusArea = &area;
    qypr::SNITrayHost host(backends);
    AreaCanvas canvas;

    area.setLayoutModules({"sni"});  // no wifi module: nm-applet stays in the tray
    host.onBackendUpdate();
    EXPECT_NEAR(host.measureWidth(canvas.painter), 64.0, 0.01);  // two cells

    area.setLayoutModules({"wifi", "sni"});  // wifi absorbs nm-applet
    host.onBackendUpdate();
    EXPECT_TRUE(host.visible);
    EXPECT_NEAR(host.measureWidth(canvas.painter), 36.0, 0.01);  // one cell
    EXPECT_EQ(host.tooltip(), std::string("1 tray items"));

    sni.items_.pop_back();  // Telegram exits: only the claimed applet is left
    host.onBackendUpdate();
    EXPECT_FALSE(host.visible);
    EXPECT_NEAR(host.measureWidth(canvas.painter), 0.0, 0.01);
}

TEST(NativeModulesForwardGesturesToClaimedApplet) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::SNIBackend sni(session);
    qypr::StatusArea area;
    area.setLayoutModules({"wifi", "bluetooth"});

    // No dbusmenu backend: its fetch is a synchronous bus call the test bus
    // cannot serve. The applet's menu path is still forwarded below.
    qypr::SystemBackends backends{};
    backends.sni = &sni;
    backends.statusArea = &area;
    qypr::WifiIndicator wifi(backends);
    qypr::BluetoothIndicator bluetooth(backends);

    // No applet running: right/middle click stay inert.
    EXPECT_FALSE(wifi.hasContextMenu());
    EXPECT_FALSE(wifi.onMiddleClick(10, 10));

    auto applet = trayItem(":1.22", "nm-applet", "Network");
    applet.menuPath = "/org/ayatana/NotificationItem/nm_applet/Menu";
    sni.items_.push_back(applet);
    EXPECT_TRUE(wifi.hasContextMenu());
    EXPECT_FALSE(bluetooth.hasContextMenu());  // blueman is not running
    EXPECT_TRUE(wifi.onMiddleClick(10, 10));   // SecondaryActivate sent

    // Without a dbusmenu there is nothing to draw, so the module asks the
    // applet to show its own menu (ContextMenu) and opens no popover.
    EXPECT_TRUE(wifi.createContextMenu(10, 10) == nullptr);

    // A module out of the layout claims nothing.
    area.setLayoutModules({"bluetooth"});
    EXPECT_FALSE(wifi.hasContextMenu());

    // The lock screen never receives the tray backend.
    qypr::SystemBackends locked = backends;
    locked.hasSession = false;
    locked.sync();
    qypr::WifiIndicator lockedWifi(locked);
    area.setLayoutModules({"wifi"});
    EXPECT_FALSE(lockedWifi.hasContextMenu());
}

namespace {

void testRightClickAndMenuKeyOpenContextMenu() {
    qypr::EventLoop loop;
    AreaHost host;
    qypr::SystemBackends backends;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"clock"};
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);

    struct MenuTarget : qypr::StatusIndicator {
        explicit MenuTarget(int& fired)
            : StatusIndicator("menu-target", qypr::Zone::Right, 1),
              fired_(&fired) {
            visible = true;
        }
        [[nodiscard]] std::string icon() const override { return "M"; }
        [[nodiscard]] std::string tooltip() const override { return "Menu target"; }
        [[nodiscard]] bool hasContextMenu() const override { return true; }
        std::unique_ptr<qypr::DetailedPopover> createContextMenu(double, double) override {
            auto menu = std::make_unique<qypr::NativeMenu>();
            menu->add("Do it", [f = fired_] { ++(*f); });
            return std::make_unique<qypr::MenuPopover>(std::move(menu), "Target");
        }

    private:
        int* fired_;
    };
    int fired = 0;
    bar.indicators().right().push_back(std::make_unique<MenuTarget>(fired));
    AreaCanvas canvas;
    bar.layout(1280, 800);
    bar.draw(canvas.painter, qypr::nowMs());

    const auto b = bar.indicators().findById("menu-target")->bounds;
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x111, true, 0));
    EXPECT_EQ(bar.popovers().ownerId(), std::string("menu-target"));
    auto* pop = bar.popovers().active();
    EXPECT_TRUE(pop != nullptr);
    pop->draw(canvas.painter, qypr::nowMs());
    const auto pb = pop->getBounds();
    // First row of the menu: fires and closes.
    EXPECT_TRUE(bar.handlePointerButton(pb.x + pb.w / 2, pb.y + 6 + 15, 0x110, true, 0));
    EXPECT_EQ(fired, 1);
    EXPECT_TRUE(bar.popovers().active() == nullptr);

    // Keyboard: focus the module and press Menu.
    bar.indicators().findById("menu-target")->focused = true;
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Menu));
    EXPECT_EQ(bar.popovers().ownerId(), std::string("menu-target"));
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));
    EXPECT_TRUE(bar.popovers().active() == nullptr);

    // Left click still takes the default path (no detail view here: nothing).
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, true, 0));
    EXPECT_TRUE(bar.popovers().active() == nullptr);
}

void testStatusAreaDetailRequestOpensAnchoredFlyout() {
    qypr::EventLoop loop;
    AreaHost host;
    qypr::SystemBackends backends;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"clock", "battery"};
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);
    AreaCanvas canvas;
    bar.layout(1280, 800);
    bar.draw(canvas.painter, qypr::nowMs());

    // The layout reached the status area.
    EXPECT_TRUE(bar.statusArea().inLayout("clock"));
    EXPECT_TRUE(bar.statusArea().inLayout("battery"));

    // A request left by a popover is served after input handling, anchored
    // to (and owned by) the requesting chevron.
    bar.statusArea().requestDetail("clock", "battery");
    bar.handleKey(XKB_KEY_a);
    EXPECT_TRUE(bar.popovers().active() != nullptr);
    EXPECT_EQ(bar.popovers().ownerId(), std::string("battery"));
    EXPECT_FALSE(bar.statusArea().takeDetailRequest().has_value());

    // Unknown module: ignored.
    bar.handleKey(XKB_KEY_Escape);
    bar.statusArea().requestDetail("nope", "battery");
    bar.handleKey(XKB_KEY_a);
    EXPECT_TRUE(bar.popovers().active() == nullptr);
}

}  // namespace

// Bar-level tests build indicators through the registry, which is complete
// only once static initialization is over: main() runs these.
void runStatusAreaBarTests() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"RightClickAndMenuKeyOpenContextMenu", testRightClickAndMenuKeyOpenContextMenu},
        {"StatusAreaDetailRequestOpensAnchoredFlyout",
         testStatusAreaDetailRequestOpensAnchoredFlyout},
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
