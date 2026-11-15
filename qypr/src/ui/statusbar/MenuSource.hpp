// MenuSource.hpp - Where a MenuPopover's items come from.
//
// A menu level is a list of MenuNode (the dbusmenu shape: label, toggle,
// submenu flag). Three sources share that shape:
//
//   - DbusMenuSource: an application's com.canonical.dbusmenu (a tray app's
//     right-click menu, a launcher quicklist).
//   - NativeMenu: items the bar builds itself, each bound to a callback.
//   - CompositeMenu: several sources stacked into one menu, separated, with
//     ids remapped so a bar-native section and an application's own menu can
//     share a single popover (a module's actions above the app's menu).
//
// A source answers fetch(parent) for one level and activate(id) for a leaf;
// MenuPopover owns the navigation stack and never needs to know which kind of
// source it is drawing.
#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "system/DbusMenuBackend.hpp"  // MenuNode

namespace qypr {

class MenuSource {
public:
    virtual ~MenuSource() = default;
    // Direct children of `parentId` (0 = the root level).
    virtual std::vector<MenuNode> fetch(int32_t parentId) = 0;
    // Fire a leaf item.
    virtual void activate(int32_t id) = 0;
};

class DbusMenuSource : public MenuSource {
public:
    DbusMenuSource(DbusMenuBackend* backend, std::string service, std::string menuPath)
        : backend_(backend),
          service_(std::move(service)),
          menuPath_(std::move(menuPath)) {}

    std::vector<MenuNode> fetch(int32_t parentId) override;
    void activate(int32_t id) override;

private:
    DbusMenuBackend* backend_;
    std::string service_;
    std::string menuPath_;
};

// Bar-native items. Ids are assigned in insertion order and never reused, so
// a click always reaches the callback the row was drawn for. Every add returns
// the new item's id; pass a submenu's id as `parent` to fill that submenu.
class NativeMenu : public MenuSource {
public:
    // A leaf. `checked` is -1 for a plain item, 0/1 for a checkmark toggle.
    int32_t add(std::string label, std::function<void()> action, bool enabled = true,
                int checked = -1, int32_t parent = 0);
    int32_t separator(int32_t parent = 0);
    // A row that drills into its own level.
    int32_t submenu(std::string label, int32_t parent = 0);

    [[nodiscard]] bool empty() const { return items_.empty(); }

    std::vector<MenuNode> fetch(int32_t parentId) override;
    void activate(int32_t id) override;

private:
    struct Item {
        MenuNode node;
        int32_t parent = 0;
        std::function<void()> action;
    };
    int32_t append(MenuNode node, int32_t parent, std::function<void()> action);

    int32_t nextId_ = 1;
    std::vector<Item> items_;
};

// Sections stacked top to bottom, a separator between non-empty sections.
class CompositeMenu : public MenuSource {
public:
    void addSection(std::unique_ptr<MenuSource> section);
    [[nodiscard]] bool empty() const { return sections_.empty(); }

    std::vector<MenuNode> fetch(int32_t parentId) override;
    void activate(int32_t id) override;

private:
    struct Mapping {
        size_t section = 0;
        int32_t original = 0;
    };
    int32_t remap(size_t section, int32_t original);
    std::vector<MenuNode> remapLevel(size_t section, std::vector<MenuNode> nodes);

    std::vector<std::unique_ptr<MenuSource>> sections_;
    std::vector<Mapping> ids_;  // composite id - 1 → (section, original id)
};

}  // namespace qypr
