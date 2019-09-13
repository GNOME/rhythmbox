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

from gi.repository import RB
from gi.repository import GObject, GLib, Gio

IMAGE_NAMES = ["cover", "album", "albumart", "front", ".folder", "folder"]
ITEMS_PER_NOTIFICATION = 10

def file_root (f_name):
	return os.path.splitext (f_name)[0].lower ()

def shared_prefix_length (a, b):
	l = 0
	while a[l] == b[l]:
		l = l+1
	return l

class LocalSearch:
	def __init__ (self):
		pass

	def finished(self, results):
		parent = self.file.get_parent()
		ordered = []
		key = RB.ExtDBKey.create_storage("album", self.album)
		key.add_field("artist", self.artists[0])

		# Compare lower case, without file extension
		for name in [file_root (self.file.get_basename())] + IMAGE_NAMES:
			for f_name in results:
				if file_root (f_name) == name:
					uri = parent.resolve_relative_path(f_name).get_uri()
					self.store.store_uri(key, RB.ExtDBSourceType.USER, uri)

		# look for file names containing the artist and album (case-insensitive)
		# (mostly for jamendo downloads)
		album = self.album.lower()
		for f_name in results:
			f_root = file_root (f_name).lower()
			for artist in self.artists:
				artist = artist.lower()
				if f_root.find (artist) != -1 and f_root.find (album) != -1:
					nkey = RB.ExtDBKey.create_storage("album", album)
					nkey.add_field("artist", artist)
					uri = parent.resolve_relative_path(f_name).get_uri()
					print("found album+artist match " + uri)
					self.store.store_uri(nkey, RB.ExtDBSourceType.USER, uri)

		# if that didn't work, look for the longest shared prefix
		# only accept matches longer than 2 to avoid weird false positives
		match_len = 2
		match = None
		for f_name in results:
			pl = shared_prefix_length(f_name, self.file.get_basename())
			if pl > match_len:
				match_len = pl
				match = f_name

		if match is not None:
			uri = parent.resolve_relative_path(match).get_uri()
			print("found prefix match " + uri)
			self.store.store_uri(key, RB.ExtDBSourceType.USER, uri)

		self.callback(self.callback_args)

	def _close_enum_cb(self, fileenum, result, results):
		try:
			fileenum.close_finish(result)
		except Exception as e:
			print("couldn't close file enumerator: %s" % e)
		

	def _enum_dir_cb(self, fileenum, result, results):
		try:
			files = fileenum.next_files_finish(result)
			if files is None or len(files) == 0:
				print("okay, done; got %d files" % len(results))
				fileenum.close_async(GLib.PRIORITY_DEFAULT, None, self._close_enum_cb, None)
				self.finished(results)
				return

			for f in files:
				ct = f.get_attribute_string("standard::content-type")
				# assume readable unless told otherwise
				readable = True
				if f.has_attribute("access::can-read"):
					readable = f.get_attribute_boolean("access::can-read")
				if ct is not None and ct.startswith("image/") and readable:
					results.append(f.get_name())

			fileenum.next_files_async(ITEMS_PER_NOTIFICATION, GLib.PRIORITY_DEFAULT, None, self._enum_dir_cb, results)
		except Exception as e:
			print("okay, probably done: %s" % e)
			import sys
			sys.excepthook(*sys.exc_info())
			self.finished(results)
			fileenum.close_async(GLib.PRIORITY_DEFAULT, None, self._close_enum_cb, None)


	def _enum_children_cb(self, parent, result, data):
		try:
			enumfiles = parent.enumerate_children_finish(result)
			enumfiles.next_files_async(ITEMS_PER_NOTIFICATION, GLib.PRIORITY_DEFAULT, None, self._enum_dir_cb, [])
		except Exception as e:
			print("okay, probably done: %s" % e)
			if e.matches(Gio.IOErrorEnum, Gio.IOErrorEnum.NOT_SUPPORTED):
				self.callback(self.callback_args)
			elif not isinstance(e, GLib.GError):
				import sys
				sys.excepthook(*sys.exc_info())
				self.callback(self.callback_args)


	def search (self, key, last_time, store, callback, *args):
		# ignore last_time

		location = key.get_info("location")
		if location is None:
			print("not searching, we don't have a location")
			callback(args)
			return

		self.file = Gio.file_new_for_uri(location)

		self.album = key.get_field("album")
		if self.album is None:
			print("not searching, we don't have an album")
			callback(args)
			return

		self.artists = key.get_field_values("artist")
		self.store = store
		self.callback = callback
		self.callback_args = args

		print('searching for local art for %s' % (self.file.get_uri()))
		parent = self.file.get_parent()
		enumfiles = parent.enumerate_children_async("standard::content-type,access::can-read,standard::name", 0, 0, None, self._enum_children_cb, None)
