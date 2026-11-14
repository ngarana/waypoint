// MenuSource.cpp - dbusmenu, bar-native and composite menu sources.
#include "ui/statusbar/MenuSource.hpp"

#include <algorithm>
#include <iterator>

namespace qypr {

// ─── DbusMenuSource ─────────────────────────────────────────────────────────

std::vector<MenuNode> DbusMenuSource::fetch(int32_t parentId) {
    if (backend_ == nullptr) return {};
    return backend_->fetch(service_, menuPath_, parentId);
}

void DbusMenuSource::activate(int32_t id) {
    if (backend_ != nullptr) backend_->clicked(service_, menuPath_, id);
}

// ─── NativeMenu ─────────────────────────────────────────────────────────────

int32_t NativeMenu::append(MenuNode node, int32_t parent, std::function<void()> action) {
    node.id = nextId_++;
    // A child makes its parent a submenu even when it was added as a leaf.
    if (parent != 0) {
        for (auto& it : items_) {
            if (it.node.id == parent) it.node.hasSubmenu = true;
        }
    }
    items_.push_back({std::move(node), parent, std::move(action)});
    return items_.back().node.id;
}

int32_t NativeMenu::add(std::string label, std::function<void()> action, bool enabled, int checked,
                        int32_t parent) {
    MenuNode n;
    n.label = std::move(label);
    n.enabled = enabled;
    if (checked >= 0) {
        n.toggleType = "checkmark";
        n.toggleState = checked;
    }
    return append(std::move(n), parent, std::move(action));
}

int32_t NativeMenu::separator(int32_t parent) {
    MenuNode n;
    n.separator = true;
    n.enabled = false;
    return append(std::move(n), parent, nullptr);
}

int32_t NativeMenu::submenu(std::string label, int32_t parent) {
    MenuNode n;
    n.label = std::move(label);
    n.hasSubmenu = true;
    return append(std::move(n), parent, nullptr);
}

std::vector<MenuNode> NativeMenu::fetch(int32_t parentId) {
    std::vector<MenuNode> out;
    for (const auto& it : items_) {
        if (it.parent == parentId) out.push_back(it.node);
    }
    return out;
}

void NativeMenu::activate(int32_t id) {
    const auto it = std::ranges::find_if(items_, [&](const Item& i) { return i.node.id == id; });
    if (it != items_.end() && it->action) it->action();
}

// ─── CompositeMenu ──────────────────────────────────────────────────────────

void CompositeMenu::addSection(std::unique_ptr<MenuSource> section) {
    if (section) sections_.push_back(std::move(section));
}

int32_t CompositeMenu::remap(size_t section, int32_t original) {
    // Reuse an existing mapping so a level fetched twice (navigating back and
    // forth) keeps stable ids — the popover's hover/selection state survives.
    for (size_t i = 0; i < ids_.size(); ++i) {
        if (ids_[i].section == section && ids_[i].original == original) {
            return static_cast<int32_t>(i + 1);
        }
    }
    ids_.push_back({section, original});
    return static_cast<int32_t>(ids_.size());
}

std::vector<MenuNode> CompositeMenu::remapLevel(size_t section, std::vector<MenuNode> nodes) {
    for (auto& n : nodes) {
        n.id = remap(section, n.id);
        n.children.clear();  // levels are fetched lazily, never pre-expanded
    }
    return nodes;
}

std::vector<MenuNode> CompositeMenu::fetch(int32_t parentId) {
    if (parentId == 0) {
        std::vector<MenuNode> out;
        for (size_t s = 0; s < sections_.size(); ++s) {
            auto level = remapLevel(s, sections_[s]->fetch(0));
            const bool anyVisible = std::ranges::any_of(
                level, [](const MenuNode& n) { return n.visible && !n.separator; });
            if (!anyVisible) continue;
            if (!out.empty()) {
                MenuNode sep;
                sep.separator = true;
                sep.enabled = false;
                // A separator id that maps to nothing activatable.
                sep.id = remap(sections_.size(), -static_cast<int32_t>(s) - 1);
                out.push_back(std::move(sep));
            }
            std::ranges::move(level, std::back_inserter(out));
        }
        return out;
    }
    const auto index = static_cast<size_t>(parentId - 1);
    if (parentId < 1 || index >= ids_.size()) return {};
    const auto [section, original] = ids_[index];
    if (section >= sections_.size()) return {};
    return remapLevel(section, sections_[section]->fetch(original));
}

void CompositeMenu::activate(int32_t id) {
    const auto index = static_cast<size_t>(id - 1);
    if (id < 1 || index >= ids_.size()) return;
    const auto [section, original] = ids_[index];
    if (section < sections_.size()) sections_[section]->activate(original);
}

}  // namespace qypr
