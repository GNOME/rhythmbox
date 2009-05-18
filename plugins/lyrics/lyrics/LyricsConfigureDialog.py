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

		self.toggle1 = builder.get_object("engine1")
		self.toggle2 = builder.get_object("engine2")
		self.toggle3 = builder.get_object("engine3")
		self.toggle4 = builder.get_object("engine4")
		self.toggle5 = builder.get_object("engine5")
		self.choose_button = builder.get_object("choose_button")
		self.path_display = builder.get_object("path_display")

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
		self.toggle4.set_active('lyricwiki.org' in engines)
		self.toggle5.set_active('winampcn.com' in engines)

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
		if self.toggle4.get_active():
			engines.append('lyricwiki.org')
		if self.toggle5.get_active():
			engines.append('winampcn.com')

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

