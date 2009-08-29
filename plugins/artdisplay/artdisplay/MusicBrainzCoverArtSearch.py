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

import rb
import rhythmdb

# musicbrainz URLs
MUSICBRAINZ_RELEASE_URL = "http://musicbrainz.org/ws/1/release/%s?type=xml"
MUSICBRAINZ_RELEASE_PREFIX = "http://musicbrainz.org/release/"
MUSICBRAINZ_RELEASE_SUFFIX = ".html"

# Amazon URL bits
AMAZON_IMAGE_URL = "http://images.amazon.com/images/P/%s.01.LZZZZZZZ.jpg"

class MusicBrainzCoverArtSearch (object):

	def __init__(self):
		pass

	def __get_release_cb (self, data):
		if data is None:
			print "musicbrainz release request returned nothing"
			self.callback(self, self.entry, [], *self.callback_args)
			return;

		try:
			parsed = dom.parseString(data)

			# just look for an ASIN tag
			asin_tags = parsed.getElementsByTagName('asin')
			if len(asin_tags) > 0:
				asin = asin_tags[0].firstChild.data

				print "got ASIN %s" % asin
				image_url = AMAZON_IMAGE_URL % asin

				self.callback(self, self.entry, [image_url], *self.callback_args)
			else:
				print "no ASIN for this release"
		except Exception, e:
			print "exception parsing musicbrainz response: %s" % e
			self.callback(self, self.entry, [], *self.callback_args)

	def search (self, db, entry, is_playing, callback, *args):

		self.callback = callback
		self.callback_args = args
		self.entry = entry

		# if we've got an album ID, we can get the album info directly
		album_id = db.entry_get(entry, rhythmdb.PROP_MUSICBRAINZ_ALBUMID)
		if album_id != "":
			# these sometimes look like full URLs, sometimes not
			if album_id.startswith(MUSICBRAINZ_RELEASE_PREFIX):
				album_id = album_id[len(MUSICBRAINZ_RELEASE_PREFIX):]

			if album_id.endswith(MUSICBRAINZ_RELEASE_SUFFIX):
				album_id = album_id[:-len(MUSICBRAINZ_RELEASE_SUFFIX)]

			print "stripped release ID: %s" % album_id

			url = MUSICBRAINZ_RELEASE_URL % (album_id)
			loader = rb.Loader()
			loader.get_url(url, self.__get_release_cb)
			return

		# otherwise, maybe we can search for the album..

		# nothing to do
		callback (self, entry, [], *args)

	def search_next (self):
		return False

	def get_result_pixbuf (self, search_results):
		return None

	def get_best_match_urls (self, search_results):
		return search_results

