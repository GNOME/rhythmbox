# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Ed Catmur <ed@catmur.co.uk>
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

import os
import rhythmdb
import gnomevfs

IMAGE_NAMES = ["cover", "album", "albumart", ".folder", "folder"]
LOAD_DIRECTORY_FLAGS = gnomevfs.FILE_INFO_GET_MIME_TYPE | gnomevfs.FILE_INFO_FORCE_FAST_MIME_TYPE

def file_root (f_name):
	return os.path.splitext (f_name)[0].lower ()

class LocalCoverArtSearch:
	def __init__ (self, loader):
		self.loader = loader

	def _load_dir_cb (self, handle, files, exception, (results, on_search_completed_cb, entry, args)):
		for f in files:
			if f.mime_type.startswith ("image/") and f.permissions & gnomevfs.PERM_USER_READ:
				results.append (f.name)
		if exception:
			on_search_completed_cb (self, entry, results, *args)
			if not issubclass (exception, gnomevfs.EOFError):
				print "Error reading \"%s\": %s" % (self.uri.parent, exception)

	def search (self, db, entry, on_search_completed_cb, *args):
		self.uri = gnomevfs.URI (db.entry_get (entry,rhythmdb.PROP_LOCATION))
		gnomevfs.async.load_directory (self.uri.parent, self._load_dir_cb, LOAD_DIRECTORY_FLAGS, data=([], on_search_completed_cb, entry, args))

	def search_next (self):
		return False

	def get_best_match_urls (self, results):
		# Compare lower case, without file extension
		for name in [file_root (self.uri.short_name)] + IMAGE_NAMES:
			for f_name in results:
				if file_root (f_name) == name:
					yield self.uri.parent.append_file_name (f_name).path

