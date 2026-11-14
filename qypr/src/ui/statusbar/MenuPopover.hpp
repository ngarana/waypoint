// MenuPopover.hpp - Generic menu renderer for MenuSource trees.
//
// Draws an application's com.canonical.dbusmenu, a bar-native menu, or both
// stacked (see MenuSource.hpp). Renders one menu level as a vertical list (separators, disabled
// items, check/radio toggles, submenu arrows) and drills into submenus in place with a back header
// — a single-popover navigation stack, so it fits the one-active- popover model. Leaf activation
// fires the item and asks the host to close.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "system/DbusMenuBackend.hpp"  // MenuNode
#include "ui/statusbar/DetailedPopover.hpp"
#include "ui/statusbar/MenuSource.hpp"

namespace qypr {

class DbusMenuBackend;

class MenuPopover : public DetailedPopover {
public:
    // `root` is the top level (children of menu id 0), already fetched.
    MenuPopover(DbusMenuBackend* backend, std::string service, std::string menuPath,
                std::vector<MenuNode> root, std::string title);
    // Any source (bar-native, composite): the root level is fetched here.
    MenuPopover(std::unique_ptr<MenuSource> source, std::string title);

    // True when the root level has at least one visible, non-separator row.
    [[nodiscard]] bool hasItems() const;

    double contentWidth() const override;
    double contentHeight() const override;
    void draw(Painter& p, int64_t now) override;
    bool handleClick(double x, double y) override;
    bool handleMotion(double x, double y) override;
    bool consumeCloseRequest() override;
    bool handleKey(uint32_t keysym) override;

private:
    struct Level {
        int32_t parentId;
        std::string title;
        std::vector<MenuNode> items;
    };
    struct Hit {
        Rect r;
        enum Kind { Back, Leaf, Submenu } kind;
        int32_t id;
        std::string label;
    };

    const Level& cur() const { return stack_.back(); }
    double rowsHeight() const;

    std::unique_ptr<MenuSource> source_;
    std::vector<Level> stack_;
    std::vector<Hit> hits_;  // rebuilt each draw, hit-tested on click
    double hoverX_ = -1, hoverY_ = -1;
    bool closeRequested_ = false;
    int selected_ = -1;
};

}  // namespace qypr
