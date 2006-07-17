# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Gareth Murphy, Martin Szulecki, 
# Ed Catmur <ed@catmur.co.uk>
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

import rhythmdb, rb
import os
import gtk

from AmazonCoverArtSearch import AmazonCoverArtSearch
from LocalCoverArtSearch import LocalCoverArtSearch

ART_SEARCHES_LOCAL = [LocalCoverArtSearch]
ART_SEARCHES_REMOTE = [AmazonCoverArtSearch]
ART_FOLDER = '~/.gnome2/rhythmbox/covers'

class CoverArtDatabase (object):
	def __init__ (self):
		self.loader = rb.Loader()

	def build_art_cache_filename (self, album, artist, extension):
		art_folder = os.path.expanduser (ART_FOLDER)
		if not os.path.exists (art_folder):
			os.mkdir (art_folder)
		if extension is None:
			extension = "jpg"

		# FIXME: the following block of code is messy and needs to be redone ASAP
		return art_folder + '/%s - %s.%s' % (artist.replace ('/', '-'), album.replace ('/', '-'), extension)	

	def engines (self, blist):
		for Engine in ART_SEARCHES_LOCAL:
			yield Engine (self.loader), Engine.__name__, False
		for Engine in ART_SEARCHES_REMOTE:
			if Engine.__name__ not in blist:
				yield Engine (self.loader), Engine.__name__, True
  
	def get_pixbuf (self, db, entry, callback):
		if entry is None:
			callback (entry, None)
			return
            
		st_artist = db.entry_get (entry, rhythmdb.PROP_ARTIST) or _("Unknown")
		st_album = db.entry_get (entry, rhythmdb.PROP_ALBUM) or _("Unknown")

		# replace quote characters
		# don't replace single quote: could be important punctuation
		for char in ["\""]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')

		rb.Coroutine (self.image_search, db, st_album, st_artist, entry, callback).begin ()

	def image_search (self, plexer, db, st_album, st_artist, entry, callback):
		art_location = self.build_art_cache_filename (st_album, st_artist, "jpg")
		blist_location = self.build_art_cache_filename (st_album, st_artist, "rb-blist")

		# Check local cache
		if os.path.exists (art_location):
			pixbuf = gtk.gdk.pixbuf_new_from_file (art_location)	
			callback (entry, pixbuf)
			return

		blist = self.read_blist (blist_location)
		for engine, engine_name, engine_remote in self.engines (blist):
			plexer.clear ()
			engine.search (db, entry, plexer.send ())
			while True:
				yield None
				_, (engine, entry, results) = plexer.receive ()
				if not results:
					break
				for url in engine.get_best_match_urls (results):
					yield self.loader.get_url (str (url), plexer.send ())
					_, (data, ) = plexer.receive ()
					pixbuf = self.image_data_load (data)
					if pixbuf:
						if engine_remote:
							pixbuf.save (art_location, "jpeg", {"quality": "100"})
						self.write_blist (blist_location, blist)
						callback (entry, pixbuf)
						return
				if not engine.search_next ():
					if engine_remote:
						blist.append (engine_name)
					break
		self.write_blist (blist_location, blist)
		callback (entry, None)

	def read_blist (self, blist_location):
		if os.path.exists (blist_location):
			return [line.strip () for line in file (blist_location)]
		else:
			return []

	def write_blist (self, blist_location, blist):
		if blist:
			blist_file = file (blist_location, 'w')
			blist_file.writelines (blist)
			blist_file.close ()
		elif os.path.exists (blist_location):
			os.unlink (blist_location)

	def image_data_load (self, data):
		if data and len (data) >= 1000:
			pbl = gtk.gdk.PixbufLoader ()
			try:
				if pbl.write (data) and pbl.close ():
					return pbl.get_pixbuf ()
			except GError:
				pass
		return None
