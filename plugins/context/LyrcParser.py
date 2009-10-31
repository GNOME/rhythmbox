# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2007 James Livingston
# Copyright (C) 2007 Sirio Bolaños Puchet
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

import urllib
import re
import rb

EXPS = ['\n', '\r', '<[iI][mM][gG][^>]*>', '<[aA][^>]*>[^<]*<\/[aA]>',
	'<[sS][cC][rR][iI][pP][tT][^>]*>[^<]*(<!--[^>]*>)*[^<]*<\/[sS][cC][rR][iI][pP][tT]>',
	'<[sS][tT][yY][lL][eE][^>]*>[^<]*(<!--[^>]*>)*[^<]*<\/[sS][tT][yY][lL][eE]>']
CEXPS = [re.compile (exp) for exp in EXPS]

SEPARATOR_RE = re.compile("<[fF][oO][nN][tT][ ]*[sS][iI][zZ][eE][ ]*='2'[ ]*>")


class LyrcParser (object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
	
	def search(self, callback, *data):
		path = 'http://www.lyrc.com.ar/en/'

		wartist = urllib.quote(self.artist)
		wtitle = urllib.quote(self.title)
		wurl = 'tema1en.php?artist=%s&songname=%s' % (wartist, wtitle)
		
		loader = rb.Loader()
		loader.get_url (path + wurl, self.got_lyrics, callback, *data)

	def got_lyrics(self, lyrics, callback, *data):
		if lyrics is None:
			callback (None, *data)
			return

		for exp in CEXPS:
			lyrics = exp.sub('', lyrics)

		lyricIndex = SEPARATOR_RE.search(lyrics)

		if lyricIndex is not None:
			callback(self.parse_lyrics(SEPARATOR_RE.split(lyrics, 1)[1]), *data)
		else:
			callback (None, *data)

	def parse_lyrics(self, lyrics):
		if re.search('<p><hr', lyrics):
			lyrics = re.split('<p><hr', lyrics, 1)[0]
		else:
			lyrics = re.split('<br><br>', lyrics, 1)[0]
		
		lyrics = re.sub('<[fF][oO][nN][tT][^>]*>', '', lyrics)
		title = re.split('(<[bB]>)([^<]*)', lyrics)[2]
		artist = re.split('(<[uU]>)([^<]*)', lyrics)[2]
		lyrics = re.sub('<[bB]>[^<].*<\/[tT][aA][bB][lL][eE]>', '', lyrics)
		lyrics = re.sub('<[Bb][Rr][^>]*>', '\n', lyrics)
		titl = "%s - %s\n\n" % (artist, title)
		lyrics = titl + lyrics
		lyrics += "\n\nLyrics provided by lyrc.com.ar"

		return lyrics

