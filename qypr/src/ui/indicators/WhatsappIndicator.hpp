// WhatsappIndicator.hpp - Status bar icon: WhatsApp
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class WhatsappIndicator : public SimpleIconIndicator {
public:
    explicit WhatsappIndicator(const SystemBackends& backends);
    std::string icon() const override;
    bool onClick(double x, double y) override;
    std::string label() const override;
};

}  // namespace qypr
