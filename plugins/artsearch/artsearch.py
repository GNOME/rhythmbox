# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2011 Jonathan Matthew
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

from gi.repository import GObject, Peas, RB, GdkPixbuf

import gettext
gettext.install('rhythmbox', RB.locale_dir())

from songinfo import AlbumArtPage

import oldcache
from lastfm import LastFMSearch
from local import LocalSearch
from musicbrainz import MusicBrainzSearch
from embedded import EmbeddedSearch

class Search(object):
	def __init__(self, store, key, last_time, searches):
		self.store = store
		self.key = key.copy()
		self.last_time = last_time
		self.searches = searches

	def next_search(self):
		if len(self.searches) == 0:
			album = self.key.get_field("album")
			if album is not None:
				key = RB.ExtDBKey.create_storage("album", album)
				key.add_field("artist", self.key.get_field("artist"))
				self.store.store(key, RB.ExtDBSourceType.NONE, None)
			return False

		search = self.searches.pop(0)
		search.search(self.key, self.last_time, self.store, self.search_done, None)
		return True

	def search_done(self, args):
		self.next_search()

class ArtSearchPlugin (GObject.GObject, Peas.Activatable):
	__gtype_name__ = 'ArtSearchPlugin'
	object = GObject.property(type=GObject.GObject)

	def __init__ (self):
		GObject.GObject.__init__ (self)

	def do_activate (self):
		self.art_store = RB.ExtDB(name="album-art")
		self.req_id = self.art_store.connect("request", self.album_art_requested)

		shell = self.object
		self.csi_id = shell.connect("create_song_info", self.create_song_info)

	def do_deactivate (self):
		self.art_store.disconnect(self.req_id)
		self.req_id = 0
		self.art_store = None

		shell = self.object
		shell.disconnect(self.csi_id)
		self.csi_id = 0

	def album_art_requested(self, store, key, last_time):
		searches = []
		if oldcache.USEFUL:
			searches.append(oldcache.OldCacheSearch())
		searches.append(EmbeddedSearch())
		searches.append(LocalSearch())
		searches.append(MusicBrainzSearch())
		searches.append(LastFMSearch())

		s = Search(store, key, last_time, searches)
		return s.next_search()

	def create_song_info(self, shell, song_info, is_multiple):
		if is_multiple is False:
			x = AlbumArtPage(shell, song_info)
