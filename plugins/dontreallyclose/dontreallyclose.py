# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2008 Jonathan Matthew
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


# this plugin is more license than plugin.

import rb
import gconf

class DontReallyClosePlugin(rb.Plugin):
	def __init__(self):
		rb.Plugin.__init__(self)
		self.delete_event_id = 0

	def delete_event_cb(self, widget, event):
		widget.hide()
		gconf.client_get_default().set_bool("/apps/rhythmbox/state/window_visible", 0)
		return True

	def activate(self, shell):
		self.delete_event_id = shell.props.window.connect('delete-event', self.delete_event_cb)

	def deactivate(self, shell):
		if self.delete_event_id != 0:
			shell.props.window.disconnect(self.delete_event_id)
			self.delete_event_id = 0

