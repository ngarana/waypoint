// PhoneConnectIndicator.hpp - Status bar icon: Phone connect
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class PhoneConnectIndicator : public SimpleIconIndicator {
public:
    explicit PhoneConnectIndicator(const SystemBackends& backends);
    bool onClick(double x, double y) override;
};

}  // namespace qypr
