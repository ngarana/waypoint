// ToplevelStates.cpp - Shared foreign-toplevel state decoding.
#include "toplevel/ToplevelStates.hpp"

namespace qypr {

namespace {
// wlr-foreign-toplevel-management-unstable-v1.xml, enum "state".
constexpr uint32_t kStateMaximized = 0;
constexpr uint32_t kStateMinimized = 1;
constexpr uint32_t kStateActivated = 2;
constexpr uint32_t kStateFullscreen = 3;
}  // namespace

ToplevelStates decodeToplevelStates(const uint32_t* states, size_t count) {
    ToplevelStates out;
    for (size_t i = 0; states != nullptr && i < count; ++i) {
        switch (states[i]) {
            case kStateActivated:
                out.active = true;
                break;
            case kStateMinimized:
                out.minimized = true;
                break;
            case kStateMaximized:
                out.maximized = true;
                break;
            case kStateFullscreen:
                out.fullscreen = true;
                break;
            default:
                break;  // future compositor flags never break the walk
        }
    }
    return out;
}

}  // namespace qypr
