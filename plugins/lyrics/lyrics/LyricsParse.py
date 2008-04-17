# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2007 James Livingston
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The Rhythmbox authors hereby grants permission for non-GPL compatible
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


import urllib
import re
import gobject
import gconf
import rb

from LyrcParser import LyrcParser
from AstrawebParser import AstrawebParser
from LeoslyricsParser import LeoslyricsParser
from LyricWikiParser import LyricWikiParser
from WinampcnParser import WinampcnParser


engines_map = {
	'lyrc.com.ar': LyrcParser,
	'astraweb.com': AstrawebParser,
	'leoslyrics.com': LeoslyricsParser,
	'lyricwiki.org': LyricWikiParser,
	'winampcn.com': WinampcnParser
}


class Parser (object):
	def __init__(self, gconf_keys, artist, title):
		self.title = title
		self.artist = artist

		try:
			self.engines = gconf.client_get_default().get_list(gconf_keys['engines'], gconf.VALUE_STRING)
			if self.engines is None:
				self.engines = []
		except gobject.GError, e:
			print e
			self.engines = []

	def searcher(self, plexer, callback, *data):
		for e in self.engines:
			plexer.clear()
			if e in engines_map:
				parser = engines_map[e] (self.artist, self.title)
				print "searching " + e + " for lyrics"

				parser.search(plexer.send())
				yield None

				_, (lyrics,) = plexer.receive()
				if lyrics is not None:
					callback (lyrics, *data)
					return

		callback (None, *data)

	def get_lyrics(self, callback, *data):
		rb.Coroutine (self.searcher, callback, *data).begin ()


