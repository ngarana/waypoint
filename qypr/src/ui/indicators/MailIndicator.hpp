// MailIndicator.hpp - Status bar icon: Mail
#pragma once

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class MailIndicator : public SimpleIconIndicator {
public:
    explicit MailIndicator(const SystemBackends& backends);
    std::string icon() const override;
    bool onClick(double x, double y) override;
    std::string label() const override;
};

}  // namespace qypr
