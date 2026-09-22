// test_bar.cpp - Status bar, QS panel, popovers. ORDER MATTERS: StatusBarFocusCycling relies on the
// TestIndicator registered by IndicatorRegistryRegisterAndCreate earlier in THIS file (intra-TU
// order is definition order; cross-TU order is not). Split verbatim from tests/unit_tests.cpp;
// bodies unchanged.
#include "test_framework.hpp"

TEST(AppAndUIHeadlessPreview) {
    qypr::App app;
    app.setIdleTimeout(10);

    // Exercises App::preview which renders both idle and revealed states to PNG
    // This exercises the full Cairo widget tree draw pipeline and event callbacks!
    int const rc = app.preview("qypr-test-preview.png", 800, 600);
    EXPECT_EQ(rc, 0);

    // Clean up preview files
    std::remove("qypr-test-preview.png");
    std::remove("qypr-test-preview-idle.png");
}
TEST(BarAppPreview) {
    qypr::BarApp app;

    // Exercises BarApp::preview which renders standalone bar + QS frames to PNG.
    int const rc = app.preview("qypr-test-bar-preview.png", 800, 600);
    EXPECT_EQ(rc, 0);

    // Clean up preview files
    std::remove("qypr-test-bar-preview.png");
    std::remove("qypr-test-bar-preview-qs.png");
    std::remove("qypr-test-bar-preview-dnd.png");
}
TEST(IconResolver) {
    qypr::IconResolver r;

    const std::string duri = "data:image/png;base64,"
                             "iVBORw0KGgoAAAANSUhEUgAAAAQAAAAECAIAAAAmkwkpAAAABmJLR0QA/wD/AP+gvaeT"
                             "AAAAEElEQVQImWP8z4AATAxEcQAz0QEH1mUzKgAAAABJRU5ErkJggg==";
    cairo_surface_t* s = r.get(duri);
    EXPECT_TRUE(s != nullptr);
    EXPECT_EQ(cairo_image_surface_get_width(s), 4);

    // Same key returns the identical, cache-owned surface (no reload).
    EXPECT_TRUE(r.get(duri) == s);

    // Empty and unknown names resolve to nothing -> glyph fallback.
    EXPECT_TRUE(r.get("") == nullptr);
    EXPECT_TRUE(r.get("qypr-no-such-icon-xyz") == nullptr);
}

// =============================================================================
// Phase 1: Status Bar Framework Tests
// =============================================================================

// Test that the IndicatorRegistry can register and create indicators.
TEST(IndicatorRegistryRegisterAndCreate) {
    auto& reg = qypr::IndicatorRegistry::instance();
    size_t const before = reg.entries_.size();

    reg.registerIndicator(
        "test-indicator", qypr::Zone::Right, 999, [](const qypr::SystemBackends&) {
            return std::make_unique<TestIndicator>("test-indicator", qypr::Zone::Right, 999);
        });

    EXPECT_EQ(reg.entries_.size(), before + 1);

    qypr::SystemBackends const backends{};
    auto items = reg.createAll(backends);
    bool found = false;
    for (auto& item : items) {
        if (item->id() == "test-indicator") {
            found = true;
            EXPECT_TRUE(static_cast<int>(item->zone()) == static_cast<int>(qypr::Zone::Right));
            EXPECT_EQ(item->priority(), 999);
        }
    }
    EXPECT_TRUE(found);
}

// Test StatusIndicator base class getters and state.
TEST(StatusIndicatorBaseClass) {
    TestIndicator ind("my-ind", qypr::Zone::Left, 100);

    EXPECT_EQ(ind.id(), std::string("my-ind"));
    EXPECT_EQ(static_cast<int>(ind.zone()), static_cast<int>(qypr::Zone::Left));
    EXPECT_EQ(ind.priority(), 100);
    EXPECT_FALSE(ind.hovered);
    EXPECT_FALSE(ind.focused);
    EXPECT_FALSE(ind.hasDetailedView());
    EXPECT_TRUE(ind.createTile() == nullptr);
    EXPECT_TRUE(ind.createDetailedView() == nullptr);
}

// Test QSToggleTile renders without crashing on a null Cairo context.
TEST(QSToggleTileDraw) {
    qypr::QSToggleTile tile(
        "WiFi", "󰤨", []() { return true; }, []() {}, []() { return std::string("MyHome"); });

    EXPECT_TRUE(tile.type() == qypr::QSTile::Type::Toggle);
    EXPECT_TRUE(tile.bounds.valid() == false);  // not yet laid out

    // Draw to a null painter — exercises the code path without a real surface.
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 200, 100);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile.bounds = {.x = 0, .y = 0, .w = 110, .h = 64};
    tile.draw(p, 1000);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// Test QSSliderTile renders and handles drag input.
TEST(QSSliderTileDrawAndDrag) {
    double vol = 0.5;
    qypr::QSSliderTile tile("󰕾", [&]() { return vol; }, [&](double v) { vol = v; });

    EXPECT_TRUE(tile.type() == qypr::QSTile::Type::Slider);

    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 60);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    tile.bounds = {.x = 0, .y = 0, .w = 360, .h = 40};
    tile.draw(p, 2000);

    // Simulate click/drag on the slider track to change value.
    tile.onClick(tile.bounds.x + (tile.bounds.w * 0.75), tile.bounds.y + (tile.bounds.h / 2.0));
    EXPECT_NEAR(vol, 0.75, 0.1);

    tile.onDrag(tile.bounds.x + (tile.bounds.w * 0.25), tile.bounds.y + (tile.bounds.h / 2.0));
    EXPECT_NEAR(vol, 0.25, 0.1);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// Test QuickSettingsPanel layout and tile aggregation.
TEST(QuickSettingsPanelLayout) {
    qypr::QuickSettingsPanel panel;

    panel.addTile(
        std::make_unique<qypr::QSToggleTile>("WiFi", "󰤨", []() { return true; }, []() {}));
    panel.addTile(
        std::make_unique<qypr::QSToggleTile>("BT", "󰂯", []() { return false; }, []() {}));
    panel.addTile(
        std::make_unique<qypr::QSSliderTile>("󰕾", []() { return 0.6; }, [](double) {}));

    panel.anchorX = 800;
    panel.anchorY = 50;

    EXPECT_TRUE(!panel.isOpen());

    // Draw the panel to exercise layout logic and populate tile bounds.
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 300);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    panel.draw(p, 3000);
    double const h = panel.contentHeight();
    EXPECT_TRUE(h > 0);

    // Click inside the first toggle tile (below the header row).
    qypr::Rect const pb = panel.getBounds();
    double const firstTileX = pb.x + 16.0 + 5.0;               // pad + small offset
    double const firstTileY = pb.y + 16.0 + 52.0 + 8.0 + 5.0;  // pad + header + gap + offset
    EXPECT_TRUE(panel.handleClick(firstTileX, firstTileY));

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// Test PopoverManager open/close lifecycle.
TEST(PopoverManagerLifecycle) {
    qypr::PopoverManager pm;
    EXPECT_TRUE(pm.active() == nullptr);

    // DetailedPopover is abstract; use a minimal concrete subclass.
    struct TestPopover : qypr::DetailedPopover {
        void draw(qypr::Painter& /*p*/, int64_t /*now*/) override {}
        [[nodiscard]] double contentHeight() const override { return 100.0; }
    };

    auto pop = std::make_unique<TestPopover>();
    qypr::DetailedPopover const* raw = pop.get();

    pm.open(std::move(pop), 100, 50);
    EXPECT_TRUE(pm.active() == raw);
    EXPECT_TRUE(pm.active()->isOpen());

    pm.closeActive();
    EXPECT_TRUE(pm.active() == nullptr);
}

// Test that StatusBar constructs, lays out, and draws without crashing.
TEST(StatusBarConstructionAndDraw) {
    qypr::EventLoop loop;

    struct DummyHost : qypr::RenderHost {
        int invalidations = 0;
        void invalidate() override { ++invalidations; }
        void requestUnlock() override {}
    } host;

    qypr::SystemBackends const backends{};
    qypr::StatusBar bar(loop, host, backends);

    // Layout at 1920x1080
    bar.layout(1920, 1080);
    EXPECT_TRUE(bar.bounds.w > 0);
    EXPECT_TRUE(bar.bounds.h > 0);

    // Draw to a real Cairo surface
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    bar.draw(p, 4000);

    // Second draw to verify no state corruption
    bar.draw(p, 5000);

    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// Test StatusBar pointer input routing (hover, click, scroll).
TEST(StatusBarPointerInput) {
    qypr::EventLoop loop;
    struct DummyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;

    qypr::SystemBackends const backends{};
    qypr::StatusBar bar(loop, host, backends);
    bar.layout(1920, 1080);

    // A display-only indicator (no detailed view, no onClick) must NOT open the
    // Quick Settings panel when activated — clicking the right-group chip
    // (which wraps indicators like WiFi|Battery|Clock) is the QS trigger.
    // A standalone click on a non-right-zone indicator activates it without
    // opening QS.
    qypr::KeyboardLayoutIndicator displayOnly(backends);
    bar.activateIndicator(displayOnly);
    EXPECT_FALSE(bar.hasOpenOverlay());
    // Nothing open → the host stays at its idle strip (overlay height 0).
    EXPECT_EQ(bar.overlayHeight(), 0);

    // Motion inside the bar
    bool handled = bar.handlePointerMotion(bar.bounds.x + 50, bar.bounds.y + 10, 1000);
    EXPECT_TRUE(handled);

    // Motion outside the bar
    handled = bar.handlePointerMotion(0, 0, 1001);
    EXPECT_FALSE(handled);

    // macOS-style: each indicator opens its own popover; Quick Settings is
    // reached by clicking the Control Center (the "power" indicator in the
    // right zone). With no SystemActions supplied (backends.power == nullptr)
    // the panel cannot open, so a stray click on empty right-zone space is
    // a harmless no-op.
    double const chipX = bar.bounds.x + bar.bounds.w - 30;
    double const chipY = bar.bounds.y + (bar.bounds.h / 2.0);
    // A click's return only reports hit-testing; what matters here is the
    // side effect (the click is processed), asserted via the overlay below.
    bar.handlePointerButton(chipX, chipY, 272, true, 1002);
    // Result is handled iff an indicator is actually present at that
    // location; the empty-slot expectation is "no overlay opens".
    EXPECT_FALSE(bar.hasOpenOverlay());
    EXPECT_EQ(bar.overlayHeight(), 0);

    // Leave clears hover
    bar.handlePointerLeave(1003);
}

// Test StatusBar keyboard focus cycling.
TEST(StatusBarFocusCycling) {
    qypr::EventLoop loop;
    struct DummyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;

    qypr::SystemBackends const backends{};
    qypr::StatusBar bar(loop, host, backends);
    bar.layout(1920, 1080);

    // cycleFocus on a bar with registered indicators should succeed
    // (the TestIndicator from IndicatorRegistryRegisterAndCreate persists in the singleton)
    bool const changed = bar.cycleFocus(false);
    // Focus should have moved to the first indicator
    EXPECT_TRUE(changed);

    // Clear and verify no crash
    bar.clearFocus();
}

// Test StatusBar animating() detects active animations.
TEST(StatusBarAnimating) {
    qypr::EventLoop loop;
    struct DummyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;

    qypr::SystemBackends const backends{};
    qypr::StatusBar bar(loop, host, backends);
    bar.layout(1920, 1080);

    // Initially not animating
    EXPECT_TRUE(!bar.animating(10000));

    // Hover over an indicator and draw to trigger its hover animation
    if (!bar.rightIndicators_.empty()) {
        auto& ind = bar.rightIndicators_.front();
        bar.handlePointerMotion(ind->bounds.x + 5, ind->bounds.y + 5, 10001);
        cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1920, 1080);
        cairo_t* cr = cairo_create(surf);
        qypr::Painter p(cr);
        bar.draw(p, 10001);  // draw triggers the hover animation on indicators
        EXPECT_TRUE(bar.animating(10001));
        cairo_destroy(cr);
        cairo_surface_destroy(surf);
    }
}

// Test theme::statusbar constants are accessible.
// Test theme::State compiled defaults are sane geometry (no config, no
// globals — the default-constructed value is the whole fixture).
TEST(StatusBarThemeConstants) {
    qypr::theme::State st;
    EXPECT_TRUE(st.statusbar.height > 0);
    EXPECT_TRUE(st.statusbar.topMargin >= 0);
    EXPECT_TRUE(st.statusbar.sideMargin >= 0);
    EXPECT_TRUE(st.statusbar.cornerRadius > 0);
    EXPECT_TRUE(st.statusbar.iconSize > 0);
    EXPECT_TRUE(st.statusbar.iconSpacing > 0);
    EXPECT_TRUE(st.statusbar.padding > 0);
    EXPECT_TRUE(st.statusbar.qsPanelWidth > 0);
    EXPECT_TRUE(st.statusbar.qsTileHeight > 0);
}
TEST(QuickSettingsPanelSecondaryRouting) {
    qypr::EventLoop loop;
    qypr::SystemBackends const backends{};  // no wifi backend → placeholder combo tile
    qypr::QuickSettingsPanel panel;
    bool pickerOpened = false;
    bool detailOpened = false;
    // Indicator tiles are added before buildTiles (the StatusBar order), so
    // the panel keeps this one instead of creating its own placeholder.
    // The role (not the title) is what reserves the Bluetooth slot.
    auto bt = std::make_unique<qypr::QSToggleTile>(
        "Bluetooth", "󰂯", []() { return true; }, []() {}, nullptr,
        qypr::Color{.r = 0, .g = 0, .b = 0, .a = 0}, qypr::QSTile::Role::Bluetooth);
    bt->setOnSecondary([&detailOpened]() { detailOpened = true; });
    panel.addTile(std::move(bt));
    panel.buildTiles(loop, backends, [&pickerOpened]() { pickerOpened = true; });

    panel.anchorX = 800;
    panel.anchorY = 50;
    panel.open();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 900, 400);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    panel.draw(p, qypr::nowMs() + 10000);

    // Right-click the Wi-Fi tile → the network picker callback fires.
    const qypr::Rect wf = panel.findTileBounds("Wi-Fi");
    EXPECT_TRUE(wf.valid());
    EXPECT_TRUE(panel.handleSecondaryClick(wf.x + (wf.w / 2.0), wf.y + (wf.h / 2.0)));
    EXPECT_TRUE(pickerOpened);
    EXPECT_FALSE(detailOpened);

    // Right-click the Bluetooth tile → its attached detail action fires.
    const qypr::Rect bb = panel.findTileBounds("Bluetooth");
    EXPECT_TRUE(bb.valid());
    EXPECT_TRUE(panel.handleSecondaryClick(bb.x + (bb.w / 2.0), bb.y + (bb.h / 2.0)));
    EXPECT_TRUE(detailOpened);
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}
// Tile identity is the stable role, never the display title: relabelled
// indicator tiles still fill their slots, so the panel adds no placeholder
// duplicates for them.
TEST(QuickSettingsPanelRoleDedup) {
    qypr::EventLoop loop;
    qypr::SystemBackends const backends{};
    qypr::QuickSettingsPanel panel;
    panel.addTile(std::make_unique<qypr::QSToggleTile>(
        "Wireless", "󰂯", []() { return true; }, []() {}, nullptr,
        qypr::Color{.r = 0, .g = 0, .b = 0, .a = 0}, qypr::QSTile::Role::Bluetooth));
    panel.addTile(std::make_unique<qypr::QSSliderTile>(
        " ", []() { return 0.5; }, [](double) {}, nullptr, nullptr, nullptr, "Lumen",
        qypr::QSTile::Role::Brightness));
    panel.buildTiles(loop, backends, []() {});

    int bt = 0;
    int br = 0;
    for (const auto& t : panel.tiles_) {
        if (t->role() == qypr::QSTile::Role::Bluetooth) { ++bt; }
        if (t->role() == qypr::QSTile::Role::Brightness) { ++br; }
    }
    EXPECT_EQ(bt, 1);
    EXPECT_EQ(br, 1);
}

TEST(StatusBarRightClickOnQSTileOpensDetail) {
    qypr::EventLoop loop;
    struct DummyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;
    qypr::SystemBus bus(loop);
    qypr::WifiBackend wb(bus);
    qypr::WifiSnapshot seeded;
    seeded.available = true;
    seeded.enabled = true;
    seeded.networks = {
        {.ssid = "Net", .strength = 70, .secured = true, .active = false, .saved = true}};
    wb.seed(seeded);

    qypr::SystemBackends backends{};
    backends.wifi = &wb;
    // Quick Settings — and therefore its command/toggle tiles — only exist on the
    // unlocked bar (QL-7: the lock process must not even build them), and only
    // the unlocked bar makes indicators interactive (QL-1/QL-2/QL-4).
    backends.sessionSurface = true;
    qypr::StatusBar bar(loop, host, backends);
    bar.setSessionContentVisible(true);  // as BarApp::run() does
    // The test binary's static-init order runs every TEST body before the
    // indicator TUs self-register, so the global registry is empty here —
    // inject the wifi indicator the way the registry would have.
    bar.rightIndicators_.push_back(std::make_unique<qypr::WifiIndicator>(backends));
    bar.layout(1920, 1080);

    // Open Quick Settings, then right-click the Wi-Fi tile: the network
    // picker must replace the QS panel (the tile's context action). The panel
    // is drawn once first so its tile bounds are laid out.
    bar.toggleQuickSettings();
    EXPECT_TRUE(bar.hasOpenOverlay());
    qypr::DetailedPopover const* qs = &bar.quickSettings();
    cairo_surface_t* surf = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 400, 500);
    cairo_t* cr = cairo_create(surf);
    qypr::Painter p(cr);
    bar.quickSettings().draw(p, qypr::nowMs() + 10000);
    const qypr::Rect wf = bar.quickSettings().findTileBounds("Wi-Fi");
    EXPECT_TRUE(wf.valid());
    bar.handlePointerButton(wf.x + (wf.w / 2.0), wf.y + (wf.h / 2.0), 0x111, true, 1000);
    EXPECT_TRUE(bar.hasOpenOverlay());
    EXPECT_TRUE(bar.popovers_.active() != nullptr);
    EXPECT_TRUE(bar.popovers_.active() != qs);  // QS replaced by the wifi picker
    cairo_destroy(cr);
    cairo_surface_destroy(surf);
}

// =============================================================================
// Phase 3c: Bluetooth Indicator Tests
// =============================================================================
TEST(StatusBarGeometryTopAndBottom) {
    qypr::EventLoop loop;
    struct Inv : qypr::Invalidator {
        void invalidate() override {}
    } inv;
    qypr::SystemBackends const b{};
    qypr::StatusBar bar(loop, inv, b);

    // Default (top): the strip sits `edgeMargin` below the top edge.
    qypr::BarGeometry top;
    top.height = 36;
    top.edgeMargin = 24;
    top.sideMargin = 48;
    top.bottom = false;
    bar.setGeometry(top);
    bar.layout(1920, 66);
    EXPECT_TRUE(std::fabs(bar.bounds.y - 24.0) < 1e-9);
    EXPECT_TRUE(std::fabs(bar.bounds.w - (1920 - 96)) < 1e-9);

    // Bottom: measured from screenH, so it pins to the lower edge — and stays
    // pinned when the host surface grows for an overlay (66 → 1200).
    qypr::BarGeometry bot = top;
    bot.bottom = true;
    bar.setGeometry(bot);
    bar.layout(1920, 66);
    EXPECT_TRUE(std::fabs(bar.bounds.y - (66 - 24 - 36)) < 1e-9);  // 6
    bar.layout(1920, 1200);
    EXPECT_TRUE(std::fabs(bar.bounds.y - (1200 - 24 - 36)) < 1e-9);  // 1140
}
TEST(PopoverGrowsAwayFromBarEdge) {
    // A bottom bar must open its panels upward, or they render off-screen.
    struct P : qypr::DetailedPopover {
        void draw(qypr::Painter& /*p*/, int64_t /*now*/) override {}
        [[nodiscard]] double contentHeight() const override { return 100.0; }
        [[nodiscard]] double contentWidth() const override { return 200.0; }
    } pop;

    pop.anchorX = 500;
    pop.anchorY = 60;
    pop.growUp = false;
    EXPECT_TRUE(std::fabs(pop.getBounds().y - 60.0) < 1e-9);   // hangs down
    EXPECT_TRUE(std::fabs(pop.getBounds().x - 300.0) < 1e-9);  // right-aligned to anchor

    pop.growUp = true;
    EXPECT_TRUE(std::fabs(pop.getBounds().y - (60.0 - 100.0)) < 1e-9);  // extends up
}

// -----------------------------------------------------------------------------
// Phase 10 — Session surface (media / notifications / power)
// -----------------------------------------------------------------------------
TEST(IndicatorWithNoBackendRendersImmediately) {
    // Registry previews and the offline --preview path supply no backends at
    // all; those must still draw their sample defaults rather than vanish.
    qypr::SystemBackends const none{};
    qypr::BatteryIndicator const battery(none);
    qypr::WifiIndicator const wifi(none);
    EXPECT_TRUE(battery.visible);
    EXPECT_TRUE(wifi.visible);
}

// -----------------------------------------------------------------------------
// Widget-level theme propagation (QYPR_DECOMPOSITION_PLAN step 4.5)
// A host-owned State must reach every widget it is cascaded to — indicators,
// the QS panel, and its tiles — not just loadThemeState() return values.
// -----------------------------------------------------------------------------
TEST(ThemePropagatesToStatusBarChildren) {
    qypr::EventLoop loop;
    struct DummyHost : qypr::RenderHost {
        void invalidate() override {}
        void requestUnlock() override {}
    } host;

    qypr::SystemBackends backends{};
    backends.sessionSurface = true;  // build internal QS tiles (header/power/…)
    qypr::StatusBar bar(loop, host, backends);

    qypr::theme::State live{};
    live.colors.primary = qypr::Color::fromHex("#010203");
    live.font.family = "PropagatedFont";
    live.statusbar.height = 42.0;
    live.statusbar.barTintAlpha = 0.55;

    bar.setTheme(live);

    // StatusBar owns a copy (derived panelSurfaceAlpha folded in)…
    EXPECT_EQ(bar.theme().font.family, std::string("PropagatedFont"));
    EXPECT_NEAR(bar.theme().statusbar.height, 42.0, 0.01);
    EXPECT_NEAR(bar.theme().colors.primary.r, 0x01 / 255.0, 0.01);
    EXPECT_NEAR(bar.theme().statusbar.barTintAlpha, 0.55, 0.01);
    // …and the copy (not the host's object) is what children read.
    EXPECT_TRUE(&bar.theme() != &live);

    // Every indicator in every zone sees the cascaded state.
    int indicators = 0;
    for (auto* list : {&bar.leftIndicators_, &bar.centerIndicators_, &bar.rightIndicators_}) {
        for (auto& ind : *list) {
            EXPECT_EQ(ind->theme().font.family, std::string("PropagatedFont"));
            EXPECT_NEAR(ind->theme().colors.primary.r, 0x01 / 255.0, 0.01);
            ++indicators;
        }
    }
    EXPECT_TRUE(indicators > 0);

    // The QS panel and every tile it owns (grid + header/power/…) follow.
    EXPECT_EQ(bar.quickSettings().theme().font.family, std::string("PropagatedFont"));
    int tiles = 0;
    for (auto& tile : bar.quickSettings().tiles_) {
        EXPECT_EQ(tile->theme().font.family, std::string("PropagatedFont"));
        ++tiles;
    }
    EXPECT_TRUE(tiles > 0);

    // A second re-theme re-cascades without rebuilding the widget tree.
    qypr::theme::State second{};
    second.font.family = "SecondFont";
    second.colors.primary = qypr::Color::fromHex("#aabbcc");
    bar.setTheme(second);
    EXPECT_EQ(bar.theme().font.family, std::string("SecondFont"));
    EXPECT_EQ(bar.quickSettings().theme().font.family, std::string("SecondFont"));
    for (auto* list : {&bar.leftIndicators_, &bar.centerIndicators_, &bar.rightIndicators_}) {
        for (auto& ind : *list) {
            EXPECT_EQ(ind->theme().font.family, std::string("SecondFont"));
        }
    }
    for (auto& tile : bar.quickSettings().tiles_) {
        EXPECT_EQ(tile->theme().font.family, std::string("SecondFont"));
    }
}
