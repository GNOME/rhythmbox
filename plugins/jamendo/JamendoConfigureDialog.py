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

import rb
from gi.repository import Gtk, Gio, GObject, PeasGtk

format_list = ['ogg3', 'mp32']

class JamendoConfigureDialog (GObject.Object, PeasGtk.Configurable):
	__gtype_name__ = 'JamendoConfigureDialog'
	object = GObject.property(type=GObject.Object)

	def __init__(self):
		GObject.Object.__init__(self)
		self.settings = Gio.Settings("org.gnome.rhythmbox.plugins.jamendo")
		self.hate = self

	def do_create_configure_widget(self):
		builder = Gtk.Builder()
		builder.add_from_file(rb.find_plugin_file(self, "jamendo-prefs.ui"))

		self.config = builder.get_object('config')
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

		self.audio_combobox.connect("changed", self.audio_combobox_changed)
		return self.config

	def audio_combobox_changed (self, combobox):
		format = self.audio_combobox.get_active()
		self.settings['format'] = format_list[format]
