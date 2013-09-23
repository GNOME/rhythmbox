# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Adam Zimmerman  <adam_zimmerman@sfu.ca>
# Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
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
import xml.sax, xml.sax.handler
import datetime, re

import rb
from gi.repository import RB

class TrackListHandler(xml.sax.handler.ContentHandler):
	def __init__(self, db, entry_type, sku_dict, home_dict, art_dict):
		xml.sax.handler.ContentHandler.__init__(self)
		self.__db = db
		self.__entry_type = entry_type
		self.__sku_dict = sku_dict
		self.__home_dict = home_dict
		self.__art_dict = art_dict
		self.__track = {}

	def startElement(self, name, attrs):
		self.__text = ""

	def endElement(self, name):
		if name == "Track":
			try:
				# prefer ogg streams to mp3
				if 'oggurl' in self.__track:
					trackurl = self.__track['oggurl']
				else:
					trackurl = self.__track['url']

				trackurl = str(trackurl)

				# add the track to the source
				entry = self.__db.entry_lookup_by_location (trackurl)
				if entry == None:
					entry = RB.RhythmDBEntry.new(self.__db, self.__entry_type, trackurl)

				# if year is not set, use launch date instead
				try:
					year = parse_int(self.__track['year'])
					if year <= 0:
						raise ValueError
				except ValueError:
					year = parse_int(self.__track['launchdate'][0:4])

				date = datetime.date(year, 1, 1).toordinal()
				try:
					tracknum = parse_int(self.__track['tracknum'])
				except ValueError:
					tracknum = 0
				try:
					duration = parse_int(self.__track['seconds'])
				except ValueError:
					duration = 0

				self.__db.entry_set(entry, RB.RhythmDBPropType.ARTIST, str(self.__track['artist']))
				self.__db.entry_set(entry, RB.RhythmDBPropType.ALBUM, str(self.__track['albumname']))
				self.__db.entry_set(entry, RB.RhythmDBPropType.TITLE, str(self.__track['trackname']))
				self.__db.entry_set(entry, RB.RhythmDBPropType.GENRE, str(self.__track['magnatunegenres']))
				self.__db.entry_set(entry, RB.RhythmDBPropType.TRACK_NUMBER, int(tracknum))
				self.__db.entry_set(entry, RB.RhythmDBPropType.DATE, int(date))
				self.__db.entry_set(entry, RB.RhythmDBPropType.DURATION, int(duration))

				key = str(trackurl)
				sku = sys.intern(str(self.__track['albumsku']))
				self.__sku_dict[key] = sku
				self.__home_dict[sku] = str(self.__track['home'])
				self.__art_dict[sku] = str(self.__track['cover_small'])

				self.__db.commit()
			except Exception as e: # This happens on duplicate uris being added
				sys.excepthook(*sys.exc_info())
				print("Couldn't add %s - %s" % (self.__track['artist'], self.__track['trackname']), e)

			self.__track = {}
		elif name == "AllSongs":
			pass # end of the file
		else:
			self.__track[name] = self.__text

	def characters(self, content):
		self.__text = self.__text + content

# parses partial integers
def parse_int(s):
	news = ""
	for c in s:
		if c in '0123456789': # only positive integers allowed
			news += c
		else:
			break
	return int(news)
