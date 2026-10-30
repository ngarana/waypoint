// UsbIndicator.hpp - Status bar icon: USB devices
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class UsbIndicator : public SimpleIconIndicator {
public:
    explicit UsbIndicator(const SystemBackends& backends);
    bool onClick(double x, double y) override;
};

}  // namespace qypr
