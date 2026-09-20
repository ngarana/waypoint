// MenuPopover.hpp - Generic menu renderer for com.canonical.dbusmenu trees.
//
// Renders one menu level as a vertical list (separators, disabled items,
// check/radio toggles, submenu arrows) and drills into submenus in place with a
// back header — a single-popover navigation stack, so it fits the one-active-
// popover model. Leaf activation fires the item and asks the host to close.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "system/DbusMenuBackend.hpp"  // MenuNode
#include "ui/statusbar/DetailedPopover.hpp"

namespace qypr {

class DbusMenuBackend;

class MenuPopover : public DetailedPopover {
public:
    // `root` is the top level (children of menu id 0), already fetched.
    MenuPopover(DbusMenuBackend* backend, std::string service, std::string menuPath,
                std::vector<MenuNode> root, std::string title);

    double contentWidth() const override;
    double contentHeight() const override;
    void draw(Painter& p, int64_t now) override;
    bool handleClick(double x, double y) override;
    bool handleDrag(double x, double y) override;
    bool consumeCloseRequest() override;

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

    DbusMenuBackend* backend_ = nullptr;
    std::string service_, menuPath_;
    std::vector<Level> stack_;
    std::vector<Hit> hits_;  // rebuilt each draw, hit-tested on click
    double hoverX_ = -1, hoverY_ = -1;
    bool closeRequested_ = false;
};

}  // namespace qypr
