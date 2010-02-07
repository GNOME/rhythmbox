# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2010 Jonathan Matthew
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
#

import rb

from config import ReplayGainConfigDialog
from player import ReplayGainPlayer

class ReplayGainPlugin(rb.Plugin):

	def __init__ (self):
		rb.Plugin.__init__ (self)
		self.config_dialog = None

	def activate (self, shell):
		self.player = ReplayGainPlayer(shell)

	def deactivate (self, shell):
		self.config_dialog = None
		self.player.deactivate()
		self.player = None

	def create_configure_dialog(self, dialog=None):
		if self.config_dialog is None:
			self.config_dialog = ReplayGainConfigDialog(self)
			self.config_dialog.connect('response', self.config_dialog_response_cb)

		self.config_dialog.present()
		return self.config_dialog

	def config_dialog_response_cb(self, dialog, response):
		dialog.hide()
