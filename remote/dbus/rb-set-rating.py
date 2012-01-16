#!/usr/bin/python
# vim: set sts=2 sw=2 et :
# Set the rating for a URI

import sys

from gi.repository import GLib, Gio

bus_type = Gio.BusType.SESSION
flags = 0
iface_info = None
proxy = Gio.DBusProxy.new_for_bus_sync(bus_type, flags, iface_info,
                                       "org.gnome.Rhythmbox3",
                                       "/org/gnome/Rhythmbox3/RhythmDB",
                                       "org.gnome.Rhythmbox3.RhythmDB", None)

entry_uri = sys.argv[1]
rating = float(sys.argv[2])
vrating = GLib.Variant("d", rating)
proxy.SetEntryProperties("(sa{sv})", entry_uri, {"rating": vrating})
