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
import gconf

import rb
import rhythmdb

# this API key belongs to jonathan@d14n.org
# and was generated specifically for this use
API_KEY = 'ff56d530598d65c1a4088e57da7be2f9'
API_URL = 'http://ws.audioscrobbler.com/2.0/'

LASTFM_LOGO = "lastfm_red_small.png"
LASTFM_TOOLTIP = (LASTFM_LOGO, _("Image provided by Last.fm"))

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

USERNAME_GCONF_KEY = "/apps/rhythmbox/audioscrobbler/username"

def user_has_account():
    username = gconf.client_get_default().get_string(USERNAME_GCONF_KEY)
    return (username is not None and username != "")

class LastFMCoverArtSearch (object):
	def __init__(self):
		pass

	def __search_request (self, artist, album, album_mbid):
		# Remove variants of Disc/CD [1-9] from album title before search
		orig_album = album
		for exp in DISC_NUMBER_REGEXS:
			p = re.compile (exp, re.IGNORECASE)
			album = p.sub ('', album)

		album.strip()

		print "searching for (%s, %s, %s)" % (artist, album, album_mbid)
		url = API_URL + "?method=album.getinfo&"
		if artist != "":
			url = url + "artist=%s&" % (urllib.quote_plus(artist))
		if album != "":
			url = url + "album=%s&" % (urllib.quote_plus(album))
		if album_mbid != "":
			url = url + "mbid=%s&" % (urllib.quote_plus(album_mbid))

		url = url + "api_key=%s" % API_KEY
		print "last.fm query url = %s" % url

		loader = rb.Loader()
		loader.get_url(url, self.__album_info_cb)

	def __album_info_cb (self, data):
		if data is None:
			print "last.fm query returned nothing"
			self.callback (self, self.entry, [], *self.callback_args)
			return

		parsed = dom.parseString(data)

		# find image URLs
		image_urls = []
		for tag in parsed.getElementsByTagName('image'):
			if tag.firstChild is None:
				print "got useless image tag"
				continue

			url = tag.firstChild.data
			url.strip()
			if url != "":
				print "found image url: %s" % url
				image_urls.append(url)

		# images tags appear in order of increasing size, and we want the largest.  probably.
		image_urls.reverse()
		self.callback (self, self.entry, image_urls, *self.callback_args)



	def search (self, db, entry, is_playing, callback, *args):
		self.entry = entry
		self.callback = callback
		self.callback_args = args

		if user_has_account() == False:
			print "can't search: no last.fm account details"
			callback (self, entry, None, *args)
			return

		artist = db.entry_get (entry, rhythmdb.PROP_ALBUM_ARTIST)
		if artist == "":
			artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		if artist == _("Unknown"):
			artist = ""

		album = db.entry_get (entry, rhythmdb.PROP_ALBUM)
		if album == _("Unknown"):
			album = ""

		album_mbid = db.entry_get (entry, rhythmdb.PROP_MUSICBRAINZ_ALBUMID)
		if (artist, album, album_mbid) == ("", "", ""):
			print "can't search: no artist, album, or album ID"
			callback (self, entry, None, *args)
			return

		self.searches = [
			(artist, album, album_mbid),
			("Various Artists", album, album_mbid)
		]
		self.searches.reverse()
		self.search_next()

	def search_next (self):
		if len(self.searches) == 0:
			return False

		args = self.searches.pop()
		self.__search_request(*args)
		return True

	def get_result_meta (self, search_results):
		return LASTFM_TOOLTIP

	def get_result_pixbuf (self, search_results):
		return None

	def get_best_match_urls (self, search_results):
		return search_results


