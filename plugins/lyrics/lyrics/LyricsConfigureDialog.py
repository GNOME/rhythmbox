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
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.



import gtk, gtk.glade
import gconf
from os import system, path

class LyricsConfigureDialog (object):
	def __init__(self, glade_file, gconf_keys):
		self.gconf = gconf.client_get_default()
		self.gconf_keys = gconf_keys
		self.gladexml = gtk.glade.XML(glade_file)
			
		self.dialog = self.gladexml.get_widget("preferences_dialog")

		self.toggle1 = self.gladexml.get_widget("engine1")
		self.toggle2 = self.gladexml.get_widget("engine2")
		self.toggle3 = self.gladexml.get_widget("engine3")
		self.choose_button = self.gladexml.get_widget("choose_button")
		self.path_display = self.gladexml.get_widget("path_display")

		self.choose_button.connect("clicked", self.choose_callback)
		self.dialog.connect("response", self.dialog_response)

		# set fields from gconf
		engines, self.folder = self.get_prefs()
		if self.folder is None:
			self.folder = '~/.lyrics'
		self.path_display.set_text(self.folder)
		self.toggle1.set_active('astraweb.com' in engines)
		self.toggle2.set_active('lyrc.com.ar' in engines)
		self.toggle3.set_active('leoslyrics.com' in engines)

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
		if self.toggle1.get_active():
			engines.append('astraweb.com')
		if self.toggle2.get_active():
			engines.append('lyrc.com.ar')
		if self.toggle3.get_active():
			engines.append('leoslyrics.com')

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
		engines = gconf.client_get_default().get_list(self.gconf_keys['engines'], gconf.VALUE_STRING)
		folder = gconf.client_get_default().get_string(self.gconf_keys['folder'])

		print "lyric engines: " + str (engines)
		print "lyric folder: " + folder
		return (engines, folder)
