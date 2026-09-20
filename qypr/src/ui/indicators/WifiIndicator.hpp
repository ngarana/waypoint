// WifiIndicator.hpp - Status bar WiFi signal icon + QS toggle tile.
//
// Pure consumer of WifiBackend snapshots; the Quick Settings toggle flips
// the radio through the backend (async WirelessEnabled write).

#pragma once

#include "ui/statusbar/StatusIndicator.hpp"
#include "system/WifiBackend.hpp"
#include <string>

namespace qypr {

class WifiIndicator : public StatusIndicator {
public:
    explicit WifiIndicator(const SystemBackends& backends);

    std::string icon() const override;
    std::string themedIcon() const override;
    std::string tooltip() const override;

    void onBackendUpdate() override;

    std::unique_ptr<QSTile> createTile() override;

    // Click the icon to open the network picker (nearby APs; join a saved one,
    // or disconnect the active one).
    bool hasDetailedView() const override { return backend_ != nullptr; }
    std::unique_ptr<DetailedPopover> createDetailedView() override;

private:
    WifiBackend* backend_ = nullptr;
    WifiSnapshot lastSnap_;
};

}  // namespace qypr
