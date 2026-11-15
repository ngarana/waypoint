#pragma once

#include <cairo/cairo.h>
#include <cstdint>
#include <functional>

namespace waylaunch {

struct CapturedFrame {
    cairo_surface_t* source = nullptr;
    bool y_invert = false;
};

class IWindowCaptureBackend {
  public:
    using Sink = std::function<void(uintptr_t, const CapturedFrame*)>;

    virtual ~IWindowCaptureBackend() = default;
    virtual bool available() const = 0;
    virtual void set_sink(Sink sink) = 0;
    virtual void request(uintptr_t handle_id) = 0;
    virtual void cancel(uintptr_t handle_id) = 0;
    virtual void cancel_all() = 0;
};

} // namespace waylaunch
