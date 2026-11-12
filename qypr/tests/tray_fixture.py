#!/usr/bin/env python3
"""Manual installed-bar integration fixture (requires PyGObject).

Run on the desktop session bus. Three temporary tray apps expose activation,
menu, secondary action and scrolling. JSON events on stdout prove delivery.
Send SIGUSR1 to toggle Passive/NeedsAttention; Ctrl+C removes all three items.
"""
import json
import signal

from gi.repository import Gio, GLib, GLibUnix

ITEM = "org.kde.StatusNotifierItem"
XML = """<node><interface name="org.kde.StatusNotifierItem">
<property name="Title" type="s" access="read"/>
<property name="Status" type="s" access="read"/>
<property name="IconName" type="s" access="read"/>
<property name="AttentionIconName" type="s" access="read"/>
<property name="ItemIsMenu" type="b" access="read"/>
<property name="Menu" type="o" access="read"/>
<method name="Activate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="SecondaryActivate"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="ContextMenu"><arg type="i" direction="in"/><arg type="i" direction="in"/></method>
<method name="Scroll"><arg type="i" direction="in"/><arg type="s" direction="in"/></method>
<signal name="NewStatus"><arg type="s"/></signal>
</interface><interface name="com.canonical.dbusmenu">
<method name="AboutToShow"><arg type="i" direction="in"/><arg type="b" direction="out"/></method>
<method name="GetLayout"><arg type="i" direction="in"/><arg type="i" direction="in"/><arg type="as" direction="in"/><arg type="u" direction="out"/><arg type="(ia{sv}av)" direction="out"/></method>
<method name="Event"><arg type="i" direction="in"/><arg type="s" direction="in"/><arg type="v" direction="in"/><arg type="u" direction="in"/></method>
</interface></node>"""
bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
info = Gio.DBusNodeInfo.new_for_xml(XML)
attention = False


def prop(_bus, _sender, path, _iface, name):
    values = {
        "Title": GLib.Variant("s", "Tray check " + path.rsplit("/", 1)[-1]),
        "Status": GLib.Variant("s", ("NeedsAttention" if attention else "Passive") if path.endswith("Hidden") else "Active"),
        "IconName": GLib.Variant("s", "applications-system"),
        "AttentionIconName": GLib.Variant("s", "dialog-warning"),
        "ItemIsMenu": GLib.Variant("b", path.endswith("Menu")),
        "Menu": GLib.Variant("o", "/Menu" if path.endswith(("Menu", "Hidden")) else "/"),
    }
    return values[name]


def method(_bus, _sender, path, _iface, name, args, invocation):
    print(json.dumps({"path": path, "method": name, "args": args.unpack()}), flush=True)
    if name == "AboutToShow":
        invocation.return_value(GLib.Variant("(b)", (False,)))
    elif name == "GetLayout":
        child = GLib.Variant("(ia{sv}av)", (1, {"label": GLib.Variant("s", "Confirm tray action")}, []))
        invocation.return_value(GLib.Variant("(u(ia{sv}av))", (1, (0, {}, [child]))))
    else:
        invocation.return_value(GLib.Variant("()", ()))


for suffix in ("Active", "Menu", "Hidden"):
    path = "/Tray/" + suffix
    bus.register_object(path, info.interfaces[0], method, prop, None)
    # Async: the watcher reads our properties while servicing registration.
    bus.call("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", "org.kde.StatusNotifierWatcher", "RegisterStatusNotifierItem", GLib.Variant("(s)", (path,)), None, Gio.DBusCallFlags.NONE, -1, None, None)
bus.register_object("/Menu", info.interfaces[1], method, None, None)


def toggle():
    global attention
    attention = not attention
    bus.emit_signal(None, "/Tray/Hidden", ITEM, "NewStatus", GLib.Variant("(s)", ("NeedsAttention" if attention else "Passive",)))
    return True


GLibUnix.signal_add(GLib.PRIORITY_DEFAULT, signal.SIGUSR1, toggle)
GLib.MainLoop().run()
