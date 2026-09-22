// StateCache.cpp - Thin facade delegating to Codec/Store/Coordinator.
#include "system/StateCache.hpp"

#include "system/StateCacheCodec.hpp"
#include "system/StateCacheCoordinator.hpp"
#include "system/StateCacheStore.hpp"

#include "core/EventLoop.hpp"

namespace qypr {

void StateCache::load() {
    codec_.load(path_);
    loaded_ = true;
}

void StateCache::seed(const SystemBackends& backends) {
    codec_.seed(backends);
}

void StateCache::track(EventLoop& loop, const SystemBackends& backends) {
    backends_ = &backends;
    coordinator_.track(loop, backends, codec_, path_);
}

void StateCache::noteChanged() {
    coordinator_.noteChanged();
}

void StateCache::flush() {
    coordinator_.flush();
}

std::string StateCache::serialize() const {
    if (backends_ == nullptr) { return {}; }
    return codec_.serialize(*backends_);
}

std::string StateCache::defaultPath() {
    return StateCacheStore::defaultPath();
}

}  // namespace qypr
