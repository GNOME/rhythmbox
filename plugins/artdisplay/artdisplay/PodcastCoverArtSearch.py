# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Martin Szulecki
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

class PodcastCoverArtSearch (object):
	def __init__ (self):
		pass

	def search (self, db, entry, is_playing, on_search_completed_callback, *args):

		# Check if entry is a podcast for performance
		if entry.get_entry_type() != db.entry_type_get_by_name("podcast-post"):
			on_search_completed_callback (self, entry, None, *args)
			return

		# Retrieve corresponding feed for this entry
		podcast_location = db.entry_get(entry, rhythmdb.PROP_SUBTITLE)
		podcast_feed_entry = db.entry_lookup_by_location(podcast_location)

		# Check for PROP_IMAGE in feed
		image_url = db.entry_get(podcast_feed_entry, rhythmdb.PROP_IMAGE)
		
		on_search_completed_callback (self, entry, image_url, *args)

	def search_next (self):
		return False

	def get_result_meta (self, search_results):
		return (None, None)

	def get_result_pixbuf (self, search_results):
		return None

	def get_best_match_urls (self, search_results):
		# Return image URL
		return [search_results]

