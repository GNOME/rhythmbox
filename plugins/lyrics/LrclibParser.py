# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2025 Jonathan Matthew
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
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.


import urllib.parse
import rb
import sys
import json

class LrclibParser (object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title

	def search(self, callback, *data):
		# lrclib.net/api/search?track_name=x&artist_name=y

		q = urllib.parse.urlencode({
			'track_name': self.title,
			'artist_name': self.artist
		})
		url = urllib.parse.urlunparse(('https', 'lrclib.net', 'api/search', '', q, ''))

		loader = rb.Loader()
		loader.get_url (url, self.got_lyrics, callback, *data)

	def got_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		if result is not None:
			j = json.loads(result)
			# just take the first one?
			if isinstance(j, list) and len(j) > 0:
				l = j[0]
				lyrics = l.get('plainLyrics')
				lyrics += "\n\nLyrics provided by lrclib.net"

				callback (lyrics, *data)
			else:
				callback (None, *data)
		else:
			callback (None, *data)


