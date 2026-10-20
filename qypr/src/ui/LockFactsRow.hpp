// LockFactsRow.hpp - The session facts line under the lock screen's clock.
//
// The redesign mock carries a ticker row of labelled values under its
// chronometer (EXT TEMP / ATMOS / RAD FLUX). qypr has no sensors to report
// there, so the row carries the facts the lock screen actually owns: the
// fingerprint state, the PAM stack in use, and the number of pending
// notifications. Purely presentational: the renderer pushes the items.

#pragma once

#include <string>
#include <vector>

#include "core/Types.hpp"
#include "render/Painter.hpp"
#include "ui/Widget.hpp"

namespace qypr {

class Painter;

class LockFactsRow : public Widget {
public:
    struct Item {
        std::string label;      // "FINGERPRINT"
        std::string value;      // "READY"
        Color dot{0, 0, 0, 0};  // alpha 0 -> the theme's accent colour
    };

    std::vector<Item> items;

    Size measure(Painter& p) const;
    void draw(Painter& p, int64_t now) override;
};

}  // namespace qypr
