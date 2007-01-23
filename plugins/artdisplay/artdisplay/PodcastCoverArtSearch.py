# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Martin Szulecki
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

class Bag: pass

class PodcastCoverArtSearch (object):
	def __init__ (self, loader):
		self.searching = False
		self.cancel = False
		self.entry = None

	def search (self, db, entry, on_search_completed_callback, *args):
		self.searching = True
		self.cancel = False
		self.entry = entry
		self.args = args

		# Check if entry is a podcast for performance
		if entry.get_entry_type() != db.entry_type_get_by_name("podcast-post"):
			on_search_completed_callback (self, self.entry, None, *self.args)
			return

		# Retrieve corresponding feed for this entry
		podcast_location = db.entry_get(entry, rhythmdb.PROP_SUBTITLE)
		podcast_feed_entry = db.entry_lookup_by_location(podcast_location)

		# Check for PROP_IMAGE in feed
		image_url = db.entry_get(podcast_feed_entry, rhythmdb.PROP_IMAGE)
		
		on_search_completed_callback (self, self.entry, image_url, *self.args)

	def search_next (self):
		return False

	def get_best_match_urls (self, search_results):
		# Return image URL
		return [search_results]
