// EmailIndicator.hpp - Status bar icon: Email
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class EmailIndicator : public SimpleIconIndicator {
public:
    explicit EmailIndicator(const SystemBackends& backends);
    std::string icon() const override;
    bool onClick(double x, double y) override;
    std::string label() const override;
};

}  // namespace qypr
