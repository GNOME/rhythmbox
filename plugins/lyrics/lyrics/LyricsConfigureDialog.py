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

import gobject
from os import system, path

import rb
from gi.repository import Gtk, GConf

class LyricsConfigureDialog (object):
	def __init__(self, builder_file, gconf_keys):
		self.gconf = GConf.Client.get_default()
		self.gconf_keys = gconf_keys

		builder = Gtk.Builder()
		builder.add_from_file(builder_file)

		self.dialog = builder.get_object("preferences_dialog")

		self.choose_button = builder.get_object("choose_button")
		self.path_display = builder.get_object("path_display")

		self.choose_button.connect("clicked", self.choose_callback)
		self.dialog.connect("response", self.dialog_response)

		# set fields from gconf
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
			self.site_checks[site_id] = checkbutton
			site_box.pack_start(checkbutton, True, True, 0)

		site_box.show_all()

	def dialog_response(self, dialog, response):
		if response == Gtk.ResponseType.OK:
			self.set_values()
			self.dialog.hide()
		elif response == Gtk.ResponseType.CANCEL or response == Gtk.ResponseType.DELETE_EVENT:
			self.dialog.hide()
		else:
			print "unexpected response type"


	def set_values(self):
		engines = []
		for s in lyrics_sites:
			check = self.site_checks[s['id']]
			if check is None:
				continue

			if check.get_active():
				engines.append(s['id'])

		if len(self.path_display.get_text()) is not 0:
			self.folder = self.path_display.get_text()

		# XXX can't set gconf string lists; doesn't matter, none of the sites work anyway
		# self.gconf.set_list(self.gconf_keys['engines'], GConf.ValueType.STRING, engines)
		self.gconf.set_string(self.gconf_keys['folder'], self.folder)

	def choose_callback(self, widget):
		def response_handler(widget, response):
			if response == Gtk.ResponseType.OK:
				path = self.chooser.get_filename()
				self.chooser.destroy()
				self.path_display.set_text(path)
			else:
				self.chooser.destroy()

		buttons = (Gtk.STOCK_CLOSE, Gtk.ResponseTypeCLOSE,
				Gtk.STOCK_OK, Gtk.ResponseType.OK)
		self.chooser = Gtk.FileChooserDialog(title=_("Choose lyrics folder..."),
					parent=None,
					action=Gtk.FileChooserAction.SELECT_FOLDER,
					buttons=buttons)
		self.chooser.connect("response", response_handler)
		self.chooser.set_modal(True)
		self.chooser.set_transient_for(self.dialog)
		self.chooser.present()

	def get_dialog (self):
		return self.dialog
	
	def get_prefs (self):
		try:
			engines = rb.get_gconf_string_list(self.gconf_keys['engines'])
		except gobject.GError, e:
			print e
			engines = []
		folder = GConf.Client.get_default().get_string(self.gconf_keys['folder'])

		print "lyric engines: " + str (engines)
		print "lyric folder: " + folder
		return (engines, folder)
