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

import xml.dom.minidom as dom

import urllib.parse
import rb
from gi.repository import RB, GLib

# musicbrainz URLs
MUSICBRAINZ_RELEASE_URL = "https://musicbrainz.org/ws/2/release/%s?inc=artists"
MUSICBRAINZ_RELEASE_PREFIX = "http://musicbrainz.org/release/"
MUSICBRAINZ_RELEASE_SUFFIX = ".html"

# cover art archive
COVERARTARCHIVE_IMAGE_URL = "https://coverartarchive.org/release/%s/front"

MUSICBRAINZ_SEARCH_QUERY = "artist:\"%s\" AND release:\"%s\""
MUSICBRAINZ_SEARCH_URL = "https://musicbrainz.org/ws/2/release/?query=%s&limit=1"

# musicbrainz IDs
MUSICBRAINZ_VARIOUS_ARTISTS = "89ad4ac3-39f7-470e-963a-56509c546377"

# Amazon URL bits
AMAZON_IMAGE_URL = "https://images-na.ssl-images-amazon.com/images/P/%s.01.LZZZZZZZ.jpg"

class MusicBrainzSearch(object):

	def get_image_cb (self, data, args):
		(storekey, store, urls, callback, cbargs) = args
		url = urls[0]
		urls = urls[1:]
		if data is None:
			self.try_image_urls(storekey, store, urls, callback, cbargs)
			return

		store.store_raw(storekey, RB.ExtDBSourceType.SEARCH, GLib.Bytes.new_take(data))
		callback(*cbargs)

	def try_image_urls (self, storekey, store, urls, callback, *args):
		if len(urls) == 0:
			print("no more image urls to try")
			callback(*args)
			return

		print("%d urls to try, trying %s" % (len(urls), urls[0]))
		loader = rb.Loader()
		loader.get_url(urls[0], self.get_image_cb, (storekey, store, urls, callback, args))


	def get_release_cb (self, data, args):
		(key, store, callback, cbargs) = args
		if data is None:
			print("musicbrainz release request returned nothing")
			callback(*cbargs)
			return

		try:
			parsed = dom.parseString(data)

			# get release MBID
			rel = parsed.getElementsByTagName('release')
			mbid = rel[0].attributes['id'].firstChild.data

			storekey = RB.ExtDBKey.create_storage('album', key.get_field('album'))

			# check that there's an artist that isn't 'various artists'
			artist_tags = parsed.getElementsByTagName('artist')
			if len(artist_tags) > 0:
				artist_id = artist_tags[0].attributes['id'].firstChild.data
				if artist_id != MUSICBRAINZ_VARIOUS_ARTISTS:
					# add the artist name (as album-artist) to the storage key
					nametags = artist_tags[0].getElementsByTagName('name')
					if len(nametags) > 0:
						artistname = nametags[0].firstChild.data
						print("got musicbrainz artist name %s" % artistname)
						storekey.add_field('artist', artistname)

			urls = []

			# just need an mbid to use coverartarchive
			urls.append(COVERARTARCHIVE_IMAGE_URL % mbid)

			# look for an ASIN tag
			asin_tags = parsed.getElementsByTagName('asin')
			if len(asin_tags) > 0:
				asin = asin_tags[0].firstChild.data
				urls.append(AMAZON_IMAGE_URL % asin)


			self.try_image_urls(storekey, store, urls, callback, args)
		except Exception as e:
			print("exception parsing musicbrainz response: %s" % e)
			callback(*cbargs)

	def try_search_artist_album (self, key, store, callback, *args):
		album = key.get_field("album")
		artist = key.get_field("artist")

		if not album or not artist:
			print("artist or album information missing")
			callback(*args)
			return

		query = MUSICBRAINZ_SEARCH_QUERY % (artist.lower(), album.lower())
		url = MUSICBRAINZ_SEARCH_URL % (urllib.parse.quote(query, safe=':'),)

		loader = rb.Loader()
		loader.get_url(url, self.get_release_cb, (key, store, callback, args))

	def search(self, key, last_time, store, callback, *args):
		key = key.copy()	# ugh
		album_id = key.get_info("musicbrainz-albumid")
		if album_id is None:
			print("no musicbrainz release ID for this track")
			self.try_search_artist_album(key, store, callback, args)
			return

		if album_id.startswith(MUSICBRAINZ_RELEASE_PREFIX):
			album_id = album_id[len(MUSICBRAINZ_RELEASE_PREFIX):]

		if album_id.endswith(MUSICBRAINZ_RELEASE_SUFFIX):
			album_id = album_id[:-len(MUSICBRAINZ_RELEASE_SUFFIX)]

		print("stripped release ID: %s" % album_id)

		url = MUSICBRAINZ_RELEASE_URL % (album_id)
		loader = rb.Loader()
		loader.get_url(url, self.get_release_cb, (key, store, callback, args))
