// StateCacheStore.hpp - Path, directory, and atomic file persistence
// for the state cache.
//
// Isolated from StateCacheCodec (typed conversion) and
// StateCacheCoordinator (debounce/sampling). This class owns the
// reliability boundary: directory creation, temporary-file writes,
// and atomic rename. Individual backends should not duplicate this
// logic — it is a correct reliability boundary.

#pragma once

#include <string>

namespace qypr {

class StateCacheStore {
public:
    // $XDG_CACHE_HOME/qypr/bar-state, else $HOME/.cache/qypr/bar-state.
    static std::string defaultPath();

    // Write `body` to `path` atomically: write to a temp file in the
    // same directory, then rename. The caller must ensure the directory
    // exists (ensureDir() or an equivalent).
    static bool writeAtomic(const std::string& path, const std::string& body);

    // Create the cache directory (and parents) if it does not exist.
    // No-op if it already exists. Returns false on unrecoverable error.
    static bool ensureDir(const std::string& path);

private:
    static std::string cacheDir();
};

}  // namespace qypr
