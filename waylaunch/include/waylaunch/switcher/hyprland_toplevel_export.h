#pragma once

#include "waylaunch/switcher/window_capture.h"
#include <memory>

namespace waylaunch {

class WaylandCore;
class WlrForeignToplevelBackend;

class HyprlandToplevelExportBackend final : public IWindowCaptureBackend {
  public:
    HyprlandToplevelExportBackend(WaylandCore* wayland, WlrForeignToplevelBackend* toplevels);
    ~HyprlandToplevelExportBackend() override;

    bool available() const override;
    void set_sink(Sink sink) override;
    void request(uintptr_t handle_id) override;
    void cancel(uintptr_t handle_id) override;
    void cancel_all() override;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace waylaunch
