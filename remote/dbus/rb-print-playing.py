#!/usr/bin/python
# Display information for currently playing song

import dbus
import dbus.glib
import gobject

bus = dbus.SessionBus()
rbshellobj = bus.get_object('org.gnome.Rhythmbox', '/org/gnome/Rhythmbox/Shell')
rbshell = dbus.Interface(rbshellobj, 'org.gnome.Rhythmbox.Shell')
rbplayerobj = bus.get_object('org.gnome.Rhythmbox', '/org/gnome/Rhythmbox/Player')
rbplayer = dbus.Interface(rbplayerobj, 'org.gnome.Rhythmbox.Player')

def playing_uri_changed(uri):
    print("Now playing: %s" % (uri,))
    props = rbshell.getSongProperties(uri)
    interesting = ['title', 'artist', 'album', 'location']
    for prop in props:
        if prop in interesting:
            print "%s: %s" % (prop, props[prop])

rbplayer.connect_to_signal('playingUriChanged', playing_uri_changed)

x = rbplayer.getPlayingUri()
playing_uri_changed(x)

loop = gobject.MainLoop()
loop.run()
