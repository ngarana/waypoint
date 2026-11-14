// AdditionalIndicators.hpp - Dedicated convenience indicators for WhatsApp and email.
// Removable drives and phones have native modules (RemovableDevicesIndicator,
// PhoneLinkIndicator).
#pragma once

#include <string>
#include <vector>

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class WhatsappIndicator : public SimpleIconIndicator {
public:
    explicit WhatsappIndicator(const SystemBackends& backends);
    void poll(int64_t now) override;
    void onBackendUpdate() override;
    std::string icon() const override;
    std::string label() const override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;

private:
    void updateRunningState();
    bool running_ = false;
};

using WhatsAppIndicator = WhatsappIndicator;

class EmailIndicator : public SimpleIconIndicator {
public:
    explicit EmailIndicator(const SystemBackends& backends);
    void poll(int64_t now) override;
    void onBackendUpdate() override;
    std::string icon() const override;
    std::string label() const override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;

private:
    void updateRunningState();
    bool running_ = false;
};

using MailIndicator = EmailIndicator;
using MailboxIndicator = EmailIndicator;
using InboxIndicator = EmailIndicator;

}  // namespace qypr
