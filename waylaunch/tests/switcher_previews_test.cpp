#include "waylaunch/switcher/switcher_previews.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

using namespace waylaunch;

class FakeCaptureBackend : public IWindowCaptureBackend {
  public:
    bool available() const override { return true; }
    void set_sink(Sink sink) override { sink_ = std::move(sink); }
    void request(uintptr_t handle_id) override {
        requests.push_back(handle_id);
        in_flight.insert(handle_id);
    }
    void cancel(uintptr_t handle_id) override {
        cancellations.push_back(handle_id);
        in_flight.erase(handle_id);
    }
    void cancel_all() override {
        ++cancel_all_count;
        in_flight.clear();
    }
    void complete(uintptr_t handle_id, bool success) {
        assert(in_flight.erase(handle_id) == 1);
        cairo_surface_t* surface = nullptr;
        CapturedFrame frame;
        if (success) {
            surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 32, 16);
            frame.source = surface;
        }
        sink_(handle_id, success ? &frame : nullptr);
        if (surface) cairo_surface_destroy(surface);
    }

    Sink sink_;
    std::vector<uintptr_t> requests;
    std::vector<uintptr_t> cancellations;
    std::unordered_set<uintptr_t> in_flight;
    int cancel_all_count = 0;
};

class MockToplevelBackend : public IToplevelBackend {
  public:
    bool init(wl_display*, wl_registry*) override { return true; }
    void add_observer(IToplevelObserver* observer) override { observers.push_back(observer); }
    void remove_observer(IToplevelObserver* observer) override { std::erase(observers, observer); }
    void activate(uintptr_t, wl_seat*) override {}
    void close(uintptr_t handle_id) override {
        std::erase_if(windows_,
                      [handle_id](const auto& window) { return window.handle_id == handle_id; });
        for (auto* observer : observers) observer->on_window_closed(handle_id);
    }
    void set_minimized(uintptr_t, bool) override {}
    const std::vector<ToplevelWindow>& windows() const override { return windows_; }
    void add_window(uintptr_t handle_id, std::string title, bool active) {
        ToplevelWindow window{
            .handle_id = handle_id,
            .app_id = title,
            .title = std::move(title),
            .icon_name = "",
            .is_active = active,
        };
        windows_.push_back(window);
        for (auto* observer : observers) observer->on_window_created(window);
    }
    void set_active(uintptr_t handle_id, bool active) {
        for (auto& window : windows_) {
            if (window.handle_id == handle_id) {
                window.is_active = active;
                for (auto* observer : observers) observer->on_window_updated(window);
                return;
            }
        }
    }

    std::vector<IToplevelObserver*> observers;

  private:
    std::vector<ToplevelWindow> windows_;
};

int main() {
    MockToplevelBackend toplevels;
    toplevels.add_window(10, "active", true);
    toplevels.add_window(20, "previous", false);
    AppSwitcherManager manager(&toplevels);
    manager.set_group_by_app(false);
    uint64_t now_ms = 100;
    auto capture = std::make_unique<FakeCaptureBackend>();
    FakeCaptureBackend* fake = capture.get();
    SwitcherPreviews previews(&manager, &toplevels, std::move(capture), 160, true,
                              [&now_ms] { return now_ms; });
    int changes = 0;
    previews.set_change_callback([&changes] { ++changes; });

    manager.show();
    previews.on_show();
    assert(fake->requests.size() == 2);
    uintptr_t const selected = manager.selected_group()->primary_handle();
    assert(fake->requests.front() == selected);
    fake->complete(selected, true);
    const auto* cached = previews.cache().get(selected);
    assert(cached && cached->surface);
    assert(changes == 1);

    now_ms += 66;
    previews.tick(now_ms);
    assert(fake->requests.size() == 3);
    assert(fake->requests.back() == selected);
    fake->complete(selected, false);
    assert(previews.cache().get(selected) == cached);

    previews.on_hide();
    assert(fake->cancel_all_count == 1);
    assert(previews.cache().get(selected) == cached);
    assert(fake->in_flight.empty());

    now_ms += 3000;
    previews.on_show();
    assert(fake->in_flight.contains(selected));
    toplevels.close(selected);
    assert(!previews.cache().get(selected));
    assert(std::ranges::find(fake->cancellations, selected) != fake->cancellations.end());

    previews.on_hide();
    fake->requests.clear();
    fake->cancellations.clear();
    toplevels.set_active(10, false);
    assert(fake->requests.size() == 1);
    assert(fake->requests.front() == 10);

    std::cout << "Switcher preview tests passed\n";
}
