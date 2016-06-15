# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2007 James Livingston
# Copyright (C) 2007 Sirio Bolaños Puchet
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

from LyricsSites import lyrics_sites

from os import system, path

import rb
from gi.repository import RB, Gtk, Gio, GObject, PeasGtk

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class LyricsConfigureDialog (GObject.Object, PeasGtk.Configurable):
	__gtype_name__ = 'LyricsConfigureDialog'
	object = GObject.property(type=GObject.Object)

	def __init__(self):
		GObject.Object.__init__(self)
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.lyrics")

	def do_create_configure_widget(self):
		builder = Gtk.Builder()
		builder.add_from_file(rb.find_plugin_file(self, "lyrics-prefs.ui"))

		self.config = builder.get_object("config")

		self.choose_button = builder.get_object("choose_button")
		self.path_display = builder.get_object("path_display")

		self.choose_button.connect("clicked", self.choose_callback)

		engines, self.folder = self.get_prefs()
		if self.folder is None:
			self.folder = '~/.lyrics'
		self.path_display.set_text(self.folder)

		# build site list
		site_box = builder.get_object("sites")
		self.site_checks = {}
		for s in lyrics_sites:
			site_id = s['id']
			checkbutton = Gtk.CheckButton(label = s['name'])
			checkbutton.set_active(s['id'] in engines)
			checkbutton.connect("toggled", self.set_sites)
			self.site_checks[site_id] = checkbutton
			site_box.pack_start(checkbutton, True, True, 0)

		site_box.show_all()

		return self.config

	def set_sites(self, widget):
		sites = []
		for s in lyrics_sites:
			check = self.site_checks[s['id']]
			if check is None:
				continue

			if check.get_active():
				sites.append(s['id'])

		print("setting lyrics sites: " + str(sites))
		self.settings['sites'] = sites


	def choose_callback(self, widget):
		def response_handler(widget, response):
			if response == Gtk.ResponseType.OK:
				path = self.chooser.get_filename()
				self.chooser.destroy()
				self.path_display.set_text(path)
				self.settings['folder'] = path
			else:
				self.chooser.destroy()

		buttons = (Gtk.STOCK_CLOSE, Gtk.ResponseType.CLOSE,
				Gtk.STOCK_OK, Gtk.ResponseType.OK)
		self.chooser = Gtk.FileChooserDialog(title=_("Choose lyrics folder..."),
					parent=None,
					action=Gtk.FileChooserAction.SELECT_FOLDER,
					buttons=buttons)
		self.chooser.connect("response", response_handler)
		self.chooser.set_modal(True)
		self.chooser.set_transient_for(self.config.get_toplevel())
		self.chooser.present()

	def get_prefs (self):
		try:
			sites = self.settings['sites']
		except GLib.GError as e:
			print(e)
			engines = []
		folder = self.settings['folder']

		print("lyric sites: " + str (sites))
		print("lyric folder: " + folder)
		return (sites, folder)
