# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Adam Zimmerman  <adam_zimmerman@sfu.ca>
# Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
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

import zipfile
import sys, os.path
import xml
import datetime
import string
import re

import rb
from gi.repository import RB
from gi.repository import GObject, Gtk, Gio, Peas, PeasGtk

from MagnatuneSource import MagnatuneSource
import MagnatuneAccount

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class MagnatuneEntryType(RB.RhythmDBEntryType):
	def __init__(self):
		RB.RhythmDBEntryType.__init__(self, name='magnatune')
		self.URIre = re.compile(r'^http://[^.]+\.magnatune\.com/')
		self.nsre = re.compile(r'\.(mp3|ogg)$')
		self.account = MagnatuneAccount.instance()

	def fix_trackurl(self, trackurl, account_type, username, password):
		return trackurl

	def do_get_playback_uri(self, entry):
		(account_type, username, password) = self.account.get()
		uri = entry.get_string(RB.RhythmDBPropType.LOCATION)
		if account_type != "none":
			uri = self.URIre.sub("http://%s:%s@%s.magnatune.com/" % (username, password, account_type), uri)
			uri = self.nsre.sub(r"_nospeech.\1", uri)
			print("converted track uri: %s" % uri)

		return uri

	def do_can_sync_metadata(self, entry):
		return True

	def do_sync_metadata(self, entry, changes):
		return



class Magnatune(GObject.GObject, Peas.Activatable):
	__gtype_name__ = 'Magnatune'
	object = GObject.property(type=GObject.GObject)

	def __init__(self):
		GObject.GObject.__init__(self)

	def download_album_action_cb(self, action, parameter):
		shell = self.object
		shell.props.selected_page.download_album()

	def artist_info_action_cb(self, action, parameter):
		shell = self.object
		shell.props.selected_page.display_artist_info()
	
	def do_activate(self):
		shell = self.object
		self.db = shell.props.db

		rb.append_plugin_source_path(self, "icons")

		self.entry_type = MagnatuneEntryType()
		self.db.register_entry_type(self.entry_type)

		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.magnatune")

		app = Gio.Application.get_default()
		action = Gio.SimpleAction(name="magnatune-album-download")
		action.connect("activate", self.download_album_action_cb)
		app.add_action(action)

		action = Gio.SimpleAction(name="magnatune-artist-info")
		action.connect("activate", self.artist_info_action_cb)
		app.add_action(action)

		builder = Gtk.Builder()
		builder.add_from_file(rb.find_plugin_file(self, "magnatune-toolbar.ui"))
		toolbar = builder.get_object("magnatune-toolbar")
		app.link_shared_menus(toolbar)

		group = RB.DisplayPageGroup.get_by_id ("stores")
		settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.magnatune")
		self.source = GObject.new(MagnatuneSource,
					  shell=shell,
					  entry_type=self.entry_type,
					  icon=Gio.ThemedIcon.new("magnatune-symbolic"),
					  plugin=self,
					  settings=settings.get_child("source"),
					  name=_("Magnatune"),
					  toolbar_menu=toolbar)

		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_display_page(self.source, group)

		self.pec_id = shell.props.shell_player.connect('playing-song-changed', self.playing_entry_changed)

	def do_deactivate(self):
		shell = self.object

		shell.props.shell_player.disconnect(self.pec_id)

		self.db.entry_delete_by_type(self.entry_type)
		self.db.commit()
		self.db = None
		self.entry_type = None
		self.source.delete_thyself()
		self.source = None

	def playing_entry_changed (self, sp, entry):
		self.source.playing_entry_changed(entry)




class MagnatuneConfig(GObject.GObject, PeasGtk.Configurable):
	__gtype_name__ = 'MagnatuneConfig'
	object = GObject.property(type=GObject.GObject)

	format_list = ['ogg', 'flac', 'wav', 'mp3-vbr', 'mp3-cbr']

	def __init__(self):
		GObject.GObject.__init__(self)
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.magnatune")
		self.account = MagnatuneAccount.instance()

	def do_create_configure_widget(self):
		# We use a dictionary so we can modify these values from within inner functions
		keyring_data = {
			'id': 0,
			'item': None
		}

		def update_sensitivity(account_type):
			has_account = account_type != "none"
			builder.get_object("username_entry").set_sensitive(has_account)
			builder.get_object("password_entry").set_sensitive(has_account)
			builder.get_object("username_label").set_sensitive(has_account)
			builder.get_object("password_label").set_sensitive(has_account)


		def fill_account_details():
			(account_type, username, password) = self.account.get()
			builder.get_object("no_account_radio").set_active(account_type == "none")
			builder.get_object("stream_account_radio").set_active(account_type == "stream")
			builder.get_object("download_account_radio").set_active(account_type == "download")

			builder.get_object("username_entry").set_text(username or "")
			builder.get_object("password_entry").set_text(password or "")

			update_sensitivity(account_type)


		def account_type_toggled(button):
			print("account type radiobutton toggled: " + button.get_name())
			account_type = {"no_account_radio": 'none', "stream_account_radio": 'stream', "download_account_radio": 'download'} 
			if button.get_active():
				self.settings['account-type'] = account_type[button.get_name()]
				update_sensitivity(account_type[button.get_name()])

		def account_details_changed(entry, event):
			username = builder.get_object("username_entry").get_text()
			password = builder.get_object("password_entry").get_text()

			if username == "" or password == "":
				print("missing something")
				return

			# should actually try a request to http://username:password@account-type.magnatune.com/
			# to check the password is correct..

			MagnatuneAccount.instance().update(username, password)

		def format_selection_changed(button):
			self.settings['format'] = self.format_list[button.get_active()]

		self.configure_callback_dic = {
			"rb_magnatune_audio_combobox_changed_cb" : format_selection_changed,
			"rb_magnatune_radio_account_toggled_cb" : account_type_toggled
		}

		builder = Gtk.Builder()
		builder.add_from_file(rb.find_plugin_file(self, "magnatune-prefs.ui"))

		dialog = builder.get_object('magnatune_vbox')

		# Set the names of the radio buttons so we can tell which one has been clicked
		for name in ("no_account_radio", "stream_account_radio", "download_account_radio"):
			builder.get_object(name).set_name(name)

		builder.get_object("audio_combobox").set_active(self.format_list.index(self.settings['format']))

		builder.connect_signals(self.configure_callback_dic)
		builder.get_object("username_entry").connect("focus-out-event", account_details_changed)
		builder.get_object("password_entry").connect("focus-out-event", account_details_changed)

		fill_account_details()
		return dialog
