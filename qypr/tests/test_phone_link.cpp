// test_phone_link.cpp - KDE Connect device reduction and the phone-link module.
#include "test_framework.hpp"

#include "system/KdeConnectBackend.hpp"
#include "ui/indicators/PhoneLinkIndicator.hpp"
#include "ui/statusbar/ListFlyout.hpp"
#include "ui/statusbar/MenuPopover.hpp"

namespace {

qypr::PhoneDevice phone(const std::string& id, const std::string& name, bool reachable,
                        bool paired = true) {
    qypr::PhoneDevice d;
    d.id = id;
    d.name = name;
    d.type = "phone";
    d.reachable = reachable;
    d.paired = paired;
    return d;
}

qypr::PhoneDevice connectedPhone() {
    auto d = phone("91e2", "REDMI Note 17", true);
    d.charge = 98;
    d.network = "LTE";
    d.signal = 4;
    d.plugins = {"kdeconnect_findmyphone", "kdeconnect_clipboard", "kdeconnect_share",
                 "kdeconnect_sftp",        "kdeconnect_ping",      "kdeconnect_notifications"};
    return d;
}

}  // namespace

TEST(PhoneLinkKeepsPairedDevicesAndDropsStaleDuplicates) {
    // The machine this was written on: the phone, a stale record of the same
    // phone from before a reinstall, an old phone, and a stranger's laptop.
    std::vector<qypr::PhoneDevice> all{
        phone("6e78", "REDMI Note 17", false), phone("c8dd", "Redmi Note 12", false),
        phone("91e2", "REDMI Note 17", true), phone("aaaa", "Laptop", true, false)};
    const auto shown = qypr::reducePhones(all);
    EXPECT_EQ(shown.size(), 2U);
    EXPECT_EQ(shown[0].id, std::string("91e2"));  // connected first
    EXPECT_EQ(shown[1].id, std::string("c8dd"));

    // With the phone out of reach both records stay: nothing says which is
    // stale.
    all[2].reachable = false;
    EXPECT_EQ(qypr::reducePhones(all).size(), 3U);

    // A stranger asking to pair is listed.
    all[3].pairRequestedByPeer = true;
    EXPECT_EQ(qypr::reducePhones(all).size(), 4U);
}

TEST(PhoneLinkDeviceStatusLine) {
    using qypr::PhoneLinkIndicator;
    auto d = connectedPhone();
    EXPECT_EQ(PhoneLinkIndicator::deviceStatus(d), std::string("98% · LTE ▂▄▆█"));
    d.charging = true;
    d.signal = 2;
    EXPECT_EQ(PhoneLinkIndicator::deviceStatus(d), std::string("98% · charging · LTE ▂▄"));
    d.charge = -1;
    d.network.clear();
    d.signal = -1;
    EXPECT_EQ(PhoneLinkIndicator::deviceStatus(d), std::string("Connected"));
    d.reachable = false;
    EXPECT_EQ(PhoneLinkIndicator::deviceStatus(d), std::string("Not connected"));
    auto stranger = phone("aaaa", "Laptop", true, false);
    stranger.pairRequestedByPeer = true;
    EXPECT_EQ(PhoneLinkIndicator::deviceStatus(stranger), std::string("Wants to pair"));
}

TEST(PhoneLinkModuleFollowsReachability) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::KdeConnectBackend kdc(loop, session);
    qypr::SystemBackends backends{};
    backends.kdeConnect = &kdc;
    qypr::PhoneLinkIndicator ind(backends);
    EXPECT_FALSE(ind.visible);
    EXPECT_EQ(ind.tooltip(), std::string("KDE Connect is not running"));
    EXPECT_TRUE(ind.sensitive());

    // Daemon running, phone paired but away: passive, in the overflow.
    kdc.snap_.available = true;
    kdc.snap_.devices = {phone("c8dd", "Redmi Note 12", false)};
    ind.onBackendUpdate();
    EXPECT_FALSE(ind.visible);
    const auto entry = ind.overflowEntry();
    EXPECT_EQ(entry.id, std::string("phone-link"));
    EXPECT_EQ(entry.title, std::string("Redmi Note 12"));
    EXPECT_EQ(entry.subtitle, std::string("Not connected"));

    // The phone connects: on the bar, out of the overflow.
    auto p = connectedPhone();
    p.notifications = {{.id = "12", .app = "Messages", .title = "Ann", .text = "hi"}};
    kdc.snap_.devices = {p, phone("c8dd", "Redmi Note 12", false)};
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_TRUE(ind.overflowEntry().id.empty());
    EXPECT_EQ(ind.tooltip(), std::string("REDMI Note 17 · 98% · LTE ▂▄▆█\n1 notification"));

    // A pairing request shows the module, tinted, even with no phone around.
    auto stranger = phone("aaaa", "Laptop", true, false);
    stranger.pairRequestedByPeer = true;
    kdc.snap_.devices = {stranger};
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_EQ(ind.tooltip(), std::string("Laptop wants to pair"));
    const qypr::Color tint = ind.iconColor();
    const qypr::Color warning = ind.theme().colors.warning;
    EXPECT_NEAR(tint.r, warning.r, 1e-9);
    EXPECT_NEAR(tint.g, warning.g, 1e-9);
    EXPECT_NEAR(tint.b, warning.b, 1e-9);

    // Right click: the phone's actions as a menu.
    kdc.snap_.devices = {connectedPhone()};
    auto menu = ind.createContextMenu(0, 0);
    EXPECT_TRUE(menu != nullptr);
    auto* pop = dynamic_cast<qypr::MenuPopover*>(menu.get());
    EXPECT_TRUE(pop != nullptr && pop->hasItems());
}

TEST(PhoneLinkFlyoutRows) {
    qypr::EventLoop loop;
    qypr::SystemBus session(loop, qypr::BusKind::Session);
    qypr::KdeConnectBackend kdc(loop, session);
    auto p = connectedPhone();
    for (int i = 0; i < 6; ++i) {
        p.notifications.push_back({.id = std::to_string(20 - i),
                                   .app = "Messages",
                                   .title = i == 0 ? "Ann" : "",
                                   .text = "text",
                                   .dismissable = i != 1});
    }
    auto stranger = phone("aaaa", "Laptop", true, false);
    stranger.pairRequestedByPeer = true;
    qypr::PhoneLinkSnapshot snap{.available = true,
                                 .devices = {stranger, p, phone("c8dd", "Redmi Note 12", false)},
                                 .error = "Couldn't ring the phone"};

    const auto m = qypr::PhoneLinkIndicator::buildFlyout(snap, &kdc, nullptr);
    std::vector<std::string> titles;
    titles.reserve(m.rows.size());
    for (const auto& r : m.rows) { titles.push_back(r.title); }
    const std::vector<std::string> expected{"Laptop wants to pair",
                                            "REDMI Note 17",
                                            "Find my phone",
                                            "Send to phone",
                                            "Phone storage",
                                            "NOTIFICATIONS",
                                            "Messages — Ann",
                                            "Messages",
                                            "Messages",
                                            "Messages",
                                            "Messages",
                                            "1 more on the phone",
                                            "NOT CONNECTED",
                                            "Redmi Note 12",
                                            "Couldn't ring the phone"};
    EXPECT_EQ(titles.size(), expected.size());
    for (size_t i = 0; i < std::min(titles.size(), expected.size()); ++i) {
        EXPECT_EQ(titles[i], expected[i]);
    }
    EXPECT_EQ(m.rows[0].actions.size(), 2U);   // Accept, Reject
    EXPECT_EQ(m.rows[3].actions.size(), 2U);   // Clipboard, Files…
    EXPECT_TRUE(m.rows[4].actions[0].closes);  // Browse hands off to the file manager
    EXPECT_EQ(m.rows[6].actions.size(), 1U);   // dismissable
    EXPECT_TRUE(m.rows[7].actions.empty());    // not dismissable
    EXPECT_TRUE(m.rows.back().tone == qypr::FlyoutRow::Tone::Error);

    // Buttons reach the backend; with no daemon under test they are no-ops.
    m.rows[2].actions[0].run();      // Ring
    m.rows.back().actions[0].run();  // Dismiss the error (on the backend's own copy)

    // No daemon: one line saying so.
    const auto down = qypr::PhoneLinkIndicator::buildFlyout({}, &kdc, nullptr);
    EXPECT_TRUE(down.rows.empty());
    EXPECT_EQ(down.empty, std::string("KDE Connect is not running"));
}
