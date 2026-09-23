// ConfigRuntime.hpp - Runtime coordinator for config watching, geometry parsing,
// and module selection reload.
#pragma once

#include <functional>
#include <optional>
#include <string>

#include "core/Config.hpp"
#include "core/ConfigWatcher.hpp"
#include "core/EventLoop.hpp"
#include "ui/statusbar/IndicatorRegistry.hpp"
#include "ui/statusbar/StatusBar.hpp"

namespace qypr {

class ConfigRuntime {
public:
    using ReloadCallback =
        std::function<void(const Config&, const BarGeometry&,
                           const std::optional<IndicatorRegistry::ModuleSelection>&)>;

    explicit ConfigRuntime(EventLoop& loop);
    ~ConfigRuntime();

    ConfigRuntime(const ConfigRuntime&) = delete;
    ConfigRuntime& operator=(const ConfigRuntime&) = delete;

    static Config loadConfig(const std::string& path = "");
    static BarGeometry readGeometry(const Config& c);
    static std::optional<IndicatorRegistry::ModuleSelection> readModules(const Config& c);
    static int reservedFor(const BarGeometry& g) {
        return static_cast<int>(g.edgeMargin + g.height + 6.0);
    }

    void startWatching(const std::string& path, ReloadCallback onReload);
    void stopWatching();

    // Trigger an immediate manual reload with the specified path.
    void reload(const ReloadCallback& onReload, const std::string& filename = "");

private:
    EventLoop& loop_;
    ConfigWatcher watcher_{loop_};
    std::string watchedPath_;
    ReloadCallback onReload_;
};

}  // namespace qypr
