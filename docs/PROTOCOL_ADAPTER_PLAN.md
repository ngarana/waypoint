# Protocol Adapter Plan (QYPR_DECOMPOSITION_PLAN step 10)

Status: implemented (`adcbb51`, `2126403`, `5e169b7`, `85a34de`, `23bb85a`;
baseline suite 137/137, now 163/163). The sections below remain the design
record; deviations made during implementation are listed under *As built*.

This document is the implementation plan for step 10 of
[`QYPR_DECOMPOSITION_PLAN.md`](QYPR_DECOMPOSITION_PLAN.md): splitting the
Wi-Fi, Bluetooth, and notification adapters into protocol client, state
reducer, and operations/model seams.

## Scope and ground rules

In scope: [`WifiBackend.cpp`](../qypr/src/system/WifiBackend.cpp) (923 lines),
[`BluetoothBackend.cpp`](../qypr/src/system/BluetoothBackend.cpp) (644), and
[`NotificationMonitor.cpp`](../qypr/src/notifications/NotificationMonitor.cpp) (474).

Out of scope: `DiscoveryController` / `PairingController` (follow-ups noted at
the end), moving anything into `common/`, and any wire-protocol or behavior
change.

Non-negotiable constraints for every commit in this plan:

1. **Behavior freeze.** These mechanisms are load-bearing and must be preserved
   exactly:
   - one fetch chain in flight at a time (`fetchInFlight_` + `pendingRefresh_`;
     `NetPhase` + `netInFlight_` + `pendingNetRefresh_`; `fetchInFlight_` +
     `pendingFetch_` for BlueZ), including the "re-run when the current chain
     lands" rule;
   - `ready()` flips once, on the first real *or definitive* result, and a later
     `seed()` never overwrites live data; Wi-Fi `seed()` also fills the chain
     staging (`netResults_`);
   - `publish()` skips when the snapshot is unchanged (no repaint storms);
   - Bluetooth transient state (`busy`, `error`, `pairing`) survives a refetch,
     and a malformed managed-objects reply publishes "unavailable" rather than
     keeping stale data;
   - notification rules: `replaces_id` updates in place, only close reasons 2/3
     remove, `transient` never queues, the 8-card and 32-pending caps, and the
     receive-only `BecomeMonitor` connection with its three match rules.
2. **No consumer-visible API change.** The public surfaces used by
   `WifiIndicator`, `BluetoothIndicator`, `QSTileFactory`, `StateCacheCodec`,
   `App`/`BarApp`, and the tests keep compiling untouched. Snapshot structs stay
   reachable through the backend headers that expose them today.
3. **Reducers and stores are bus-free.** No `sd-bus` include in `*Model`,
   `*Reducer`, `*Store`, or `NotificationPolicy`.
4. **Test seams, given the mock reality.** [`mocks.cpp`](../qypr/tests/mocks.cpp)
   mocks the session bus as "everything succeeds" (`open_user`,
   `BecomeMonitor`, `add_filter`) and leaves the system bus unavailable; it
   cannot fabricate message bodies for the `sd_bus_message_read*` walkers.
   Therefore: message→typed-event parsing stays in the client/parser and is
   covered only by the existing constructions/seeded-snapshot tests; all state
   logic moves behind typed structs with new unit tests; user commands go
   through narrow virtual ports so tests assert the exact calls and their order
   without a bus.

## Current state (what is tangled today)

| File | Lines | Responsibilities sharing one class |
|---|---|---|
| [`WifiBackend.cpp`](../qypr/src/system/WifiBackend.cpp) | 923 | reply walkers, D-Bus calls, 4-step fetch chain, 4-phase network chain, saved-connection cache, user commands, snapshot publication |
| [`BluetoothBackend.cpp`](../qypr/src/system/BluetoothBackend.cpp) | 644 | managed-objects walk, refetch serialization, signal handlers, power defer, device ops with busy/error, pair→trust→connect continuation, discovery lifetime, agent wiring |
| [`NotificationMonitor.cpp`](../qypr/src/notifications/NotificationMonitor.cpp) | 474 | monitor connection + fd, backlog fetch, message parsing, hint decoding, sensitive-app policy, replacement/close/pending correlation, capacity, accents, teardown |

## Wi-Fi

### Target files

```text
system/WifiModel.hpp            WifiAp, WifiSnapshot (moved verbatim), WifiApReading
system/WifiSnapshotReducer.*    pure: merge AP readings, saved flags, sort, publication
system/NetworkManagerClient.*   sd-bus only: constants, walkers, calls, reply decoding
system/WifiOperations.*         user commands behind WifiCommandPort + fake-port tests
system/WifiBackend.*            facade: lifecycle, subscriptions, both chains, publication
```

`WifiBackend.hpp` includes the model header, so every existing include site
(`StateCacheCodec`, `WifiIndicator`, `QSTileFactory`, tests) is unchanged.

### Exact seams

`WifiModel.hpp` (new, bus-free):

```cpp
struct WifiApReading {           // one decoded AccessPoint GetAll reply
    std::string ssid;
    int strength = 0;
    bool secured = false;
    bool active = false;
};
// WifiAp is untouched: ssid, strength, secured, active, saved (+operator==).
// WifiSnapshot is untouched: available, enabled, connected, ssid, strength,
// scanning, networks (+operator==, which is the publish() guard).
```

`WifiSnapshotReducer` (pure; no sd-bus):

```cpp
struct WifiFetchState {          // everything publish() reads today
    bool haveDevice = false;
    bool wirelessEnabled = false;
    bool scanning = false;
    uint32_t deviceState = 0;    // 100 = NM_DEVICE_STATE_ACTIVATED
    std::string apSsid;
    int apStrength = 0;
    std::vector<WifiAp> networks;
};
WifiSnapshot reducePublished(const WifiFetchState& s);          // publish() body
WifiSnapshot withRadioState(WifiSnapshot s, bool on);           // setEnabled() body
std::vector<WifiAp> mergeApReading(std::vector<WifiAp> current,
                                   const WifiApReading& r);     // dedupe/max/sticky
void applySavedFlags(std::vector<WifiAp>& aps,
                     const std::vector<std::pair<std::string, std::string>>& saved);
void sortForPicker(std::vector<WifiAp>& aps);                   // active, then strength
```

`NetworkManagerClient` (owns every `sd_bus_*` use, including the walkers
`extractByteArray/extractByte/extractU32`, `parseConnectionSsid`, and the
protocol constants; async calls keep the existing `sd_bus_message*`+userdata
callback shape so the chains move over unchanged):

```cpp
class NetworkManagerClient {                 // constructed with SystemBus&
    bool available() const;
    void getDevices(FnPtr cb, void* ud);                       // GetDevices
    void getAll(const std::string& path, const char* iface, FnPtr cb, void* ud);
    void listConnections(FnPtr cb, void* ud);
    void getConnectionSettings(const std::string& conn, FnPtr cb, void* ud);
    void getAccessPointProps(const std::string& ap, FnPtr cb, void* ud);
    static std::string parseConnectionSsid(sd_bus_message* reply);
    std::vector<std::pair<std::string, std::string>> savedConnections();  // sync walk
    void setWirelessEnabled(bool on);
    void requestScan(const std::string& device);
    void activateConnection(const std::string& conn, const std::string& device);
    void addAndActivate(const std::string& device, const std::string& ssid,
                        const std::string* psk);
    void deleteConnection(const std::string& conn);
    void disconnectDevice(const std::string& device);
};
```

`WifiOperations` (pure logic; the port is the test seam):

```cpp
class WifiCommandPort {                      // implemented by NetworkManagerClient
public:
    virtual ~WifiCommandPort() = default;
    virtual bool available() const = 0;
    virtual std::string devicePath() const = 0;
    virtual std::vector<std::pair<std::string, std::string>> savedConnections() = 0;
    virtual void setWirelessEnabled(bool on) = 0;
    virtual void requestScan(const std::string& device) = 0;
    virtual void activateConnection(const std::string& conn,
                                    const std::string& device) = 0;
    virtual void addAndActivate(const std::string& device, const std::string& ssid,
                                const std::string* psk) = 0;
    virtual void deleteConnection(const std::string& conn) = 0;
    virtual void disconnectDevice(const std::string& device) = 0;
};

class WifiOperations {
public:
    struct Result { bool refreshNetworks = false; };   // backend re-runs the chain
    explicit WifiOperations(WifiCommandPort& port);
    void setSavedCache(std::vector<std::pair<std::string, std::string>> pairs);
    bool savedCacheWarm() const;
    Result connectAp(const WifiAp& ap);   // saved -> Activate; unsaved -> AddAndActivate
    Result connectPsk(const std::string& ssid, const std::string& psk);  // empty = no-op
    Result connectSsid(const std::string& ssid);   // cache first, then cold sync walk
    Result forgetSsid(const std::string& ssid);    // Delete + savedDirty + refresh
    void disconnect();
    void setEnabled(bool on);                      // radio toggle (snapshot via reducer)
    bool requestScan();                            // true when a scan was started
};
```

`WifiBackend` keeps: `start()`, `subscribeSignals()`, the 4-step fetch chain,
the 4-phase network chain (chain state stays in the facade, never in the
client), `publish()` (now `reducePublished`), the signal handlers, `seed()`
(including the `netResults_` staging), `snapshot()/ready()/setOnChange()`, and
delegates commands to `WifiOperations`. Target size: ~330 lines.

### Wi-Fi tests (new TU tests/test_wifi.cpp)

- WifiReducerBuildsSnapshot — no device → `available=false`; radio off →
  networks cleared and `scanning=false`; state==100 + `apSsid` →
  `connected/ssid/strength`; state!=100 with an AP → connected stays false.
- `WifiReducerMergesApReadings` — duplicate SSID strengthens (`max`),
  `active`/`secured` are sticky-OR, a new SSID appends, an empty SSID is
  ignored.
- `WifiReducerSavedFlagsAndSort` — saved flags matched by SSID; active first,
  then strength-descending, stable for equal strengths.
- `WifiReducerRadioOffClears` — `withRadioState(false)` clears connected, ssid,
  strength, scanning, networks; `withRadioState(true)` only sets the flag.
- `WifiOperationsJoinSavedAndOpen` (fake port) — saved AP →
  `activateConnection` only; unsaved → `addAndActivate(ssid, nullptr)`;
  secured unsaved goes through `connectPsk` with the password pointer; no
  device → no calls at all.
- `WifiOperationsForgetAndDisconnect` — unknown SSID → no Delete, no refresh;
  known → Delete then `Result{refreshNetworks=true}`; `disconnect()` without a
  device makes no call.
- `WifiOperationsColdSavedFallback` — an empty cache consults
  `port.savedConnections()`; a warm cache that lacks the SSID returns empty
  without a bus walk.
- Existing seeded-snapshot tests (`WifiPopover*`, `QSWifiComboTileZones`) stay
  green untouched — they prove the facade behavior is unchanged.

## Bluetooth

### Target files
```text
system/BluetoothModel.hpp             BtDevice, BluetoothSnapshot, BtPairRequest (moved),
                                      BluetoothAdapterReading, BluetoothDeviceReading,
                                      BluetoothManagedObjects
system/BluetoothSnapshotReducer.*     pure: managed objects -> snapshot, transient
                                      preservation, optimistic power, op lifecycle
system/BluezClient.*                  sd-bus only: walkers, GetManagedObjects, Set,
                                      device/adapter calls, RemoveDevice
system/BluetoothOperations.*          commands behind BluezCommandPort + pair
                                      continuation as a testable state machine
system/BluetoothBackend.*             facade: lifecycle, subscriptions, serialization,
                                      agent ownership, discovery state, publication
```

`BtPairRequest` moves from [`BluetoothAgent.hpp`](../qypr/src/system/BluetoothAgent.hpp)
to the model; `BluetoothAgent.hpp` includes the model, so its users
(`BluetoothIndicator`, tests) are unchanged.
### Exact seams

```cpp
// BluetoothModel.hpp (bus-free)
struct BluetoothAdapterReading { bool powered = false; bool discovering = false; };
struct BluetoothDeviceReading {
    std::string path; std::string alias; std::string name;
    bool connected = false; bool paired = false;
    std::string icon; int battery = -1;
};
struct BluetoothManagedObjects {
    bool haveAdapter = false;
    BluetoothAdapterReading adapter;
    std::vector<BluetoothDeviceReading> devices;
};
```

```cpp
// BluetoothSnapshotReducer (pure)
BluetoothSnapshot reduceManagedObjects(const BluetoothManagedObjects& m);
BluetoothSnapshot preserveTransient(BluetoothSnapshot next,
                                    const BluetoothSnapshot& prev);  // busy/error/pairing
BluetoothSnapshot withPowered(BluetoothSnapshot s, bool on);          // optimistic clear
BluetoothSnapshot withOpBusy(BluetoothSnapshot s, const std::string& path);
BluetoothSnapshot withOpEnded(BluetoothSnapshot s, const std::string& error);
BluetoothSnapshot withPairingRequest(BluetoothSnapshot s, const BtPairRequest& r);
std::string deviceNameFor(const BluetoothSnapshot& s, const std::string& path);
```

```cpp
// BluezClient (sd-bus; reply callbacks keep the sd-bus function-pointer shape)
void getManagedObjects(FnPtr cb, void* ud);
void setAdapterPowered(const std::string& adapter, bool on, FnPtr cb, void* ud);
void callDevice(const std::string& path, const char* member, uint64_t timeoutUs,
                FnPtr cb, void* ud);
void callAdapter(const std::string& adapter, const char* member, FnPtr cb, void* ud);
void removeDevice(const std::string& adapter, const std::string& path,
                  FnPtr cb, void* ud);
static bool parseManagedObjects(sd_bus_message* m, BluetoothManagedObjects* out);
```

```cpp
// BluetoothOperations: the port carries std::function reply handlers so a fake
// can drive the pair->trust->connect chain synchronously.
class BluezCommandPort {
public:
    using Reply = std::function<void(bool ok, const std::string& error)>;
    virtual ~BluezCommandPort() = default;
    virtual bool available() const = 0;
    virtual std::string adapterPath() const = 0;
    virtual void getManagedObjects(Reply onDone) = 0;              // refetch request
    virtual void setPowered(bool on, Reply onReply) = 0;
    virtual void callDevice(const std::string& path, const char* member,
                            uint64_t timeoutUs, Reply onReply) = 0;
    virtual void callAdapter(const char* member, Reply onReply) = 0;
    virtual void removeDevice(const std::string& path, Reply onReply) = 0;
};

class BluetoothOperations {
public:
    using Publish = std::function<void(BluetoothSnapshot&&)>;
    using SnapshotFn = std::function<const BluetoothSnapshot&()>;
    BluetoothOperations(BluezCommandPort& port, Publish publish, SnapshotFn current);
    bool setPowered(bool on);                     // deferred while the adapter is unknown
    bool applyPendingPower();                     // after a fetch produced an adapter
    void connectDevice(const std::string& path);
    void disconnectDevice(const std::string& path);
    void pairDevice(const std::string& path);     // Pair -> on ok: Trust, then Connect
    void forgetDevice(const std::string& path);   // RemoveDevice on the *adapter*
    // discovery: one call in flight, wantDiscovery survives power-off->on,
    // a refused StartDiscovery clears the intent (never spins)
    bool startDiscovery();
    void stopDiscovery();
    bool tryStartDiscovery();                     // re-checked after every fetch
    bool discoveryCallInFlight() const;
};
```

`BluetoothBackend` keeps: `start()` + agent wiring (`setNameResolver`,
`setOnRequest`, `agentEnabled_`, `onBluezLost` on owner loss), signal
subscriptions, `refetch()`/`endFetch()` serialization, `onGetManagedObjects`
(now: client walk → reducer → `preserveTransient` → publish, then pending
power + `tryStartDiscovery`), `publish()`/`publishUnavailable()`, the
property/interface/owner handlers, and `respondPairing*`. Target size: ~260
lines.

### Bluetooth tests (new TU tests/test_bluetooth.cpp)

- BluetoothReducerBuildsSnapshot — adapter+devices →
  available/powered/discovering/connectedCount/firstDevice (first connected in
  walk order); no adapter → `available=false`.
- `BluetoothReducerAliasBeatsName` — alias wins regardless of key order; name
  only when the alias is absent; both empty leaves the name empty.
- `BluetoothReducerPreservesTransientState` — busy/error/pairing copied from
  the previous snapshot across a refetch.
- `BluetoothReducerPowerOffClears` — optimistic off: counts and firstDevice
  zeroed, discovering off, every device disconnected.
- `BluetoothReducerOpLifecycle` — `withOpBusy` sets the busy row and clears
  the error; `withOpEnded` clears busy and stores the error text.
- `BluetoothOperationsPairSequence` (fake port) — `pairDevice` issues Pair;
  a successful reply issues Trust and then Connect; a failed reply issues no
  calls and requests a refetch.
- `BluetoothOperationsPowerDefer` — no adapter: no Set, a refetch is requested
  and the pending toggle applies on the next adapter; adapter known: Set
  immediately.
- `BluetoothOperationsForgetUsesAdapter` — RemoveDevice carries the adapter
  and device paths; without an adapter: no call, error recorded.
- `BluetoothOperationsDiscoveryLifetime` — `startDiscovery` twice issues one
  Start; a refusal clears the intent (no retry); stop while discovering issues
  Stop; stop while not discovering issues nothing.
- `BluetoothAgentGating` — `setPairingAgentEnabled(false)` is observable
  through `pairingAgentEnabled()` and the backend constructs/tears down
  cleanly without a bus (QL-1 regression guard).
- Existing `BluetoothIndicator*` tests stay green untouched.

## Notifications

### Target files

```text
notifications/NotificationPolicy.*      pure: sensitive-app list + hint fold
notifications/NotificationParser.*      sd-bus: Notify / return / close -> typed events
notifications/NotificationStore.*       pure: replacement, close, capacity, pending
notifications/NotificationTransport.*   sd-bus: monitor connection, fd, backlog fetch
notifications/NotificationMonitor.*     facade: theme + transport + store + publication
```

### Exact seams

```cpp
// NotificationPolicy.hpp (bus-free; the sensitive-app list is path-injectable)
class NotificationPolicy {
public:
    static std::vector<std::string> defaultSensitiveApps();
    void load(const std::string& path);          // missing file -> defaults
    bool isSensitiveApp(const std::string& app) const;  // case-folded substring
};

struct NotifyHints {
    uint8_t urgency = 1;
    bool transient = false;
    bool sensitive = false;
    std::string desktopEntry;
};
// One hint-dict entry folded in; unknown keys return the input unchanged.
NotifyHints applyHint(const std::string& key, bool numeric, uint64_t num,
                      const std::string& str, NotifyHints h);
```

```cpp
// NotificationParser.hpp (sd-bus walking only; no policy, no storage)
struct NotifyEvent {
    std::string app, icon, summary, body;
    uint32_t replacesId = 0;
    std::vector<std::pair<std::string, std::string>> actions;
    NotifyHints hints;
    uint64_t cookie = 0;
    std::string sender;
};
struct ClosedEvent { uint32_t id = 0; uint32_t reason = 0; };
struct ReturnEvent { uint64_t replyCookie = 0; std::string destination; uint32_t daemonId = 0; };
std::optional<NotifyEvent> parseNotify(sd_bus_message* m);
std::optional<ClosedEvent> parseClosed(sd_bus_message* m);
std::optional<ReturnEvent> parseReturn(sd_bus_message* m);
```

```cpp
// NotificationStore.hpp (pure; owns notes_, nextKey_, pending_)
class NotificationStore {
public:
    static constexpr size_t kMaxHeld = 8;
    static constexpr size_t kMaxPending = 32;
    // transient or empty title+body -> returns 0 (not queued);
    // replacesId -> updates in place, keeping the local id;
    // otherwise appends with a fresh id and trims to kMaxHeld.
    uint64_t add(const NotifyEvent& e, const Color& accent);
    bool attachDaemonId(const ReturnEvent& r);            // cookie+sender match
    bool close(uint32_t daemonId, uint32_t reason);       // only reasons 2 and 3
    void seed(std::vector<Notification> backlog);          // capacity + ids, oldest first
    const std::vector<Notification>& notes() const;
    // accent: critical -> theme red; else theme accent cycled by key
    static Color accentFor(const theme::State& t, uint64_t key, uint8_t urgency);
};
```

```cpp
// NotificationTransport.hpp (sd-bus; the monitor connection)
class NotificationTransport {
public:
    struct Handlers {
        std::function<void(const NotifyEvent&)> onNotify;
        std::function<void(const ReturnEvent&)> onReturn;
        std::function<void(const ClosedEvent&)> onClosed;
        std::function<void(const std::vector<Notification>&)> onBacklog;
        std::function<void()> onLost;                     // bus error -> teardown
    };
    NotificationTransport(EventLoop& loop);
    ~NotificationTransport();
    void setHandlers(Handlers h);
    bool start(bool seedFromLog);                         // backlog, then BecomeMonitor
    void teardown();                                       // fd/filter/bus; posted
};
```

`NotificationMonitor` becomes: `start(seedFromLog)` (transport + handlers),
`notifications()` (store), `setOnChange`, `setTheme` (recompute nothing;
accent is applied at insert via the store call), `teardown()` delegating.
Target size: ~150 lines.

### Notifications tests (extend `tests/test_notifications.cpp`)

- `NotificationStoreReplacementKeepsIdentity` — `replaces_id` updates the
  card in place with the same local id and no capacity trim.
- `NotificationStoreSkipsTransientAndEmpty` — transient and empty
  title+body never queue; an action-only card still queues.
- `NotificationStoreCapacityDropsOldest` — 9 inserts leave the newest 8.
- `NotificationStoreCloseReasons` — reason 1 ignored, 2 and 3 remove,
  unknown id is a no-op (no spurious change signal).
- `NotificationStorePendingCorrelation` — reply cookie+sender attaches the
  daemon id to the right card; wrong cookie or wrong sender is ignored; the
  33rd pending entry clears the table (stale, unanswered).
- `NotificationPolicySensitiveApps` — temp file: comments, blank lines,
  surrounding whitespace, lower-casing, substring matching; missing file →
  the built-in defaults.
- `NotificationHintsFold` — urgency numeric; transient bool; `sensitive`
  and `x-kde-privacy` numeric; `visibility` numeric (`< 2` sensitive) and
  string (`private`/`secret`); `desktop-entry`/`desktop_entry`; unknown
  keys unchanged.
- `NotificationAccentForUrgency` — critical is the theme red; others cycle
  the eight accents deterministically by key.
- `NotificationTransportNoBus` — with `g_mock_sdbus_fail = true`,
  `start()` returns false and `teardown()` is safe.
- Existing `NotificationMonitor` and `NotificationLog` tests must stay
  green (they exercise the mocked `BecomeMonitor` success path and
  `drain()`/`teardown()`).

## Migration order and commit plan

Run one commit per backend, always building + `ctest` green, with tests in the
same commit as the code.

| # | Commit | Assets | Validates |
|---|---|---|---|
| 0 | `docs: add protocol adapter plan (step 10)` | `docs/PROTOCOL_ADAPTER_PLAN.md` | doc-path gate, format, tidy, invariants |
| 1 | `refactor(qypr): add wifi model and snapshot reducer` | system/WifiModel.hpp, system/WifiSnapshotReducer.{hpp,cpp}, CMakeLists.txt, tests/test_wifi.cpp | 135 → 135 (no new assertion yet), `test_wifi` built |
| 2 | `refactor(qypr): extract NetworkManagerClient and WifiOperations` | system/NetworkManagerClient.{hpp,cpp}, system/WifiOperations.{hpp,cpp}, CMakeLists.txt, tests/test_wifi.cpp (expanded) | behavior freeze for `WifiIndicator` tests; new unit tests green |
| 3 | `refactor(qypr): add bluetooth model and snapshot reducer` | system/BluetoothModel.hpp, system/BluetoothSnapshotReducer.{hpp,cpp}, CMakeLists.txt, tests/test_bluetooth.cpp | Qt guard + `BluetoothAgent` compat; `BluetoothIndicator` tests green |
| 4 | `refactor(qypr): extract BluezClient and BluetoothOperations` | system/BluezClient.{hpp,cpp}, system/BluetoothOperations.{hpp,cpp}, CMakeLists.txt, tests/test_bluetooth.cpp (expanded) | behavior freeze + QL-1 gating test |
| 5 | `refactor(qypr): split notification transport, parser, policy, and store` | notifications/NotificationPolicy.{hpp,cpp}, NotificationParser.{hpp,cpp}, NotificationStore.{hpp,cpp}, NotificationTransport.{hpp,cpp}, NotificationMonitor.{hpp,cpp}, CMakeLists.txt, tests/test_notifications.cpp (expanded) | 135 → new total |
| 6 | `docs: mark protocol adapters (step 10) done` | `docs/QYPR_DECOMPOSITION_PLAN.md`, `README.md`, `ARCHITECTURE_REVIEW.md` | counts updated to actual test numbers |

**After step 6, expect the qypr suite to sit at roughly 155 tests** (135 existing +
~7 Wi-Fi, ~10 Bluetooth, ~8 Notifications). Adjust the README count to the
actual `ctest` total after each commit.

## Verification per commit

1. `cmake `--compile-commands` … -DCMAKE_VERBOSE_MAKEFILE=ON` + build.
2. `ctest -L unit -L integration --output-on-failure --no-hardware-dialogs` — every
   existing test still green, new tests green.
3. `clang-format --mode=file` and `run-clang-tidy` on the new files.
4. `g++ … -fPIE -pie -std=c++17 -Wall -Wextra -Werror -Wconversion -Wtype-limits`
   clean (the baseline stack the codebase enforces).
5. Doc-path gate: no dangling backticked paths; every forward reference resolves.
6. Invariants: no public header removed (only added), `BtPairRequest` reachable
   through `BluetoothAgent.hpp`, `QtGuard` still static in the configurator and
   both runtime factories.

## Risks and how each is contained

1. **Chain serialization/ordering.** Chain state (`fetchInFlight_`, `pendingRefresh_`,
   `netInFlight_`, `NetPhase`, `pendingFetch_`) stays in the facade — the client and
   reducer are stateless (except caches: `savedConnections()`, the saved-pairs cache).
2. **`publish()` skip-when-equal + `ready()` flip.** Each facade keeps exactly one
   publication seam; reducer equality remains `operator==` on the same fields, so the
   guard fires identically.
3. **Wi-Fi `seed()` staging.** `seed()` still fills `netResults_`; the reducer reads
   from staging exactly as `publish()` did, so StageCacheCodec and the seed tests
   remain valid.
4. **Bluetooth malformed managed-objects.** The client returns false on a bad reply;
   the facade publishes "unavailable" exactly as `publishUnavailable()` did today, so
   the forever-defer bug stays documented and unchanged.
5. **Agent behavior (QL-1).** Agent enablement (`setPairingAgentEnabled`) stays in
   the facade; `onBluezLost` on owner loss is preserved; the lock-runtime gating
   assertion checks the flag, not UI visibility.
6. **Notification transient/sensitive/visibility semantics.** ported verbatim into
   `applyHint` / `isSensitiveApp` / `NotificationStore::add`; the 8-card and
   32-pending caps and the backlog record decode (which must tolerate 7-field older
   records via `enter_container` exact-type matching) are unchanged.
7. **sd-bus mock constraints.** New code never calls an unmocked sd-bus symbol in a
   test path: operations take `WifiCommandPort`/`BluezCommandPort`, stores/policy take
   typed structs, and pending/seeded tests stay where they are. The `BecomeMonitor`
   success path still covers `NotificationTransport::start()` where needed.
8. **Include compatibility.** No public header removed; `WifiModel`/`BluetoothModel`
   are included through the backend headers that already expose their types, so
   `StateCacheCodec`, `WifiIndicator`, `BluetoothIndicator`, `QSTileFactory`, and the
   tests compile without change.

## As built (deviations from the plan above)

- Verification followed the repo gates (`AGENT.md` §3.1: warning-free build,
  all three `ctest` suites, invariants, format + strict tidy on touched
  files), not the Verification section below it (no Qt, no `ctest -L`
  labels, C++20, `lint.sh` workflow here).
- `NotifyEvent` also carries `postedAt` and a cookie-validity flag (the
  pending table only registers successful cookie reads, as before), and the
  desktop-entry hint stays inside `NotifyHints`; the store takes the theme
  (not a precomputed accent) since card ids are assigned inside `add()`.
  `hasPending()` preserves the parse early-out.
- `BluezCommandPort` methods report whether the call was sent (the enqueue-
  failure paths distinguish "could not be sent" from reply failures, as
  before), plus `setTrusted`; fetch serialization calls the client directly,
  so the port carries no `getManagedObjects`.
- Both facades adapt the port to their device/adapter path (`FacadePort` in
  each backend TU); device and adapter state never enters the clients.
- Indicator reach-in tests (`mon.notes_`) moved to an explicit `testNotes`
  seam rather than touching store internals through the private hack.
- Counts: 137 → 141 → 145 → 150 → 154 → 163 across the five commits.

## Out of scope (follow-ups)

- Splitting `DiscoveryController` and `PairingController` into their own
  headers — the plan keeps them folded into `BluetoothOperations`/`BluetoothBackend`
  for now; separate them only if they gain their own policy (max scan duration,
  picker ownership, lock-time pairing gating).
- Moving the client into `common/` — there is no cross-app use today, so it stays in
  `qypr/src/system`.
- Sharing the `NotificationLog` wire-record format closely with the parser — the
  types already interoperate; a tighter shared record struct is a cleanup, not a
  precondition for this step.

<!-- END -->
