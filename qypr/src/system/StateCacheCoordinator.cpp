// StateCacheCoordinator.cpp - Event-loop debounce and backend sampling.
#include "system/StateCacheCoordinator.hpp"

#include "system/StateCacheCodec.hpp"
#include "system/StateCacheStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>

namespace qypr {

void StateCacheCoordinator::track(EventLoop& loop, const SystemBackends& backends,
                                  StateCacheCodec& codec, const std::string& path) {
    loop_ = &loop;
    backends_ = &backends;
    codec_ = &codec;
    path_ = path;
}

void StateCacheCoordinator::noteChanged() {
    if (loop_ == nullptr || timer_ >= 0) { return; }
    timer_ = loop_->addTimer(kDebounceMs, /*repeat=*/false, [this] {
        timer_ = -1;
        flush();
    });
}

void StateCacheCoordinator::flush() {
    if (backends_ == nullptr || codec_ == nullptr) { return; }
    const std::string body = codec_->serialize(*backends_);
    if (body.empty() || body == lastWritten_) { return; }

    if (!StateCacheStore::writeAtomic(path_, body)) { return; }
    lastWritten_ = body;
}

}  // namespace qypr
