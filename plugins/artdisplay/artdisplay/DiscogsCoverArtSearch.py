# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
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

import urllib
import xml.dom.minidom as dom
import re
import StringIO
import gzip
import time
import httplib
import threading

import rb
import rhythmdb

from rb.stringmatch import string_match

# match quality parameters
DEFAULT_MATCH = 0.35		# used when the item doesn't have the match property
MINIMUM_MATCH = 0.5		# ignore results below this quality
REJECT_MATCH = 0.3		# reject results if either match strength is below this

# list of format types to avoid if possible
# these tend to have poor or non-square cover art
BAD_FORMAT_LIST = ('Cassette', 'VHS', 'DVD', 'CDr', 'Promo', 'White Label')
BAD_FORMAT_PENALTY = -0.05
# slight penalty for vinyl in order to get CD cover images where possible.
# there tends to be slightly better coverage for CDs.
VINYL_PENALTY = -0.02

# this API key belongs to jonathan@d14n.org ('qwe' on discogs)
# and was generated specifically for this use
API_KEY = '45be40f6dd'

DISC_NUMBER_REGEXS = (
	"\(disc *[0-9]+\)",
	"\(cd *[0-9]+\)",
	"\[disc *[0-9]+\]",
	"\[cd *[0-9]+\]",
	" - disc *[0-9]+$",
	" - cd *[0-9]+$",
	" disc *[0-9]+$",
	" cd *[0-9]+$"
)

last_poke = 0

class PokeThread (threading.Thread):
	def __init__(self):
		threading.Thread.__init__(self)

	def run(self):
		c = httplib.HTTPConnection('www.discogs.com')
		c.connect()
		c.request('GET', '/release/1?f=xml&api_key=' + API_KEY, headers = { 'Accept-Encoding': 'gzip' })
		c.getresponse()
		c.close()

class DiscogsCoverArtSearch (object):
	def __init__(self):
		pass

	def __poke(self):
		global last_poke
		# an oddity here is that discogs claims to require the 'accept-encoding: gzip'
		# header in all requests, but after the first, it seems to stop checking.
		# this works out pretty well for us, because the gvfs http backend doesn't send that
		# header, but we can understand the responses.

		# assuming it's going to forget after a while, we'll just poke it every hour or so.
		# this probably actually depends on IP address more than anything else, but we
		# don't really have the ability to determine when that changes.
		if time.time() < (last_poke + 3600.0):
			return

		last_poke = time.time()
		poker = PokeThread()
		poker.start()

	def __decompress(self, data):
		sz = gzip.GzipFile(mode = 'r', fileobj = StringIO.StringIO(data))
		return sz.read()

	def __search_cb (self, data, (artist, album)):
		if data is None:
			print "search returned nothing"
			self.callback (self, self.entry, [], *self.callback_args)
			return

		try:
			parsed = dom.parseString(self.__decompress(data))
		except Exception, e:
			print "error processing response data: %s" % e
			self.callback (self, self.entry, [], *self.callback_args)
			return

		# probably check for exact matches?

		# track best combined and album matches separately
		# (best album match works pretty well for multi-artist albums)
		best_match = 0.0
		best_id = None
		best_album_id = None
		best_album_match = 0.0

		# look for releases that sort of match
		for r in parsed.getElementsByTagName('result'):

			# check it's a release
			if r.attributes['type'].value != u'release':
				continue

			# split into artist and album, match against the search terms
			titletag = r.getElementsByTagName('title')[0]
			title = titletag.firstChild.data
			(rel_artist, rel_album) = title.split(" - ", 1)

			# calculate the release format penalty
			# we rely on the format descriptor appearing somewhere in the freeform
			# 'summary' tag.  we don't care where.
			match_penalty = 0.0
			summary = r.getElementsByTagName('summary')[0].firstChild.data
			for badformat in BAD_FORMAT_LIST:
				if summary.find(badformat) != -1:
					match_penalty = BAD_FORMAT_PENALTY

			# vinyl penalty only applies if the other one doesn't
			if match_penalty > -0.01 and summary.find('Vinyl') != -1:
				match_penalty = VINYL_PENALTY


			# search result URLs include artist/title slugs, so they don't work with API requests
			# the release ID is the last path fragment.
			this_url = r.getElementsByTagName('uri')[0].firstChild.data
			this_release_id = this_url.split('/')[-1]

			artist_match = string_match(artist, rel_artist)
			album_match = string_match(album, rel_album)
			# this probably isn't a good way to combine matches
			this_match = ((artist_match + album_match) / 2) + match_penalty

			# is this the new best match?
			if album_match < REJECT_MATCH or artist_match < REJECT_MATCH:
				print "result \"%s\" rejected (%f, %f)" % (title, album_match, artist_match)
			elif this_match > best_match:
				best_id = this_release_id
				best_match = this_match
				print "result \"%s\" is the new best match (%f)" % (title, this_match)
			else:
				print "result \"%s\" discarded, %f < %f" % (title, this_match, best_match)

			# is this the new best album match?
			album_match = album_match + match_penalty
			if album_match > best_album_match:
				print "result \"%s\" is the new best album match (%f)" % (title, album_match)
				best_album_match = album_match
				best_album_id = this_release_id

		# figure out if we got a result good enough to use
		fetch_id = None
		if best_match > MINIMUM_MATCH:
			print "best result has match strength %f, fetching release %s" % (best_match, best_id)
			fetch_id = best_id
		elif best_album_match > MINIMUM_MATCH:
			print "best album result has match strength %f, fetching release %s" % (best_album_match, best_album_id)
			fetch_id = best_album_id
		else:
			print "no suitable results found"

		# if we did, get the release info, which contains the image URLs
		if fetch_id is not None:
			xml_url = "http://www.discogs.com/release/%s?f=xml&api_key=%s" % (fetch_id, API_KEY)
			loader = rb.Loader()
			loader.get_url(xml_url, self.__get_release_cb)
		else:
			self.callback (self, self.entry, [], *self.callback_args)


	def __get_release_cb (self, data):
		if data is None:
			print "release returned nothing"
			self.callback (self, self.entry, [], *self.callback_args)
			return

		try:
			parsed = dom.parseString(self.__decompress(data))
		except Exception, e:
			print "error processing response data: %s" % e
			self.callback (self, self.entry, [], *self.callback_args)
			return

		# find image URLs.  don't think there's much point using secondary images.
		image_urls = []
		for tag in parsed.getElementsByTagName('image'):
			type = tag.attributes['type'].value
			if type != 'primary':

			url = tag.attributes['uri'].value
			url.strip()
			if url != "":
				print "found image url: %s" % url
				image_urls.append(url)

		self.callback (self, self.entry, [image_urls], *self.callback_args)



	def search (self, db, entry, is_playing, callback, *args):
		self.entry = entry
		self.callback = callback
		self.callback_args = args

		artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		if artist == _("Unknown"):
			artist = ""

		album = db.entry_get (entry, rhythmdb.PROP_ALBUM)
		if album == _("Unknown"):
			album = ""

		# Remove variants of Disc/CD [1-9] from album title before search
		orig_album = album
		for exp in DISC_NUMBER_REGEXS:
			p = re.compile (exp, re.IGNORECASE)
			album = p.sub ('', album)

		album.strip()

		if (artist, album) == ("", ""):
			print "can't search: no artist or album"
			callback (self, entry, None, *args)
			return

		# trick discogs into handling requests without 'accept-encoding: gzip'
		self.__poke()

		print "searching for (%s, %s)" % (artist, album)
		terms = artist + " " + album
		url = "http://www.discogs.com/search?type=all&f=xml&q=%s&api_key=%s" % (urllib.quote_plus(terms), API_KEY)

		loader = rb.Loader()
		loader.get_url(url, self.__search_cb, (artist, album))


	def search_next (self):
		return False

	def get_result_pixbuf (self, search_results):
		return None

	def get_best_match_urls (self, search_results):
		if search_results == []:
			return []
		return search_results[0]


