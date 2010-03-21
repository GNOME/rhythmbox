# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009  Jonathan Matthew  <jonathan@d14n.org>
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

import rhythmdb

import gst
import gobject
import gtk

class EmbeddedCoverArtSearch (object):
	def __init__ (self):
		pass

	def _tag_cb (self, bus, message):
		taglist = message.parse_tag()
		for tag in (gst.TAG_IMAGE, gst.TAG_PREVIEW_IMAGE):
			if tag not in taglist.keys():
				continue

			print "got image tag %s" % tag
			try:
				loader = gtk.gdk.PixbufLoader()
				if loader.write(taglist[tag]) and loader.close():
					print "successfully extracted pixbuf"
					self.got_pixbuf = True
					self.callback(self, self.entry, loader.get_pixbuf(), *self.args)
					return
			except gobject.GError:
				continue

	def _state_changed_cb (self, bus, message):
		if message.src != self.pipeline:
			return

		old, new, pending = message.parse_state_changed()
		if ((new, pending) == (gst.STATE_PAUSED, gst.STATE_VOID_PENDING)):
			print "pipeline has gone to PAUSED"
			self.pipeline.set_state(gst.STATE_NULL)
			if self.got_pixbuf is False:
				self.callback(self, self.entry, None, *self.args)

	def _error_cb (self, bus, message):
		error = message.parse_error()
		print "got error: %s" % error[1]
		self.pipeline.set_state(gst.STATE_NULL)
		self.callback(self, self.entry, None, *self.args)

	def _decoded_pad_cb (self, decodebin, pad, last):
		if self.sinkpad.is_linked():
			return

		caps = pad.get_caps()
		if caps[0].get_name() in ('audio/x-raw-float', 'audio/x-raw-int'):
			print "linking decoded audio pad to fakesink"
			pad.link(self.sinkpad)

	def search (self, db, entry, is_playing, on_search_completed, *args):

		# only search if we're not already playing this entry
		if is_playing:
			print "not checking for embedded cover art in playing entry"
			on_search_completed (self, entry, None, *args)
			return

		# only search local files
		uri = db.entry_get(entry, rhythmdb.PROP_LOCATION)
		if uri.startswith("file://") is False:
			print "not checking for embedded cover art in non-local entry %s" % uri
			on_search_completed (self, entry, None, *args)
			return

		self.entry = entry
		self.args = args
		self.callback = on_search_completed
		self.args = args
		self.got_pixbuf = False

		# set up pipeline and bus callbacks
		self.pipeline = gst.Pipeline()
		bus = self.pipeline.get_bus()
		bus.add_signal_watch()
		bus.connect("message::tag", self._tag_cb)
		bus.connect("message::state-changed", self._state_changed_cb)
		bus.connect("message::error", self._error_cb)

		# create elements
		self.src = gst.element_make_from_uri(gst.URI_SRC, uri)
		self.decodebin = gst.element_factory_make("decodebin2")
		self.sink = gst.element_factory_make("fakesink")
		self.decodebin.connect('new-decoded-pad', self._decoded_pad_cb)

		self.pipeline.add(self.src, self.decodebin, self.sink)
		self.src.link(self.decodebin)

		self.sinkpad = self.sink.get_pad('sink')

		self.pipeline.set_state(gst.STATE_PAUSED)


	def search_next (self):
		return False

	def get_result_meta (self, search_results):
		return (None, None)

	def get_result_pixbuf (self, search_results):
		return search_results

	def get_best_match_urls (self, search_results):
		return []

