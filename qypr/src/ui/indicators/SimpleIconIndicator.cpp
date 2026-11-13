// SimpleIconIndicator.cpp
#include "ui/indicators/SimpleIconIndicator.hpp"

#include "system/DesktopIndex.hpp"

namespace qypr {

SimpleIconIndicator::SimpleIconIndicator(const SystemBackends& backends, const std::string& id,
                                         Zone zone, int priority, std::string iconGlyph,
                                         std::string tooltipText, std::string themedIconName)
    : StatusIndicator(id, zone, priority),
      backends_(backends),
      iconGlyph_(std::move(iconGlyph)),
      tooltipText_(std::move(tooltipText)),
      themedIconName_(std::move(themedIconName)) {
    visible = true;
}

std::string SimpleIconIndicator::icon() const {
    return iconGlyph_;
}
std::string SimpleIconIndicator::themedIcon() const {
    return themedIconName_;
}
std::string SimpleIconIndicator::tooltip() const {
    return tooltipText_;
}
bool SimpleIconIndicator::onClick(double, double) {
    return false;
}

}  // namespace qypr
