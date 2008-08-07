# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Jonathan Matthew
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# The Rhythmbox authors hereby grants permission for non-GPL compatible
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

import gobject

use_gio = False
try:
	import gio
	# before 2.15.3, file.load_contents_async didn't work correctly
	if gio.pygio_version > (2,15,2):
		use_gio = True
except:
	# probably don't have gio at all
	pass

if use_gio is False:
	import gnomevfs

class GioSrc(object):
	def __init__ (self):
		pass

	def _contents_cb(self, file, result, (callback, args)):
		try:
			(contents, length, etag) = file.load_contents_finish(result)
			callback(contents, *args)
		except Exception, e:
			print "error getting file contents: %s" % e
			callback(None, *args)

	def get_url (self, url, callback, *args):
		try:
			file = gio.File(url)
			file.load_contents_async(callback = self._contents_cb, user_data = (callback, args))
		except Exception, e:
			print "error getting file contents: %s" % e
			callback(None, *args)


class GnomeVFSAsyncSrc (object):  
	def __init__ (self):
	    	self.chunk = 4096

	def read_cb (self, handle, buffer, exc_type, bytes_requested, (data, callback, args)):
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				gobject.idle_add (callback, data, *args)
				handle.close (lambda *args: None)
			else:
				gobject.idle_add (callback, None, *args)
				handle.close (lambda *args: None)
			return
 			
		data += buffer
		handle.read (self.chunk, self.read_cb, (data, callback, args))

	def open_cb (self, handle, exc_type, (data, callback, args)):
		if exc_type:
			gobject.idle_add (callback, None, *args)
			return

		handle.read (self.chunk, self.read_cb, (data, callback, args))
    
	def get_url (self, url, callback, *args):
		gnomevfs.async.open (url, self.open_cb, data=("", callback, args))

def Loader ():
	if use_gio:
		return GioSrc()
	else:
		return GnomeVFSAsyncSrc()

