# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2007 Austin  <austiny@sohu.com>
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

import sys
import urllib.parse
import re
import rb
from xml.dom import minidom

class WinampcnParser(object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
	
	def search(self, callback, *data):

		# encode search string
		title_encode = urllib.parse.quote(self.title.replace(' ', '').encode('gbk'))
		artist_encode = urllib.parse.quote(self.artist.replace(' ', '').encode('gbk'))
		url = 'http://www.winampcn.com/lyrictransfer/get.aspx?song=%s&artist=%s&lsong=%s&Datetime=20060601' % (title_encode, artist_encode, title_encode)
		
		loader = rb.Loader()
		loader.get_url (url, self.got_lyrics, callback, *data)
		
	def got_lyrics(self, xmltext, callback, *data):
		# retrieve xml content
		if xmltext is None:
			print("no response")
			callback (None, *data)
			return
		xmltext = xmltext.decode('gbk')

		try:
			xmltext = xmltext.replace('encoding="gb2312"', 'encoding="UTF-8"')
			xmldoc = minidom.parseString(xmltext)
			root = xmldoc.documentElement

			lrcurl = root.getElementsByTagName('LyricUrl')[0].childNodes[0].data
			if lrcurl is None:
				print("no lyric urls")
				callback (xmltext, *data)
				return

			# download the lyrics file
			lrcurl = lrcurl.replace('%3A', ':');
			print("url: %s" % lrcurl)

			loader = rb.Loader()
			loader.get_url (lrcurl, self.parse_lyrics, callback, *data)
		except:
			callback (None, *data)


	def parse_lyrics(self, lyrics, callback, *data):

		if lyrics is None:
			print("no lyrics")
			callback (None, *data)
			return

		# transform it into plain text
		lrcplaintext = lyrics.decode('gbk')
		try:
			lrcplaintext = re.sub(r'\[.*?\]', '', lrcplaintext)
		except:
			print("unable to decode lyrics")
			callback (None, *data)
			return

		lrcplaintext += "\n\nLyrics provided by winampcn.com"
		callback(lrcplaintext, *data)
