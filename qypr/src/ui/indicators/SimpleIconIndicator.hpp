// SimpleIconIndicator.hpp - Display-only status bar applet carrying a single icon.
#pragma once

#include <string>

#include "core/Types.hpp"
#include "ui/statusbar/StatusIndicator.hpp"

namespace qypr {

class SimpleIconIndicator : public StatusIndicator {
public:
    SimpleIconIndicator(const SystemBackends& backends, const std::string& id, Zone zone,
                        int priority, std::string iconGlyph, std::string tooltipText,
                        std::string themedIconName = "");

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;
    bool onClick(double x, double y) override;

protected:
    SystemBackends backends_;
    std::string iconGlyph_;
    std::string tooltipText_;
    std::string themedIconName_;
};

}  // namespace qypr
