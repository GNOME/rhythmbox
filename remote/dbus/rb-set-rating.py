#!/usr/bin/python
# Set the rating for a URI

import dbus, sys

bus = dbus.SessionBus()
rbshellobj = bus.get_object('org.gnome.Rhythmbox', '/org/gnome/Rhythmbox/Shell')
rbshell = dbus.Interface(rbshellobj, 'org.gnome.Rhythmbox.Shell')

rbshell.setSongProperty(sys.argv[1], "rating", dbus.Double(float(sys.argv[2])))

