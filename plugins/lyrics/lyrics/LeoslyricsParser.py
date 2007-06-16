# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2005 Eduardo Gonzalez
# Copyright (C) 2006 Jonathan Matthew
# Copyright (C) 2007 James Livingston
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
from xml.dom import minidom


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

		try:
			xmldoc = minidom.parseString(lyrics).documentElement
		except e:
			print e
			callback (None, *data)
			return

		result_code = xmldoc.getElementsByTagName('response')[0].getAttribute('code')
		if result_code != '0':
			xmldoc.unlink()
			callback (None, *data)
			return
		
		matches = xmldoc.getElementsByTagName('result')[:10]
		
		i = 0
		for match in matches:
			title = match.getElementsByTagName('title')[0].firstChild.data
			artist = match.getElementsByTagName('name')[0].firstChild.data
			
			if (re.search(self.title.lower().strip(), title.lower().strip()) and
					re.search(self.artist.lower().strip(), artist.lower().strip())):
				continue

			matches = matches[i:]
			i += 1
		
		hids = map(lambda x: x.getAttribute('hid'), matches)

		if len(hids) == 0:
			xmldoc.unlink()
			callback (None, *data)
			return

		xmldoc.unlink()

		lurl = "http://api.leoslyrics.com/api_lyrics.php?auth=Rhythmbox&hid=%s" % (urllib.quote(hids[0].encode('utf-8')))
		loader = rb.Loader()
		loader.get_url (lurl, self.parse_lyrics, callback, *data)


	def parse_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		try:
			xmldoc = minidom.parseString(result).documentElement
		except e:
			print e
			callback (None, *data)
			return

		lyrics = xmldoc.getElementsByTagName('title')[0].firstChild.nodeValue
		lyrics += ' - ' + xmldoc.getElementsByTagName('artist')[0].getElementsByTagName('name')[0].firstChild.nodeValue + '\n\n'
		lyrics += xmldoc.getElementsByTagName('text')[0].firstChild.nodeValue
		xmldoc.unlink()

		lyrics += "\n\nLyrics provided by leoslyrics.com"

		callback (lyrics, *data)
