# Application status in the bar

The status area answers three different questions, and each has one owner.
Mixing them is what produced duplicate icons (a Wi-Fi module *and* nm-applet's
tray icon *and* a notification-apps entry for nm-applet):

| Question | Owner | Source |
|---|---|---|
| What state is the system in? (network, Bluetooth, sound, drives, phone) | native modules: `wifi`, `bluetooth`, `volume`, … | system services (NetworkManager, BlueZ, PipeWire, …) |
| Which apps want my attention? | `notification-apps` | the notification daemon (one icon per app with live notifications) |
| What else is running in the background? | `sni` (tray host) | StatusNotifierItem apps not claimed by a native module |

All three are session-only and hidden on the lock screen.

## Native modules absorb the applets that duplicate them

A desktop shell avoids duplicates at the source: nm-applet's autostart entry
has `NotShowIn=KDE;GNOME;` because those desktops ship their own network
indicator. A bar running under a compositor session cannot rely on that — the
applets are usually started by hand, and some must keep running (nm-applet is
NetworkManager's secret agent: it answers Wi-Fi password prompts). So qypr
decides at runtime instead, in `StatusArea` (`src/ui/statusbar/StatusArea.hpp`).

Each native module **claims** the tray applets that duplicate it, matched on
the item's StatusNotifierItem `Id` (case-insensitive; a trailing `*` is a
prefix match):

| Module | Claims by default |
|---|---|
| `wifi` | `nm-applet`, `nm-tray`, `network-manager-applet`, `org.freedesktop.network-manager-applet` |
| `bluetooth` | `blueman`, `blueman-*`, `blueberry*` |
| `volume` | `pasystray` |
| `removable-devices` | `udiskie` |
| `phone-link` | `kdeconnect*`, `org.kde.kdeconnect*`, `kde connect` |

A claimed applet's icon is never drawn. Its menu moves onto the module:

- **Right click** (or the Menu key on a focused module) opens the applet's own
  D-Bus menu under the module's name. An applet without a menu is asked to
  show its own (`ContextMenu`). A module whose applet is not running has no
  context menu, and right click keeps its old meaning (the detail flyout).
- **Middle click** forwards `SecondaryActivate`.

A claim only holds while the claiming module is in the layout. Removing `wifi`
from `bar.conf` hands nm-applet back to the tray instead of making it
unreachable.

`[tray]` in `bar.conf` extends the claims for applets the defaults don't know:

```ini
[tray]
claims-wifi = iwgtk
claims-bluetooth = overskride
```

(`claims-volume`, `claims-removable-devices` and `claims-phone-link` work the
same way. `pinned` and `hidden` list tray Ids for the background-apps
overflow and are read now, but nothing uses them yet.)

## removable-devices

Drives come from UDisks2 (`src/system/UDisksBackend.hpp`), pushed on
InterfacesAdded/Removed and PropertiesChanged; nothing is polled. A drive is
removable when UDisks2 says so (`Removable`, `MediaRemovable`, `Ejectable`)
or it sits on a hot-plug bus (USB, SD, FireWire). sysfs `removable` is not
used: a USB hard disk reports 0 there because its medium can't be taken out,
yet it can certainly be unplugged.

- A drive with a volume in `/etc/fstab` or `/etc/crypttab` (UDisks2's
  `Block.Configuration`) is *fixed*: a permanent part of the system, so it
  makes the module passive instead of putting it on the bar. Any other
  attached drive shows the module.
- **Eject** runs the same sequence file managers use: unmount every mounted
  volume, lock every unlocked LUKS container, then `PowerOff` (or `Eject`
  for media that can only be ejected). The first failure stops it. A busy
  volume lists the processes holding files under its mount point. While an
  operation runs, or after it fails, the module stays on the bar.
- **Open** opens a mounted volume in the file manager (`xdg-open`), mounting
  it first if needed.
- Polkit decides what needs a password. UDisks2 asks it, and the session's
  polkit agent shows the prompt; the bar never elevates privileges itself.
- Ids `usb` and `usb-eject` build this module. Listing several of them gives
  one instance.

## notification-apps

One icon per application that has live notifications, from the notification
daemon. It does not use StatusNotifierItem.

- Left click focuses the app's window (matched by desktop entry / app id) or
  launches it, and clears its notifications.
- Right/middle click dismisses that app's notifications.
- The tooltip shows the count and the latest title.

Notification cards and history stay with `notifications`.

## The tray host (`sni`)

Unclaimed StatusNotifierItems are shown by `sni`:

- Active items appear in registration order, up to six icons. Passive items
  and any more than six sit behind an overflow chevron, which also appears
  when every item is passive.
- NeedsAttention shows the attention icon when the app supplies one, plus a
  dot.
- Left click invokes `Activate`, or opens the menu for `ItemIsMenu` items.
- Right click opens the item's D-Bus menu, falling back to `ContextMenu`.
- Middle click invokes `SecondaryActivate`. The wheel calls
  `Scroll(delta, orientation)`.
- The overflow works with the pointer and the keyboard: Tab/arrows to move,
  Enter/Space to activate, Menu or right click for the item's menu.
- Items go away when the app unregisters or exits, including rows in an open
  overflow. Overflow rows find their app again on activation, so an app that
  exits while its row is visible can't redirect the click to a different app.

XEmbed-only tray clients need a separate compatibility bridge.

## References

- [StatusNotifierItem specification](https://specifications.freedesktop.org/status-notifier-item/latest/status-notifier-item.html): item `Id`, status, menus and input methods.
- [KDE StatusNotifierItem tutorial](https://techbase.kde.org/Development/Tutorials/PortToKStatusNotifierItem): Passive/Active item visibility.
- [KDE Plasma panel](https://docs.kde.org/stable_kf6/en/plasma-desktop/plasma-desktop/panel.html): system tray and hidden entries.
- [Microsoft notification area](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area): application status icons and their actions.
- [Desktop Entry specification](https://specifications.freedesktop.org/desktop-entry-spec/latest/): `OnlyShowIn` / `NotShowIn`.

## Verification

Unit tests: `tests/test_status_area.cpp` (claims follow the layout, `[tray]`
config, composite menus, the tray host skipping claimed items, modules
forwarding gestures, right click / Menu key routing, alias dedupe),
`tests/test_removable_drives.cpp` (UDisks2 reduction, the removal plan, busy
processes, the module and its flyout),
`NotificationAppsIndicatorDynamic` in `tests/test_indicators.cpp`, and
`TrayAndNotificationAppsAreDistinct` in `tests/test_flyouts.cpp`.

On the desktop, run `python qypr/tests/tray_fixture.py` on the session bus. It
registers three temporary apps and logs the protocol calls it receives as
JSON. Click Active, right/middle click it, scroll on it, open Menu and invoke
its action, and do the same from Hidden in the overflow. Send SIGUSR1 to the
fixture's PID to switch Hidden between Passive and NeedsAttention. Stop the
fixture to check that its items are removed, including while the overflow is
open.

For claims: with nm-applet and blueman-applet running and `wifi` and
`bluetooth` in the layout, neither applet shows a tray icon. Right click on
`wifi` opens nm-applet's menu, and on `bluetooth` blueman's.
