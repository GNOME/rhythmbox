# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Ed Catmur <ed@catmur.co.uk>
# Copyright (C) 2009 - Jonathan Matthew <jonathan@d14n.org>
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

import os
import rhythmdb
import rb
import gobject
import gio

IMAGE_NAMES = ["cover", "album", "albumart", ".folder", "folder"]
ITEMS_PER_NOTIFICATION = 10
ART_SAVE_NAME = 'Cover.jpg'
ART_SAVE_FORMAT = 'jpeg'
ART_SAVE_SETTINGS = {"quality": "100"}

def file_root (f_name):
	return os.path.splitext (f_name)[0].lower ()

def shared_prefix_length (a, b):
	l = 0
	while a[l] == b[l]:
		l = l+1
	return l


class LocalCoverArtSearch:
	def __init__ (self):
		pass

	def _enum_dir_cb(self, fileenum, result, (results, on_search_completed_cb, entry, args)):
		try:
			files = fileenum.next_files_finish(result)
			if files is None or len(files) == 0:
				print "okay, done; got %d files" % len(results)
				on_search_completed_cb(self, entry, results, *args)
				return

			for f in files:
				ct = f.get_attribute_string("standard::fast-content-type")
				if ct.startswith("image/") and f.get_attribute_boolean("access::can-read"):
					results.append(f.get_name())	# hm

			fileenum.next_files_async(ITEMS_PER_NOTIFICATION, callback = self._enum_dir_cb, user_data=(results, on_search_completed_cb, entry, args))
		except Exception, e:
			print "okay, probably done: %s" % e
			on_search_completed_cb(self, entry, results, *args)

	def search (self, db, entry, on_search_completed_cb, *args):

		self.file = gio.File(entry.get_playback_uri())
		if self.file.get_uri_scheme() in ('http','cdda'):
			print 'not searching for local art for %s' % (self.file.get_uri())
			on_search_completed_cb (self, entry, [], *args)
			return

		self.artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		self.album = db.entry_get (entry, rhythmdb.PROP_ALBUM)

		print 'searching for local art for %s' % (self.file.get_uri())
		parent = self.file.get_parent()
		enumfiles = parent.enumerate_children(attributes="standard::fast-content-type,access::can-read,standard::name")
		enumfiles.next_files_async(ITEMS_PER_NOTIFICATION, callback = self._enum_dir_cb, user_data=([], on_search_completed_cb, entry, args))

	def search_next (self):
		return False

	def get_best_match_urls (self, results):
		parent = self.file.get_parent()

		# Compare lower case, without file extension
		for name in [file_root (self.file.get_basename())] + IMAGE_NAMES:
			for f_name in results:
				if file_root (f_name) == name:
					yield parent.resolve_relative_path(f_name).get_uri()

		# look for file names containing the artist and album (case-insensitive)
		# (mostly for jamendo downloads)
		artist = self.artist.lower()
		album = self.album.lower()
		for f_name in results:
			f_root = file_root (f_name).lower()
			if f_root.find (artist) != -1 and f_root.find (album) != -1:
				yield parent.resolve_relative_path(f_name).get_uri()

		# if that didn't work, look for the longest shared prefix
		# only accept matches longer than 2 to avoid weird false positives
		match = (2, None)
		for f_name in results:
			pl = shared_prefix_length(f_name, self.file.get_basename())
			if pl > match[0]:
				match = (pl, f_name)

		if match[1] is not None:
			yield parent.resolve_relative_path(match[1]).get_uri()

	def pixbuf_save (self, plexer, pixbuf, uri):
		def pixbuf_cb(buf):
			f = gio.File(uri)
			f.replace_contents_async(buf, plexer.send())
			yield None
			_, (file, result) = plexer.receive()
			try:
				file.replace_contents_finish(result)
			except Exception, e:
				print "error creating \"%s\": %s" % (uri, e)

		pixbuf.save_to_callback(pixbuf_cb, ART_SAVE_FORMAT, ART_SAVE_SETTINGS)

	def _save_dir_cb (self, enum, result, (db, entry, dir, pixbuf)):
		artist, album = [db.entry_get (entry, x) for x in [rhythmdb.PROP_ARTIST, rhythmdb.PROP_ALBUM]]
		try:
			files = enum.next_files_finish(result)
			if len(files) == 0:
				art_file = dir.resolve_relative_path(file.get_display_name())

			for f in files:
				ct = f.get_attribute_string("standard::fast-content-type")
				if ct.startswith("image/") or ct.startswith("x-directory/"):
					continue

				uri = dir.resolve_relative_path(f.get_name())
				u_entry = db.entry_lookup_by_location (uri)
				if e_entry:
					u_artist, u_album = [db.entry_get (u_entry, x) for x in [rhythmdb.PROP_ARTIST, rhythmdb.PROP_ALBUM]]
					if album != u_album:
						print "Not saving local art; encountered media with different album (%s, %s, %s)" % (uri, u_artist, u_album)
						enum.close()
						return
					continue
				print "Not saving local art; encountered unknown file (%s)" % uri
				enum.close()
				return
		except Exception, e:
			print "Error reading \"%s\": %s" % (dir, exception)

	def save_pixbuf (self, db, entry, pixbuf):
		uri = entry.get_playback_uri()
		if uri is None or uri == '':
			return

		f = gio.File(uri)
		if uri.get_uri_scheme() == 'http':
			print "not saving local art for %s" % uri
			return

		print 'checking whether to save local art for %s' % uri
		parent = f.get_parent()
		enumfiles = parent.enumerate_children(attributes="standard::fast-content-type,access::can-read,standard::name")
		enumfiles.next_files_async(ITEMS_PER_NOTIFICATION, callback = self._save_dir_cb, user_data=(db, entry, parent, pixbuf))

