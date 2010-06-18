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
import gio
import itertools
import gobject

from PodcastCoverArtSearch import PodcastCoverArtSearch
from MusicBrainzCoverArtSearch import MusicBrainzCoverArtSearch
from LastFMCoverArtSearch import LastFMCoverArtSearch
from EmbeddedCoverArtSearch import EmbeddedCoverArtSearch
from LocalCoverArtSearch import LocalCoverArtSearch

from urllib import unquote, pathname2url

ART_SEARCHES_LOCAL = [LocalCoverArtSearch, EmbeddedCoverArtSearch]
ART_SEARCHES_REMOTE = [PodcastCoverArtSearch, LastFMCoverArtSearch, MusicBrainzCoverArtSearch]
OLD_ART_FOLDER = '~/.gnome2/rhythmbox/covers'

ART_FOLDER = os.path.join(rb.user_cache_dir(), 'covers')
ART_CACHE_EXTENSION_JPG = 'jpg'
ART_CACHE_EXTENSION_PNG = 'png'
ART_CACHE_EXTENSION_META = 'rb-meta'
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

def get_search_props(db, entry):
	artist = db.entry_get(entry, rhythmdb.PROP_ALBUM_ARTIST)
	if artist == "":
		artist = db.entry_get(entry, rhythmdb.PROP_ARTIST)
	album = db.entry_get(entry, rhythmdb.PROP_ALBUM)
	return (artist, album)


class CoverArtDatabase (object):
	def __init__ (self):
		self.ticket = TicketSystem ()
		self.same_search = {}

	def build_art_cache_filename (self, db, entry, extension):
		artist, album = get_search_props(db, entry)
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

		art_location_url = self.cache_pixbuf(db, entry, pixbuf)
		callback (entry, pixbuf, art_location_url, None, None)
		for Engine in ART_SEARCHES_LOCAL:
			try:
				Engine ().save_pixbuf (db, entry, pixbuf)
			except AttributeError:
				pass

	def cache_pixbuf (self, db, entry, pixbuf):
		if entry is None or pixbuf is None:
			return None

		meta_location = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_META)
		self.write_meta_file (meta_location, None, None)

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
		return "file://" + pathname2url(art_location)

	def cancel_get_pixbuf (self, entry):
		self.ticket.purge (entry)
  
	def get_pixbuf (self, db, entry, is_playing, callback):
		if entry is None:
			callback (entry, None, None, None, None)
			return

		st_artist, st_album = get_search_props(db, entry)

		# replace quote characters
		# don't replace single quote: could be important punctuation
		for char in ["\""]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')

		rb.Coroutine (self.image_search, db, st_album, st_artist, entry, is_playing, callback).begin ()

	def image_search (self, plexer, db, st_album, st_artist, entry, is_playing, callback):
		art_location_jpg = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_JPG)
		art_location_png = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_PNG)
		art_location_meta = self.build_art_cache_filename (db, entry, ART_CACHE_EXTENSION_META)
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
			(tooltip_image, tooltip_text) = self.read_meta_file (art_location_meta)
			art_location_url = "file://" + pathname2url(art_location)
			callback (entry, pixbuf, art_location_url, tooltip_image, tooltip_text)
			return

		# Check if we're already searching for art for this album
		# (this won't work for compilations, but there isn't much we can do about that)
		def find_same_search(a, b, db):
			a_artist, a_album = get_search_props(db, a)
			b_artist, b_album = get_search_props(db, b)
			return (a_artist == b_artist) and (a_album == b_album)

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

				def handle_result_pixbuf (pixbuf, engine_uri, tooltip_image, tooltip_text, should_save):
					if self.ticket.release (entry, ticket):
						if should_save:
							if pixbuf.get_has_alpha ():
								pixbuf.save (art_location_png, ART_CACHE_FORMAT_PNG, ART_CACHE_SETTINGS_PNG)
								uri = "file://" + pathname2url(art_location_png)
							else:
								pixbuf.save (art_location_jpg, ART_CACHE_FORMAT_JPG, ART_CACHE_SETTINGS_JPG)
								uri = "file://" + pathname2url(art_location_jpg)

							self.write_meta_file (art_location_meta, tooltip_image, tooltip_text)
						else:
							uri = engine_uri

						print "found image for %s" % (db.entry_get(entry, rhythmdb.PROP_LOCATION))
						callback (entry, pixbuf, uri, tooltip_image, tooltip_text)
						for m in self.same_search.pop(entry, []):
							print "and for same search %s" % (db.entry_get(m, rhythmdb.PROP_LOCATION))
							callback (m, pixbuf, uri, tooltip_image, tooltip_text)

					self.write_blist (blist_location, blist)
					self.same_search.pop (entry, None)

				# fetch the meta details for the engine
				(tooltip_image, tooltip_text) = engine.get_result_meta (results)

				# first check if the engine gave us a pixbuf
				pixbuf = engine.get_result_pixbuf (results)
				if pixbuf:
					handle_result_pixbuf (pixbuf, None, tooltip_image, tooltip_text, True)
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
						handle_result_pixbuf (pixbuf, url, tooltip_image, tooltip_text, engine_remote)
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
			callback (entry, None, None, None, None)
			for m in self.same_search.pop (entry, []):
				print "or for same search %s" % (db.entry_get(m, rhythmdb.PROP_LOCATION))
				callback (m, None, None, None, None)

		self.write_blist (blist_location, blist)
		self.same_search.pop (entry, None)


	def read_meta_file (self, meta_location):
		if os.path.exists (meta_location):
			data = [line.strip () for line in file (meta_location)]
			return (data[0], data[1])
		else:
			return (None, None)

	def write_meta_file (self, meta_location, tooltip_image, tooltip_text):
		if tooltip_text is not None:
			meta_file = file (meta_location, 'w')
			meta_file.writelines([tooltip_image, "\n", tooltip_text, "\n"])
			meta_file.close()
		elif os.path.exists (meta_location):
			os.unlink (meta_location)


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
