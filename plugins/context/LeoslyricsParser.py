# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Jonathan Matthew
# Copyright (C) 2007 James Livingston
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


import urllib
import re
import rb

from rb.stringmatch import string_match

# these numbers pulled directly from the air
artist_match = 0.8
title_match = 0.5

# Python 2.4 compatibility
try:
	from xml.etree import cElementTree
except:
	import cElementTree



class LeoslyricsParser(object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
	
	def search(self, callback, *data):
		artist = urllib.quote(self.artist)
		title = urllib.quote(self.title)

		htstring = 'http://api.leoslyrics.com/api_search.php?auth=Rhythmbox&artist=%s&songtitle=%s' % (artist, title)
			
		loader = rb.Loader()
		loader.get_url (htstring, self.got_lyrics, callback, *data)

	def got_lyrics (self, lyrics, callback, *data):
		if lyrics is None:
			callback (None, *data)
			return

		element = cElementTree.fromstring(lyrics)
		if element.find("response").attrib['code'] is not '0':
			print "got failed response:" + lyrics
			callback (None, *data)
			return

		match = None
		matches = element.find("searchResults").findall("result")
		print "got %d result(s)" % (len(matches))
		for m in matches:
			matchtitle = m.findtext("title")
			matchartist = m.findtext("artist/name")

			# if we don't know the artist, then anyone will do
			if self.artist != "":
				artist_str = string_match(self.artist, matchartist)
			else:
				artist_str = artist_match + 0.1

			title_str = string_match(self.title, matchtitle)
			if artist_str > artist_match and title_str > title_match:
				print "found acceptable match, artist: %s (%f), title: %s (%f)" % (matchartist, artist_str, matchtitle, title_str)
				match = m
				break
			else:
				print "skipping match, artist: %s (%f), title: %s (%f)" % (matchartist, artist_str, matchtitle, title_str)

		if match is not None:
			hid = m.attrib['hid'].encode('utf-8')
			lurl = "http://api.leoslyrics.com/api_lyrics.php?auth=Rhythmbox&hid=%s" % (urllib.quote(hid))
			loader = rb.Loader()
			loader.get_url (lurl, self.parse_lyrics, callback, *data)
		else:
			print "no acceptable match found"
			callback (None, *data)


	def parse_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		element = cElementTree.fromstring(result)

		lyrics = element.find('lyric').find('text').text
		lyrics += "\n\nLyrics provided by leoslyrics.com"

		callback (lyrics, *data)

