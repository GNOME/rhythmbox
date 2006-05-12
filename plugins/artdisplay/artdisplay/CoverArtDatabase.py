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

import rhythmdb
import os
import gtk

from AmazonCoverArtSearch import AmazonCoverArtSearch
from Loader import Loader

ART_FOLDER = '~/.gnome2/rhythmbox/covers'

class CoverArtDatabase (object):
	def __init__ (self):
		self.loader = Loader()

	def create_search (self):
		return AmazonCoverArtSearch (self.loader)

	def build_art_cache_filename (self, album, artist, extension):
		art_folder = os.path.expanduser (ART_FOLDER)
		if not os.path.exists (art_folder):
			os.mkdir (art_folder)
		if extension is None:
			extension = "jpg"

		# FIXME: the following block of code is messy and needs to be redone ASAP
		return art_folder + '/%s - %s.%s' % (artist.replace ('/', '-'), album.replace ('/', '-'), extension)	

	def get_pixbuf (self, db, entry, callback):
		if entry is None:
			callback (entry, None)
			return
            
		st_artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		st_album = db.entry_get (entry, rhythmdb.PROP_ALBUM)

		# Handle special case
		if st_album == "":
			st_album = "Unknown"
		if st_artist == "":
			st_artist = "Unknown"

		# If unknown artist and album there is no point continuing
		if st_album == "Unknown" and st_artist == "Unknown":
			callback (entry, None)
			return

		# replace quote characters
		# don't replace single quote: could be important punctuation
		for char in ["\""]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')

		art_location = self.build_art_cache_filename (st_album, st_artist, "jpg")
		blist_location = self.build_art_cache_filename (st_album, st_artist, "rb-blist")

		# Check local cache
		if os.path.exists (art_location):
			pixbuf = gtk.gdk.pixbuf_new_from_file (art_location)	
			callback (entry, pixbuf)
		# Check for unsuccessful previous image download to prevent overhead search
		elif os.path.exists (blist_location):
			callback (entry, None)
		else:
			# Otherwise spawn (online) search-engine search
			se = self.create_search ()
			se.search (db, entry, self.on_search_engine_results, callback)

	def on_search_engine_results (self, search_engine, entry, results, callback):
		if results is None:
			self._do_blacklist_and_callback (search_engine, callback)
			return

		# Get best match from results
		best_match = search_engine.get_best_match (results)

		if best_match is None:
			self._do_blacklist_and_callback (search_engine, callback)
			return

		# Attempt to download image for best match
		pic_url = str (best_match.ImageUrlLarge)
		self.loader.get_url (pic_url, self.on_image_data_received, search_engine, "large", callback, best_match)

	def _do_blacklist_and_callback (self, search_engine, callback):
		self._create_blacklist (search_engine.st_artist, search_engine.st_album)
		callback (search_engine.entry, None)

	def _create_blacklist (self, artist, album):
		location = self.build_art_cache_filename (album, artist, "rb-blist")
		f = file (location, 'w')
		f.close ()
		return location

	def _create_artwork (self, artist, album, image_data):
		location = self.build_art_cache_filename (album, artist, "jpg")
		f = file (location, 'wb')
		f.write (image_data)
		f.close ()
		return location

	def on_image_data_received (self, image_data, search_engine, image_version, callback, best_match):
		if image_data is None:
			res = search_engine.search_next ()
			if not res:
				self._do_blacklist_and_callback (search_engine, callback)
			return

		if len (image_data) < 1000:
			if image_version == "large" and best_match is not None:
				# Fallback and try to load medium one
				pic_url = str (best_match.ImageUrlMedium)
				self.loader.get_url (pic_url, self.on_image_data_received, search_engine, "medium", callback, best_match)
				return

			res = search_engine.search_next ()
			if not res:
				# only write the blist if there are no more queries to try
				self._do_blacklist_and_callback (search_engine, callback)

		else:
			location = self._create_artwork (search_engine.st_artist, search_engine.st_album, image_data)
			pixbuf = gtk.gdk.pixbuf_new_from_file (location)
			callback (search_engine.entry, pixbuf)
