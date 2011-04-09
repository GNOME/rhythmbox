# -*- coding: utf-8 -*-

# JamendoConfigureDialog.py
#
# Copyright (C) 2007 - Guillaume Desmottes
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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

import gobject

import rb
from gi.repository import Gtk, Gio

# format_list = ['ogg3', 'mp32']

class JamendoConfigureDialog (object):
	def __init__(self, builder_file):
		self.settings = Gio.Settings("org.gnome.rhythmbox.plugins.jamendo")

		builder = Gtk.Builder()
		builder.add_from_file(builder_file)

		self.dialog = builder.get_object('preferences_dialog')
		self.audio_combobox = builder.get_object("audio_combobox")

		# probably should just bind this, but too lazy

		format_text = self.settings['format']
		if not format_text:
			format_text = "ogg3"
		try:
			format = format_list.index(format_text)
		except ValueError:
			format = 0
		self.audio_combobox.set_active(format)

		self.dialog.connect("response", self.dialog_response)
		self.audio_combobox.connect("changed", self.audio_combobox_changed)

	def get_dialog (self):
		return self.dialog

	def dialog_response (self, dialog, response):
		dialog.hide()

	def audio_combobox_changed (self, combobox):
		format = self.audio_combobox.get_active()
		self.settings['format'] = format_list[format]
