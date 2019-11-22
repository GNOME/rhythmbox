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

import gi
gi.require_version('Soup', '2.4')
from gi.repository import GObject, GLib, Gio, Soup
import sys

from rbconfig import rhythmbox_version

def call_callback(callback, data, args):
	try:
		v = callback(data, *args)
		return v
	except Exception as e:
		sys.excepthook(*sys.exc_info())

loader_session = None

class Loader(object):
	def __init__ (self):
		global loader_session
		if loader_session is None:
			loader_session = Soup.Session()
			loader_session.props.user_agent = "Rhythmbox/" + rhythmbox_version
		self._cancel = Gio.Cancellable()

	def _message_cb(self, session, message, data):
		status = message.props.status_code
		if status == 200:
			call_callback(self.callback, message.props.response_body_data.get_data(), self.args)
		else:
			call_callback(self.callback, None, self.args)

	def get_url (self, url, callback, *args):
		self.url = url
		self.callback = callback
		self.args = args
		try:
			global loader_session
			req = Soup.Message.new("GET", url)
			headers = req.props.request_headers
			loader_session.queue_message(req, self._message_cb, None)
		except Exception as e:
			sys.excepthook(*sys.exc_info())
			callback(None, *args)

	def cancel (self):
		self._cancel.cancel()


