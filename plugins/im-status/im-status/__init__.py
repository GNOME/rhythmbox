# coding: utf-8
# vim: set et sw=2:
# 
# Copyright (C) 2007-2008 - Vincent Untz
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The Rhythmbox authors hereby grant permission for non-GPL compatible
# GStreamer plugins to be used and distributed together with GStreamer
# and Rhythmbox. This permission is above and beyond the permissions granted
# by the GPL license by which Rhythmbox is covered. If you modify this code
# you may extend this exception to your version of the code, but you are not
# obligated to do so. If you do not wish to do so, delete this exception
# statement from your version.
# 
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import rhythmdb, rb
try:
  import dbus
  use_gossip = True
except ImportError:
  use_gossip = False
try:
  import empathy
  use_empathy = True
  empathy_idle = empathy.Idle ()
except ImportError:
  use_empathy = False

NORMAL_SONG_ARTIST = 'artist'
NORMAL_SONG_TITLE  = 'title'
NORMAL_SONG_ALBUM  = 'album'
STREAM_SONG_ARTIST = 'rb:stream-song-artist'
STREAM_SONG_TITLE  = 'rb:stream-song-title'
STREAM_SONG_ALBUM  = 'rb:stream-song-album'

BUS_NAME = 'org.gnome.Gossip'
OBJ_PATH = '/org/gnome/Gossip'
IFACE_NAME = 'org.gnome.Gossip'

class IMStatusPlugin (rb.Plugin):
  def __init__ (self):
    rb.Plugin.__init__ (self)

  def activate (self, shell):
    self.shell = shell
    sp = shell.get_player ()
    self.psc_id  = sp.connect ('playing-song-changed',
                               self.playing_entry_changed)
    self.pc_id   = sp.connect ('playing-changed',
                               self.playing_changed)
    self.pspc_id = sp.connect ('playing-song-property-changed',
                               self.playing_song_property_changed)

    self.current_entry = None
    self.current_artist = None
    self.current_title = None
    self.current_album = None

    self.save_status ()

    if sp.get_playing ():
      self.set_entry (sp.get_playing_entry ())

  def deactivate (self, shell):
    self.shell = None
    sp = shell.get_player ()
    sp.disconnect (self.psc_id)
    sp.disconnect (self.pc_id)
    sp.disconnect (self.pspc_id)

    if self.current_entry is not None:
      self.restore_status ()

  def playing_changed (self, sp, playing):
    if playing:
      self.set_entry (sp.get_playing_entry ())
    else:
      self.current_entry = None
      self.restore_status ()

  def playing_entry_changed (self, sp, entry):
    if sp.get_playing ():
      self.set_entry (entry)

  def playing_song_property_changed (self, sp, uri, property, old, new):
    relevant = False
    if sp.get_playing () and property in (NORMAL_SONG_ARTIST, STREAM_SONG_ARTIST):
      self.current_artist = new
      relevant = True
    elif sp.get_playing () and property in (NORMAL_SONG_TITLE, STREAM_SONG_TITLE):
      self.current_title = new
      relevant = True
    elif sp.get_playing () and property in (NORMAL_SONG_ALBUM, STREAM_SONG_ALBUM):
      self.current_album = new
      relevant = True

    if relevant:
      self.set_status ()

  def set_entry (self, entry):
    if entry == self.current_entry:
      return

    if self.current_entry == None:
      self.save_status ()
    self.current_entry = entry

    if entry is None:
      self.restore_status ()
      return

    self.set_status_from_entry ()

  def set_status_from_entry (self):
    db = self.shell.get_property ("db")
    self.current_artist = db.entry_get (self.current_entry, rhythmdb.PROP_ARTIST)
    self.current_title  = db.entry_get (self.current_entry, rhythmdb.PROP_TITLE)
    self.current_album  = db.entry_get (self.current_entry, rhythmdb.PROP_ALBUM)

    if self.current_entry.get_entry_type().category == rhythmdb.ENTRY_STREAM:
      if not self.current_artist:
        self.current_artist = db.entry_request_extra_metadata (self.current_entry, STREAM_SONG_ARTIST)
      if not self.current_title:
        self.current_title  = db.entry_request_extra_metadata (self.current_entry, STREAM_SONG_TITLE)
      if not self.current_album:
        self.current_album  = db.entry_request_extra_metadata (self.current_entry, STREAM_SONG_ALBUM)

    self.set_status ()

  def set_status (self):
    subs = {
        'artist': self.current_artist,
        'album': self.current_album,
        'title': self.current_title
    }
    if self.current_artist:
      if self.current_title:
        # Translators: do not translate %(artist)s or %(title)s, they are
        # string substitution markers (like %s) for the artist and title of
        # the current playing song.  They can be reordered if necessary.
        new_status = _(u"♫ %(artist)s - %(title)s ♫") % subs
      elif self.current_album:
        # Translators: do not translate %(artist)s or %(album)s, they are
        # string substitution markers (like %s) for the artist and album name
        # of the current playing song.  They can be reordered if necessary.
        new_status = _(u"♫ %(artist)s - %(album)s ♫") % subs
    elif self.current_album:
      # Translators: do not translate %(album)s, it is a string substitution
      # marker (like %s) for the album name of the current playing song.
      new_status = _(u"♫ %(album)s ♫") % subs
    elif self.current_title:
      # Translators: do not translate %(title)s, it is a string substitution
      # marker (like %s) for the title of the current playing song.
      new_status = _(u"♫ %(title)s ♫") % subs
    else:
      new_status = _(u"♫ Listening to music... ♫")

    self.set_gossip_status (new_status)
    self.set_empathy_status (new_status)

  def save_status (self):
    self.saved_gossip = self.get_gossip_status ()
    self.saved_empathy = self.get_empathy_status ()

  def restore_status (self):
    if self.saved_gossip != None:
      self.set_gossip_status (self.saved_gossip)
    if self.saved_empathy != None:
      self.set_empathy_status (self.saved_empathy)

  def set_gossip_status (self, new_status):
    if not use_gossip:
      return

    try:
      bus = dbus.SessionBus ()
      gossip_obj = bus.get_object (BUS_NAME, OBJ_PATH)
      gossip = dbus.Interface (gossip_obj, IFACE_NAME)

      state, status = gossip.GetPresence ("")
      gossip.SetPresence (state, new_status)
    except dbus.DBusException:
      pass

  def get_gossip_status (self):
    if not use_gossip:
      return

    try:
      bus = dbus.SessionBus ()
      gossip_obj = bus.get_object (BUS_NAME, OBJ_PATH)
      gossip = dbus.Interface (gossip_obj, IFACE_NAME)

      state, status = gossip.GetPresence ("")
      return status
    except dbus.DBusException:
      return None

  def set_empathy_status (self, new_status):
    if not use_empathy:
      return

    empathy_idle.set_status (new_status)

  def get_empathy_status (self):
    if not use_empathy:
      return

    return empathy_idle.get_status ()

