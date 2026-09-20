// test_desktop.cpp - Desktop entry parsing/search.
// Split verbatim from tests/unit_tests.cpp; bodies unchanged.
#include "test_framework.hpp"

TEST(DesktopIndexParsesEntry) {
    qypr::DesktopEntry e;
    const std::string ok = "[Desktop Entry]\n"
                           "Type=Application\n"
                           "Name=Firefox\n"
                           "Name[de]=Feuerfuchs\n"
                           "Exec=firefox %u\n"
                           "Icon=firefox\n"
                           "Terminal=false\n";
    EXPECT_TRUE(qypr::DesktopIndex::parseEntry(ok, e));
    EXPECT_EQ(e.name, std::string("Firefox"));  // unlocalized Name wins
    EXPECT_EQ(e.exec, std::string("firefox"));  // %u stripped
    EXPECT_EQ(e.icon, std::string("firefox"));
    EXPECT_FALSE(e.terminal);

    // NoDisplay, Hidden, and non-Application entries are skipped.
    qypr::DesktopEntry skip;
    EXPECT_FALSE(qypr::DesktopIndex::parseEntry(
        "[Desktop Entry]\nType=Application\nName=X\nExec=x\nNoDisplay=true\n", skip));
    EXPECT_FALSE(qypr::DesktopIndex::parseEntry(
        "[Desktop Entry]\nType=Application\nName=X\nExec=x\nHidden=true\n", skip));
    EXPECT_FALSE(
        qypr::DesktopIndex::parseEntry("[Desktop Entry]\nType=Link\nName=X\nURL=y\n", skip));
    // A trailing [Desktop Action] group must not leak into the main entry.
    qypr::DesktopEntry e2;
    EXPECT_TRUE(
        qypr::DesktopIndex::parseEntry("[Desktop Entry]\nType=Application\nName=Term\nExec=st\n"
                                       "[Desktop Action new]\nName=New\nExec=st -e other\n",
                                       e2));
    EXPECT_EQ(e2.exec, std::string("st"));
}
TEST(DesktopIndexCleanExec) {
    EXPECT_EQ(qypr::DesktopIndex::cleanExec("app %F --flag"), std::string("app --flag"));
    EXPECT_EQ(qypr::DesktopIndex::cleanExec("app %U"), std::string("app"));
    EXPECT_EQ(qypr::DesktopIndex::cleanExec("100%% real"), std::string("100% real"));
}
TEST(DesktopIndexSearchRanksPrefix) {
    // Build the index by hand via parseEntry → a private path is not needed; we
    // exercise search() against a live load() instead (see below), so here we
    // just confirm an empty query on a fresh index is safe.
    qypr::DesktopIndex const idx;
    EXPECT_TRUE(idx.search("anything").empty());  // nothing loaded → empty
    EXPECT_TRUE(idx.entries().empty());
}
