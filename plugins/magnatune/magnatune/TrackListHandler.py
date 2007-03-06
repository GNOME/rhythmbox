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
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import rhythmdb
import xml.sax, xml.sax.handler
import datetime

class TrackListHandler(xml.sax.handler.ContentHandler):

	def __init__(self, db, entry_type, sku_dict, home_dict, buy_dict):
		xml.sax.handler.ContentHandler.__init__(self)
		self.__db = db
		self.__entry_type = entry_type
		self.__sku_dict = sku_dict
		self.__home_dict = home_dict
		self.__buy_dict = buy_dict
		self.__track = {}

	def startElement(self, name, attrs):
		self.__text = ""

	def endElement(self, name):
		if name == "Track":
			try:
				# add the track to the source
				entry = self.__db.entry_lookup_by_location (self.__track['url'])
				if entry == None:
					entry = self.__db.entry_new(self.__entry_type, self.__track['url'])

				# if year is not set, use launch date instead
				try:
					year = int(self.__track['year'])
					if (year <= 0):
						raise ValueError
				except ValueError:
					year = int(self.__track['launchdate'][0:4])

				date = datetime.date(year, 1, 1).toordinal()

				self.__db.set(entry, rhythmdb.PROP_ARTIST, self.__track['artist'])
				self.__db.set(entry, rhythmdb.PROP_ALBUM, self.__track['albumname'])
				self.__db.set(entry, rhythmdb.PROP_TITLE, self.__track['trackname'])
				self.__db.set(entry, rhythmdb.PROP_TRACK_NUMBER, int(self.__track['tracknum']))
				self.__db.set(entry, rhythmdb.PROP_DATE, date)
				self.__db.set(entry, rhythmdb.PROP_GENRE, self.__track['mp3genre'])
				self.__db.set(entry, rhythmdb.PROP_DURATION, int(self.__track['seconds']))
				self.__sku_dict[self.__track['url']] = self.__track['albumsku']
				self.__home_dict[self.__track['url']] = self.__track['home']
				self.__buy_dict[self.__track['url']] = self.__track['buy'].replace("buy_album", "buy_cd", 1)

				self.__db.commit()
			except Exception,e: # This happens on duplicate uris being added
				print "Couldn't add %s - %s" % (self.__track['artist'], self.__track['trackname']), e

			self.__track = {}
		elif name == "AllSongs":
			pass # end of the file
		else:
			self.__track[name] = self.__text

	def characters(self, content):
		self.__text = self.__text + content
