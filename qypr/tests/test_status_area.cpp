// test_status_area.cpp - Tray claims, menu sources, context-menu routing.
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>

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
    area.pinned_ = {"telegramdesktop"};
    qypr::SystemBackends backends{};
    backends.sni = &sni;
    backends.statusArea = &area;
    qypr::BackgroundAppsIndicator host(backends);
    AreaCanvas canvas;

    // No wifi module: nm-applet is an ordinary background app, behind the
    // chevron; pinned Telegram has its own cell.
    area.setLayoutModules({"background-apps"});
    host.onBackendUpdate();
    EXPECT_NEAR(host.measureWidth(canvas.painter), 64.0, 0.01);  // Telegram + chevron

    area.setLayoutModules({"wifi", "background-apps"});  // wifi absorbs nm-applet
    host.onBackendUpdate();
    EXPECT_TRUE(host.visible);
    EXPECT_NEAR(host.measureWidth(canvas.painter), 36.0, 0.01);  // Telegram, no chevron
    EXPECT_EQ(host.tooltip(), std::string("Nothing running in the background"));

    sni.items_.pop_back();  // Telegram exits: only the claimed applet is left
    host.onBackendUpdate();
    EXPECT_FALSE(host.visible);
    EXPECT_NEAR(host.measureWidth(canvas.painter), 0.0, 0.01);
}

TEST(BackgroundAppsPinnedHiddenAndAttention) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::SNIBackend sni(session);
    sni.items_.push_back(trayItem(":1.22", "nm-applet", "Network"));
    sni.items_.push_back(trayItem(":1.80", "TelegramDesktop", "Telegram"));
    sni.items_.push_back(trayItem(":1.81", "steam", "Steam"));
    sni.items_.push_back(trayItem(":1.82", "discord", "Discord"));
    sni.items_.push_back(trayItem(":1.83", "keepassxc", "KeePassXC"));

    qypr::StatusArea area;
    area.setLayoutModules({"wifi", "background-apps"});
    area.pinned_ = {"telegram"};  // by title (keys are case-insensitive)
    area.hidden_ = {"steam"};     // by Id
    using BA = qypr::BackgroundAppsIndicator;
    EXPECT_TRUE(BA::stripItems(&sni, &area) == std::vector<size_t>{1});
    EXPECT_TRUE(BA::overflowItems(&sni, &area) == (std::vector<size_t>{3, 4}));

    // Asking for attention promotes an app for as long as it asks; a hidden
    // or claimed one stays where it is.
    sni.items_[4].status = "NeedsAttention";
    sni.items_[2].status = "NeedsAttention";
    sni.items_[0].status = "NeedsAttention";
    EXPECT_TRUE(BA::stripItems(&sni, &area) == (std::vector<size_t>{1, 4}));
    EXPECT_TRUE(BA::overflowItems(&sni, &area) == std::vector<size_t>{3});
    sni.items_[4].status = "Active";
    EXPECT_TRUE(BA::stripItems(&sni, &area) == std::vector<size_t>{1});

    // The strip never outgrows its limit; the rest overflow.
    area.pinned_.clear();
    sni.items_.clear();
    for (size_t i = 0; i < BA::kStripLimit + 2; ++i) {
        auto item = trayItem(":1." + std::to_string(100 + i), "app" + std::to_string(i), "App");
        item.status = "NeedsAttention";
        sni.items_.push_back(item);
    }
    EXPECT_EQ(BA::stripItems(&sni, &area).size(), BA::kStripLimit);
    EXPECT_EQ(BA::overflowItems(&sni, &area).size(), 2U);

    // No status area (lock screen or tests): every item is eligible.
    EXPECT_EQ(BA::overflowItems(&sni, nullptr).size(), 2U);
}

TEST(MergedModulesReachTheirAppletFromTheFlyout) {
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

    // No applet running: no "More from" row, no applet menu, middle click inert.
    EXPECT_EQ(wifi.tray_.link().text(), std::string());
    EXPECT_EQ(wifi.tray_.link().height(), 0.0);
    EXPECT_TRUE(wifi.createAppletMenu() == nullptr);
    EXPECT_FALSE(wifi.onMiddleClick(10, 10));

    auto applet = trayItem(":1.22", "nm-applet", "Network");
    applet.menuPath = "/org/ayatana/NotificationItem/nm_applet/Menu";
    sni.items_.push_back(applet);
    EXPECT_EQ(wifi.tray_.link().text(), std::string("More from nm-applet"));
    EXPECT_EQ(bluetooth.tray_.link().text(), std::string());  // blueman is not running
    EXPECT_TRUE(wifi.onMiddleClick(10, 10));                  // SecondaryActivate sent

    // The row asks the bar to swap the flyout for the applet's menu.
    wifi.tray_.link().open();
    const auto req = area.takeDetailRequest();
    EXPECT_TRUE(req.has_value() && req->appletMenu && req->moduleId == "wifi");
    // Without a dbusmenu there is nothing to draw, so the module asks the
    // applet to show its own menu (ContextMenu) and opens no popover.
    EXPECT_TRUE(wifi.createAppletMenu() == nullptr);

    // A module out of the layout claims nothing.
    area.setLayoutModules({"bluetooth"});
    EXPECT_EQ(wifi.tray_.link().text(), std::string());

    // The lock screen never receives the tray backend.
    qypr::SystemBackends locked = backends;
    locked.hasSession = false;
    locked.sync();
    qypr::WifiIndicator lockedWifi(locked);
    area.setLayoutModules({"wifi"});
    EXPECT_EQ(lockedWifi.tray_.link().text(), std::string());
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

void testAliasesBuildOneModule() {
    qypr::SystemBackends b;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.left = {"usb"};
    modules.right = {"usb-eject", "clock", "removable-devices"};
    const auto built = qypr::IndicatorRegistry::instance().createAll(b, &modules);
    EXPECT_EQ(built.size(), 2U);
    EXPECT_EQ(built[0]->id(), std::string("removable-devices"));
    EXPECT_TRUE(built[0]->zone() == qypr::Zone::Left);  // first listing wins
    EXPECT_EQ(built[1]->id(), std::string("clock"));
    modules.left = {"tray", "sni"};
    modules.right = {"background-apps"};
    const auto trays = qypr::IndicatorRegistry::instance().createAll(b, &modules);
    EXPECT_EQ(trays.size(), 1U);
    EXPECT_EQ(trays[0]->id(), std::string("background-apps"));
    modules.left = {"kde-connect", "phone-connect"};
    modules.right = {"phone-link"};
    const auto phones = qypr::IndicatorRegistry::instance().createAll(b, &modules);
    EXPECT_EQ(phones.size(), 1U);
    EXPECT_EQ(phones[0]->id(), std::string("phone-link"));
    // No module list (every registered id): still one instance per module.
    const auto everything = qypr::IndicatorRegistry::instance().createAll(b, nullptr);
    std::vector<std::string> ids;
    ids.reserve(everything.size());
    for (const auto& ind : everything) { ids.push_back(ind->id()); }
    std::ranges::sort(ids);
    EXPECT_TRUE(std::ranges::adjacent_find(ids) == ids.end());
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

void testOverflowListsPassiveModules() {
    qypr::EventLoop loop;
    AreaHost host;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::SNIBackend sni(session);
    qypr::SystemBackends backends;
    backends.sni = &sni;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"clock", "background-apps"};
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);

    // A module with nothing to say right now (think: the fstab drive) hides
    // from the strip and lists itself behind the chevron instead.
    struct Passive : qypr::StatusIndicator {
        explicit Passive(int& fired)
            : StatusIndicator("passive-target", qypr::Zone::Right, 1),
              fired_(&fired) {
            visible = false;
        }
        [[nodiscard]] std::string icon() const override { return "P"; }
        [[nodiscard]] std::string tooltip() const override { return "Passive"; }
        [[nodiscard]] bool hasDetailedView() const override { return true; }
        std::unique_ptr<qypr::DetailedPopover> createDetailedView() override {
            auto menu = std::make_unique<qypr::NativeMenu>();
            menu->add("Act", [f = fired_] { ++(*f); });
            return std::make_unique<qypr::MenuPopover>(std::move(menu), "Passive");
        }
        [[nodiscard]] qypr::OverflowEntry overflowEntry() const override {
            if (visible) { return {}; }
            return {.id = id(), .title = "Shared drive", .subtitle = "4.0 TB", .glyph = "P"};
        }

    private:
        int* fired_;
    };
    int fired = 0;
    bar.indicators().right().push_back(std::make_unique<Passive>(fired));
    auto* chevron = bar.indicators().findById("background-apps");
    EXPECT_TRUE(chevron != nullptr);
    EXPECT_FALSE(chevron->visible);  // built before the passive module existed

    bar.refreshFromBackends();  // the overflow host settles after the others
    EXPECT_TRUE(chevron->visible);
    EXPECT_EQ(chevron->tooltip(), std::string("1 more status"));
    sni.items_.push_back(trayItem(":1.80", "TelegramDesktop", "Telegram"));
    bar.refreshFromBackends();
    EXPECT_EQ(chevron->tooltip(), std::string("1 more status, 1 background app"));

    AreaCanvas canvas;
    bar.layout(1280, 800);
    bar.draw(canvas.painter, qypr::nowMs());
    const auto b = chevron->bounds;
    EXPECT_NEAR(b.w, 36.0, 0.01);
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, true, 0));
    bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, false, 0);
    EXPECT_EQ(bar.popovers().ownerId(), std::string("background-apps"));
    auto* overflow = bar.popovers().active();
    EXPECT_TRUE(overflow != nullptr);
    overflow->draw(canvas.painter, qypr::nowMs());
    if (const char* dir = std::getenv("QYPR_TEST_ARTIFACT_DIR")) {
        cairo_save(canvas.cr);
        cairo_set_operator(canvas.cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(canvas.cr);
        cairo_restore(canvas.cr);
        bar.setBackdrop(true, 0.96);
        bar.draw(canvas.painter, qypr::nowMs() + 10000);
        std::filesystem::create_directories(dir);
        const auto file = std::filesystem::path(dir) / "background-apps-overflow.png";
        EXPECT_EQ(cairo_surface_write_to_png(canvas.surface, file.c_str()), CAIRO_STATUS_SUCCESS);
    }

    // The STATUS row opens the module's own flyout, anchored to the chevron.
    const auto pb = overflow->getBounds();
    EXPECT_TRUE(bar.handlePointerButton(pb.x + pb.w / 2, pb.y + 8 + 24 + 21, 0x110, true, 0));
    auto* flyout = bar.popovers().active();
    EXPECT_TRUE(flyout != nullptr);
    EXPECT_TRUE(flyout != overflow);
    EXPECT_EQ(bar.popovers().ownerId(), std::string("background-apps"));
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));
    EXPECT_TRUE(bar.popovers().active() == nullptr);

    // Keyboard: open the overflow, Down to the row, Enter.
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x110, true, 0));
    bar.popovers().active()->draw(canvas.painter, qypr::nowMs());
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Down));
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Return));
    EXPECT_TRUE(bar.popovers().active() != nullptr);
    EXPECT_EQ(bar.popovers().ownerId(), std::string("background-apps"));
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));

    // Once the module has something to show it leaves the overflow, and with
    // nothing else there the chevron goes too.
    bar.indicators().findById("passive-target")->visible = true;
    sni.items_.clear();
    bar.refreshFromBackends();
    EXPECT_FALSE(chevron->visible);
    EXPECT_EQ(fired, 0);
}

}  // namespace

// Bar-level tests build indicators through the registry, which is complete
// only once static initialization is over: main() runs these.
namespace {

// A bus dropped before any backend sees it. sd_bus_open_system() is not
// mocked, so the Wi-Fi backend must never reach the real NetworkManager; and
// a dbusmenu fetch builds its message with calls the session-bus mock does
// not cover. Offline, both fail fast, as with the services absent.
struct OfflineBus : qypr::SystemBus {
    OfflineBus(qypr::EventLoop& loop, qypr::BusKind kind) : SystemBus(loop, kind) { teardown(); }
};

void testMergedFlyoutSwapsToAppletMenuAndBack() {
    qypr::EventLoop loop;
    AreaHost host;
    OfflineBus systemBus(loop, qypr::BusKind::System);
    OfflineBus menuBus(loop, qypr::BusKind::Session);
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::WifiBackend wifiBackend(loop, systemBus);
    wifiBackend.seed({.available = true, .enabled = true});
    qypr::SNIBackend sni(session);
    auto applet = trayItem(":1.22", "nm-applet", "Network");
    applet.menuPath = "/org/ayatana/NotificationItem/nm_applet/Menu";
    sni.items_.push_back(applet);
    // Its fetch fails offline: the applet section is empty, the back row is
    // still there.
    qypr::DbusMenuBackend menus(menuBus);
    qypr::SystemBackends backends;
    backends.sessionSurface = true;
    backends.wifi = &wifiBackend;
    backends.sni = &sni;
    backends.dbusMenu = &menus;
    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"wifi"};
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);
    bar.refreshFromBackends();
    AreaCanvas canvas;
    bar.layout(1280, 800);
    bar.draw(canvas.painter, qypr::nowMs());
    auto* wifi = bar.indicators().findById("wifi");
    EXPECT_TRUE(wifi != nullptr && wifi->visible);
    if (wifi == nullptr) return;
    const auto b = wifi->bounds;
    auto isMenu = [&] {
        return dynamic_cast<qypr::MenuPopover*>(bar.popovers().active()) != nullptr;
    };

    // Right click opens the flyout, not a menu.
    EXPECT_TRUE(bar.handlePointerButton(b.x + b.w / 2, b.y + b.h / 2, 0x111, true, 0));
    EXPECT_EQ(bar.popovers().ownerId(), std::string("wifi"));
    EXPECT_TRUE(bar.popovers().active() != nullptr && !isMenu());

    // Its last row swaps in nm-applet's menu, anchored to the module.
    auto* flyout = bar.popovers().active();
    flyout->draw(canvas.painter, qypr::nowMs() + 1000);
    const auto fb = flyout->getBounds();
    if (const char* dir = std::getenv("QYPR_TEST_ARTIFACT_DIR")) {
        std::filesystem::create_directories(dir);
        bar.draw(canvas.painter, qypr::nowMs() + 1000);
        const auto path = std::filesystem::path(dir) / "wifi-merged-flyout.png";
        EXPECT_EQ(cairo_surface_write_to_png(canvas.surface, path.c_str()), CAIRO_STATUS_SUCCESS);
    }
    EXPECT_TRUE(bar.handlePointerButton(fb.x + fb.w / 2, fb.y + fb.h - 12 - 17, 0x110, true, 0));
    EXPECT_TRUE(isMenu());
    EXPECT_EQ(bar.popovers().ownerId(), std::string("wifi"));
    auto* menu = dynamic_cast<qypr::MenuPopover*>(bar.popovers().active());
    if (menu == nullptr) return;
    EXPECT_EQ(menu->stack_.front().items.front().label, std::string("\u2039 Wi-Fi"));
    if (const char* dir = std::getenv("QYPR_TEST_ARTIFACT_DIR")) {
        std::filesystem::create_directories(dir);
        cairo_save(canvas.cr);
        cairo_set_operator(canvas.cr, CAIRO_OPERATOR_CLEAR);
        cairo_paint(canvas.cr);
        cairo_restore(canvas.cr);
        bar.draw(canvas.painter, qypr::nowMs() + 1000);
        const auto path = std::filesystem::path(dir) / "wifi-applet-menu.png";
        EXPECT_EQ(cairo_surface_write_to_png(canvas.surface, path.c_str()), CAIRO_STATUS_SUCCESS);
    }

    // Its first row goes back to the flyout.
    menu->draw(canvas.painter, qypr::nowMs() + 1000);
    const auto mb = menu->getBounds();
    EXPECT_TRUE(bar.handlePointerButton(mb.x + mb.w / 2, mb.y + 6 + 15, 0x110, true, 0));
    EXPECT_TRUE(bar.popovers().active() != nullptr && !isMenu());
    EXPECT_EQ(bar.popovers().ownerId(), std::string("wifi"));
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Escape));

    // The Menu key opens the same flyout.
    bar.popovers().clear();
    wifi->focused = true;
    EXPECT_TRUE(bar.handleKey(XKB_KEY_Menu));
    EXPECT_TRUE(bar.popovers().active() != nullptr && !isMenu());
}

}  // namespace

void runStatusAreaBarTests() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"RightClickAndMenuKeyOpenContextMenu", testRightClickAndMenuKeyOpenContextMenu},
        {"StatusAreaDetailRequestOpensAnchoredFlyout",
         testStatusAreaDetailRequestOpensAnchoredFlyout},
        {"AliasesBuildOneModule", testAliasesBuildOneModule},
        {"OverflowListsPassiveModules", testOverflowListsPassiveModules},
        {"MergedFlyoutSwapsToAppletMenuAndBack", testMergedFlyoutSwapsToAppletMenuAndBack},
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
