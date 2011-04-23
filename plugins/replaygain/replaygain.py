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
from gi.repository import GObject, Peas
from gi.repository import RB

from config import ReplayGainConfig
from player import ReplayGainPlayer

class ReplayGainPlugin(GObject.Object, Peas.Activatable):
	__gtype_name__ = 'ReplayGainPlugin'
	object = GObject.property (type=GObject.Object)

	def __init__ (self):
		GObject.Object.__init__ (self)
		self.config_dialog = None

	def do_activate (self):
		self.player = ReplayGainPlayer(self.object)

	def do_deactivate (self):
		self.config_dialog = None
		self.player.deactivate()
		self.player = None
