# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2009 - Jonathan Matthew
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

import gobject
import gtk

def callback_with_gdk_lock(callback, data, args):
	gtk.gdk.threads_enter()
	v = callback(data, *args)
	gtk.gdk.threads_leave()
	return v


class GioLoader(object):
	def __init__ (self):
		self._cancel = gio.Cancellable()

	def _contents_cb (self, file, result):
		try:
			(contents, length, etag) = file.load_contents_finish(result)
			callback_with_gdk_lock(self.callback, contents, self.args)
		except Exception, e:
			# somehow check if we just got cancelled
			callback_with_gdk_lock(self.callback, None, self.args)

	def get_url (self, url, callback, *args):
		self.url = url
		self.callback = callback
		self.args = args
		try:
			file = gio.File(url)
			file.load_contents_async(callback = self._contents_cb, cancellable=self._cancel)
		except Exception, e:
			print "error getting contents of %s: %s" % (url, e)
			callback(None, *args)

	def cancel (self):
		self._cancel.cancel()


class GioChunkLoader(object):
	def __init__ (self):
		self._cancel = gio.Cancellable()

	def _callback(self, result):
		return self.callback(result, self.total, *self.args)

	def _callback_gdk(self, result):
		gtk.gdk.threads_enter()
		v = self._callback(result)
		gtk.gdk.threads_leave()
		return v

	def _error_idle_cb(self, error):
		self._callback_gdk(error)
		return False

	def _read_idle_cb(self, (stream, data)):
		if (self._callback_gdk(data) is not False) and data:
			stream.read_async (self.chunksize, self._read_cb, cancellable=self._cancel)
		else:
			# finished or cancelled by callback
			stream.close()

		return False

	def _read_cb(self, stream, result):
		try:
			data = stream.read_finish(result)
		except gio.Error, e:
			print "error reading file %s: %s" % (self.uri, e.message)
			stream.close()
			gobject.idle_add(self._error_idle_cb, e)
		
		# this is mostly here to hack around bug 575781
		gobject.idle_add(self._read_idle_cb, (stream, data))


	def _open_cb(self, file, result):
		try:
			stream = file.read_finish(result)
		except gio.Error, e:
			print "error reading file %s: %s" % (self.uri, e.message)
			self._callback_gdk(e)
		
		stream.read_async(self.chunksize, self._read_cb, cancellable=self._cancel)

	def _info_cb(self, file, result):
		try:
			info = file.query_info_finish(result)
			self.total = info.get_attribute_uint64(gio.FILE_ATTRIBUTE_STANDARD_SIZE)

			file.read_async(self._open_cb, cancellable=self._cancel)
		except gio.Error, e:
			print "error checking size of source file %s: %s" % (self.uri, e.message)
			self._callback_gdk(e)


	def get_url_chunks (self, uri, chunksize, want_size, callback, *args):
		try:
			self.uri = uri
			self.chunksize = chunksize
			self.total = 0
			self.callback = callback
			self.args = args

			file = gio.File(uri)
			if want_size:
				file.query_info_async(self._info_cb, gio.FILE_ATTRIBUTE_STANDARD_SIZE, cancellable=self._cancel)
			else:
				file.read_async(self._open_cb, cancellable=self.cancel)
		except gio.Error, e:
			print "error reading file %s: %s" % (uri, e.message)
			self._callback(e)

	def cancel (self):
		self._cancel.cancel()


class GioUpdateCheck(object):
	def __init__ (self):
		self._cancel = gio.Cancellable()

	def _file_info_cb (self, file, result):
		try:
			rfi = file.query_info_finish(result)

			remote_mod = rfi.get_attribute_uint64(gio.FILE_ATTRIBUTE_TIME_MODIFIED)
			callback_with_gdk_lock(self.callback, remote_mod != self.local_mod, self.args)
		except Exception, e:
			print "error checking for update: %s" % e
			callback_with_gdk_lock(self.callback, False, self.args)

	def check_for_update (self, local, remote, callback, *args):
		self.local = local
		self.remote = remote
		self.callback = callback
		self.args = args

		try:
			lf = gio.File(local)
			lfi = lf.query_info(gio.FILE_ATTRIBUTE_TIME_MODIFIED)
			self.local_mod = lfi.get_attribute_uint64(gio.FILE_ATTRIBUTE_TIME_MODIFIED)

			rf = gio.File(remote)
			rf.query_info_async(self._file_info_cb, gio.FILE_ATTRIBUTE_TIME_MODIFIED, cancellable=self._cancel)
		except Exception, e:
			print "error checking for update: %s" % e
			self.callback(True, *self.args)

	def cancel (self):
		self._cancel.cancel()


class GnomeVFSLoader (object):
	def __init__ (self):
		self.chunk = 4096

	def _read_cb (self, handle, buffer, exc_type, bytes_req):
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				callback_with_gdk_lock (self.callback, self.data, self.args)
				handle.close (lambda *args: None)
			else:
				callback_with_gdk_lock (self.callback, None, self.args)
				handle.close (lambda *args: None)
			return
 			
		self.data += buffer
		handle.read (self.chunk, self._read_cb)

	def _open_cb (self, handle, exc_type):
		if exc_type:
			callback_with_gdk_lock (self.callback, None, self.args)
			return

		self.handle = handle
		self.data = ""
		handle.read (self.chunk, self._read_cb)
    
	def get_url (self, url, callback, *args):
		self.url = url
		self.callback = callback
		self.args = args
		gnomevfs.async.open (url, self._open_cb)

	def cancel (self):
		self.handle.cancel()


class GnomeVFSChunkLoader (object):
	def __init__ (self):
		pass

	def _callback(self, result):
		return self.callback(result, self.total, *self.args)

	def _callback_gdk(self, result):
		gtk.gdk.threads_enter()
		v = self._callback(result)
		gtk.gdk.threads_leave()
		return v

	def _read_cb (self, handle, buffer, exc_type, bytes_requested):
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				self._callback_gdk (None)
			else:
				self._callback_gdk (exc_type())
			handle.close (lambda *args: None)
		else:
			if self._callback_gdk (buffer) is False:
				handle.close(lambda *args: None)
			else:
				handle.read (self.chunksize, self._read_cb)


	def _open_cb (self, handle, exc_type):
		if exc_type:
			self._callback_gdk (exc_type())
		else:
			handle.read (self.chunksize, self._read_cb)

	def _info_cb (self, handle, results):
		try:
			(uri, exc, info) = results[0]
			self.total = info.size
		except ValueError:
			pass

		self.handle = gnomevfs.async.open (self.vfs_uri, self._open_cb)

	def get_url_chunks (self, uri, chunksize, want_size, callback, *args):
		self.uri = uri
		self.chunksize = chunksize
		self.total = 0
		self.callback = callback
		self.args = args
		self.vfs_uri = gnomevfs.URI(uri)
		if want_size:
			self.handle = gnomevfs.async.get_file_info ((self.vfs_uri,), self._info_cb)
		else:
			self.handle = gnomevfs.async.open (self.vfs_uri, self._open_cb)

	def cancel (self):
		if self.handle:
			self.handle.cancel()


class GnomeVFSUpdateCheck (object):
	def __init__ (self):
		pass

	def _info_cb (self, handle, results):
		(local_uri, local_exc, local_info) = results[0]
		(remote_uri, remote_exc, remote_info) = results[1]

		result = True

		if remote_exc:
			print "error checking remote URI %s: %s" % (self.remote, remote_exc)
			result = False
		elif local_exc:
			if issubclass (local_exc, gnomevfs.NotFoundError):
				print "local URI %s not found" % self.local
			else:
				print "error checking local URI %s: %s" % (self.local, local_exc)
		else:
			try:
				result = (remote_info.mtime > local_info.mtime)
			except ValueError, e:
				print "error comparing modification times: %s" % e
		
		callback_with_gdk_lock (self.callback, result, self.args)


	def check_for_update (self, local, remote, callback, *args):
		self.callback = callback
		self.args = args
		self.local = local
		self.remote = remote

		uris = (gnomevfs.URI(local), gnomevfs.URI(remote))
		self.handle = gnomevfs.async.get_file_info (uris, self._info_cb)

	def cancel (self):
		self.handle.cancel ()






# now figure out which set of implementations to use

use_gio = False
try:
	import gio
	# before 2.16.0, file.load_contents_async didn't work correctly
	if gio.pygio_version > (2,15,4):
		use_gio = True
except:
	# probably don't have gio at all
	pass

if use_gio:
	Loader = GioLoader
	ChunkLoader = GioChunkLoader
	UpdateCheck = GioUpdateCheck
else:
	import gnomevfs
	Loader = GnomeVFSLoader
	ChunkLoader = GnomeVFSChunkLoader
	UpdateCheck = GnomeVFSUpdateCheck

