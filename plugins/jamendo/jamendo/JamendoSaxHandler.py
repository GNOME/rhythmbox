# -*- coding: utf-8 -*-

# JamendoSaxHandler.py
#
# Copyright (C) 2007 - Guillaume Desmottes
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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

import xml.sax, xml.sax.handler

markups = ["JamendoData", "Artists", "artist", "Albums", "album", "Tracks", "track"]
ignore = ["location", "country", "state", "city", "latitude", "longitude"]

class JamendoSaxHandler(xml.sax.handler.ContentHandler):
	def __init__(self):
		xml.sax.handler.ContentHandler.__init__(self)

		self.current = {}

	def startElement(self, name, attrs):
		self.__text = ""
		self.__ignore = False

		if name in markups:
			fct = getattr (self, "start" + name)
			fct (attrs)

		if name in ignore:
			self.__ignore = True

	def endElement(self, name):
		if name in markups:
			fct = getattr (self, "end" + name)
			fct ()
		elif self.__ignore is False:
			self.current[name] = self.__text

	def characters(self, content):
		if self.__ignore is False:
			self.__text = self.__text + content

	# start markups
	def startJamendoData (self, attrs):
		pass

	def startArtists (self, attrs):
		self.artists = {}

	def startartist (self, attrs):
		self.artist = {}
		for attr in attrs.getNames():
			self.artist[attr] = attrs[attr]
		self.current = self.artist

	def startAlbums (self, attrs):
		self.albums = {}

	def startalbum (self, attrs):
		self.album = {}
		for attr in attrs.getNames():
			self.album[attr] = attrs[attr]
		self.current = self.album

	def startTracks (self, attrs):
		self.tracks = {}

	def starttrack (self, attrs):
		self.track = {}
		for attr in attrs.getNames():
			self.track[attr] = attrs[attr]
		self.current = self.track

	# end markups
	def endJamendoData (self):
		pass # end of file

	def endArtists (self):
		pass # we have load all artists

	def endartist (self):
		self.artists[self.artist['id']] = self.artist
		
	def endAlbums (self):
		self.artist['ALBUMS'] = self.albums

	def endalbum (self):
		self.albums[self.album['id']] = self.album

	def endTracks (self):
		self.album['TRACKS'] = self.tracks

	def endtrack (self):
		self.tracks[self.track['id']] = self.track


if __name__ == "__main__":
	parser = xml.sax.make_parser()
	handler = JamendoSaxHandler()
	parser.setContentHandler(handler)
	datasource = open("/tmp/dbdump.en.xml")
	#datasource = open("exemple_jamendo.xml")
	parser.parse(datasource)
	#print handler.artists
	#print handler.albums
	#print handler.tracks

	tracks = handler.tracks
	artists = handler.artists
	albums = handler.albums
	for track_key in tracks.keys():
		track = tracks[track_key]
		album = albums[track['albumID']]
		artist = artists[album['artistID']]
		#print track['dispname'], track['trackno'], track['lengths'], album['dispname'], artist['dispname']
		print album['P2PLinks']
