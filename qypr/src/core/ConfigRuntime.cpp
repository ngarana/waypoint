// ConfigRuntime.cpp - Implementation of config coordinator.
#include "core/ConfigRuntime.hpp"

#include <algorithm>
#include <cstdio>

namespace qypr {

ConfigRuntime::ConfigRuntime(EventLoop& loop) : loop_(loop) {}

ConfigRuntime::~ConfigRuntime() {
    stopWatching();
}

Config ConfigRuntime::loadConfig(const std::string& path) {
    Config c;
    c.load(path);
    return c;
}

BarGeometry ConfigRuntime::readGeometry(const Config& c) {
    BarGeometry g;  // defaults = theme constants (the lockscreen strip)
    g.height = c.getDouble("bar", "height", g.height);
    g.edgeMargin = c.getDouble("bar", "margin", g.edgeMargin);
    g.sideMargin = c.getDouble("bar", "margin-side", g.sideMargin);
    const std::string pos = c.getString("bar", "position", "top");
    g.bottom = (pos == "bottom");
    if (pos != "top" && pos != "bottom") {
        std::fprintf(stderr, "qypr-bar: unknown position '%s' (want top|bottom); using top\n",
                     pos.c_str());
    }
    return g;
}

std::optional<IndicatorRegistry::ModuleSelection> ConfigRuntime::readModules(const Config& c) {
    const bool any = c.has("bar", "modules-left") || c.has("bar", "modules-center") ||
                     c.has("bar", "modules-right");
    if (!any) {
        return std::nullopt;  // no module keys: keep the compiled default set
    }

    IndicatorRegistry::ModuleSelection sel;
    sel.left = c.getList("bar", "modules-left");
    sel.center = c.getList("bar", "modules-center");
    sel.right = c.getList("bar", "modules-right");

    const auto known = IndicatorRegistry::instance().registeredIds();
    for (const auto* zone : {&sel.left, &sel.center, &sel.right}) {
        for (const auto& id : *zone) {
            if (std::ranges::find(known, id) == known.end()) {
                std::string all;
                for (const auto& k : known) { all += (all.empty() ? "" : ", ") + k; }
                std::fprintf(stderr, "qypr-bar: unknown module '%s' (have: %s)\n", id.c_str(),
                             all.c_str());
            }
        }
    }
    return sel;
}

void ConfigRuntime::startWatching(const std::string& path, ReloadCallback onReload) {
    stopWatching();
    watchedPath_ = path;
    onReload_ = std::move(onReload);
    if (!watchedPath_.empty()) {
        watcher_.watch(watchedPath_, [this] {
            if (onReload_) {
                Config c;
                c.load(watchedPath_);
                auto geom = readGeometry(c);
                auto mods = readModules(c);
                onReload_(c, geom, mods);
            }
        });
    }
}

void ConfigRuntime::stopWatching() {
    watcher_.stop();
    watchedPath_.clear();
}

void ConfigRuntime::reload(ReloadCallback onReload, const std::string& filename) {
    Config c = loadConfig(filename);
    auto geom = readGeometry(c);
    auto mods = readModules(c);
    if (onReload) { onReload(c, geom, mods); }
}

}  // namespace qypr
