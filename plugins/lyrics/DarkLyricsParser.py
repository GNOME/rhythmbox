# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2008, 2009, 2010 Edgar Luna
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

import re
import string
import rb
import stringmatch

min_artist_match = .5
min_song_match = .5

class DarkLyricsParser (object):
	"""Parser for Lyrics from www.darklyrics.com"""


	def __init__(self, artist, title):
		self.artist = artist
		self.title = title
		self.artist_ascii = ''
		self.titlenumber = ''

	def search(self, callback, *data):
		"""Do a request of a specific url based on artist's first letter name."""

		self.artist_ascii = ''.join(c for c in self.artist.lower() \
						    if c in string.ascii_letters)
		self.artist_ascii = self.artist_ascii.lower()
		firstcharurl = 'http://www.darklyrics.com/%s.html' % (self.artist_ascii[0])
		loader = rb.Loader()
		loader.get_url (firstcharurl, self.search_artist, callback, *data)

	def search_artist(self, artist_page, callback, *data):
		"""Search for the link to the page of artist in artists_page
		"""
		if artist_page is None:
			callback (None, *data)
			return
		artist_page = artist_page.decode('iso-8859-1')
		link_section = re.split ('tban.js', artist_page, 1)[1]
		pattern_link =  '<a href="'
		pattern_artist = '([^"]*)">*([^<]*)<'
		links = re.split (pattern_link, link_section.lower())
		links.pop(0)
		best_match = ()
		smvalue_bestmatch = 0
		for line in links:
			artist = re.findall(pattern_artist, line)
			if len(artist) == 0:
				continue
			artist_link, artist_name = artist[0]
			artist_url = 'http://www.darklyrics.com/%s' % (artist_link)
			if artist_link[:5] == 'http:':
				continue
			artist_name = artist_name.strip()
			smvalue = stringmatch.string_match (artist_name, self.artist_ascii)
			if smvalue > min_artist_match and smvalue > smvalue_bestmatch:
				best_match = (smvalue, artist_url, artist_name)
				smvalue_bestmatch = smvalue

		if not best_match:
			# Lyrics are located in external site
			callback (None, *data)
			return
		loader = rb.Loader ()
		self.artist  = best_match[2]
		loader.get_url (best_match[1], self.search_song, callback, *data)

	class SongFound (object):
		def __init__ (self, smvalue, title, number, album, artist):
			self.smvalue = smvalue
			self.title = title
			self.number = number
			self.album = album
			self.artist = artist

		def __str__(self):
			return '(' + str(self.smvalue) + '. ' + self.title + '. ' + self.album + '. ' + self.artist + ')'

	def search_song (self, songlist, callback, *data):
		"""If artist's page is found, search_song looks for the song.

		The artist page contains a list of all the albums and
		links to the songs lyrics from this.
		"""
		if songlist is None:
			callback (None, *data)
			return
		songlist = songlist.decode('iso-8859-1')
		# Search for all the <a>
		# filter for those that has the artist name string_match
		#        and for those which its content is artist string_match
		# Sort by values given from string_match
		# and get the best
		link_section = re.split('LYRICS</h1>', songlist)[1]
		link_section = link_section.lower()
		pattern_song = '<a href="../lyrics/(.*)/(.*).html#([^"]+)">(.*)</a>'
		matches = re.findall (pattern_song.lower(), link_section)
		best_match = ""
		for line in matches:
			artist, album, number, title = line
			smvalue = stringmatch.string_match (title.lower().replace(' ', '' ),
					   self.title.lower().replace(' ', ''))
			if smvalue > min_song_match:
				best_match  = self.SongFound(smvalue,
							     title,
							     number,
							     album,
							     artist)
				break
		if not best_match:
			callback (None, *data)
			return
		loader = rb.Loader ()
		url = 'http://www.darklyrics.com/lyrics/%s/%s.html' % (best_match.artist, best_match.album)
		self.title = best_match.title
		self.titlenumber = best_match.number
		loader.get_url (url, self.parse_lyrics, callback, *data)

	def parse_lyrics (self, album, callback, *data):
		"""In the album's page parse_lyrics get the lyrics of the song.

		This page contains all the lyrics for self.album, but
		this method get rides of everything that isn't the
		lyrics of self.title"""
		if album is None:
			callback (None, *data)
			return
		album = album.decode('iso-8859-1')
		titleline = '<a name="%s">%s. %s(.*?)</a>' % \
		    (self.titlenumber, self.titlenumber, re.escape(self.title.title()))
		lyricmatch = re.split (titleline, album)
		if len (lyricmatch) > 2:
			lyrics = lyricmatch[2]
			lyrics = lyrics.split('<h3>')[0]
			lyrics = lyrics.replace ('\r', "")
			lyrics = re.sub (r'<.*?>', "", lyrics)
			lyrics = lyrics.strip ("\n")
			title = "%s - %s\n\n" % (self.artist.title(), self.title.title())

			lyrics = title + str (lyrics)
			lyrics += "\n\nLyrics provided by Dark Lyrics"
			callback (lyrics, *data)
		else:
			callback (None, *data)
			return
