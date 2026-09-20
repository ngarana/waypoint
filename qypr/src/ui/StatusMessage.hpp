// StatusMessage.hpp - Authentication status line (port of StatusMessage.qml).

#pragma once

#include <string>

#include "render/Painter.hpp"

namespace qypr {

class StatusMessage {
public:
    std::string message;
    bool isError = false;

    Size measure(Painter& p) const;
    // Draw centred on centerX with its top at topY.
    void draw(Painter& p, double centerX, double topY) const;
};

}  // namespace qypr
