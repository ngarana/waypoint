// UsbEjectIndicator.hpp - Status bar icon: Eject USB device
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class UsbEjectIndicator : public SimpleIconIndicator {
public:
    explicit UsbEjectIndicator(const SystemBackends& backends);
    bool onClick(double x, double y) override;
};

}  // namespace qypr
