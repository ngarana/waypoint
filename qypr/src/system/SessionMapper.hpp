// SessionMapper.hpp - Infers which windows live on which workspace.
//
// Neither ext-workspace-v1 nor wlr-foreign-toplevel-management exposes a
// window↔workspace association, and qypr deliberately speaks no per-WM IPC.
// But a compositor's own focus behaviour leaks the mapping, continuously and
// for free: when you switch workspaces the destination's window becomes
// activated; when you move the focused window elsewhere it *stays* activated
// while a different workspace becomes active. Observing the pair
// (newly-activated toplevel, active workspace) across consecutive snapshots
// therefore yields ground-truth bindings without asking anyone.
//
// The four inference rules, applied on every ingest():
//   1. Birth      — a never-seen toplevel is assigned to the active workspace
//                   (compositors spawn windows on the focused one).
//   2. Bind       — a toplevel that just became activated is (re-)assigned to
//                   the active workspace. This single rule also catches moves:
//                   moving the focused window keeps it activated while the
//                   destination workspace flips to active.
//   3. Stickiness — windows with no fresh observation keep their last known
//                   home (minimized / background windows stay put).
//   4. Adoption   — when a workspace disappears (close / merge / renumber) its
//                   residents are adopted by the next workspace observed as
//                   active — in practice the neighbour that received them.
//
// Pure value-in/diff/out logic: no Wayland, no D-Bus, no I/O — trivially unit
// testable by feeding hand-built snapshots.

#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "system/ToplevelBackend.hpp"   // ToplevelSnapshot, ToplevelWindow
#include "system/WorkspaceBackend.hpp"  // WorkspaceSnapshot

namespace qypr {

// One workspace cluster in the merged view: the workspace plus the ids of the
// toplevels believed to live there, in stable toplevel arrival order.
struct SessionCluster {
    std::string name;
    bool active = false;
    bool urgent = false;
    std::vector<uint64_t> windows;
};

// The merged session picture the pager renders from.
struct SessionView {
    bool available = false;                // any of the two protocols present
    bool hasWorkspaces = false;            // ext-workspace-v1 usable (else flat taskbar)
    std::vector<SessionCluster> clusters;  // display order
    std::vector<uint64_t> unassigned;      // no home yet (rare)
    std::vector<ToplevelWindow> windows;   // full toplevel list, arrival order

    const ToplevelWindow* window(uint64_t id) const {
        for (const auto& w : windows)
            if (w.id == id) return &w;
        return nullptr;
    }
};

class SessionMapper {
public:
    // Diff `ws`/`tl` against the previously ingested pair, update the inferred
    // homes, rebuild the view. `nowMs` only orders rule 4 against rule 2 within
    // one call; passing nowMs() is fine.
    void ingest(const WorkspaceSnapshot& ws, const ToplevelSnapshot& tl);

    const SessionView& view() const { return view_; }

    // Inferred home of a window ("" = unassigned). Test/diagnostic aid.
    std::string homeOf(uint64_t id) const;

private:
    void rebuildView(const WorkspaceSnapshot& ws, const ToplevelSnapshot& tl);

    // windowId → workspace name; absent or "" = unassigned.
    std::unordered_map<uint64_t, std::string> home_;
    WorkspaceSnapshot lastWs_;
    ToplevelSnapshot lastTl_;
    bool haveLast_ = false;
    SessionView view_;
};

}  // namespace qypr
