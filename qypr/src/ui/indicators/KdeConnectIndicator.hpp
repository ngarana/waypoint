// KdeConnectIndicator.hpp - Status bar icon: KDE Connect
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class KdeConnectIndicator : public SimpleIconIndicator {
public:
    explicit KdeConnectIndicator(const SystemBackends& backends);
    bool onClick(double x, double y) override;
};

}  // namespace qypr
