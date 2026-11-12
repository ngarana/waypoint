# Persistent application status

`notification-apps` is a StatusNotifierItem host: applications register an
icon while they run, update their status, and supply their own actions.
`notifications` continues to own notification cards and history. A toast
alone does not create a persistent application indicator.

`notification-apps` and `sni` use the same implementation. If both appear in
module lists, the first configured occurrence wins; defaults create one tray.
Both are session-only and hidden on the lock screen.

- Active applications appear in registration order, up to six icons.
- Passive applications and additional icons remain accessible using the
  overflow chevron, including when every application is passive.
- NeedsAttention displays the app's attention icon when supplied, plus a dot.
- Left click invokes Activate, or opens the app menu for ItemIsMenu apps.
- Right click opens the app's D-Bus menu, falling back to ContextMenu.
- Middle click invokes SecondaryActivate. Wheel input uses Scroll(delta,
  orientation), as specified by StatusNotifierItem.
- Overflow supports pointer hover, Tab/arrow navigation, Enter/Space to
  activate and the Menu key/right click for app actions.
- Icons disappear when the application unregisters or exits. Opening an app
  does not clear notification history. Missing icons get an initial fallback.

Overflow resolves app identities again when activating: an application that
exits while its row is visible cannot redirect that click to another app.
Apps must publish a StatusNotifierItem; XEmbed-only tray clients require a
separate compatibility bridge.

## References

- [Microsoft notification area](https://learn.microsoft.com/en-us/windows/win32/shell/notification-area): registered application status icons and application-controlled actions.
- [KDE Plasma panel](https://docs.kde.org/stable_kf6/en/plasma-desktop/plasma-desktop/panel.html): system tray and hidden entries.
- [KDE StatusNotifierItem tutorial](https://techbase.kde.org/Development/Tutorials/PortToKStatusNotifierItem): Passive/Active item visibility.
- [StatusNotifierItem specification](https://specifications.freedesktop.org/status-notifier-item/latest/status-notifier-item.html): item lifetime, status, icons, menus and input methods.

## Verification

The `NotificationAppsPersistentTray` regression covers independence from toast
history, passive-only overflow, missing icons, bounded width, attention, and
removal/replacement while a popup is open. Registry tests cover alias deduplication.

For actual desktop routing, run `python qypr/tests/tray_fixture.py` on the session
bus. It registers three temporary apps and logs received protocol calls as JSON.
Click Active, right/middle click it, scroll on it, open Menu and invoke its action,
and repeat activation/context actions from Hidden in overflow. Send SIGUSR1 to
the fixture PID to switch Hidden between Passive and NeedsAttention. Stop the
fixture to verify automatic removal, including while overflow is open.

Installed-session verification also caught two routing defects now fixed:
wheel events must reach all three configured bar zones, and watcher ownership
subscriptions must use the D-Bus service name (without the object path). Exiting
one process removes all of its registered items, including rows in open overflow.
