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

import urllib.parse
import re
import rb

# these numbers pulled directly from the air
artist_match = 0.8
title_match = 0.5

class AstrawebParser (object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
		
	def search(self, callback, *data):
		wartist = urllib.parse.quote_plus(self.artist)
		wtitle = urllib.parse.quote_plus(self.title)

		wurl = 'http://search.lyrics.astraweb.com/?word=%s+%s' % (wartist, wtitle)

		loader = rb.Loader()
		loader.get_url (wurl, self.got_results, callback, *data)

	def got_results (self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		result = result.decode('iso-8859-1')	# no indication of anything else..
		results = re.sub('\n', '', re.sub('\r', '', result))

		if re.search('(<tr><td bgcolor="#BBBBBB".*)(More Songs &gt)', results) is not None:
			body = re.split('(<tr><td bgcolor="#BBBBBB".*)(More Songs &gt)', results)[1]
			entries = re.split('<tr><td bgcolor="#BBBBBB"', body)
			entries.pop(0)
			print("found %d entries; looking for [%s,%s]" % (len(entries), self.title, self.artist))
			for entry in entries:
				url = re.split('(\/display[^"]*)', entry)[1]
				artist = re.split('(Artist:.*html">)([^<]*)', entry)[2].strip()
				title = re.split('(\/display[^>]*)([^<]*)', entry)[2][1:].strip()

				if self.artist != "":
					artist_str = rb.string_match(self.artist, artist)
				else:
					artist_str = artist_match + 0.1

				title_str = rb.string_match(self.title, title)

				print("checking [%s,%s]: match strengths [%f,%f]" % (title.strip(), artist.strip(), title_str, artist_str))
				if title_str > title_match and artist_str > artist_match:
					loader = rb.Loader()
					loader.get_url ('http://display.lyrics.astraweb.com' + url, self.parse_lyrics, callback, *data)
					return

		callback (None, *data)
		return

	def parse_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		result = result.decode('iso-8859-1')
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
