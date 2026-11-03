// AdditionalIndicators.hpp - Dedicated convenience indicators for USB, KDE Connect, WhatsApp,
// Email.
#pragma once

#include <string>
#include <vector>

#include "ui/indicators/SimpleIconIndicator.hpp"

namespace qypr {

class UsbIndicator : public SimpleIconIndicator {
public:
    explicit UsbIndicator(const SystemBackends& backends);
    void poll(int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;

private:
    void updatePresence();
    std::vector<std::string> mountedDrives_;
};

class UsbEjectIndicator : public SimpleIconIndicator {
public:
    explicit UsbEjectIndicator(const SystemBackends& backends);
    void poll(int64_t now) override;
    void onBackendUpdate() override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;

private:
    void updatePresence();
    std::vector<std::string> mountedDrives_;
};

class KdeConnectIndicator : public SimpleIconIndicator {
public:
    explicit KdeConnectIndicator(const SystemBackends& backends);
    void poll(int64_t now) override;
    void onBackendUpdate() override;
    std::string label() const override;
    bool onClick(double x, double y) override;
    bool onSecondaryClick(double x, double y) override;

private:
    void updateRunningState();
    pid_t cachedPid_ = 0;
    int64_t lastCheckMs_ = 0;
    bool running_ = false;
};

using PhoneConnectIndicator = KdeConnectIndicator;

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
using UsbManagerIndicator = UsbIndicator;
using EjectIndicator = UsbEjectIndicator;
using KdeconnectIndicator = KdeConnectIndicator;

}  // namespace qypr
