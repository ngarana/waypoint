# Application status in the bar

The status area answers three different questions, and each has one owner.
Mixing them is what produced duplicate icons (a Wi-Fi module *and* nm-applet's
tray icon *and* a notification-apps entry for nm-applet):

| Question | Owner | Source |
|---|---|---|
| What state is the system in? (network, Bluetooth, sound, drives, phone) | native modules: `wifi`, `bluetooth`, `volume`, … | system services (NetworkManager, BlueZ, PipeWire, …) |
| Which apps want my attention? | `notification-apps` (and `whatsapp`, `email` for one kind of app) | the notification daemon, the apps' own LauncherEntry (unread count, progress, urgency), their windows and desktop files |
| What else is running or relevant? | `background-apps` (the overflow chevron; old ids `sni`, `tray`) | native modules that are passive right now, and StatusNotifierItem apps not claimed by a native module |

All three are session-only and hidden on the lock screen.

## Native modules absorb the applets that duplicate them

A desktop shell avoids duplicates at the source: nm-applet's autostart entry
has `NotShowIn=KDE;GNOME;` because those desktops ship their own network
indicator. A bar running under a compositor session cannot rely on that — the
applets are usually started by hand, and some must keep running (nm-applet is
NetworkManager's secret agent: it answers Wi-Fi password prompts). So qypr
decides at runtime instead, in `StatusArea` (`src/ui/statusbar/StatusArea.hpp`).

Each native module **claims** the tray applets that duplicate it, matched on
the item's StatusNotifierItem `Id` (case-insensitive; `*` at the end is a
prefix match, at the start a suffix match, at both ends a substring match):

| Module | Claims by default |
|---|---|
| `wifi` | `nm-applet`, `nm-tray`, `network-manager-applet`, `org.freedesktop.network-manager-applet` |
| `bluetooth` | `blueman`, `blueman-*`, `blueberry*` |
| `volume` | `pasystray` |
| `removable-devices` | `udiskie` |
| `phone-link` | `kdeconnect*`, `org.kde.kdeconnect*`, `kde connect` |
| `whatsapp` | `*whatsapp*` |
| `email` | `category:email`, `*mail*`, `thunderbird*`, `org.mozilla.thunderbird*`, `net.thunderbird*`, `betterbird*`, `eu.betterbird*`, `evolution`, `org.gnome.evolution`, `geary`, `org.gnome.geary` |

The app modules (`whatsapp`, `email`) match the same patterns against apps
as well: an app's desktop id, name, notification app names and window app
ids, and `category:<Name>` against its desktop-entry categories (see
[App status](#app-status-notification-apps-whatsapp-email)).

A claimed applet's icon is never drawn. Its menu moves onto the module, in
one of two ways:

- **Merged** (`wifi`, `bluetooth`): the module has one surface. Left click,
  right click and the Menu key all open its flyout, and the flyout's last
  row, "More from nm-applet ›" (or blueman), swaps the flyout for the
  applet's own D-Bus menu (`src/ui/statusbar/AppletLink.hpp`). That menu's
  first row, "‹ Wi-Fi", goes back. The row is there only while the applet
  runs.
- **Split** (`volume`, `phone-link`, `removable-devices`): right click (or the
  Menu key on a focused module) opens the applet's menu under the module's
  name, below the module's own actions where it has any. A module whose
  applet is not running and that has no actions of its own keeps right
  click's old meaning (the detail flyout).

Either way, an applet without a D-Bus menu is asked to show its own
(`ContextMenu`), and **middle click** forwards `SecondaryActivate`.

A claim only holds while the claiming module is in the layout. Removing `wifi`
from `bar.conf` hands nm-applet back to the tray instead of making it
unreachable.

`[tray]` in `bar.conf` extends the claims for applets the defaults don't know:

```ini
[tray]
claims-wifi = iwgtk
claims-bluetooth = overskride
```

(`claims-volume`, `claims-removable-devices`, `claims-phone-link`,
`claims-whatsapp` and `claims-email` work the same way.) The same section places the apps nothing claims:

```ini
[tray]
pinned = TelegramDesktop   # its own icon next to the chevron
hidden = steam             # never shown
```

Both match the item's `Id` or its title, case-insensitively.

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

## phone-link

A phone paired through KDE Connect (`src/system/KdeConnectBackend.hpp`),
read from kdeconnectd on the session bus and refreshed when it signals a
change. A burst of signals, such as a phone reconnecting and reloading all its
plugins, is coalesced into one re-read.

- On the bar while a paired device is connected or a device asks to pair;
  in the overflow while paired devices are out of reach; absent without the
  daemon or a paired device. A stale record of a phone (same name and type
  as a connected device: what a reinstall leaves behind) is not listed.
- The flyout shows battery and signal, **Ring**, **Clipboard** /
  **Files…** (the desktop's file chooser portal) and **Browse** (SFTP), plus
  the phone's notifications. **Dismiss** clears a notification on the phone.
  Pairing requests offer **Accept** / **Reject**.
- Phone notifications stay in memory only, for as long as the phone has
  them. The module is session-sensitive, so they never reach the lock screen.
- Ids `kde-connect` and `phone-connect` build this module.

## App status (`notification-apps`, `whatsapp`, `email`)

An app reports its state through channels that know nothing of each other,
and each names the app differently:

| Channel | Carries | Names the app by |
|---|---|---|
| Notification daemon | live notifications, critical urgency | `desktop-entry` hint, else app name |
| Unity LauncherEntry (`src/system/ApplicationsBackend.hpp`) | the app's own unread count, a progress fraction, urgency, a quicklist (dbusmenu) | `application://<id>.desktop` |
| wlr-foreign-toplevel | open windows | Wayland app id (the desktop id, or the entry's `StartupWMClass`: `crx_<id>` for browser web apps) |
| Desktop entry | name, icon, categories, `Actions=`, `DBusActivatable` | file id |

`buildAppStatus()` (`src/ui/indicators/AppStatus.hpp`) resolves every name
through the desktop index and merges what refers to the same `.desktop` file
into one record. A notification without the hint joins its app when another
notification tied the same app name to an entry. Without a match the
lowercased raw name is the key, so the merge degrades to exact names. The
index is read the first time an app has something to show, not at startup.

LauncherEntry updates are pushed by the apps (Telegram, Thunderbird, Element,
Signal, Chromium and Electron apps, Firefox downloads, …) as a broadcast
`Update` signal; each carries only what changed. An entry goes when its app
leaves the bus or has nothing left to show. Some apps only send it while
`com.canonical.Unity` is owned, so the bar asks for that name, without
replacing or queueing behind an owner (Plasma's task manager, a dock), and
lets a shell that starts later take it over.

**notification-apps** shows every app with something to say: notifications,
an unread count, progress or urgency. Apps that a `whatsapp` or `email`
module in the layout shows are left to that module, the way native modules
claim tray applets. **whatsapp** and **email** (old id `mail`) show the apps their claims match,
while such an app has news, a window open or a tray icon. They also claim
that app's tray icon, so it doesn't appear in the overflow as well.

Each app is one button: the app's icon, a badge (the app's own count when it
sends one, from 1; otherwise its notifications, from 2), a progress bar, and
a dot for critical notifications or urgency.

- **Left click** focuses the app's window (the next one when one of its
  windows is focused, so repeated clicks cycle). An app module whose app has
  no window but a tray icon forwards `Activate` to the tray icon, which shows
  the app's hidden window. Otherwise the app is started: `Activate` on its
  bus name for a `DBusActivatable` entry (its `Exec` if that call fails), its
  `Exec` otherwise. The app's notifications are cleared.
- **Right click** (or Menu) opens the app's menu: its desktop actions ("New
  Window", "New Private Window"; `ActivateAction` for a `DBusActivatable`
  entry), its LauncherEntry quicklist, the tray icon's menu (app modules),
  and "Clear notifications".
- **Middle click** clears the app's notifications.
- The tooltip shows the counts, the progress and the latest notification.

Notification cards and history stay with `notifications`.

## The overflow (`background-apps`)

One chevron holds everything that is running or relevant but doesn't earn a
permanent place on the bar. It appears only when there is something behind
it, and points the way it opens (down on a top bar, up on a bottom bar).
Opened, it lists two sections:

- **STATUS** — native modules that are passive right now and list themselves
  through `overflowEntry()`: a drive that lives in fstab (`removable-devices`),
  a paired phone that is away (`phone-link`). The row opens that module's own
  flyout, anchored to the chevron. The bar updates the overflow after every
  other module, so a module that just hid itself is already listed.
- **BACKGROUND APPS** — StatusNotifierItem apps no native module claims
  (Telegram, Steam, a password manager), in registration order.

Tray apps are the last resort, so none of them takes bar space by default.
Two exceptions put an app's own icon next to the chevron, up to six: `[tray]
pinned`, and an app asking for attention (`NeedsAttention`), for as long as it
asks — with the app's attention icon when it supplies one, plus a dot.
`[tray] hidden` keeps an app out of sight entirely; a claimed applet never
appears here.

On a tray app (strip icon or overflow row):

- Left click invokes `Activate`, or opens the menu for `ItemIsMenu` items.
- Right click (or Menu in the overflow) opens the item's D-Bus menu, falling
  back to `ContextMenu`.
- Middle click invokes `SecondaryActivate`; the wheel calls
  `Scroll(delta, orientation)` (strip icons).
- The overflow works with the pointer and the keyboard: Tab/arrows to move,
  Enter/Space to activate, Menu for the item's menu.
- Items go away when the app unregisters or exits, including rows in an open
  overflow. Overflow rows find their app again on activation, so an app that
  exits while its row is visible can't redirect the click to a different app.

XEmbed-only tray clients need a separate compatibility bridge.

## References

- [StatusNotifierItem specification](https://specifications.freedesktop.org/status-notifier-item/latest/status-notifier-item.html): item `Id`, status, menus and input methods.
- [KDE StatusNotifierItem tutorial](https://techbase.kde.org/Development/Tutorials/PortToKStatusNotifierItem): Passive/Active item visibility.
- [KDE Plasma panel](https://docs.kde.org/stable_kf6/en/plasma-desktop/plasma-desktop/panel.html): system tray and hidden entries.
- [Microsoft notification area](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area): application status icons and their actions.
- [Desktop Entry specification](https://specifications.freedesktop.org/desktop-entry-spec/latest/): `OnlyShowIn` / `NotShowIn`, `Actions`, `StartupWMClass`, `DBusActivatable` and the `org.freedesktop.Application` interface.
- [Unity LauncherAPI](https://wiki.ubuntu.com/Unity/LauncherAPI): the `com.canonical.Unity.LauncherEntry` `Update` signal (count, progress, urgent, quicklist).

## Verification

Unit tests: `tests/test_status_area.cpp` (claims follow the layout, `[tray]`
config, composite menus, the overflow skipping claimed items, pinned /
hidden / attention placement, passive module rows opening their flyout from
the chevron, modules forwarding gestures, right click / Menu key routing,
alias dedupe, the merged wifi flyout swapping to nm-applet's menu and back),
`tests/test_removable_drives.cpp` (UDisks2 reduction, the removal plan, busy
processes, the module and its flyout), `tests/test_phone_link.cpp` (device
list, status line, reachability policy, flyout), `tests/test_app_status.cpp`
(desktop actions and `StartupWMClass`, LauncherEntry updates and expiry, the
merged app record, app claims, the app modules, the app menu,
notification-apps leaving claimed apps to their module),
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
`bluetooth` in the layout, neither applet shows a tray icon. The `wifi`
flyout ends with "More from nm-applet ›", which opens nm-applet's menu, and
the `bluetooth` flyout with blueman's.
