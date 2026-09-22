// StateCacheStore.cpp - Path, directory, and atomic file persistence.
#include "system/StateCacheStore.hpp"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace qypr {

std::string StateCacheStore::cacheDir() {
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"); xdg != nullptr && *xdg != '\0') {
        return std::string(xdg) + "/qypr";
    }
    const char* home = std::getenv("HOME");
    return std::string(home != nullptr ? home : ".") + "/.cache/qypr";
}

std::string StateCacheStore::defaultPath() {
    return cacheDir() + "/bar-state";
}

bool StateCacheStore::ensureDir(const std::string& path) {
    std::string dir = path;
    // Strip the filename to get the directory.
    auto slash = dir.rfind('/');
    if (slash != std::string::npos && slash > 0) { dir = dir.substr(0, slash); }
    if (::mkdir(dir.c_str(), 0700) != 0) {
        if (errno == EEXIST) { return true; }
        return false;
    }
    return true;
}

bool StateCacheStore::writeAtomic(const std::string& path, const std::string& body) {
    if (!ensureDir(path)) { return false; }

    const std::string tmp = path + ".tmp";
    std::FILE* f = std::fopen(tmp.c_str(), "we");
    if (f == nullptr) { return false; }
    const size_t written = std::fwrite(body.data(), 1, body.size(), f);
    const bool ok = written == body.size() && std::fflush(f) == 0;
    std::fclose(f);
    if (!ok) {
        ::unlink(tmp.c_str());
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        return false;
    }
    return true;
}

}  // namespace qypr
