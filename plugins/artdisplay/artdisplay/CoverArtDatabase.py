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
from DiscogsCoverArtSearch import DiscogsCoverArtSearch
from MusicBrainzCoverArtSearch import MusicBrainzCoverArtSearch
from EmbeddedCoverArtSearch import EmbeddedCoverArtSearch

from urllib import unquote

import gio
from LocalCoverArtSearchGIO import LocalCoverArtSearch

ART_SEARCHES_LOCAL = [LocalCoverArtSearch, EmbeddedCoverArtSearch]
ART_SEARCHES_REMOTE = [PodcastCoverArtSearch, MusicBrainzCoverArtSearch, DiscogsCoverArtSearch]
OLD_ART_FOLDER = '~/.gnome2/rhythmbox/covers'

ART_FOLDER = os.path.join(rb.user_cache_dir(), 'covers')
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

	def dump (self):
		for k in self.hash.keys():
			print " item %s: %s" % (str(k), self.hash[k])

		print "dead tickets: %s" % str(self.dead)

	def get (self, item):
		ticket = self.counter.next ()
		self.hash.setdefault (item, set ()).add (ticket)
		return ticket

	def find (self, item, comparator, *args):
		for titem in self.hash.keys():
			if comparator(item, titem, *args):
				return titem
		return None

	def bury (self, ticket):
		try:
			self.dead.remove (ticket)
			return True
		except KeyError:
			return False

	def forget (self, item, ticket):
		try:
			tickets = self.hash[item]
			tickets.remove(ticket)
			if len(tickets) == 0:
				del self.hash[item]
			return True
		except KeyError:
			self.dead.remove (ticket)
			return False

	def purge (self, item):
		tickets = self.hash.pop (item, set())
		self.dead.update (tickets)

	def release (self, item, ticket):
		try:
			othertickets = self.hash.pop (item) - set([ticket])
			self.dead.update (othertickets)
			return True
		except KeyError:
			self.dead.remove (ticket)
			return False


class CoverArtDatabase (object):
	def __init__ (self):
		self.ticket = TicketSystem ()
		self.same_search = {}

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

		artist = artist.replace('/', '-')
		album = album.replace('/', '-')
		return art_folder + '/%s - %s.%s' % (artist, album, extension)	

	def engines (self, blist):
		for Engine in ART_SEARCHES_LOCAL:
			yield Engine (), Engine.__name__, False
		for Engine in ART_SEARCHES_REMOTE:
			if Engine.__name__ not in blist:
				yield Engine (), Engine.__name__, True
	
	def set_pixbuf_from_uri (self, db, entry, uri, callback):
		def loader_cb (data):
			self.set_pixbuf (db, entry, self.image_data_load (data), callback)

		l = rb.Loader()
		l.get_url (str (uri), loader_cb)

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
				Engine ().save_pixbuf (db, entry, pixbuf)
			except AttributeError:
				pass

	def cancel_get_pixbuf (self, entry):
		self.ticket.purge (entry)
  
	def get_pixbuf (self, db, entry, is_playing, callback):
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

		rb.Coroutine (self.image_search, db, st_album, st_artist, entry, is_playing, callback).begin ()

	def image_search (self, plexer, db, st_album, st_artist, entry, is_playing, callback):
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

		# Check if we're already searching for art for this album
		# (this won't work for compilations, but there isn't much we can do about that)
		def find_same_search(a, b, db):
			for prop in (rhythmdb.PROP_ARTIST, rhythmdb.PROP_ALBUM):
				if db.entry_get(a, prop) != db.entry_get(b, prop):
					return False

			return True

		match_entry = self.ticket.find(entry, find_same_search, db)
		if match_entry is not None:
			print "entry %s matches existing search for %s" % (
				 db.entry_get (entry, rhythmdb.PROP_LOCATION),
				 db.entry_get (match_entry, rhythmdb.PROP_LOCATION))
			self.same_search.setdefault (match_entry, []).append(entry)
			return

		blist = self.read_blist (blist_location)
		ticket = self.ticket.get (entry)
		for engine, engine_name, engine_remote in self.engines (blist):
			plexer.clear ()
			engine.search (db, entry, is_playing, plexer.send ())
			while True:
				yield None
				_, (engine, entry, results) = plexer.receive ()
				if not results:
					break

				def handle_result_pixbuf (pixbuf, engine_uri, should_save):
					if self.ticket.release (entry, ticket):
						if should_save:
							if pixbuf.get_has_alpha ():
								pixbuf.save (art_location_png, ART_CACHE_FORMAT_PNG, ART_CACHE_SETTINGS_PNG)
								uri = art_location_png
							else:
								pixbuf.save (art_location_jpg, ART_CACHE_FORMAT_JPG, ART_CACHE_SETTINGS_JPG)
								uri = art_location_jpg
						else:
							uri = engine_uri

						print "found image for %s" % (db.entry_get(entry, rhythmdb.PROP_LOCATION))
						callback (entry, pixbuf, uri)
						for m in self.same_search.pop(entry, []):
							print "and for same search %s" % (db.entry_get(m, rhythmdb.PROP_LOCATION))
							callback (m, pixbuf, uri)

					self.write_blist (blist_location, blist)
					self.same_search.pop (entry, None)


				# first check if the engine gave us a pixbuf
				pixbuf = engine.get_result_pixbuf (results)
				if pixbuf:
					handle_result_pixbuf (pixbuf, None, True)
					return

				# then check URIs
				for url in engine.get_best_match_urls (results):
					if str(url) == "":
						print "got empty url from engine %s." % (engine)
						continue

					l = rb.Loader()
					yield l.get_url (str (url), plexer.send ())
					_, (data, ) = plexer.receive ()
					pixbuf = self.image_data_load (data)
					if pixbuf:
						handle_result_pixbuf (pixbuf, url, engine_remote)
						return

				if not engine.search_next ():
					if engine_remote:
						blist.append (engine_name)
					break

				if self.ticket.bury (ticket):
					self.write_blist (blist_location, blist)
					self.same_search.pop (entry, None)
					return

			if self.ticket.bury (ticket):
				self.write_blist (blist_location, blist)
				self.same_search.pop (entry, None)
				return

		if self.ticket.forget (entry, ticket):
			print "didn't find image for %s" % (db.entry_get(entry, rhythmdb.PROP_LOCATION))
			callback (entry, None, None)
			for m in self.same_search.pop (entry, []):
				print "or for same search %s" % (db.entry_get(m, rhythmdb.PROP_LOCATION))
				callback (m, None, None)

		self.write_blist (blist_location, blist)
		self.same_search.pop (entry, None)

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
