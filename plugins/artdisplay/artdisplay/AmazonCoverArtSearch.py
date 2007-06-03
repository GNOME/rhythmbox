# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Gareth Murphy, Martin Szulecki
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

from xml.dom import minidom
import re
import locale
import urllib

import rhythmdb

LICENSE_KEY = "18C3VZN9HCECM5G3HQG2"
DEFAULT_LOCALE = "en_US"
ASSOCIATE = "webservices-20"


class Bag: pass

class AmazonCoverArtSearch (object):
	def __init__ (self, loader):
		self.searching = False
		self.cancel = False
		self.loader = loader
		self._supportedLocales = {
			"en_US" : ("us", "xml.amazon.com"),
			"en_GB" : ("uk", "xml-eu.amazon.com"),
			"de" : ("de", "xml-eu.amazon.com"),
			"ja" : ("jp", "xml.amazon.co.jp")
		}
		self.db = None
		self.entry = None

	def __get_locale (self):
		default = locale.getdefaultlocale ()
		lc_id = DEFAULT_LOCALE
		if default[0] is not None:
			if self._supportedLocales.has_key (default[0]):
				lc_id = default[0]

		lc_host = self._supportedLocales[lc_id][1]
		lc_name = self._supportedLocales[lc_id][0]
		return ((lc_host, lc_name))

	def search (self, db, entry, on_search_completed_callback, *args):
		self.searching = True
		self.cancel = False
		self.db = db
		self.entry = entry
		self.on_search_completed_callback = on_search_completed_callback
		self.args = args
		self.keywords = []

		st_artist = db.entry_get (entry, rhythmdb.PROP_ARTIST) or _("Unknown")
		st_album = db.entry_get (entry, rhythmdb.PROP_ALBUM) or _("Unknown")

		# Tidy up

		# Replace quote characters
		# don't replace single quote: could be important punctuation
		for char in ["\""]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')


		self.st_album = st_album
		self.st_artist = st_artist

		# Remove variants of Disc/CD [1-9] from album title before search
		for exp in ["\([Dd]isc *[1-9]+\)", "\([Cc][Dd] *[1-9]+\)"]:
			p = re.compile (exp)
			st_album = p.sub ('', st_album)

		st_album_no_vol = st_album
		for exp in ["\(*[Vv]ol.*[1-9]+\)*"]:
			p = re.compile (exp)
			st_album_no_vol = p.sub ('', st_album_no_vol)

		self.st_album_no_vol = st_album_no_vol

		# Save current search's entry properties
		self.search_album = st_album
		self.search_artist = st_artist
		self.search_album_no_vol = st_album_no_vol
		
		# TODO: Improve to decrease wrong cover downloads, maybe add severity?
		# Assemble list of search keywords (and thus search queries)
		if st_album == _("Unknown"):
			self.keywords.append ("%s Best of" % (st_artist))
			self.keywords.append ("%s Greatest Hits" % (st_artist))
			self.keywords.append ("%s Essential" % (st_artist))
			self.keywords.append ("%s Collection" % (st_artist))
			self.keywords.append ("%s" % (st_artist))
		elif st_artist == _("Unknown"):
			self.keywords.append ("%s" % (st_album))
			if st_album_no_vol != st_artist:
				self.keywords.append ("%s" % (st_album_no_vol))
			self.keywords.append ("Various %s" % (st_album))
		else:
			if st_album != st_artist:
				self.keywords.append ("%s %s" % (st_artist, st_album))
				if st_album_no_vol != st_album:
					self.keywords.append ("%s %s" % (st_artist, st_album_no_vol))
				if (st_album != _("Unknown")):
					self.keywords.append ("Various %s" % (st_album))
			self.keywords.append ("%s" % (st_artist))

		# Initiate asynchronous search
		self.search_next ();

	def __build_url (self, keyword):
		(lc_host, lc_name) = self.__get_locale ()

		url = "http://" + lc_host + "/onca/xml3?f=xml"
		url += "&t=%s" % ASSOCIATE
		url += "&dev-t=%s" % LICENSE_KEY
		url += "&type=%s" % 'lite'
		url += "&locale=%s" % lc_name
		url += "&mode=%s" % 'music'
		url += "&%s=%s" % ('KeywordSearch', urllib.quote (keyword))

		return url

	def search_next (self):
		self.searching = True
		
		if len (self.keywords)==0:
			keyword = None
		else:
			keyword = self.keywords.pop (0)

		if keyword is None:
			# No keywords left to search -> no results
			self.on_search_completed (None)
			ret = False
		else:
			# Retrieve search for keyword
			url = self.__build_url (keyword.strip ())
			self.loader.get_url (url, self.on_search_response)
			ret = True

		return ret

	def __unmarshal (self, element):
		rc = Bag ()
		if isinstance (element, minidom.Element) and (element.tagName == 'Details'):
			rc.URL = element.attributes["url"].value
		childElements = [e for e in element.childNodes if isinstance (e, minidom.Element)]
		if childElements:
			for child in childElements:
				key = child.tagName
				if hasattr (rc, key):
					if type (getattr (rc, key)) <> type ([]):
						setattr (rc, key, [getattr (rc, key)])
					setattr (rc, key, getattr (rc, key) + [self.__unmarshal (child)])
				elif isinstance(child, minidom.Element) and (child.tagName == 'Details'):
					setattr (rc,key,[self.__unmarshal(child)])
				else:
					setattr (rc, key, self.__unmarshal(child))
		else:
			rc = "".join ([e.data for e in element.childNodes if isinstance (e, minidom.Text)])
			if element.tagName == 'SalesRank':
				rc = rc.replace ('.', '')
				rc = rc.replace (',', '')
				rc = int (rc)
		return rc

	def on_search_response (self, result_data):
		if result_data is None:
			self.search_next()
			return

		try:
			xmldoc = minidom.parseString (result_data)
		except:
			self.search_next()
			return
		
		data = self.__unmarshal (xmldoc).ProductInfo

		if hasattr(data, 'ErrorMsg'):
			# Search was unsuccessful, try next keyword
			self.search_next ()
		else:
			# We got some search results
			self.on_search_results (data.Details)

	def on_search_results (self, results):
		self.on_search_completed (results)

	def on_search_completed (self, result):
		self.on_search_completed_callback (self, self.entry, result, *self.args)
		self.searching = False

	def __tidy_up_string (self, s):
		# Lowercase
		s = s.lower ()
		# Strip
		s = s.strip ()

		# TODO: Convert accented to unaccented (fixes matching Salomé vs Salome)
		s = s.replace (" - ", " ")	
		s = s.replace (": ", " ")
		s = s.replace (" & ", " and ")

		return s

	def __valid_match (self, item):
		if item.ImageUrlLarge == "" and item.ImageUrlMedium == "":
			print "%s doesn't have image URLs; ignoring" % (item.URL)
			return False
		return True

	def get_best_match_urls (self, search_results):
		# Default to "no match", our results must match our criteria
		best_match = None

		search_results = filter(self.__valid_match, search_results)
		try:
			if self.search_album != _("Unknown"):
				album_check = self.__tidy_up_string (self.search_album)
				for item in search_results:

					# Check for album name in ProductName
					product_name = self.__tidy_up_string (item.ProductName)

					if product_name == album_check:
						# Found exact album, can not get better than that
						best_match = item
						break
					# If we already found a best_match, just keep checking for exact one
					elif (best_match is None) and (product_name.find (album_check) != -1):
						best_match = item

			# If we still have no definite hit, use first result where artist matches
			if (self.search_album == _("Unknown") and self.search_artist != _("Unknown")):
				artist_check = self.__tidy_up_string (self.search_artist)
				if best_match is None:
					# Check if artist appears in the Artists list
					hit = False
					for item in search_results:

						if type (item.Artists.Artist) <> type ([]):
							artists = [item.Artists.Artist]
						else:
							artists = item.Artists.Artist

						for artist in artists:
							artist = self.__tidy_up_string (artist)
							if artist.find (artist_check) != -1:
								best_match = item
								hit = True
								break
						if hit:
							break

			if best_match:
				return filter(lambda x: x != "", [item.ImageUrlLarge, item.ImageUrlMedium])
			else:
				return []

		except TypeError:
			return []
