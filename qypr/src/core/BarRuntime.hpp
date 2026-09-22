// BarRuntime.hpp - Runtime coordinator for qypr-bar lifecycle.
#pragma once

#include <functional>

#include "core/EventLoop.hpp"
#include "core/Interfaces.hpp"
#include "wayland/BarDisplay.hpp"

namespace qypr {

class BarSurfaceController;
class ConfigRuntime;
class ThemeRuntime;
class BarBackendLifecycle;

class BarRuntime {
public:
    BarRuntime(EventLoop& loop, BarDisplay& display, BarSurfaceController& surfaceController,
               ConfigRuntime& configRuntime, ThemeRuntime& themeRuntime,
               BarBackendLifecycle& backendLifecycle);

    // Runs the bar: connects display, attaches render/input hooks, pumps first
    // roundtrip, starts delayed backends, and enters the event loop.
    int run(InputSink* sink, RenderFn renderFn, AnimatingFn animatingFn,
            std::function<void()> setupProtocols, std::function<void()> onFirstFrame);

    void quit();

private:
    EventLoop& loop_;
    BarDisplay& display_;
    BarSurfaceController& surfaceController_;
    ConfigRuntime& configRuntime_;
    ThemeRuntime& themeRuntime_;
    BarBackendLifecycle& backendLifecycle_;
};

}  // namespace qypr
