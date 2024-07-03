# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 Hardy Beltran Monasterios
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
import re
import sys

# Deal with html entities and utf-8
# code taken from django/utils/text.py

from html.entities import name2codepoint

pattern = re.compile(r"&(#?\w+?);")

def _replace_entity(match):
	text = match.group(1)
	if text[0] == u'#':
		text = text[1:]
		try:
			if text[0] in u'xX':
				c = int(text[1:], 16)
			else:
				c = int(text)
			return chr(c)
		except ValueError:
			return match.group(0)
	else:
		try:
			return chr(name2codepoint[text])
		except (ValueError, KeyError):
			return match.group(0)

def unescape_entities(text):
	return pattern.sub(_replace_entity, text)

class TerraParser (object):
	def __init__(self, artist, title):
		self.artist = artist
		self.title = title

	def search(self, callback, *data):
		path = 'http://letras.mus.br/'

		artist = urllib.parse.quote(self.artist)
		title = urllib.parse.quote(self.title)
		join = urllib.parse.quote(' - ')

		wurl = 'winamp.php?t=%s%s%s' % (artist, join, title)
		print("search URL: " + wurl)

		loader = rb.Loader()
		loader.get_url (path + wurl, self.got_lyrics, callback, *data)

	def got_lyrics(self, result, callback, *data):
		if result is None:
			callback (None, *data)
			return

		if result is not None:
			result = result.decode('utf-8')
			if re.search('M&uacute;sica n&atilde;o encontrada', result):
				print("not found")
				callback (None, *data)
			elif re.search('<div id="letra">', result):
				callback(self.parse_lyrics(result), *data)
			else:
				callback (None, *data)
		else:
			callback (None, *data)


	def parse_lyrics(self, source):
		def unspace(x):
			return " ".join(x.split())
		def untag(x):
			return re.sub('<.*?>', '\n', x)

		source = re.split('<div id="letra">', source)[1]
		source = re.split('</?div.*?>', source)
		# source[1] = artist+title
		# source[3] = lyrics

		header = "".join(source[1].splitlines())
		# <h1><a>title</a></h1> <h2><a>artist</a></h2>
		bits = re.findall('<h.>(.*?)</h.>', header)
		artistitle = unspace(untag(" - ".join(bits)))

		lyrics = unescape_entities(artistitle) + "\n" + unescape_entities(untag(source[3]))
		lyrics += "\n\nEsta letra foi disponibilizada pelo site\nhttp://letras.mus.br"
		return lyrics
