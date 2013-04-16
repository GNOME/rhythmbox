# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - Ed Catmur <ed@catmur.co.uk>
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

class Coroutine:
	"""A simple message-passing coroutine implementation. 
	Not thread- or signal-safe. 
	Usage:
		def my_iter (plexer, args):
			some_async_task (..., callback=plexer.send (tokens))
			yield None
			tokens, (data, ) = plexer.receive ()
			...
		Coroutine (my_iter, args).begin ()
	"""
	def __init__ (self, iter, *args):
		self._continuation = iter (self, *args)
		self._executing = False
	def _resume (self):
		if not self._executing:
			self._executing = True
			try:
				try:
					next(self._continuation)
					while self._data:
						next(self._continuation)
				except StopIteration:
					pass
			finally:
				self._executing = False
	def clear (self):
		self._data = []
	def begin (self):
		self.clear ()
		self._resume ()
	def send (self, *tokens):
		def callback (*args):
			self._data.append ((tokens, args))
			self._resume ()
		return callback
	def receive (self):
		return self._data.pop (0)

