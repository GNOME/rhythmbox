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

class AstrawebParser (object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
		
	def search(self, callback, *data):
		wartist = re.sub('%20', '+', urllib.quote(self.artist))
		wtitle = re.sub('%20', '+', urllib.quote(self.title))

		wurl = 'http://search.lyrics.astraweb.com/?word=%s+%s' % (wartist, wtitle)

		loader = rb.Loader()
		loader.get_url (wurl, self.got_results, callback, *data)

	def got_results (self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		results = re.sub('\n', '', re.sub('\r', '', result))

		if re.search('(<tr><td bgcolor="#BBBBBB".*)(More Songs &gt)', results) is not None:
			body = re.split('(<tr><td bgcolor="#BBBBBB".*)(More Songs &gt)', results)[1]
			entries = re.split('<tr><td bgcolor="#BBBBBB"', body)
			entries.pop(0)
			for entry in entries:
				url = re.split('(\/display[^"]*)', entry)[1]
				artist = re.split('(Artist:.*html">)([^<]*)', entry)[2]
				title = re.split('(\/display[^>]*)([^<]*)', entry)[2][1:]
							
				if not ((re.search(self.title.lower().strip(), title.lower().strip()) is None)):
					if not (re.search(self.artist.lower().strip(), artist.lower().strip()) is None):
						loader = rb.Loader()
						loader.get_url ('http://display.lyrics.astraweb.com' + url, self.parse_lyrics, callback, *data)
						return
				continue

		callback (None, *data)
		return

	def parse_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		result = re.sub('\n', '', re.sub('\r', '', result))
	   
		artist_title = re.split('(<title>Lyrics: )([^<]*)', result)[2]
		artist = artist_title.split( " - " )[0]
		title  = artist_title.split( " - " )[1]
		
		title = "%s - %s\n\n" % (artist, title)
		lyrics = re.split('(<font face=arial size=2>)(.*)(<\/font><br></td><td*)', result)[2]
		lyrics = title + lyrics
		lyrics = re.sub('<[Bb][Rr][^>]*>', '\n', lyrics)
		lyrics += "\n\nLyrics provided by lyrics.astraweb.com"

		callback (lyrics, *data)

