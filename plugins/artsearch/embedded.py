# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2013 - Jonathan Matthew <jonathan@d14n.org>
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

from gi.repository import RB
from gi.repository import Gst, GstPbutils

class EmbeddedSearch(object):

	def finished_cb(self, discoverer):
		self.callback(self.callback_args)

	def discovered_cb(self, discoverer, info, error):
		tags = info.get_tags()
		if tags is None:
			return

		for tagname in ('image', 'preview-image'):
			(found, sample) = tags.get_sample(tagname)
			if not found:
				print("no %s" % tagname)
				continue

			pixbuf = RB.gst_process_embedded_image(tags, tagname)
			if not pixbuf:
				print("no pixbuf in %s" % tagname)
				continue

			print("trying to store pixbuf from %s" % tagname)
			key = RB.ExtDBKey.create_storage("album", self.search_key.get_field("album"))
			artists = self.search_key.get_field_values("artist")
			key.add_field("artist", artists[0])
			self.store.store(key, RB.ExtDBSourceType.EMBEDDED, pixbuf)
			return


	def search (self, key, last_time, store, callback, *args):
		location = key.get_info("location")
		if location is None:
			print("not searching, we don't have a location")
			callback(args)
			return

		if location.startswith("file://") is False:
			print("not searching in non-local file %s" % location)
			callback(args)
			return

		# should avoid checking the playing entry, since the player already handles that

		self.callback = callback
		self.callback_args = args
		self.store = store
		self.search_key = key

		print("discovering %s" % location)
		self.discoverer = GstPbutils.Discoverer(timeout=Gst.SECOND*5)
		self.discoverer.connect('finished', self.finished_cb)
		self.discoverer.connect('discovered', self.discovered_cb)
		self.discoverer.start()
		self.discoverer.discover_uri_async(location)
