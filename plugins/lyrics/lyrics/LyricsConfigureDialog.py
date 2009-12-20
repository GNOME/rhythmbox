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

import gobject, gtk
import gconf
from os import system, path

class LyricsConfigureDialog (object):
	def __init__(self, builder_file, gconf_keys):
		self.gconf = gconf.client_get_default()
		self.gconf_keys = gconf_keys

		builder = gtk.Builder()
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
			checkbutton = gtk.CheckButton(label = s['name'])
			checkbutton.set_active(s['id'] in engines)
			self.site_checks[site_id] = checkbutton
			site_box.pack_start(checkbutton)

		site_box.show_all()

	def dialog_response(self, dialog, response):
		if response == gtk.RESPONSE_OK:
			self.set_values()
			self.dialog.hide()
		elif response == gtk.RESPONSE_CANCEL or response == gtk.RESPONSE_DELETE_EVENT:
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

		self.gconf.set_list(self.gconf_keys['engines'], gconf.VALUE_STRING, engines)
		self.gconf.set_string(self.gconf_keys['folder'], self.folder)

	def choose_callback(self, widget):
		def response_handler(widget, response):
			if response == gtk.RESPONSE_OK:
				path = self.chooser.get_filename()
				self.chooser.destroy()
				self.path_display.set_text(path)
			else:
				self.chooser.destroy()

		buttons = (gtk.STOCK_CLOSE, gtk.RESPONSE_CLOSE,
				gtk.STOCK_OK, gtk.RESPONSE_OK)
		self.chooser = gtk.FileChooserDialog(title=_("Choose lyrics folder..."),
					parent=None,
					action=gtk.FILE_CHOOSER_ACTION_SELECT_FOLDER,
					buttons=buttons)
		self.chooser.connect("response", response_handler)
		self.chooser.set_modal(True)
		self.chooser.set_transient_for(self.dialog)
		self.chooser.present()

	def get_dialog (self):
		return self.dialog
	
	def get_prefs (self):
		try:
			engines = gconf.client_get_default().get_list(self.gconf_keys['engines'], gconf.VALUE_STRING)
			if engines is None:
				engines = []
		except gobject.GError, e:
			print e
			engines = []
		folder = gconf.client_get_default().get_string(self.gconf_keys['folder'])

		print "lyric engines: " + str (engines)
		print "lyric folder: " + folder
		return (engines, folder)

