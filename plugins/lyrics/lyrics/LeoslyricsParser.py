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
# The Rhythmbox authors hereby grants permission for non-GPL compatible
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

from xml.etree import cElementTree



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

		#FIXME: check non-exact matches
		match = element.find("searchResults").find("result")
		if match.attrib["exactMatch"] is None:
			print "no exact match:" + lyrics
			callback (None, *data)
			return

		lurl = "http://api.leoslyrics.com/api_lyrics.php?auth=Rhythmbox&hid=%s" % (urllib.quote(match.attrib["hid"].encode('utf-8')))
		loader = rb.Loader()
		loader.get_url (lurl, self.parse_lyrics, callback, *data)
			
	def parse_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		element = cElementTree.fromstring(result)

		lyrics = element.find('lyric').find('text').text
		lyrics += "\n\nLyrics provided by leoslyrics.com"

		callback (lyrics, *data)
