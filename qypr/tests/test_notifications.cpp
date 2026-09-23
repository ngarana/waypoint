// test_notifications.cpp - Notification monitor/log.
#include "test_framework.hpp"

#include <filesystem>
#include <fstream>

#include "notifications/NotificationPolicy.hpp"
#include "notifications/NotificationStore.hpp"
#include "notifications/NotificationTransport.hpp"

// g_mock_sdbus_fail is defined in mocks.cpp.
// NOLINTNEXTLINE(readability-identifier-naming) // name dictated by mocks.cpp
extern bool g_mock_sdbus_fail;

namespace {

bool sameColor(const qypr::Color& a, const qypr::Color& b) {
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

qypr::NotifyEvent makeEvent(const std::string& app, const std::string& title,
                            const std::string& body = "body") {
    qypr::NotifyEvent e;
    e.app = app;
    e.summary = title;
    e.body = body;
    e.hints.urgency = 1;
    e.haveCookie = true;
    e.cookie = 100;
    e.sender = "sender";
    return e;
}

// The guard restores the flag so one failing test cannot pollute the bus
// behaviour of the rest of the suite.
struct MockSdbusFailGuard {
    MockSdbusFailGuard() { g_mock_sdbus_fail = true; }
    ~MockSdbusFailGuard() { g_mock_sdbus_fail = false; }
    MockSdbusFailGuard(const MockSdbusFailGuard&) = delete;
    MockSdbusFailGuard& operator=(const MockSdbusFailGuard&) = delete;
    MockSdbusFailGuard(MockSdbusFailGuard&&) = delete;
    MockSdbusFailGuard& operator=(MockSdbusFailGuard&&) = delete;
};

}  // namespace

TEST(NotificationMonitor) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor mon(loop);

    EXPECT_TRUE(mon.start(false));
    EXPECT_TRUE(mon.notifications().empty());

    // Direct invocation to get coverage on teardown and other internal calls
    mon.teardown();
}
TEST(NotificationLog) {
    qypr::EventLoop loop;
    qypr::NotificationMonitor const mon(loop);
    qypr::NotificationLog log(loop, mon);

    EXPECT_TRUE(log.start());
    log.drain();
}

TEST(NotificationStoreReplacementKeepsIdentity) {
    qypr::NotificationStore store;
    qypr::theme::State theme;
    uint64_t first = store.add(makeEvent("Mail", "Hello"), false, theme);
    EXPECT_TRUE(first != 0);

    qypr::NotifyEvent update = makeEvent("Mail", "Hello again");
    update.replacesId = 1;  // daemon id of the first card
    // Correlate the daemon id first, as the bus reply would.
    qypr::ReturnEvent ret;
    ret.replyCookie = 100;
    ret.destination = "sender";
    ret.daemonId = 1;
    EXPECT_TRUE(store.attachDaemonId(ret));
    uint64_t second = store.add(update, false, theme);
    EXPECT_EQ(first, second);
    EXPECT_EQ(static_cast<int>(store.notes().size()), 1);
    EXPECT_EQ(store.notes().at(0).title, std::string("Hello again"));
}

TEST(NotificationStoreSkipsTransientAndEmpty) {
    qypr::NotificationStore store;
    qypr::theme::State theme;
    qypr::NotifyEvent transient = makeEvent("Volume", "OSD");
    transient.hints.transient = true;
    EXPECT_EQ(store.add(transient, false, theme), 0U);
    EXPECT_TRUE(store.notes().empty());

    EXPECT_EQ(store.add(makeEvent("App", "", ""), false, theme), 0U);
    EXPECT_TRUE(store.notes().empty());

    // A card with actions and a body still queues.
    qypr::NotifyEvent actionOnly = makeEvent("App", "", "");
    actionOnly.actions.emplace_back("default", "Open");
    actionOnly.body = "x";
    EXPECT_TRUE(store.add(actionOnly, false, theme) != 0);
}

TEST(NotificationStoreCapacityDropsOldest) {
    qypr::NotificationStore store;
    qypr::theme::State theme;
    for (int i = 0; i < 9; ++i) {
        qypr::NotifyEvent e = makeEvent("App", "t" + std::to_string(i), "b");
        e.cookie = 100 + static_cast<uint64_t>(i);
        EXPECT_TRUE(store.add(e, false, theme) != 0);
    }
    EXPECT_EQ(static_cast<int>(store.notes().size()), 8);
    EXPECT_EQ(store.notes().front().title, std::string("t1"));
    EXPECT_EQ(store.notes().back().title, std::string("t8"));
}

TEST(NotificationStoreCloseReasons) {
    qypr::NotificationStore store;
    qypr::theme::State theme;
    store.add(makeEvent("A", "one"), false, theme);
    store.add(makeEvent("B", "two"), false, theme);
    // Correlate daemon ids 1 and 2.
    for (uint32_t id = 1; id <= 2; ++id) {
        qypr::ReturnEvent ret;
        ret.replyCookie = 100;
        ret.destination = "sender";
        ret.daemonId = id;
        store.attachDaemonId(ret);
    }
    EXPECT_FALSE(store.close(1, 1));  // expired popups stay queued
    EXPECT_EQ(static_cast<int>(store.notes().size()), 2);
    EXPECT_TRUE(store.close(1, 2));
    EXPECT_FALSE(store.close(9, 3));  // unknown id: no-op
    EXPECT_TRUE(store.close(2, 3));
    EXPECT_TRUE(store.notes().empty());
}

TEST(NotificationStorePendingCorrelation) {
    qypr::NotificationStore store;
    qypr::theme::State theme;
    store.add(makeEvent("A", "one"), false, theme);

    qypr::ReturnEvent wrongCookie;
    wrongCookie.replyCookie = 999;
    wrongCookie.destination = "sender";
    wrongCookie.daemonId = 7;
    EXPECT_FALSE(store.attachDaemonId(wrongCookie));

    qypr::ReturnEvent wrongSender;
    wrongSender.replyCookie = 100;
    wrongSender.destination = "other";
    wrongSender.daemonId = 7;
    EXPECT_FALSE(store.attachDaemonId(wrongSender));

    qypr::ReturnEvent good;
    good.replyCookie = 100;
    good.destination = "sender";
    good.daemonId = 7;
    EXPECT_TRUE(store.attachDaemonId(good));
    EXPECT_EQ(store.notes().at(0).daemonId, 7U);

    // The 33rd pending entry clears the table (stale, unanswered): the very
    // first filler cookie no longer correlates afterwards.
    for (int i = 0; i < 33; ++i) {
        qypr::NotifyEvent e = makeEvent("A", "filler", "b");
        e.cookie = 200 + static_cast<uint64_t>(i);
        store.add(e, false, theme);
    }
    qypr::ReturnEvent cleared;
    cleared.replyCookie = 200;
    cleared.destination = "sender";
    cleared.daemonId = 9;
    EXPECT_FALSE(store.attachDaemonId(cleared));
}

TEST(NotificationPolicySensitiveApps) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path();
    const std::filesystem::path conf = dir / "qypr-test-sensitive.conf";
    {
        std::ofstream f(conf);
        f << "# comment line\n\n   Signal  \nWhatsApp\n";
    }
    qypr::NotificationPolicy policy;
    policy.load(conf.string());
    EXPECT_TRUE(policy.isSensitiveApp("Signal"));
    EXPECT_TRUE(policy.isSensitiveApp("org.signal.app"));  // substring
    EXPECT_TRUE(policy.isSensitiveApp("WHATSAPP"));        // case-folded
    EXPECT_FALSE(policy.isSensitiveApp("Calendar"));
    std::filesystem::remove(conf);

    // Missing file falls back to the built-in defaults.
    qypr::NotificationPolicy fallback;
    fallback.load((dir / "qypr-test-does-not-exist.conf").string());
    EXPECT_TRUE(fallback.isSensitiveApp("telegram"));
}

TEST(NotificationHintsFold) {
    qypr::NotifyHints h;
    h = qypr::applyHint("urgency", true, 2, "", h);
    EXPECT_EQ(h.urgency, 2);
    h = qypr::applyHint("transient", true, 1, "", h);
    EXPECT_TRUE(h.transient);
    h = qypr::applyHint("x-kde-privacy", true, 1, "", h);
    EXPECT_TRUE(h.sensitive);
    qypr::NotifyHints vis;
    vis = qypr::applyHint("visibility", true, 1, "", vis);
    EXPECT_TRUE(vis.sensitive);
    vis = qypr::NotifyHints{};
    vis = qypr::applyHint("visibility", true, 2, "", vis);
    EXPECT_FALSE(vis.sensitive);
    vis = qypr::applyHint("visibility", false, 0, "secret", vis);
    EXPECT_TRUE(vis.sensitive);
    vis = qypr::applyHint("desktop_entry", false, 0, "org.app", vis);
    EXPECT_EQ(vis.desktopEntry, std::string("org.app"));
    qypr::NotifyHints before = vis;
    vis = qypr::applyHint("mystery-key", true, 9, "x", vis);
    EXPECT_TRUE(vis.urgency == before.urgency && vis.transient == before.transient &&
                vis.sensitive == before.sensitive && vis.desktopEntry == before.desktopEntry);
}

TEST(NotificationAccentForUrgency) {
    qypr::theme::State theme;
    EXPECT_TRUE(sameColor(qypr::NotificationStore::accentFor(theme, 3, 2), theme.colors.red));
    EXPECT_TRUE(sameColor(qypr::NotificationStore::accentFor(theme, 0, 1), theme.colors.blue));
    // Deterministic by key: same key, same accent.
    EXPECT_TRUE(sameColor(qypr::NotificationStore::accentFor(theme, 5, 0),
                          qypr::NotificationStore::accentFor(theme, 5, 0)));
}

TEST(NotificationTransportNoBus) {
    MockSdbusFailGuard guard;
    qypr::EventLoop loop;
    qypr::NotificationTransport transport(loop);
    EXPECT_FALSE(transport.start());
    transport.teardown();  // safe while inert
}
