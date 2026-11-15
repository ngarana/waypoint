#include "waylaunch/switcher/switcher_previews.h"
#include "waylaunch/switcher/preview_scheduler.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <utility>

namespace waylaunch {

namespace {

uint64_t steady_clock_ms() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

} // namespace

SwitcherPreviews::SwitcherPreviews(AppSwitcherManager* manager, IToplevelBackend* toplevels,
                                   std::unique_ptr<IWindowCaptureBackend> capture_backend,
                                   int preview_height, bool live_previews, Clock now_ms)
    : manager_(manager), toplevels_(toplevels), capture_backend_(std::move(capture_backend)),
      preview_height_(std::max(64, preview_height)), live_previews_(live_previews),
      now_ms_(now_ms ? std::move(now_ms) : Clock{steady_clock_ms}) {
    if (capture_backend_) {
        capture_backend_->set_sink([this](uintptr_t handle_id, const CapturedFrame* frame) {
            handle_frame(handle_id, frame);
        });
    }
    if (toplevels_) {
        for (const ToplevelWindow& window : toplevels_->windows()) {
            active_state_[window.handle_id] = window.is_active;
        }
        toplevels_->add_observer(this);
    }
}

SwitcherPreviews::~SwitcherPreviews() {
    if (toplevels_) toplevels_->remove_observer(this);
    if (capture_backend_) capture_backend_->cancel_all();
}

bool SwitcherPreviews::available() const {
    return capture_backend_ && capture_backend_->available();
}

void SwitcherPreviews::on_show() {
    visible_ = true;
    if (!live_previews_) last_captured_.clear();
    tick(now());
}

void SwitcherPreviews::on_hide() {
    visible_ = false;
    if (capture_backend_) capture_backend_->cancel_all();
    in_flight_.clear();
}

void SwitcherPreviews::tick(uint64_t now_ms) {
    if (!visible_ || !available() || !manager_) return;
    if (toplevels_) {
        std::unordered_set<uintptr_t> live_handles;
        for (const ToplevelWindow& window : toplevels_->windows()) {
            live_handles.insert(window.handle_id);
        }
        cache_.retain_only(live_handles);
    }
    std::vector<uintptr_t> handles = ordered_handles();
    uintptr_t selected = 0;
    if (const AppGroup* group = manager_->selected_group()) selected = group->primary_handle();
    for (uintptr_t handle_id :
         PreviewScheduler::due(now_ms, handles, selected, in_flight_, last_captured_, failed_until_,
                               live_previews_)) {
        request(handle_id);
    }
}

void SwitcherPreviews::on_window_created(const ToplevelWindow& window) {
    active_state_[window.handle_id] = window.is_active;
}

void SwitcherPreviews::on_window_updated(const ToplevelWindow& window) {
    auto const it = active_state_.find(window.handle_id);
    bool const was_active = it != active_state_.end() && it->second;
    active_state_[window.handle_id] = window.is_active;
    if (was_active && !window.is_active) on_window_deactivated(window.handle_id);
}

void SwitcherPreviews::on_window_closed(uintptr_t handle_id) {
    if (capture_backend_) capture_backend_->cancel(handle_id);
    in_flight_.erase(handle_id);
    cache_.evict(handle_id);
    last_captured_.erase(handle_id);
    failed_until_.erase(handle_id);
    active_state_.erase(handle_id);
    if (on_change_) on_change_();
}

void SwitcherPreviews::on_window_deactivated(uintptr_t handle_id) {
    if (visible_ || !available() || in_flight_.contains(handle_id)) return;
    auto const candidate = PreviewScheduler::on_hidden_deactivate(handle_id);
    if (candidate) request(*candidate);
}

void SwitcherPreviews::request(uintptr_t handle_id) {
    if (!capture_backend_ || handle_id == 0 || in_flight_.size() >= 4 ||
        !in_flight_.insert(handle_id).second) {
        return;
    }
    capture_backend_->request(handle_id);
}

void SwitcherPreviews::handle_frame(uintptr_t handle_id, const CapturedFrame* frame) {
    if (in_flight_.erase(handle_id) == 0U) return;
    uint64_t const captured_at = now();
    if (frame && frame->source) {
        int const source_width = cairo_image_surface_get_width(frame->source);
        int const source_height = cairo_image_surface_get_height(frame->source);
        ThumbnailSurface thumbnail =
            make_thumbnail(frame->source, frame->y_invert,
                           static_cast<int>(std::lround(preview_height_ * 2.2)), preview_height_);
        if (thumbnail && source_width > 0 && source_height > 0) {
            cache_.put(handle_id, std::move(thumbnail),
                       static_cast<double>(source_width) / source_height, captured_at);
            failed_until_.erase(handle_id);
        } else {
            failed_until_[handle_id] = captured_at + 2000;
        }
    } else {
        failed_until_[handle_id] = captured_at + 2000;
    }
    last_captured_[handle_id] = captured_at;
    if (on_change_) on_change_();
}

std::vector<uintptr_t> SwitcherPreviews::ordered_handles() const {
    std::vector<uintptr_t> result;
    if (!manager_) return result;
    for (const AppGroup& group : manager_->app_groups()) {
        uintptr_t const handle_id = group.primary_handle();
        if (handle_id != 0) result.push_back(handle_id);
    }
    return result;
}

uint64_t SwitcherPreviews::now() const { return now_ms_(); }

} // namespace waylaunch
