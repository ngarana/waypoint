#pragma once

#include "toplevel/ToplevelBackend.hpp"
#include "waylaunch/switcher/app_switcher_manager.h"
#include "waylaunch/switcher/thumbnail.h"
#include "waylaunch/switcher/window_capture.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace waylaunch {

class SwitcherPreviews : public IToplevelObserver {
  public:
    using Clock = std::function<uint64_t()>;
    using ChangeCallback = std::function<void()>;

    SwitcherPreviews(AppSwitcherManager* manager, IToplevelBackend* toplevels,
                     std::unique_ptr<IWindowCaptureBackend> capture_backend, int preview_height,
                     bool live_previews, Clock now_ms = {});
    ~SwitcherPreviews() override;

    void on_show();
    void on_hide();
    void tick(uint64_t now_ms);
    void on_window_closed(uintptr_t handle_id) override;
    void on_window_created(const ToplevelWindow& window) override;
    void on_window_updated(const ToplevelWindow& window) override;
    void on_window_deactivated(uintptr_t handle_id);

    const ThumbnailCache& cache() const { return cache_; }
    bool available() const;
    void set_change_callback(ChangeCallback callback) { on_change_ = std::move(callback); }

  private:
    void request(uintptr_t handle_id);
    void handle_frame(uintptr_t handle_id, const CapturedFrame* frame);
    std::vector<uintptr_t> ordered_handles() const;
    uint64_t now() const;

    AppSwitcherManager* manager_ = nullptr;
    IToplevelBackend* toplevels_ = nullptr;
    std::unique_ptr<IWindowCaptureBackend> capture_backend_;
    int preview_height_ = 160;
    bool live_previews_ = true;
    bool visible_ = false;
    Clock now_ms_;
    ChangeCallback on_change_;
    ThumbnailCache cache_;
    std::unordered_set<uintptr_t> in_flight_;
    std::unordered_map<uintptr_t, uint64_t> last_captured_;
    std::unordered_map<uintptr_t, uint64_t> failed_until_;
    std::unordered_map<uintptr_t, bool> active_state_;
};

} // namespace waylaunch
