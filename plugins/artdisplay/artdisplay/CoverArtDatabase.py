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

import rhythmdb, rb
import os
import gtk
import itertools
import gobject

from PodcastCoverArtSearch import PodcastCoverArtSearch
from AmazonCoverArtSearch import AmazonCoverArtSearch
from LocalCoverArtSearch import LocalCoverArtSearch

from urllib import unquote

ART_SEARCHES_LOCAL = [LocalCoverArtSearch]
ART_SEARCHES_REMOTE = [PodcastCoverArtSearch, AmazonCoverArtSearch]
OLD_ART_FOLDER = '~/.gnome2/rhythmbox/covers'
# complicated way of saying ~/.cache/rhythmbox/covers
ART_FOLDER = os.path.join(os.environ.get('XDG_CACHE_HOME',
		os.path.join(os.environ.get('XDG_HOME_DIR',
				os.environ.get('HOME','~')),
				'.cache')), 'rhythmbox/covers')
ART_CACHE_EXTENSION_JPG = 'jpg'
ART_CACHE_EXTENSION_PNG = 'png'
ART_CACHE_FORMAT_JPG = 'jpeg'
ART_CACHE_FORMAT_PNG = 'png'
ART_CACHE_SETTINGS_JPG = {"quality": "100"}
ART_CACHE_SETTINGS_PNG = {"compression": "9"}

class TicketSystem:
	def __init__ (self):
		self.counter = itertools.count ()
		self.hash = {}
		self.dead = set ()

	def get (self, item):
		ticket = self.counter.next ()
		self.hash.setdefault (item, set ()).add (ticket)
		return ticket

	def bury (self, ticket):
		try:
			self.dead.remove (ticket)
			return True
		except KeyError:
			return False

	def forget (self, item, ticket):
		try:
			self.hash[item].remove (ticket)
			return True
		except KeyError:
			self.dead.remove (ticket)
			return False

	def purge (self, item):
		self.dead.update (self.hash.pop (item, set ()))

	def release (self, item, ticket):
		try:
			self.dead.update (self.hash.pop (item) - set([ticket]))
			return True
		except KeyError:
			self.dead.remove (ticket)
			return False

class CoverArtDatabase (object):
	def __init__ (self):
		self.loader = rb.Loader()
		self.ticket = TicketSystem ()

	def build_art_cache_filename (self, db, entry, extension):
		artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		album = db.entry_get (entry, rhythmdb.PROP_ALBUM)
		art_folder = os.path.expanduser (ART_FOLDER)
		old_art_folder = os.path.expanduser (OLD_ART_FOLDER)
		if not os.path.exists (art_folder) and os.path.exists (old_art_folder):
			parent = os.path.dirname(os.path.abspath(art_folder))
			if not os.path.exists (parent):
				os.makedirs (parent)
			os.rename (old_art_folder, art_folder)
		if not os.path.exists (art_folder):
			os.makedirs (art_folder)

		# FIXME: the following block of code is messy and needs to be redone ASAP
		return art_folder + '/%s - %s.%s' % (artist.replace ('/', '-'), album.replace ('/', '-'), extension)	

	def engines (self, blist):
		for Engine in ART_SEARCHES_LOCAL:
			yield Engine (self.loader), Engine.__name__, False
		for Engine in ART_SEARCHES_REMOTE:
			if Engine.__name__ not in blist:
				yield Engine (self.loader), Engine.__name__, True
	
	def set_pixbuf_from_uri (self, db, entry, uri, callback):
		def loader_cb (data):
			self.set_pixbuf (db, entry, self.image_data_load (data), callback)
		self.loader.get_url (str (uri), loader_cb)

	def set_pixbuf (self, db, entry, pixbuf, callback):
		if entry is None or pixbuf is None:
			return
		if pixbuf.get_has_alpha():
			art_location = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_PNG)
			art_cache_format = ART_CACHE_FORMAT_PNG
			art_cache_settings = ART_CACHE_SETTINGS_PNG
		else:
			art_location = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_JPG)
			art_cache_format = ART_CACHE_FORMAT_JPG
			art_cache_settings = ART_CACHE_SETTINGS_JPG
		self.ticket.purge (entry)
		pixbuf.save (art_location, art_cache_format, art_cache_settings)
		callback (entry, pixbuf, art_location)
		for Engine in ART_SEARCHES_LOCAL:
			try:
				Engine (self.loader).save_pixbuf (db, entry, pixbuf)
			except AttributeError:
				pass

	def cancel_get_pixbuf (self, entry):
		self.ticket.purge (entry)
  
	def get_pixbuf (self, db, entry, callback):
		if entry is None:
			callback (entry, None, None)
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
		art_location_jpg = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_JPG)
		art_location_png = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_PNG)
		blist_location = self.build_art_cache_filename (db, entry, "rb-blist")

		art_location = None
		if os.path.exists (art_location_jpg):
			art_location = art_location_jpg
		if os.path.exists (art_location_png):
			art_location = art_location_png

		# Check local cache
		if art_location:
			self.ticket.purge (entry)
			pixbuf = gtk.gdk.pixbuf_new_from_file (art_location)	
			callback (entry, pixbuf, art_location)
			return

		blist = self.read_blist (blist_location)
		ticket = self.ticket.get (entry)
		for engine, engine_name, engine_remote in self.engines (blist):
			plexer.clear ()
			engine.search (db, entry, plexer.send ())
			while True:
				yield None
				_, (engine, entry, results) = plexer.receive ()
				if not results:
					break
				for url in engine.get_best_match_urls (results):
					if str(url) == "":
						print "got empty url from engine %s." % (engine)
						continue

					yield self.loader.get_url (str (url), plexer.send ())
					_, (data, ) = plexer.receive ()
					pixbuf = self.image_data_load (data)
					if pixbuf:
						if self.ticket.release (entry, ticket):
							if engine_remote:
								if pixbuf.get_has_alpha ():
									pixbuf.save (art_location_png, ART_CACHE_FORMAT_PNG, ART_CACHE_SETTINGS_PNG)
								else:
									pixbuf.save (art_location_jpg, ART_CACHE_FORMAT_JPG, ART_CACHE_SETTINGS_JPG)
								uri = art_location
							else:
								uri = unquote (str (url))
							callback (entry, pixbuf, uri)
						self.write_blist (blist_location, blist)
						return
				if not engine.search_next ():
					if engine_remote:
						blist.append (engine_name)
					break
				if self.ticket.bury (ticket):
					self.write_blist (blist_location, blist)
					return
			if self.ticket.bury (ticket):
				self.write_blist (blist_location, blist)
				return
		if self.ticket.forget (entry, ticket):
			callback (entry, None, None)
		self.write_blist (blist_location, blist)

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
			except gobject.GError:
				pass
		return None
