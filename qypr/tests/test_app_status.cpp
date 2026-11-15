// test_app_status.cpp - Desktop actions, LauncherEntry state, the app model,
// and the modules built on it (notification-apps, whatsapp, email).
//
// Nothing here launches or activates anything: no test sets
// SystemBackends::loop or ::applications, so open() and menu actions have no
// way to spawn, and ApplicationsBackend::start() (which would ask for a bus
// name) is never called.
#include "test_framework.hpp"

#include <cstdlib>
#include <filesystem>

#include "system/ApplicationsBackend.hpp"
#include "system/DesktopIndex.hpp"
#include "system/ToplevelBackend.hpp"
#include "ui/indicators/AdditionalIndicators.hpp"
#include "ui/indicators/AppStatus.hpp"
#include "ui/indicators/NotificationAppsIndicator.hpp"
#include "ui/statusbar/MenuPopover.hpp"
#include "ui/statusbar/StatusArea.hpp"

namespace {

constexpr const char* kFirefox = "[Desktop Entry]\n"
                                 "Type=Application\n"
                                 "Name=Firefox\n"
                                 "Exec=firefox %u\n"
                                 "Icon=firefox\n"
                                 "StartupWMClass=firefox\n"
                                 "DBusActivatable=false\n"
                                 "Actions=new-window;new-private-window;missing;\n"
                                 "\n"
                                 "[Desktop Action new-window]\n"
                                 "Name=New Window\n"
                                 "Name[de]=Neues Fenster\n"
                                 "Exec=firefox --new-window %u\n"
                                 "\n"
                                 "[Desktop Action new-private-window]\n"
                                 "Name=New Private Window\n"
                                 "Exec=firefox --private-window %u\n"
                                 "\n"
                                 "[Desktop Action unlisted]\n"
                                 "Name=Not in Actions=\n"
                                 "Exec=firefox --unlisted\n";

constexpr const char* kWhatsAppWeb = "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=WhatsApp Web\n"
                                     "Exec=/usr/bin/chromium --profile-directory=Default "
                                     "--app-id=hnpfjngllnobngcgfapefoaidbinmjnm\n"
                                     "Icon=chrome-hnpfjngllnobngcgfapefoaidbinmjnm-Default\n"
                                     "StartupWMClass=crx_hnpfjngllnobngcgfapefoaidbinmjnm\n";

constexpr const char* kTelegram = "[Desktop Entry]\n"
                                  "Type=Application\n"
                                  "Name=Telegram\n"
                                  "Exec=telegram-desktop -- %u\n"
                                  "Icon=org.telegram.desktop\n"
                                  "StartupWMClass=TelegramDesktop\n"
                                  "DBusActivatable=true\n"
                                  "Actions=quit;\n"
                                  "[Desktop Action quit]\n"
                                  "Name=Quit Telegram\n";

constexpr const char* kThunderbird = "[Desktop Entry]\n"
                                     "Type=Application\n"
                                     "Name=Thunderbird\n"
                                     "Exec=thunderbird %u\n"
                                     "Icon=thunderbird\n"
                                     "Categories=Network;Email;\n";

// An index built from entry bodies, without touching the filesystem.
qypr::DesktopIndex indexOf(const std::vector<std::pair<std::string, const char*>>& files) {
    qypr::DesktopIndex idx;
    for (const auto& [id, body] : files) {
        qypr::DesktopEntry e;
        if (qypr::DesktopIndex::parseEntry(body, e)) {
            e.id = id;
            idx.entries_.push_back(std::move(e));
        }
    }
    idx.loaded_ = true;
    return idx;
}

qypr::Notification note(const std::string& app, const std::string& desktopEntry,
                        const std::string& title, uint32_t daemonId, int64_t postedAt) {
    qypr::Notification n;
    n.app = app;
    n.desktopEntry = desktopEntry;
    n.title = title;
    n.daemonId = daemonId;
    n.postedAt = postedAt;
    return n;
}

qypr::ToplevelWindow window(uint64_t id, const std::string& appId, bool active = false) {
    qypr::ToplevelWindow w;
    w.id = id;
    w.appId = appId;
    w.active = active;
    return w;
}

const qypr::AppStatus* findApp(const std::vector<qypr::AppStatus>& apps, const std::string& key) {
    for (const auto& a : apps) {
        if (a.key == key) return &a;
    }
    return nullptr;
}

std::vector<std::string> labels(const qypr::MenuPopover& pop) {
    std::vector<std::string> out;
    for (const auto& n : pop.stack_.front().items) {
        out.push_back(n.separator ? std::string("---") : n.label);
    }
    return out;
}

}  // namespace

TEST(DesktopEntryParsesActionsAndWindowClass) {
    qypr::DesktopEntry e;
    EXPECT_TRUE(qypr::DesktopIndex::parseEntry(kFirefox, e));
    EXPECT_EQ(e.exec, std::string("firefox"));  // action groups don't leak in
    EXPECT_EQ(e.startupWMClass, std::string("firefox"));
    EXPECT_FALSE(e.dbusActivatable);
    // Listed order; a listed id without a group and an unlisted group are
    // both ignored.
    EXPECT_EQ(e.actions.size(), 2U);
    EXPECT_EQ(e.actions[0].id, std::string("new-window"));
    EXPECT_EQ(e.actions[0].name, std::string("New Window"));  // unlocalized
    EXPECT_EQ(e.actions[0].exec, std::string("firefox --new-window"));
    EXPECT_EQ(e.actions[1].exec, std::string("firefox --private-window"));

    // An action without Exec only works through D-Bus activation.
    qypr::DesktopEntry tg;
    EXPECT_TRUE(qypr::DesktopIndex::parseEntry(kTelegram, tg));
    EXPECT_TRUE(tg.dbusActivatable);
    EXPECT_EQ(tg.actions.size(), 1U);
    EXPECT_EQ(tg.actions[0].exec, std::string());
    std::string notDbus = kTelegram;
    notDbus.replace(notDbus.find("DBusActivatable=true"), 20, "DBusActivatable=no");
    EXPECT_TRUE(qypr::DesktopIndex::parseEntry(notDbus, tg));
    EXPECT_TRUE(tg.actions.empty());
}

TEST(DesktopIndexResolvesWindowsByStartupWMClass) {
    const auto idx = indexOf({{"chrome-hnpfjngllnobngcgfapefoaidbinmjnm-Default", kWhatsAppWeb},
                              {"firefox", kFirefox},
                              {"org.telegram.desktop", kTelegram}});
    const auto* pwa = idx.resolveWindow("crx_hnpfjngllnobngcgfapefoaidbinmjnm");
    EXPECT_TRUE(pwa != nullptr);
    EXPECT_EQ(pwa->name, std::string("WhatsApp Web"));
    EXPECT_EQ(idx.resolveWindow("TelegramDesktop")->id, std::string("org.telegram.desktop"));
    EXPECT_EQ(idx.resolveWindow("firefox")->id, std::string("firefox"));
    EXPECT_EQ(idx.byId("ORG.TELEGRAM.DESKTOP")->name, std::string("Telegram"));
    EXPECT_TRUE(idx.byId("telegram") == nullptr);  // byId is exact
    EXPECT_TRUE(idx.resolveWindow("") == nullptr);
}

TEST(LauncherEntryUrisAndActivationPaths) {
    using qypr::ApplicationsBackend;
    EXPECT_EQ(ApplicationsBackend::desktopIdFromUri("application://org.telegram.desktop.desktop"),
              std::string("org.telegram.desktop"));
    EXPECT_EQ(ApplicationsBackend::desktopIdFromUri("application://firefox.desktop"),
              std::string("firefox"));
    EXPECT_EQ(ApplicationsBackend::desktopIdFromUri("file:///x.desktop"), std::string());
    EXPECT_EQ(ApplicationsBackend::objectPathFor("org.gnome.Maps"), std::string("/org/gnome/Maps"));
    EXPECT_EQ(ApplicationsBackend::objectPathFor("org.gnome.Nautilus-dev"),
              std::string("/org/gnome/Nautilus_dev"));
}

TEST(LauncherEntryUpdatesMergeAndExpire) {
    using qypr::ApplicationsBackend;
    using qypr::LauncherUpdate;
    std::vector<qypr::LauncherEntry> entries;
    ApplicationsBackend::applyUpdate(entries, ":1.40", "org.telegram.desktop",
                                     LauncherUpdate{.count = 12, .countVisible = true});
    EXPECT_EQ(entries.size(), 1U);
    EXPECT_EQ(entries[0].badge(), 12);
    EXPECT_TRUE(entries[0].shownProgress() < 0);

    // A later update carries only what changed.
    ApplicationsBackend::applyUpdate(entries, ":1.40", "org.telegram.desktop",
                                     LauncherUpdate{.progress = 1.5, .progressVisible = true});
    EXPECT_EQ(entries[0].badge(), 12);
    EXPECT_EQ(entries[0].shownProgress(), 1.0);  // clamped

    // A hidden count is no badge.
    ApplicationsBackend::applyUpdate(entries, ":1.40", "org.telegram.desktop",
                                     LauncherUpdate{.countVisible = false});
    EXPECT_EQ(entries[0].badge(), 0);
    // Nothing left to show: the entry goes.
    ApplicationsBackend::applyUpdate(entries, ":1.40", "org.telegram.desktop",
                                     LauncherUpdate{.progressVisible = false});
    EXPECT_TRUE(entries.empty());

    // A quicklist alone keeps an entry; "/" means none.
    ApplicationsBackend::applyUpdate(entries, ":1.41", "firefox",
                                     LauncherUpdate{.quicklist = std::string("/menu")});
    ApplicationsBackend::applyUpdate(entries, ":1.42", "thunderbird",
                                     LauncherUpdate{.count = 3, .countVisible = true});
    EXPECT_EQ(entries.size(), 2U);
    ApplicationsBackend::applyUpdate(entries, ":1.41", "firefox",
                                     LauncherUpdate{.quicklist = std::string("/")});
    EXPECT_EQ(entries.size(), 1U);
    ApplicationsBackend::applyUpdate(entries, ":1.42", "", LauncherUpdate{.urgent = true});
    EXPECT_EQ(entries.size(), 1U);  // no desktop id: ignored

    // The sender leaves the bus.
    EXPECT_FALSE(ApplicationsBackend::dropSender(entries, ":1.99"));
    EXPECT_TRUE(ApplicationsBackend::dropSender(entries, ":1.42"));
    EXPECT_TRUE(entries.empty());
}

TEST(AppStatusMergesNotificationsLauncherAndWindows) {
    const auto idx = indexOf({{"org.telegram.desktop", kTelegram},
                              {"firefox", kFirefox},
                              {"chrome-hnpfjngllnobngcgfapefoaidbinmjnm-Default", kWhatsAppWeb}});
    const std::vector<qypr::Notification> notes{
        note("Telegram Desktop", "org.telegram.desktop", "Alice", 11, 1000),
        note("Telegram Desktop", "", "Bob", 12, 3000),  // no hint: the app name resolves
        note("notify-send", "", "Build finished", 13, 2000)};
    std::vector<qypr::LauncherEntry> launcher{
        {.desktopId = "org.telegram.desktop",
         .sender = ":1.40",
         .count = 12,
         .countVisible = true,
         .quicklist = "/quicklist"},
        {.desktopId = "firefox", .sender = ":1.50", .progress = 0.25, .progressVisible = true}};
    const std::vector<qypr::ToplevelWindow> windows{
        window(1, "TelegramDesktop"), window(2, "crx_hnpfjngllnobngcgfapefoaidbinmjnm"),
        window(3, "kitty")};

    const auto apps = qypr::buildAppStatus(
        {.notifications = &notes, .launcher = &launcher, .windows = &windows, .index = &idx});
    // News first (newest notification first), then window-only apps.
    EXPECT_EQ(apps.size(), 5U);
    EXPECT_EQ(apps[0].key, std::string("org.telegram.desktop"));
    EXPECT_EQ(apps[1].key, std::string("notify-send"));
    EXPECT_EQ(apps[2].key, std::string("firefox"));
    EXPECT_FALSE(apps[3].hasNews());

    const auto& tg = apps[0];
    EXPECT_EQ(tg.name, std::string("Telegram"));
    EXPECT_EQ(tg.icon, std::string("org.telegram.desktop"));
    EXPECT_EQ(tg.notifications, 2);
    EXPECT_EQ(tg.badge, 12);
    EXPECT_EQ(tg.count(), 12);  // the app's own count wins
    EXPECT_EQ(tg.latestTitle, std::string("Bob"));
    EXPECT_EQ(tg.daemonIds.size(), 2U);
    EXPECT_EQ(tg.windows.size(), 1U);
    EXPECT_EQ(tg.menuService, std::string(":1.40"));
    EXPECT_EQ(tg.menuPath, std::string("/quicklist"));
    EXPECT_EQ(apps[2].progress, 0.25);

    const auto* pwa = findApp(apps, "chrome-hnpfjngllnobngcgfapefoaidbinmjnm-default");
    EXPECT_TRUE(pwa != nullptr);
    EXPECT_EQ(pwa->name, std::string("WhatsApp Web"));
    EXPECT_TRUE(pwa->matches("*whatsapp*"));
    const auto* kitty = findApp(apps, "kitty");
    EXPECT_TRUE(kitty != nullptr && kitty->desktopId.empty());

    // Without an index the channels still merge on identical names.
    const std::vector<qypr::Notification> raw{note("Mail", "thunderbird", "Hi", 21, 10)};
    const std::vector<qypr::ToplevelWindow> rawWindows{window(4, "thunderbird")};
    const auto plain =
        qypr::buildAppStatus({.notifications = &raw, .windows = &rawWindows, .index = nullptr});
    EXPECT_EQ(plain.size(), 1U);
    EXPECT_EQ(plain[0].name, std::string("Mail"));
    EXPECT_EQ(plain[0].windows.size(), 1U);
}

TEST(AppClaimPatterns) {
    using qypr::StatusArea;
    EXPECT_TRUE(StatusArea::patternMatches("*whatsapp*", "com.github.eneshecan.WhatsAppForLinux"));
    EXPECT_TRUE(StatusArea::patternMatches("*.desktop", "org.telegram.desktop"));
    EXPECT_FALSE(StatusArea::patternMatches("*.desktop", "telegram"));
    EXPECT_TRUE(StatusArea::patternMatches("blueman-*", "blueman-applet"));
    EXPECT_FALSE(StatusArea::patternMatches("blueman", "blueman-applet"));

    qypr::AppStatus mail;
    mail.key = "org.gnome.evolution";
    mail.name = "Evolution";
    mail.categories = "GNOME;GTK;Office;Email;Calendar;";
    EXPECT_TRUE(mail.matches("category:email"));
    EXPECT_TRUE(mail.matches("category:Email"));
    EXPECT_FALSE(mail.matches("category:mail"));
    EXPECT_TRUE(mail.matchesAny(StatusArea::defaultClaims("email")));
    EXPECT_FALSE(mail.matchesAny(StatusArea::defaultClaims("whatsapp")));

    // The claims only hold while the module is in the layout.
    StatusArea area;
    EXPECT_TRUE(area.claimPatterns("email").empty());
    area.setLayoutModules({"email"});
    EXPECT_FALSE(area.claimPatterns("email").empty());
}

TEST(AppModuleShowsRunningApp) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor mon(loop);
    qypr::ToplevelBackend toplevel;
    auto idx = indexOf({{"chrome-hnpfjngllnobngcgfapefoaidbinmjnm-Default", kWhatsAppWeb},
                        {"thunderbird", kThunderbird}});
    qypr::SystemBackends b{};
    b.notifications = &mon;
    b.toplevel = &toplevel;
    b.desktopIndex = &idx;

    qypr::WhatsappIndicator whatsapp(b);
    qypr::EmailIndicator email(b);
    EXPECT_TRUE(whatsapp.sensitive());
    whatsapp.onBackendUpdate();
    EXPECT_FALSE(whatsapp.visible);
    EXPECT_EQ(whatsapp.tooltip(), std::string("WhatsApp is not running"));

    // The web app's window is open: the module shows it, with no news.
    toplevel.snap_.windows = {window(7, "crx_hnpfjngllnobngcgfapefoaidbinmjnm", true),
                              window(8, "thunderbird")};
    whatsapp.onBackendUpdate();
    email.onBackendUpdate();
    EXPECT_TRUE(whatsapp.visible);
    EXPECT_EQ(whatsapp.apps().size(), 1U);
    EXPECT_EQ(whatsapp.tooltip(), std::string("WhatsApp Web (running)"));
    EXPECT_EQ(email.apps().size(), 1U);  // by its Email category
    EXPECT_EQ(email.apps()[0].name, std::string("Thunderbird"));

    // A message arrives.
    mon.testNotes().push_back(note("WhatsApp Web", "", "Mum", 31, 100));
    whatsapp.onBackendUpdate();
    EXPECT_EQ(whatsapp.apps()[0].notifications, 1);
    EXPECT_TRUE(whatsapp.tooltip().find("1 notification") != std::string::npos);
    EXPECT_TRUE(whatsapp.tooltip().find("Mum") != std::string::npos);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 40);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    whatsapp.bounds = {.x = 0, .y = 0, .w = whatsapp.measureWidth(p), .h = 36};
    EXPECT_EQ(whatsapp.bounds.w, 36.0);
    whatsapp.draw(p, 0);
    EXPECT_TRUE(whatsapp.onClick(10, 18));  // focuses window 7's neighbour: no-op here
    EXPECT_FALSE(whatsapp.onClick(100, 18));
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

TEST(AppMenuListsActionsAndClear) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::NotificationMonitor mon(loop);
    qypr::NotificationActions actions(session);
    auto idx = indexOf({{"firefox", kFirefox}});
    qypr::SystemBackends b{};
    b.notifications = &mon;
    b.notificationActions = &actions;
    b.desktopIndex = &idx;
    mon.testNotes().push_back(note("Firefox", "firefox", "Download complete", 41, 10));
    mon.testNotes().push_back(note("Firefox", "firefox", "Reminder", 42, 20));

    qypr::NotificationAppsIndicator ind(b);
    ind.onBackendUpdate();
    EXPECT_EQ(ind.apps().size(), 1U);
    ind.bounds = {.x = 0, .y = 0, .w = 36, .h = 36};
    EXPECT_FALSE(ind.onSecondaryClick(10, 18));  // routed to the context menu
    auto menu = ind.createContextMenu(10, 18);
    EXPECT_TRUE(menu != nullptr);
    const auto* pop = dynamic_cast<qypr::MenuPopover*>(menu.get());
    EXPECT_TRUE(pop != nullptr);
    const std::vector<std::string> want{"New Window", "New Private Window", "---",
                                        "Clear 2 notifications"};
    EXPECT_TRUE(labels(*pop) == want);
    // The keyboard's Menu key aims at the module's centre: a one-app module
    // still opens that app's menu.
    EXPECT_TRUE(ind.createContextMenu(-50, 18) != nullptr);
}

TEST(AppButtonsDrawBadgesProgressAndUrgency) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::ApplicationsBackend apps(loop, session);  // never started
    apps.entries_ = {{.desktopId = "org.telegram.desktop", .count = 12, .countVisible = true},
                     {.desktopId = "firefox", .progress = 0.6, .progressVisible = true},
                     {.desktopId = "thunderbird", .urgent = true}};
    qypr::NotificationMonitor mon(loop);
    mon.testNotes().push_back(note("Telegram Desktop", "org.telegram.desktop", "Bob", 1, 10));
    auto critical = note("Backup", "", "Disk full", 2, 20);
    critical.urgency = 2;
    mon.testNotes().push_back(critical);
    auto idx = indexOf({{"org.telegram.desktop", kTelegram},
                        {"firefox", kFirefox},
                        {"thunderbird", kThunderbird}});
    qypr::SystemBackends b{};
    b.notifications = &mon;
    b.applications = &apps;
    b.desktopIndex = &idx;

    qypr::NotificationAppsIndicator ind(b);
    ind.onBackendUpdate();
    EXPECT_EQ(ind.apps().size(), 4U);
    EXPECT_EQ(ind.apps()[0].name, std::string("Backup"));  // newest notification
    EXPECT_TRUE(ind.tooltip().find("4 apps:") == 0);
    EXPECT_TRUE(ind.tooltip().find("Telegram (12)") != std::string::npos);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 160, 36);
    cairo_t* cr = cairo_create(surf);
    cairo_set_source_rgb(cr, 0.12, 0.12, 0.16);
    cairo_paint(cr);
    qypr::Painter p(cr);
    ind.bounds = {.x = 0, .y = 0, .w = ind.measureWidth(p), .h = 36};
    EXPECT_EQ(ind.bounds.w, (4 * 28.0) + (3 * 4.0) + 8.0);
    ind.draw(p, 0);
    ind.onHoverMotion(40, 18);  // Telegram
    EXPECT_TRUE(ind.tooltip().find("12 unread, 1 notification") != std::string::npos);
    ind.onHoverMotion(70, 18);  // Firefox
    EXPECT_EQ(ind.tooltip(), std::string("Firefox (60%)"));
    ind.onHoverMotion(100, 18);  // Thunderbird
    EXPECT_EQ(ind.tooltip(), std::string("Thunderbird (needs attention)"));
    if (const char* dir = std::getenv("QYPR_TEST_ARTIFACT_DIR")) {
        std::filesystem::create_directories(dir);
        const auto path = std::filesystem::path(dir) / "app-status.png";
        EXPECT_EQ(cairo_surface_write_to_png(surf, path.c_str()), CAIRO_STATUS_SUCCESS);
    }
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// ── Bar level (main-run: needs the complete registry) ───────────────────────

namespace {

qypr::AppStatusIndicator* appModule(qypr::StatusBar& bar, const std::string& id) {
    for (auto& ind : bar.indicators().right()) {
        if (ind->id() == id) return dynamic_cast<qypr::AppStatusIndicator*>(ind.get());
    }
    return nullptr;
}

struct AppHost : qypr::Invalidator {
    void invalidate() override {}
};

void testNotificationAppsLeavesClaimedAppsToTheirModule() {
    qypr::EventLoop loop;
    AppHost host;
    qypr::NotificationMonitor mon(loop);
    mon.testNotes().push_back(note("WhatsApp", "whatsapp-linux-desktop", "Mum", 1, 10));
    mon.testNotes().push_back(note("Thunderbird", "thunderbird", "Invoice", 2, 20));
    mon.testNotes().push_back(note("Telegram Desktop", "org.telegram.desktop", "Bob", 3, 30));
    qypr::SystemBackends backends;
    backends.sessionSurface = true;
    backends.notifications = &mon;

    qypr::IndicatorRegistry::ModuleSelection modules;
    modules.right = {"notification-apps", "whatsapp", "mail"};
    qypr::StatusBar bar(loop, host, backends, &modules);
    bar.setSessionContentVisible(true);
    bar.refreshFromBackends();
    auto* all = appModule(bar, "notification-apps");
    auto* whatsapp = appModule(bar, "whatsapp");
    auto* email = appModule(bar, "email");  // `mail` builds the email module
    EXPECT_TRUE(all != nullptr && whatsapp != nullptr && email != nullptr);
    if (all == nullptr || whatsapp == nullptr || email == nullptr) return;
    EXPECT_EQ(all->apps().size(), 1U);
    EXPECT_EQ(all->apps()[0].key, std::string("org.telegram.desktop"));
    EXPECT_EQ(whatsapp->apps().size(), 1U);
    EXPECT_EQ(email->apps().size(), 1U);

    // Without the app modules, notification-apps shows every app.
    modules.right = {"notification-apps"};
    qypr::StatusBar plain(loop, host, backends, &modules);
    plain.setSessionContentVisible(true);
    plain.refreshFromBackends();
    auto* only = appModule(plain, "notification-apps");
    EXPECT_TRUE(only != nullptr && only->apps().size() == 3U);

    // A notification change reaches the modules through the bar's refresh.
    mon.testNotes().clear();
    bar.refreshFromBackends();
    EXPECT_FALSE(all->visible);
    EXPECT_FALSE(whatsapp->visible);
}

}  // namespace

void runAppStatusBarTests() {
    const std::vector<std::pair<std::string, void (*)()>> tests{
        {"NotificationAppsLeavesClaimedAppsToTheirModule",
         testNotificationAppsLeavesClaimedAppsToTheirModule},
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
