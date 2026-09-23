// test_qs.cpp - Unit tests for the QuickSettingsModel tile policy.

#include "test_framework.hpp"

#include "ui/statusbar/QuickSettingsModel.hpp"

namespace {

std::unique_ptr<qypr::QSTile> makeToggle(const std::string& title, qypr::QSTile::Role role) {
    return std::make_unique<qypr::QSToggleTile>(
        title, "i", []() { return true; }, []() {}, nullptr,
        qypr::Color{.r = 0, .g = 0, .b = 0, .a = 0}, role);
}

}  // namespace

TEST(QuickSettingsModelOrdering) {
    qypr::QuickSettingsModel model;
    model.addTile(makeToggle("B", qypr::QSTile::Role::Bluetooth));
    model.addTile(makeToggle("D", qypr::QSTile::Role::Dnd));
    EXPECT_EQ(static_cast<int>(model.tiles().size()), 2);
    // Insertion order is grid order.
    EXPECT_TRUE(model.tiles().at(0)->role() == qypr::QSTile::Role::Bluetooth);
    EXPECT_TRUE(model.tiles().at(1)->role() == qypr::QSTile::Role::Dnd);

    model.clearTiles();
    EXPECT_TRUE(model.tiles().empty());
    EXPECT_FALSE(model.hasRole(qypr::QSTile::Role::Bluetooth));
}

TEST(QuickSettingsModelDedupe) {
    qypr::QuickSettingsModel model;
    model.addTile(makeToggle("BT", qypr::QSTile::Role::Bluetooth));
    model.addTile(makeToggle("Wifi", qypr::QSTile::Role::Wifi));
    model.addTile(makeToggle("Vol", qypr::QSTile::Role::Volume));
    model.addTile(makeToggle("Dnd", qypr::QSTile::Role::Dnd));
    // A null entry is kept by addTile but never matches the filter.
    model.addTile(nullptr);

    // Only the panel-owned singles (Wifi, Volume) drop; the rest stay.
    EXPECT_EQ(model.removePanelOwnedDuplicates(), 2U);
    EXPECT_EQ(static_cast<int>(model.tiles().size()), 3);
    EXPECT_TRUE(model.hasRole(qypr::QSTile::Role::Bluetooth));
    EXPECT_TRUE(model.hasRole(qypr::QSTile::Role::Dnd));
    EXPECT_FALSE(model.hasRole(qypr::QSTile::Role::Wifi));
    EXPECT_FALSE(model.hasRole(qypr::QSTile::Role::Volume));

    // Second run removes nothing.
    EXPECT_EQ(model.removePanelOwnedDuplicates(), 0U);
}

TEST(QuickSettingsModelFindByRole) {
    qypr::QuickSettingsModel model;
    EXPECT_TRUE(model.findByRole(qypr::QSTile::Role::Dnd) == nullptr);
    model.addTile(makeToggle("D1", qypr::QSTile::Role::Dnd));
    model.addTile(makeToggle("D2", qypr::QSTile::Role::Dnd));
    // First match wins.
    const qypr::QSTile* found = model.findByRole(qypr::QSTile::Role::Dnd);
    EXPECT_TRUE(found != nullptr);
    EXPECT_TRUE(found == model.tiles().at(0).get());
    EXPECT_TRUE(model.findByRole(qypr::QSTile::Role::Screenshot) == nullptr);
}

TEST(QuickSettingsModelThemeCascade) {
    qypr::QuickSettingsModel model;
    model.addTile(makeToggle("B", qypr::QSTile::Role::Bluetooth));
    model.addTile(nullptr);
    // Cascading a theme must not crash on null entries and must reach tiles.
    qypr::theme::State state;
    model.setTheme(state);
    EXPECT_EQ(static_cast<int>(model.tiles().size()), 2);
}
