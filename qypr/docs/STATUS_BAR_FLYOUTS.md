# Status bar flyouts

Click a module to open its controls next to that module. Click it again to
close, or click a different flyout module to switch directly. Escape and a
click outside dismiss the panel. An outside click is consumed: it does not
activate an application or destructive action underneath. Pointer leave and
inactivity do not dismiss controls.

Panels align with the initiating module in the left, centre or right zone,
open inward from either bar edge, and remain within the output with an 8px
inset. Oversized content scrolls inside a clipped viewport. The active module
has a tinted background and an edge marker. Opening fades opacity while
keeping control positions fixed. Only the initiating output renders the
panel and takes its keyboard focus; other outputs retain their normal bars.

Quick Settings separates power and close, highlights hovered/focused tiles,
and exposes Bluetooth/night-light details with a separate chevron. The body
of a toggle still changes its state. Tab and Shift+Tab traverse controls;
Enter/Space activate, arrows adjust sliders, and Right opens a selected
toggle's details. Power dismisses Quick Settings before handing over to the
configured power application.

Volume includes the master level/mute control, output selection and per-app
levels. Per-app information remains hidden on the lock screen. Slider capture
starts on the track, follows the pointer outside the panel, and always ends
on release. Mute clicks do not start drags. Calendar supports Left/Right,
Page Up/Page Down and Home (today). Tray menus support arrow/Tab navigation
and Enter/Space activation. Wi-Fi and Bluetooth lists scroll down for a
positive vertical wheel delta.

## Design references

- [Apple: Use Control Center on Mac](https://support.apple.com/en-ie/guide/mac-help/mchl50f94f8f/27/mac/27)
  describes draggable sliders, direct toggles and expandable detail controls.
- [Microsoft: Flyout controls](https://learn.microsoft.com/windows/apps/design/controls/dialogs-and-flyouts/flyouts)
  describes control-relative placement, Escape/outside dismissal, keyboard
  containment and deliberate pass-through between related flyout triggers.
- [Microsoft: Connect to Wi-Fi](https://support.microsoft.com/en-us/windows/experience/connectivity-networking/connect-to-a-wi-fi-network-in-windows)
  documents Quick Settings entry through network, sound and battery icons.

These informed the interaction model; the existing module configuration and
qypr theme remain authoritative.

## Verification

`tests/test_flyouts.cpp` runs after static module registration. It exercises
all 28 shipped module names (including aliases) with disconnected/seeded
backends, plus switching, same-trigger close, outside/Escape dismissal,
all three zones at both edges and three screen widths, overflow clipping,
slider release outside, Quick Settings hover/details/keyboard, and reload.
Existing indicator/backend suites exercise notification actions, Wi-Fi
password entry and saved/open-network routing, Bluetooth operations, DND,
workspace/pager actions and lock-screen restrictions.

```sh
cmake --build qypr/build --parallel
ctest --test-dir qypr/build --output-on-failure
QYPR_TEST_ARTIFACT_DIR=/tmp/qypr-flyout-previews qypr/build/qypr-test
```

The optional artifact directory receives PNGs of each module's detail panel.
The tests do not connect hardware, launch applications, eject disks or
perform power actions. A separate nested Hyprland session was also used to smoke-test clock,
Wi-Fi, Bluetooth, brightness, volume/output lists, battery and Quick Settings;
same-trigger close, module switching, Escape/outside dismissal, top/bottom
placement, and a hot-plugged second output at 2x scale. That check caught and
verified the fix for late output-scale events. Physical pairing, joining a
network, ejecting storage and power operations were not exercised.
