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

import rhythmdb
import xml.sax, xml.sax.handler
import datetime

data = {"artist" : ["name"],
        "album" : ["name","id","releasedate","id3genre"],
        "track" : ["name","id","numalbum","duration","id3genre"]}
        
stream_url = "http://api.jamendo.com/get2/stream/track/redirect/?id=%s&streamencoding=mp31"

class JamendoSaxHandler(xml.sax.handler.ContentHandler):
	def __init__(self,db,entry_type):
		xml.sax.handler.ContentHandler.__init__(self)
		self.__db = db
		self.__entry_type = entry_type
		self.__data = {}
		for section in data:
			self.__data[section]={}
		self.__section = ""
		self.__num_tracks = 0


	def startElement(self, name, attrs):
		self.__text = ""
		self.__parse_content = False

		if name in data:
			self.__section = name
		elif self.__section and name in data[self.__section]:
			self.__parse_content = True

	def endElement(self, name):
		if self.__parse_content:
			self.__data[self.__section][name] = self.__text
		elif name == "track":
			self.__num_tracks = self.__num_tracks + 1

			track_url = stream_url % (self.__data["track"]["id"])	
						
			release_date = self.__data["album"]["releasedate"]
			year = int(release_date[0:4])
			date = datetime.date(year, 1, 1).toordinal()

			try:
				albumgenre = genre_id3[int(self.__data["album"]["id3genre"])]
			except Exception:
				albumgenre = _('Unknown')			

			try:
				duration = int(float(self.__data["track"]["duration"]))
			except Exception:
				duration = 0
			
			entry = self.__db.entry_lookup_by_location (track_url)
			if entry == None:
				entry = self.__db.entry_new(self.__entry_type, track_url)
			self.__db.set(entry, rhythmdb.PROP_ARTIST, self.__data["artist"]["name"])
			self.__db.set(entry, rhythmdb.PROP_ALBUM, self.__data["album"]["name"])
			self.__db.set(entry, rhythmdb.PROP_TITLE, self.__data["track"]["name"])
			self.__db.set(entry, rhythmdb.PROP_TRACK_NUMBER, int(self.__data["track"]["numalbum"]))
			self.__db.set(entry, rhythmdb.PROP_DATE, date)
			self.__db.set(entry, rhythmdb.PROP_GENRE, albumgenre)
			self.__db.set(entry, rhythmdb.PROP_DURATION, duration)

			# slight misuse, but this is far more efficient than having a python dict
			# containing this data.
			self.__db.set(entry, rhythmdb.PROP_MUSICBRAINZ_ALBUMID, self.__data["album"]["id"])
			
			if self.__num_tracks % 1000 == 0:
				self.__db.commit()
		elif name == "JamendoData":
			self.__db.commit()
		#clean up data
		if name in data:
			self.__data[name].clear ()

	def characters(self, content):
		if self.__parse_content:
			self.__text = self.__text + content

genre_id3 = ["Blues","Classic Rock","Country","Dance","Disco","Funk","Grunge","Hip-Hop","Jazz","Metal","New Age","Oldies","Other","Pop","R&B","Rap","Reggae","Rock","Techno","Industrial","Alternative","Ska","Death Metal","Pranks","Soundtrack","Euro-Techno","Ambient","Trip-Hop","Vocal","Jazz+Funk","Fusion","Trance","Classical","Instrumental","Acid","House","Game","Sound Clip","Gospel","Noise","AlternRock","Bass","Soul","Punk","Space","Meditative","Instrumental Pop","Instrumental Rock","Ethnic","Gothic","Darkwave","Techno-Industrial","Electronic","Pop-Folk","Eurodance","Dream","Southern Rock","Comedy","Cult","Gangsta","Top 40","Christian Rap","Pop/Funk","Jungle","Native American","Cabaret","New Wave","Psychadelic","Rave","Showtunes","Trailer","Lo-Fi","Tribal","Acid Punk","Acid Jazz","Polka","Retro","Musical","Rock & Roll","Hard Rock","Folk","Folk-Rock","National Folk","Swing","Fast Fusion","Bebob","Latin","Revival","Celtic","Bluegrass","Avantgarde","Gothic Rock","Progressive Rock","Psychedelic Rock","Symphonic Rock","Slow Rock","Big Band","Chorus","Easy Listening","Acoustic","Humour","Speech","Chanson","Opera","Chamber Music","Sonata","Symphony","Booty Bass","Primus","Porn Groove","Satire","Slow Jam","Club","Tango","Samba","Folklore","Ballad","Power Ballad","Rhythmic Soul","Freestyle","Duet","Punk Rock","Drum Solo","Acapella","Euro-House","Dance Hall"]
