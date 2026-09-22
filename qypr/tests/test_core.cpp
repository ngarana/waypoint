// test_core.cpp - Core: types, event loop, config/watcher, indicator registry.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(Types) {
    // Color Hex Parsing
    qypr::Color const c1 = qypr::Color::fromHex("#ff0000");
    EXPECT_NEAR(c1.r, 1.0, 0.01);
    EXPECT_NEAR(c1.g, 0.0, 0.01);
    EXPECT_NEAR(c1.b, 0.0, 0.01);
    EXPECT_NEAR(c1.a, 1.0, 0.01);

    qypr::Color const c2 = qypr::Color::fromHex("#8000ff00");
    EXPECT_NEAR(c2.a, 0.5, 0.05);  // alpha first #AARRGGBB
    EXPECT_NEAR(c2.r, 0.0, 0.01);
    EXPECT_NEAR(c2.g, 1.0, 0.01);
    EXPECT_NEAR(c2.b, 0.0, 0.01);

    qypr::Color const c3 = c1.withAlpha(0.2);
    EXPECT_NEAR(c3.a, 0.2, 0.01);

    qypr::Color const c4 = qypr::Color::rgba(0.1, 0.2, 0.3, 0.4);
    EXPECT_NEAR(c4.r, 0.1, 0.01);
    EXPECT_NEAR(c4.g, 0.2, 0.01);
    EXPECT_NEAR(c4.b, 0.3, 0.01);
    EXPECT_NEAR(c4.a, 0.4, 0.01);

    // Rect Primitives
    qypr::Rect const r{.x = 10, .y = 20, .w = 100, .h = 200};
    EXPECT_TRUE(r.contains(15, 25));
    EXPECT_FALSE(r.contains(5, 25));
    EXPECT_FALSE(r.contains(15, 250));
    EXPECT_NEAR(r.cx(), 60.0, 0.01);
    EXPECT_NEAR(r.cy(), 120.0, 0.01);
    EXPECT_TRUE(r.valid());

    qypr::Rect const rInv{.x = 0, .y = 0, .w = -10, .h = 10};
    EXPECT_FALSE(rInv.valid());

    // Easing & Lerp
    EXPECT_NEAR(qypr::clamp01(1.5), 1.0, 0.001);
    EXPECT_NEAR(qypr::clamp01(-0.5), 0.0, 0.001);
    EXPECT_NEAR(qypr::lerp(10.0, 20.0, 0.5), 15.0, 0.001);

    EXPECT_NEAR(qypr::ease::linear(0.5), 0.5, 0.001);
    EXPECT_NEAR(qypr::ease::inOutQuad(0.0), 0.0, 0.001);
    EXPECT_NEAR(qypr::ease::inOutQuad(1.0), 1.0, 0.001);
    EXPECT_NEAR(qypr::ease::outBack(0.0), 0.0, 0.001);
    EXPECT_NEAR(qypr::ease::outBack(1.0), 1.0, 0.001);

    // Animated Class
    qypr::Animated anim(0.0);
    EXPECT_NEAR(anim.target(), 0.0, 0.001);
    EXPECT_FALSE(anim.active(qypr::nowMs()));

    anim.animateTo(1.0, 100, qypr::ease::linear);
    EXPECT_NEAR(anim.target(), 1.0, 0.001);

    int64_t const start = qypr::nowMs();
    EXPECT_TRUE(anim.active(start));
    EXPECT_NEAR(anim.value(start), 0.0, 0.001);
    EXPECT_NEAR(anim.value(start + 50), 0.5, 0.1);     // middle
    EXPECT_NEAR(anim.value(start + 200), 1.0, 0.001);  // past end
    EXPECT_FALSE(anim.active(start + 200));

    anim.set(5.0);
    EXPECT_NEAR(anim.value(qypr::nowMs()), 5.0, 0.001);
    EXPECT_FALSE(anim.active(qypr::nowMs()));
}
TEST(EventLoop) {
    qypr::EventLoop loop;

    // Test post task
    bool postedRan = false;
    loop.post([&] {
        postedRan = true;
        loop.quit();
    });

    // Test prepare callback
    bool prepareRan = false;
    loop.addPrepare([&] { prepareRan = true; });

    // Run the loop which should exit via post
    loop.run();

    EXPECT_TRUE(postedRan);
    EXPECT_TRUE(prepareRan);

    // Test timers
    bool timerRan = false;
    qypr::EventLoop loop2;
    int const tfd = loop2.addTimer(5, false, [&] {
        timerRan = true;
        loop2.quit();
    });
    EXPECT_TRUE(tfd >= 0);
    loop2.run();
    EXPECT_TRUE(timerRan);

    // Test repeating timer cancellation
    int timerCount = 0;
    qypr::EventLoop loop3;
    int repTfd = loop3.addTimer(2, true, [&] {
        timerCount++;
        if (timerCount == 2) {
            loop3.removeTimer(repTfd);
            loop3.quit();
        }
    });
    loop3.run();
    EXPECT_EQ(timerCount, 2);
}
TEST(ConfigWatcherRearmInsideCallback) {
    qypr::EventLoop loop;
    qypr::ConfigWatcher watcher(loop);
    const std::string path = "/tmp/qypr-watch-rearm-" + std::to_string(::getpid()) + ".css";
    {
        std::ofstream f(path);
        f << "v1\n";
    }

    int fires = 0;
    std::function<void()> onFire = [&] {
        ++fires;
        watcher.stop();
        watcher.watch(path, onFire);  // re-arm inside our own invocation
    };
    watcher.watch(path, onFire);

    std::thread t([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        {
            std::ofstream f(path, std::ios::app);
            f << "x\n";
        }  // fire 1 (+ re-arm)
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        {
            std::ofstream f(path, std::ios::app);
            f << "y\n";
        }  // fire 2
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        loop.post([&] { loop.quit(); });
    });
    loop.run();
    t.join();
    EXPECT_EQ(fires, 2);
    ::unlink(path.c_str());
}

// palette-mode selects which matugen file feeds the theme; the light file
// falls back to the dark file when unset; light mode softens the text shadow
// unless the user set an explicit shadow-opacity.
TEST(ThemeLoadThemeOverrides) {
    const std::string path = writeTempConfig("[theme]\n"
                                             "font-family = JetBrains Mono\n"
                                             "font-size = 20\n"
                                             "primary = #ff0000\n"
                                             "bar-height = 42.0\n");
    qypr::Config c;
    c.load(path);
    qypr::theme::AutoPalette pal = qypr::theme::AutoPalette::fromConfig(c, 12);
    qypr::theme::loadTheme(c, pal);
    EXPECT_EQ(qypr::theme::font::family, std::string("JetBrains Mono"));
    EXPECT_EQ(qypr::theme::font::size, 20);
    EXPECT_NEAR(qypr::theme::color::primary.r, 1.0, 0.01);
    EXPECT_NEAR(qypr::theme::color::primary.g, 0.0, 0.01);
    EXPECT_NEAR(qypr::theme::statusbar::height, 42.0, 0.01);
    ::unlink(path.c_str());
}
TEST(ConfigParsing) {
    const std::string path = writeTempConfig("# a comment\n"
                                             "// another comment\n"
                                             "\n"
                                             "[bar]\n"
                                             "position = bottom\n"
                                             "height = 40\n"
                                             "backdrop = 0.5\n"
                                             "auto-hide = yes\n"
                                             "modules-left = workspaces, clock\n"
                                             "  spaced-key   =   value with inner spaces  \n"
                                             "junk line without equals\n"
                                             "\n"
                                             "[clock]\n"
                                             "format = %H:%M\n");

    qypr::Config c;
    EXPECT_TRUE(c.load(path));
    EXPECT_TRUE(c.loaded());

    // Typed accessors + section scoping.
    EXPECT_EQ(c.getString("bar", "position", "top"), std::string("bottom"));
    EXPECT_EQ(c.getInt("bar", "height", 36), 40);
    EXPECT_TRUE(std::fabs(c.getDouble("bar", "backdrop", 0.8) - 0.5) < 1e-9);
    EXPECT_TRUE(c.getBool("bar", "auto-hide", false));
    EXPECT_EQ(c.getString("clock", "format", "x"), std::string("%H:%M"));

    // Ends trimmed, inner spaces kept.
    EXPECT_EQ(c.getString("bar", "spaced-key", ""), std::string("value with inner spaces"));

    // Lists split + trim.
    auto mods = c.getList("bar", "modules-left");
    EXPECT_EQ(static_cast<int>(mods.size()), 2);
    EXPECT_EQ(mods.at(0), std::string("workspaces"));
    EXPECT_EQ(mods.at(1), std::string("clock"));

    // Absent keys fall back; a key in the wrong section is absent.
    EXPECT_EQ(c.getInt("bar", "nope", 7), 7);
    EXPECT_EQ(c.getString("bar", "format", "def"), std::string("def"));  // clock's, not bar's
    EXPECT_FALSE(c.has("bar", "format"));
    EXPECT_TRUE(c.has("clock", "format"));

    ::unlink(path.c_str());
}
TEST(ConfigMissingFileIsNotAnError) {
    // The compiled-in bar must still run with no config at all.
    qypr::Config c;
    EXPECT_FALSE(c.load("/tmp/qypr-definitely-does-not-exist-9182.conf"));
    EXPECT_FALSE(c.loaded());
    EXPECT_EQ(c.getInt("bar", "height", 36), 36);  // default survives
    EXPECT_EQ(static_cast<int>(c.getList("bar", "modules-left").size()), 0);
}
TEST(ConfigMalformedValuesKeepDefaults) {
    const std::string path =
        writeTempConfig("[bar]\nheight = not-a-number\nbackdrop = \nflag = maybe\n");
    qypr::Config c;
    EXPECT_TRUE(c.load(path));
    EXPECT_EQ(c.getInt("bar", "height", 36), 36);  // stoi throws → default
    EXPECT_TRUE(std::fabs(c.getDouble("bar", "backdrop", 0.8) - 0.8) < 1e-9);
    EXPECT_TRUE(c.getBool("bar", "flag", true));  // unparseable → default
    ::unlink(path.c_str());
}
TEST(ConfigEmptyListEmptiesZone) {
    // An explicitly empty value means "this zone is empty", which must be
    // distinguishable from "key absent" (= use defaults).
    const std::string path = writeTempConfig("[bar]\nmodules-center =\n");
    qypr::Config c;
    EXPECT_TRUE(c.load(path));
    EXPECT_TRUE(c.has("bar", "modules-center"));
    std::vector<std::string> const fallback{"active-window"};
    EXPECT_EQ(static_cast<int>(c.getList("bar", "modules-center", fallback).size()), 0);
    // Absent key → caller's fallback.
    EXPECT_EQ(static_cast<int>(c.getList("bar", "modules-left", fallback).size()), 1);
    ::unlink(path.c_str());
}
TEST(ConfigDirRespectsXdg) {
    // Single-threaded test binary — TEST bodies run sequentially during
    // static init; no concurrent env users.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    const char* old = ::getenv("XDG_CONFIG_HOME");
    const std::string saved = (old != nullptr) ? old : "";
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::setenv("XDG_CONFIG_HOME", "/tmp/xdg-probe", 1);
    EXPECT_EQ(qypr::Config::configDir(), std::string("/tmp/xdg-probe/qypr"));
    EXPECT_EQ(qypr::Config::defaultPath(), std::string("/tmp/xdg-probe/qypr/bar.conf"));

    // Without XDG_CONFIG_HOME it falls back to $HOME/.config/qypr.
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::unsetenv("XDG_CONFIG_HOME");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    ::setenv("HOME", "/tmp/home-probe", 1);
    EXPECT_EQ(qypr::Config::configDir(), std::string("/tmp/home-probe/.config/qypr"));

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    if (!saved.empty()) { ::setenv("XDG_CONFIG_HOME", saved.c_str(), 1); }
}

// These use a LOCAL registry rather than instance(). The TEST macro runs bodies
// during static initialisation, so the compiled-in REGISTER_INDICATOR set is not
// guaranteed to exist yet (cross-TU static init order is unspecified) — a test
// leaning on it would pass or fail by link order. A local registry is hermetic
// and also keeps fake indicators out of the global one.
namespace {
qypr::IndicatorRegistry::Factory testFactory(const std::string& id, qypr::Zone z, int p) {
    return [id, z, p](const qypr::SystemBackends&) {
        return std::make_unique<TestIndicator>(id, z, p);
    };
}
}  // namespace
TEST(RegistryModuleSelection) {
    qypr::IndicatorRegistry reg;  // ctor reachable via `#define private public`
    reg.registerIndicator("clock", qypr::Zone::Left, 0, testFactory("clock", qypr::Zone::Left, 0));
    reg.registerIndicator("battery", qypr::Zone::Right, 500,
                          testFactory("battery", qypr::Zone::Right, 500));
    reg.registerIndicator("volume", qypr::Zone::Right, 200,
                          testFactory("volume", qypr::Zone::Right, 200));
    reg.registerIndicator("brightness", qypr::Zone::Right, 100,
                          testFactory("brightness", qypr::Zone::Right, 100));

    // Config-driven selection: only the named ids, in the listed order, re-homed
    // to the zone they were listed under.
    qypr::SystemBackends const b{};
    qypr::IndicatorRegistry::ModuleSelection sel;
    sel.left = {"clock"};                  // compiled Left, stays Left
    sel.center = {"battery"};              // compiled Right → re-homed to Center
    sel.right = {"volume", "brightness"};  // reversed vs compiled priority

    auto made = reg.createAll(b, &sel);
    EXPECT_EQ(static_cast<int>(made.size()), 4);
    EXPECT_EQ(made.at(0)->id(), std::string("clock"));
    EXPECT_TRUE(made.at(0)->zone() == qypr::Zone::Left);
    // Re-homed: a module lands in the zone it was listed under.
    EXPECT_EQ(made.at(1)->id(), std::string("battery"));
    EXPECT_TRUE(made.at(1)->zone() == qypr::Zone::Center);
    // Listed order wins over compiled priority (brightness=100 < volume=200
    // would otherwise sort first).
    EXPECT_EQ(made.at(2)->id(), std::string("volume"));
    EXPECT_EQ(made.at(3)->id(), std::string("brightness"));
    EXPECT_TRUE(made.at(2)->zone() == qypr::Zone::Right);
}
TEST(RegistryUnknownModuleIsSkipped) {
    // A typo must drop that module, never crash the bar.
    qypr::IndicatorRegistry reg;
    reg.registerIndicator("clock", qypr::Zone::Left, 0, testFactory("clock", qypr::Zone::Left, 0));
    reg.registerIndicator("battery", qypr::Zone::Right, 500,
                          testFactory("battery", qypr::Zone::Right, 500));

    qypr::SystemBackends const b{};
    qypr::IndicatorRegistry::ModuleSelection sel;
    sel.left = {"clock", "no-such-module", "battery"};
    auto made = reg.createAll(b, &sel);
    EXPECT_EQ(static_cast<int>(made.size()), 2);
    EXPECT_EQ(made.at(0)->id(), std::string("clock"));
    EXPECT_EQ(made.at(1)->id(), std::string("battery"));
}
TEST(RegistryNullSelectionKeepsCompiledDefaults) {
    // No selection (the lock screen's path): every registered indicator, grouped
    // by compiled zone, priority ascending — unchanged behaviour.
    qypr::IndicatorRegistry reg;
    reg.registerIndicator("battery", qypr::Zone::Right, 500,
                          testFactory("battery", qypr::Zone::Right, 500));
    reg.registerIndicator("brightness", qypr::Zone::Right, 100,
                          testFactory("brightness", qypr::Zone::Right, 100));
    reg.registerIndicator("clock", qypr::Zone::Left, 0, testFactory("clock", qypr::Zone::Left, 0));

    qypr::SystemBackends const b{};
    auto all = reg.createAll(b, nullptr);
    EXPECT_EQ(static_cast<int>(all.size()), 3);
    EXPECT_EQ(static_cast<int>(reg.registeredIds().size()), 3);
    // Left zone first, then Right by ascending priority.
    EXPECT_EQ(all.at(0)->id(), std::string("clock"));
    EXPECT_EQ(all.at(1)->id(), std::string("brightness"));  // 100 before 500
    EXPECT_EQ(all.at(2)->id(), std::string("battery"));
    for (size_t i = 1; i < all.size(); ++i) {
        if (all.at(i - 1)->zone() == all.at(i)->zone()) {
            EXPECT_TRUE(all.at(i - 1)->priority() <= all.at(i)->priority());
        }
    }
}
TEST(ClockFormatFromConfig) {
    const std::string path = writeTempConfig("[clock]\nformat = %Y\n");
    qypr::Config c;
    EXPECT_TRUE(c.load(path));

    qypr::SystemBackends b{};
    b.config = &c;
    qypr::ClockIndicator clk(b);
    clk.poll(1'000'000);  // force a refresh past the 1s gate

    // %Y renders a 4-digit year — proves the config format is in effect.
    const std::string label = clk.label();
    EXPECT_EQ(static_cast<int>(label.size()), 4);
    EXPECT_TRUE(label.at(0) == '2');

    // No config → compiled default (contains ":" from %-I:%M).
    qypr::SystemBackends const plain{};
    qypr::ClockIndicator def(plain);
    def.poll(1'000'000);
    EXPECT_TRUE(def.label().find(':') != std::string::npos);

    ::unlink(path.c_str());
}
