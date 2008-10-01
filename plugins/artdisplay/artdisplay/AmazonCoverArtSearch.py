# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Gareth Murphy, Martin Szulecki
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

from xml.dom import minidom
import re
import locale
import urllib

import rhythmdb

LICENSE_KEY = "18C3VZN9HCECM5G3HQG2"
DEFAULT_LOCALE = "en_US"
ASSOCIATE = "webservices-20"

# We are not allowed to batch more than 2 requests at once
# http://docs.amazonwebservices.com/AWSEcommerceService/4-0/PgCombiningOperations.html
MAX_BATCH_JOBS = 2


class Bag: pass

class AmazonCoverArtSearch (object):
	def __init__ (self, loader):
		self.searching = False
		self.cancel = False
		self.loader = loader
		self.db = None
		self.entry = None
		(self.tld, self.encoding) = self.__get_locale ()

	def __get_locale (self):
		# "JP is the only locale that correctly takes UTF8 input. All other locales use LATIN1."
		# http://developer.amazonwebservices.com/connect/entry.jspa?externalID=1295&categoryID=117
		supported_locales = {
			"en_US" : ("com", "latin1"),
			"en_GB" : ("co.uk", "latin1"),
			"de" : ("de", "latin1"),
			"ja" : ("jp", "utf8")
		}

		lc_id = DEFAULT_LOCALE
		default = locale.getdefaultlocale ()[0]
		if default:
			if supported_locales.has_key (default):
				lc_id = default
			else:
				lang = default.split("_")[0]
				if supported_locales.has_key (lang):
					lc_id = lang

		return supported_locales[lc_id]

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

		if st_artist == st_album == _("Unknown"):
			self.on_search_completed (None)
			return

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
				self.keywords.append ("Various %s" % (st_album))
			self.keywords.append ("%s" % (st_artist))

		# Initiate asynchronous search
		self.search_next ()

	def search_next (self):
		if len (self.keywords) == 0:
			# No keywords left to search -> no results
			self.on_search_completed (None)
			return False

		self.searching = True

		url = "http://ecs.amazonaws." + self.tld + "/onca/xml" \
		      "?Service=AWSECommerceService"                   \
		      "&AWSAccessKeyId=" + LICENSE_KEY +               \
		      "&AssociateTag=" + ASSOCIATE +                   \
		      "&ResponseGroup=Images,ItemAttributes"           \
		      "&Operation=ItemSearch"                          \
		      "&ItemSearch.Shared.SearchIndex=Music"

		job = 1
		while job <= MAX_BATCH_JOBS and len (self.keywords) > 0:
			keyword = self.keywords.pop (0)
			keyword = keyword.encode (self.encoding, "ignore")
			keyword = keyword.strip ()
			keyword = urllib.quote (keyword)
			url += "&ItemSearch.%d.Keywords=%s" % (job, keyword)
			job += 1

		# Retrieve search for keyword
		self.loader.get_url (url, self.on_search_response)
		return True

	def __unmarshal (self, element):
		rc = Bag ()
		child_elements = [e for e in element.childNodes if isinstance (e, minidom.Element)]
		if child_elements:
			for child in child_elements:
				key = child.tagName
				if hasattr (rc, key):
					if not isinstance (getattr (rc, key), list):
						setattr (rc, key, [getattr (rc, key)])
					getattr (rc, key).append (self.__unmarshal (child))
				# get_best_match_urls() wants a list, even if there is only one item/artist
				elif child.tagName in ("Items", "Item", "Artist"):
					setattr (rc, key, [self.__unmarshal(child)])
				else:
					setattr (rc, key, self.__unmarshal(child))
		else:
			rc = "".join ([e.data for e in element.childNodes if isinstance (e, minidom.Text)])
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
		
		data = self.__unmarshal (xmldoc)
		if not hasattr (data, "ItemSearchResponse") or \
		   not hasattr (data.ItemSearchResponse, "Items"):
			# Something went wrong ...
			self.search_next ()
		else:
			# We got some search results
			self.on_search_results (data.ItemSearchResponse.Items)

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
		return (hasattr (item, "LargeImage") or hasattr (item, "MediumImage")) \
		       and hasattr (item, "ItemAttributes")

	def get_best_match_urls (self, search_results):
		# Default to "no match", our results must match our criteria
		best_match = None

		for result in search_results:
			if not hasattr (result, "Item"):
				# Search was unsuccessful, try next batch job
				continue

			items = filter(self.__valid_match, result.Item)
			if self.search_album != _("Unknown"):
				album_check = self.__tidy_up_string (self.search_album)
				for item in items:
					if not hasattr (item.ItemAttributes, "Title"):
						continue

					album = self.__tidy_up_string (item.ItemAttributes.Title)
					if album == album_check:
						# Found exact album, can not get better than that
						best_match = item
						break
					# If we already found a best_match, just keep checking for exact one
					# Check the results for both an album name that contains the name
					# we're searching for, and an album name that's a substring of the
					# name we're searching for
					elif (best_match is None) and \
					     (album.find (album_check) != -1 or
					      album_check.find (album) != -1):
						best_match = item

			# If we still have no definite hit, use first result where artist matches
			if (self.search_album == _("Unknown") and self.search_artist != _("Unknown")):
				artist_check = self.__tidy_up_string (self.search_artist)
				if best_match is None:
					# Check if artist appears in the Artists list
					hit = False
					for item in items:
						if not hasattr (item.ItemAttributes, "Artist"):
							continue

						for artist in item.ItemAttributes.Artist:
							artist = self.__tidy_up_string (artist)
							if artist.find (artist_check) != -1:
								best_match = item
								hit = True
								break
						if hit:
							break

			urls = [getattr (best_match, size).URL for size in ("LargeImage", "MediumImage")
			        if hasattr (best_match, size)]
			if urls:
				return urls

		# No search was successful
		return []
