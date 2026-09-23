// WifiSnapshotReducer.hpp - Pure WiFi snapshot logic (bus-free).
//
// Every function here is a verbatim extraction of a WifiBackend shaping step:
// no sd-bus types, no I/O, no backend state. The facade assembles a
// WifiFetchState from chain results and publishes reducePublished(); user
// commands go through WifiOperations.

#pragma once

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "system/WifiModel.hpp"

namespace qypr {

// Everything publish() reads, assembled by the facade from chain state.
struct WifiFetchState {
    bool haveDevice = false;
    bool wirelessEnabled = false;
    bool scanning = false;
    uint32_t deviceState = 0;  // 100 = NM_DEVICE_STATE_ACTIVATED
    std::string apSsid;
    int apStrength = 0;
    std::vector<WifiAp> networks;
};

// publish() body: availability, radio gate, connection truth (device state,
// not AP presence), and the publish guard inputs.
WifiSnapshot reducePublished(const WifiFetchState& s);

// setEnabled() body: radio toggle plus the immediate snapshot mutation.
WifiSnapshot withRadioState(WifiSnapshot s, bool on);

// netStepAp() merge: BSSIDs of one SSID collapse keeping the strongest
// signal; active/secured are sticky-OR; an empty SSID is ignored.
std::vector<WifiAp> mergeApReading(std::vector<WifiAp> current, const WifiApReading& r);

// finishNetFetch() saved flags: mark APs whose SSID has a saved profile.
void applySavedFlags(std::vector<WifiAp>& aps,
                     const std::vector<std::pair<std::string, std::string>>& saved);

// finishNetFetch() sort: active first, then strength-descending, stable.
void sortForPicker(std::vector<WifiAp>& aps);

}  // namespace qypr
