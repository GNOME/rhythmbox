# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2011 Jonathan Matthew  <jonathan@d14n.org>
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

import os.path
import urllib.request

import rb
from gi.repository import RB

import gettext
gettext.install('rhythmbox', RB.locale_dir())

ART_FOLDER = os.path.expanduser(os.path.join(RB.user_cache_dir(), 'covers'))
USEFUL = os.path.exists(ART_FOLDER)

class OldCacheSearch(object):
	def __init__(self):
		pass

	def filename (self, album, artist, extension):
		artist = artist.replace('/', '-')
		album = album.replace('/', '-')
		return os.path.join(ART_FOLDER, '%s - %s.%s' % (artist, album, extension))

	def search(self, key, last_time, store, callback, *args):
		album = key.get_field("album")
		artists = key.get_field_values("artist") or []

		print("looking for %s by %s" % (album, str(artists)))
		for artist in artists:
			for ext in ('jpg', 'png'):
				path = self.filename(album, artist, ext)
				if os.path.exists(path):
					print("found %s" % path)
					uri = "file://" + urllib.request.pathname2url(path)
					storekey = RB.ExtDBKey.create_storage('album', album)
					storekey.add_field("artist", artist)
					store.store_uri(storekey, RB.ExtDBSourceType.SEARCH, uri)
					callback(*args)
					return

		callback(*args)
