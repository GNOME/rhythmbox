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

import rhythmdb
import gnomekeyring as keyring
import xml.sax, xml.sax.handler
import datetime, re, urllib

class TrackListHandler(xml.sax.handler.ContentHandler):
	def __init__(self, db, entry_type, sku_dict, home_dict, art_dict, account_type, username, password):
		xml.sax.handler.ContentHandler.__init__(self)
		self.__db = db
		self.__entry_type = entry_type
		self.__sku_dict = sku_dict
		self.__home_dict = home_dict
		self.__art_dict = art_dict
		self.__track = {}
		self.__account_type = account_type
		self.__user = urllib.quote(username)
		self.__pw = urllib.quote(password)
		self.__URIre = re.compile(r'^http://[^.]+\.magnatune\.com/')
		self.__nsre = re.compile(r'\.(mp3|ogg)$')

	def startElement(self, name, attrs):
		self.__text = ""

	def fix_trackurl(self, trackurl):
		trackurl = self.__URIre.sub("http://%s:%s@%s.magnatune.com/" % (self.__user, self.__pw, self.__account_type), trackurl)
		trackurl = self.__nsre.sub(r"_nospeech.\1", trackurl)
		return trackurl

	def endElement(self, name):
		if name == "Track":
			try:
				# prefer ogg streams to mp3
				if 'oggurl' in self.__track:
					trackurl = self.__track['oggurl']
				else:
					trackurl = self.__track['url']
				# use ad-free tracks if available
				if self.__account_type != 'none':
					trackurl = self.fix_trackurl(trackurl)

				# add the track to the source
				entry = self.__db.entry_lookup_by_location (trackurl)
				if entry == None:
					entry = self.__db.entry_new(self.__entry_type, trackurl)

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

				self.__db.set(entry, rhythmdb.PROP_ARTIST, self.__track['artist'])
				self.__db.set(entry, rhythmdb.PROP_ALBUM, self.__track['albumname'])
				self.__db.set(entry, rhythmdb.PROP_TITLE, self.__track['trackname'])
				self.__db.set(entry, rhythmdb.PROP_TRACK_NUMBER, tracknum)
				self.__db.set(entry, rhythmdb.PROP_DATE, date)
				self.__db.set(entry, rhythmdb.PROP_GENRE, self.__track['magnatunegenres'])
				self.__db.set(entry, rhythmdb.PROP_DURATION, duration)

				key = str(trackurl)
				sku = intern(str(self.__track['albumsku']))
				self.__sku_dict[key] = sku
				self.__home_dict[sku] = str(self.__track['home'])
				self.__art_dict[sku] = str(self.__track['cover_small'])

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

# parses partial integers
def parse_int(s):
	news = ""
	for c in s:
		if c in '0123456789': # only positive integers allowed
			news += c
		else:
			break
	return int(news)
