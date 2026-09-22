// BarRuntime.cpp - Implementation of bar runtime coordinator.
#include "core/BarRuntime.hpp"

#include <cstdio>

#include "core/BarBackendLifecycle.hpp"
#include "core/BarSurfaceController.hpp"
#include "core/ConfigRuntime.hpp"
#include "core/ThemeRuntime.hpp"

namespace qypr {

BarRuntime::BarRuntime(EventLoop& loop, BarDisplay& display,
                       BarSurfaceController& surfaceController, ConfigRuntime& configRuntime,
                       ThemeRuntime& themeRuntime, BarBackendLifecycle& backendLifecycle)
    : loop_(loop),
      display_(display),
      surfaceController_(surfaceController),
      configRuntime_(configRuntime),
      themeRuntime_(themeRuntime),
      backendLifecycle_(backendLifecycle) {}

int BarRuntime::run(InputSink* sink, RenderFn renderFn, AnimatingFn animatingFn,
                    std::function<void()> setupProtocols, std::function<void()> onFirstFrame) {
    if (!display_.connect()) {
        std::fprintf(stderr, "qypr-bar: no Wayland display or no wlr-layer-shell support\n");
        return 1;
    }

    display_.setInputSink(sink);
    display_.setRenderFn(std::move(renderFn));
    display_.setAnimatingFn(std::move(animatingFn));

    if (setupProtocols) { setupProtocols(); }

    display_.roundtrip();

    if (onFirstFrame) { onFirstFrame(); }

    loop_.run();
    return 0;
}

void BarRuntime::quit() {
    loop_.quit();
}

}  // namespace qypr
