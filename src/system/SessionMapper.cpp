// SessionMapper.cpp - Focus-correlation residency inference (see header).
#include "system/SessionMapper.hpp"

#include <algorithm>
#include <ranges>

namespace qypr {

namespace {

std::string pickActive(const WorkspaceSnapshot& now, const WorkspaceSnapshot* prev) {
    const WorkspaceInfo* fallback = nullptr;
    for (const auto& w : now.workspaces) {
        if (!w.active) { continue; }
        if (fallback == nullptr) { fallback = &w; }
        // Multi-output compositors report one active workspace per output; the
        // one that *just* became active is the switch/move target and wins.
        const bool wasActive =
            prev != nullptr && std::ranges::any_of(prev->workspaces, [&](const WorkspaceInfo& p) {
                return p.name == w.name && p.active;
            });
        if (!wasActive) { return w.name; }
    }
    return fallback != nullptr ? fallback->name : std::string{};
}

const ToplevelWindow* findWin(const ToplevelSnapshot& snap, uint64_t id) {
    for (const auto& w : snap.windows) {
        if (w.id == id) { return &w; }
    }
    return nullptr;
}

bool hasWorkspace(const WorkspaceSnapshot& ws, const std::string& name) {
    return std::ranges::any_of(ws.workspaces,
                               [&](const WorkspaceInfo& w) { return w.name == name; });
}

}  // namespace

void SessionMapper::ingest(const WorkspaceSnapshot& ws, const ToplevelSnapshot& tl) {
    const std::string active = pickActive(ws, haveLast_ ? &lastWs_ : nullptr);
    const std::string prevActive = haveLast_ ? pickActive(lastWs_, nullptr) : std::string{};

    if (!ws.available) {
        home_.clear();  // no workspace signal at all: nothing to infer against
    }

    // Newly-arrived toplevels: birthplace heuristic (rule 1).
    // Freshly-activated toplevels: bind rule (rule 2).
    // A window that *stays* activated across an active-workspace change was
    // moved there while focused — carry it along (also rule 2's move form).
    const bool activeChanged = haveLast_ && ws.available && !active.empty() && active != prevActive;
    for (const auto& w : tl.windows) {
        const ToplevelWindow* prev = haveLast_ ? findWin(lastTl_, w.id) : nullptr;
        const bool born = prev == nullptr;
        const bool newlyActivated = w.active && (prev == nullptr || !prev->active);
        if (born && !active.empty()) { home_[w.id] = active; }
        if (newlyActivated && !active.empty()) { home_[w.id] = active; }
        if (activeChanged && w.active && prev != nullptr && prev->active) { home_[w.id] = active; }
    }

    // Forget closed windows.
    for (auto it = home_.begin(); it != home_.end();) {
        if (findWin(tl, it->first) == nullptr) {
            it = home_.erase(it);
        } else {
            ++it;
        }
    }

    // Adoption (rule 4): homes pointing at vanished workspaces go to limbo and
    // are adopted by whatever is active now (in practice the neighbour that
    // received them). Skipped without workspace info — homes are meaningless.
    if (ws.available && !ws.workspaces.empty()) {
        for (auto& [id, name] : home_) {
            if (!name.empty() && !hasWorkspace(ws, name)) { name.clear(); }
        }
        if (!active.empty()) {
            for (auto& [id, name] : home_) {
                if (name.empty()) { name = active; }
            }
        }
    }

    rebuildView(ws, tl);
    lastWs_ = ws;
    lastTl_ = tl;
    haveLast_ = true;
}

std::string SessionMapper::homeOf(uint64_t id) const {
    auto it = home_.find(id);
    return it == home_.end() ? std::string{} : it->second;
}

void SessionMapper::rebuildView(const WorkspaceSnapshot& ws, const ToplevelSnapshot& tl) {
    view_.available = tl.available || ws.available;
    view_.hasWorkspaces = ws.available && !ws.workspaces.empty();
    view_.clusters.clear();
    view_.unassigned.clear();
    view_.windows = tl.windows;

    if (view_.hasWorkspaces) {
        for (const auto& info : ws.workspaces) {
            SessionCluster c;
            c.name = info.name;
            c.active = info.active;
            c.urgent = info.urgent;
            for (const auto& w : tl.windows) {
                if (homeOf(w.id) == info.name) { c.windows.push_back(w.id); }
            }
            view_.clusters.push_back(std::move(c));
        }
    }
    for (const auto& w : tl.windows) {
        if (homeOf(w.id).empty()) { view_.unassigned.push_back(w.id); }
    }
}

}  // namespace qypr
