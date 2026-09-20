// test_system.cpp - System stats/backends, state cache, misc hardware.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(VideoPlayer) {
    qypr::EventLoop loop;
    class DummyHost : public qypr::RenderHost {
    public:
        void invalidate() override { invCalled = true; }
        void requestUnlock() override {}
        bool invCalled = false;
    } host;

    qypr::VideoPlayer player(loop, host);
    EXPECT_FALSE(player.hasFrame());

    EXPECT_TRUE(player.init("playlists"));
    player.start();
    player.pause();
    player.resume();
    player.stop();
}
TEST(SystemStatsParsesCpuLine) {
    uint64_t busy = 0;
    uint64_t total = 0;
    // user nice system idle iowait irq softirq steal → busy = total - idle - iowait.
    EXPECT_TRUE(qypr::SystemStats::parseCpuLine("cpu  100 0 50 800 40 5 5 0", busy, total));
    EXPECT_EQ(static_cast<long long>(total), 1000LL);
    EXPECT_EQ(static_cast<long long>(busy), 160LL);  // 1000 - (800 idle + 40 iowait)

    // Per-core lines and non-cpu lines are rejected.
    uint64_t b2 = 0;
    uint64_t t2 = 0;
    EXPECT_FALSE(qypr::SystemStats::parseCpuLine("cpu0 1 2 3 4", b2, t2));
    EXPECT_FALSE(qypr::SystemStats::parseCpuLine("intr 12345", b2, t2));
}
TEST(SystemStatsParsesMemPercent) {
    // MemTotal 1000, MemAvailable 250 → used 750 → 75%.
    const std::string mi = "MemTotal:        1000 kB\n"
                           "MemFree:          100 kB\n"
                           "MemAvailable:     250 kB\n"
                           "Buffers:           10 kB\n";
    double const pct = qypr::SystemStats::parseMemUsedPercent(mi);
    EXPECT_TRUE(pct > 74.9 && pct < 75.1);

    // Missing MemAvailable → -1 (honest failure, not a bogus number).
    EXPECT_TRUE(qypr::SystemStats::parseMemUsedPercent("MemTotal: 1000 kB\n") < 0);
}
TEST(SystemStatsSampleCpuDelta) {
    // The first sample has no baseline (0%); a hand-driven second sample would
    // need a real /proc, so just prove sample() is callable and internally
    // consistent on this machine.
    qypr::SystemStats s;
    qypr::SysSample const a = s.sample();
    EXPECT_TRUE(a.valid);  // /proc exists on the test host
    EXPECT_TRUE(a.memPercent >= 0.0 && a.memPercent <= 100.0);
}

// -----------------------------------------------------------------------------
// Idle inhibitor ("keep awake") — gating without a compositor
// -----------------------------------------------------------------------------
TEST(IdleInhibitorGating) {
    // No backend at all (lock screen): the indicator never appears.
    qypr::SystemBackends const none{};
    qypr::IdleInhibitorIndicator noBackend(none);
    noBackend.onBackendUpdate();
    EXPECT_FALSE(noBackend.visible);
    EXPECT_EQ(noBackend.tooltip(), std::string("Keep awake"));

    // Backend present but not init()'d (no compositor global): still unavailable,
    // so still hidden, and a click is a no-op (not consumed).
    qypr::IdleInhibitor idle;
    EXPECT_FALSE(idle.available());
    EXPECT_FALSE(idle.active());
    qypr::SystemBackends b{};
    b.idleInhibitor = &idle;
    qypr::IdleInhibitorIndicator ind(b);
    ind.onBackendUpdate();
    EXPECT_FALSE(ind.visible);
    EXPECT_FALSE(ind.onClick(0, 0));  // unavailable → not consumed
}

// -----------------------------------------------------------------------------
// Application launcher (.desktop parsing + search) — pure, no filesystem
// -----------------------------------------------------------------------------
TEST(StateCacheSeedFillsPlaceholderThenDefersToLiveData) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(bus);

    // Nothing has replied: the indicator would draw its neutral glyph.
    EXPECT_FALSE(battery.ready());

    qypr::BatterySnapshot cached;
    cached.present = true;
    cached.percentage = 61;
    battery.seed(cached);
    EXPECT_TRUE(battery.ready());
    EXPECT_EQ(battery.snapshot().percentage, 61);

    // Once the backend is live the daemon is the authority — a later seed (a
    // second load, a reload) must never drag the display back to stale values.
    qypr::BatterySnapshot stale;
    stale.present = true;
    stale.percentage = 5;
    battery.seed(stale);
    EXPECT_EQ(battery.snapshot().percentage, 61);
}
TEST(StateCacheRoundTripsThroughAFile) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(bus);
    qypr::WifiBackend wifi(bus);

    qypr::BatterySnapshot b;
    b.present = true;
    b.percentage = 42;
    battery.seed(b);

    qypr::WifiSnapshot w;
    w.available = true;
    w.enabled = true;
    w.connected = true;
    w.ssid = "Test Net";  // spaces must survive: values are trimmed, not split
    w.strength = 77;
    wifi.seed(w);

    qypr::SystemBackends backends{};
    backends.battery = &battery;
    backends.wifi = &wifi;

    const std::string path = "/tmp/qypr-test-state";
    ::unlink(path.c_str());

    qypr::StateCache writer;
    writer.path_ = path;
    writer.track(loop, backends);
    writer.flush();

    // Read it back into fresh backends — the next boot's first frame.
    qypr::BatteryBackend battery2(bus);
    qypr::WifiBackend wifi2(bus);
    qypr::SystemBackends restored{};
    restored.battery = &battery2;
    restored.wifi = &wifi2;

    qypr::StateCache reader;
    reader.path_ = path;
    reader.load();
    reader.seed(restored);

    EXPECT_TRUE(battery2.ready());
    EXPECT_EQ(battery2.snapshot().percentage, 42);
    EXPECT_TRUE(wifi2.ready());
    EXPECT_EQ(wifi2.snapshot().ssid, std::string("Test Net"));
    EXPECT_EQ(wifi2.snapshot().strength, 77);
    EXPECT_TRUE(wifi2.snapshot().connected);

    ::unlink(path.c_str());
}
TEST(StateCacheToleratesMissingAndCorruptFiles) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(bus);
    qypr::SystemBackends backends{};
    backends.battery = &battery;

    // No file yet (first ever run): seeding does nothing and the indicator keeps
    // its placeholder. This must never be an error path.
    qypr::StateCache missing;
    missing.path_ = "/tmp/qypr-test-state-absent";
    missing.load();
    missing.seed(backends);
    EXPECT_FALSE(battery.ready());

    // Corrupt input — the file is written asynchronously and the machine can
    // lose power mid-write, so garbage has to be survivable. Every key falls
    // back to its default, and a battery that is not "present" is never seeded,
    // so nonsense never reaches the screen.
    const std::string path = "/tmp/qypr-test-state-corrupt";
    {
        std::ofstream f(path);
        f << "\x01\x02 not an ini file\n[batt\npercentage = \n= 99\n";
    }
    qypr::StateCache corrupt;
    corrupt.path_ = path;
    corrupt.load();
    corrupt.seed(backends);
    EXPECT_FALSE(battery.ready());
    ::unlink(path.c_str());
}
TEST(HardwareIndicatorsHideUntilTheirBackendReports) {
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(bus);
    qypr::SystemBackends backends{};
    backends.battery = &battery;

    // Nothing has reported yet: the indicator draws nothing at all rather than a
    // neutral glyph that looks live and says nothing. At login this is the state
    // it would otherwise sit in for seconds, since UPower starts after the bar.
    qypr::BatteryIndicator ind(backends);
    EXPECT_FALSE(ind.visible);
    EXPECT_TRUE(ind.label().empty());  // and never an invented "0%"

    // A seeded snapshot is enough to reveal it — that is exactly why StateCache
    // seeds before the first frame rather than waiting for the daemon.
    qypr::BatterySnapshot s;
    s.present = true;
    s.percentage = 61;
    battery.seed(s);
    ind.onBackendUpdate();
    EXPECT_TRUE(ind.visible);
    EXPECT_EQ(ind.label(), std::string("61%"));

    // A backend that reports a definitive "no battery here" (a desktop) keeps
    // the indicator hidden even though it is now loaded.
    qypr::SystemBus bus2(loop);
    qypr::BatteryBackend absent(bus2);
    qypr::BatterySnapshot none;
    none.present = false;
    absent.seed(none);  // seed() only gates on ready(), not on presence
    absent.notifyReady();
    qypr::SystemBackends deskBackends{};
    deskBackends.battery = &absent;
    qypr::BatteryIndicator deskInd(deskBackends);
    deskInd.onBackendUpdate();
    EXPECT_FALSE(deskInd.visible);
}
TEST(StateCacheSkipsRedundantWrites) {
    // noteChanged() is wired to the repaint hook, which also fires for hover and
    // animation. Unchanged content must not cause file I/O.
    qypr::EventLoop loop;
    qypr::SystemBus bus(loop);
    qypr::BatteryBackend battery(bus);
    qypr::BatterySnapshot b;
    b.present = true;
    b.percentage = 30;
    battery.seed(b);

    qypr::SystemBackends backends{};
    backends.battery = &battery;

    const std::string path = "/tmp/qypr-test-state-nowrite";
    ::unlink(path.c_str());

    qypr::StateCache cache;
    cache.path_ = path;

    cache.track(loop, backends);
    cache.flush();

    EXPECT_EQ(::access(path.c_str(), F_OK), 0);  // first flush wrote it

    // Same state → flush() must leave the file completely alone. Deleting it and
    // confirming it is not recreated proves no write happened at all.
    ::unlink(path.c_str());
    cache.flush();
    EXPECT_TRUE(::access(path.c_str(), F_OK) != 0);

    ::unlink(path.c_str());
}

// =============================================================================
// Security review regressions (docs/LOCK_SECURITY_REVIEW.md, QL-1..QL-7)
// =============================================================================

// QL-3: the secret lives in SecureBuffer only. Move must transfer ownership
// without copying, leave the source usable (LockScreen keeps typing), and
// mutating operations must keep the buffer's C-string view well formed (PAM
// builds its reply from cStr()).
