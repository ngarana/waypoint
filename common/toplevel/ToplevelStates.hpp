// ToplevelStates.hpp - Shared wlr-foreign-toplevel state decoding.
//
// Both suite clients (qypr's ToplevelBackend, waylaunch's
// WlrForeignToplevelBackend) used to hand-roll this uint32 array walk with
// different enum spellings and different flag coverage — the "state flags"
// divergence from ARCHITECTURE_REVIEW finding 4. One decoder, tested once,
// mapped onto each client's own model at its boundary; both integrations
// (snapshot vs observer) stay untouched.
//
// Flag values are protocol-stable (0=maximized, 1=minimized, 2=activated,
// 3=fullscreen); see the canonical common/protocols/
// wlr-foreign-toplevel-management-unstable-v1.xml. Unknown values are
// ignored so a future compositor flag can never break the walk.
#pragma once

#include <cstddef>
#include <cstdint>

namespace qypr {

struct ToplevelStates {
    bool active = false;
    bool minimized = false;
    bool maximized = false;
    bool fullscreen = false;

    bool operator==(const ToplevelStates&) const = default;
};

ToplevelStates decodeToplevelStates(const uint32_t* states, size_t count);

}  // namespace qypr
