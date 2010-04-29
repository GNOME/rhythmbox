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

import rhythmdb, rb
import gobject
import gtk
import gconf, gnome
import gnomekeyring as keyring

import urllib
import zipfile
import sys, os.path
import xml
import datetime
import string

from MagnatuneSource import MagnatuneSource

popup_ui = """
<ui>
  <popup name="MagnatuneSourceViewPopup">
    <menuitem name="AddToQueueLibraryPopup" action="AddToQueue"/>
    <menuitem name="MagnatuneDownloadAlbum" action="MagnatuneDownloadAlbum"/>
    <menuitem name="MagnatuneArtistInfo" action="MagnatuneArtistInfo"/>
    <menuitem name="MagnatuneCancelDownload" action="MagnatuneCancelDownload"/>
    <separator/>
    <menuitem name="BrowseGenreLibraryPopup" action="BrowserSrcChooseGenre"/>
    <menuitem name="BrowseArtistLibraryPopup" action="BrowserSrcChooseArtist"/>
    <menuitem name="BrowseAlbumLibraryPopup" action="BrowserSrcChooseAlbum"/>
    <separator/>
    <menuitem name="PropertiesLibraryPopup" action="MusicProperties"/>
  </popup>
</ui>
"""

class MagnatuneEntryType(rhythmdb.EntryType):
	def __init__(self):
		rhythmdb.EntryType.__init__(self, name='magnatune')

	def can_sync_metadata(self, entry):
		return True

class Magnatune(rb.Plugin):
	client = gconf.client_get_default()

	format_list = ['ogg', 'flac', 'wav', 'mp3-vbr', 'mp3-cbr']

	gconf_keys = {
		'format': "/apps/rhythmbox/plugins/magnatune/format",
		'pay': "/apps/rhythmbox/plugins/magnatune/pay",
		'ccauthtoken': "/apps/rhythmbox/plugins/magnatune/ccauthtoken",
		'continue': "/apps/rhythmbox/plugins/magnatune/continue",
		'account_type': "/apps/rhythmbox/plugins/magnatune/account_type"
	}


	#
	# Core methods
	#

	def __init__(self):
		rb.Plugin.__init__(self)

	def activate(self, shell):
		self.shell = shell # so the source can update the progress bar
		self.db = shell.get_property("db")

		self.entry_type = MagnatuneEntryType()
		self.db.register_entry_type(self.entry_type)

		theme = gtk.icon_theme_get_default()
		rb.append_plugin_source_path(theme, "/icons")

		width, height = gtk.icon_size_lookup(gtk.ICON_SIZE_LARGE_TOOLBAR)
		icon = rb.try_load_icon(theme, "magnatune", width, 0)

		group = rb.rb_source_group_get_by_name("stores")
		self.source = gobject.new(MagnatuneSource,
					  shell=shell,
					  entry_type=self.entry_type,
					  source_group=group,
					  icon=icon,
					  plugin=self)

		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_source(self.source, None) # Add the source to the list

		manager = shell.get_player().get_property('ui-manager')
		# Add the popup menu actions
		self.action_group = gtk.ActionGroup('MagnatunePluginActions')

		action = gtk.Action('MagnatuneDownloadAlbum', _('Download Album'),
				_("Download this album from Magnatune"),
				'gtk-save')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").download_album())
		self.action_group.add_action(action)
		action = gtk.Action('MagnatuneArtistInfo', _('Artist Information'),
				_("Get information about this artist"),
				'gtk-info')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").display_artist_info())
		self.action_group.add_action(action)
		action = gtk.Action('MagnatuneCancelDownload', _('Cancel Downloads'),
				_("Stop downloading purchased albums"),
				'gtk-stop')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").cancel_downloads())
		action.set_sensitive(False)
		self.action_group.add_action(action)

		manager.insert_action_group(self.action_group, 0)
		self.ui_id = manager.add_ui_from_string(popup_ui)

		self.pec_id = shell.get_player().connect('playing-song-changed', self.playing_entry_changed)
		manager.ensure_update()

	def deactivate(self, shell):
		manager = shell.get_player().get_property('ui-manager')
		manager.remove_ui(self.ui_id)
		manager.remove_action_group(self.action_group)
		self.action_group = None

		shell.get_player().disconnect(self.pec_id)

		self.db.entry_delete_by_type(self.entry_type)
		self.db.commit()
		self.db = None
		self.entry_type = None
		self.source.delete_thyself()
		self.source = None
		self.shell = None

	def playing_entry_changed (self, sp, entry):
		self.source.playing_entry_changed(entry)

	def create_configure_dialog(self, dialog=None):
		# We use a dictionary so we can modify these values from within inner functions
		keyring_data = {
			'id': 0,
			'item': None
		}

		def got_items(result, items):
			def created_item(result, id):
				if result is None: # Item successfully created
					keyring_data['id'] = id
					keyring.item_get_info(None, id, got_item)
				else:
					print "Couldn't create keyring item: " + str(result)
					fill_account_details()
					dialog.present()
			def got_item(result, item):
				if result is None: # Item retrieved successfully
					keyring_data['item'] = item
				else:
					print "Couldn't retrieve keyring item: " + str(result)
				fill_account_details()
				dialog.present()


			if result is None and len(items) != 0: # Got list of search results
				keyring_data['id'] = items[0].item_id
				keyring.item_get_info(None, keyring_data['id'], got_item)
			elif result == keyring.NoMatchError or len(items) == 0: # No items were found, so we'll create one
				keyring.item_create(None,
				                    keyring.ITEM_GENERIC_SECRET,
				                    "Rhythmbox: Magnatune account information",
				                    {'rhythmbox-plugin': 'magnatune'},
				                    "", # Empty secret for now
				                    True,
				                    created_item)
			else: # Some other error occurred
				print "Couldn't access keyring: " + str(result)
				fill_account_details()
				dialog.present()


		if dialog == None:
			def fill_account_details():
				account_type = self.client.get_string(self.gconf_keys['account_type'])
				builder.get_object("no_account_radio").set_active(account_type == "none")
				builder.get_object("stream_account_radio").set_active(account_type == "stream")
				builder.get_object("download_account_radio").set_active(account_type == "download")

				username = ""
				password = ""
				try:
					if keyring_data['item']:
						username, password = keyring_data['item'].get_secret().split('\n')
				except ValueError: # Couldn't parse the secret, probably because it's empty
					pass
				builder.get_object("username_entry").set_text(username)
				builder.get_object("password_entry").set_text(password)

				has_account = account_type != "none"
				builder.get_object("username_entry").set_sensitive(has_account)
				builder.get_object("password_entry").set_sensitive(has_account)
				builder.get_object("username_label").set_sensitive(has_account)
				builder.get_object("password_label").set_sensitive(has_account)

				builder.get_object("account_changed_label").hide()

			def account_type_toggled (button):
				print "account type radiobutton toggled: " + button.get_name()
				account_type = {"no_account_radio": "none", "stream_account_radio": "stream", "download_account_radio": "download"}
				if button.get_active():
					self.client.set_string(self.gconf_keys['account_type'], account_type[button.get_name()])
					if account_type[button.get_name()] == 'none':
						builder.get_object("username_label").set_sensitive(False)
						builder.get_object("username_entry").set_sensitive(False)
						builder.get_object("password_label").set_sensitive(False)
						builder.get_object("password_entry").set_sensitive(False)
					else:
						builder.get_object("username_label").set_sensitive(True)
						builder.get_object("username_entry").set_sensitive(True)
						builder.get_object("password_label").set_sensitive(True)
						builder.get_object("password_entry").set_sensitive(True)
					builder.get_object("account_changed_label").show()

			def account_details_changed(entry):
				username = builder.get_object("username_entry").get_text()
				password = builder.get_object("password_entry").get_text()
				if keyring_data['item']:
					keyring_data['item'].set_secret('\n'.join((username, password)))

				builder.get_object("account_changed_label").show()

			def close_button_pressed(x, y):
				try:
					if keyring_data['id'] and keyring_data['item']:
						# The async version is not in the python bindings, grr...
						keyring.item_set_info_sync(None, keyring_data['id'], keyring_data['item'])
					else:
						rb.error_dialog(title = _("Couldn't store account information"),
						                message = _("There was a problem accessing the keyring. Check the debug output for more information."))
				except Exception, e:
					rb.error_dialog(title = _("Couldn't store account information"),
					                message = str(e))
				dialog.hide()


			self.configure_callback_dic = {
				"rb_magnatune_audio_combobox_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['format'], self.format_list[w.get_active()]),

				"rb_magnatune_radio_account_toggled_cb" : account_type_toggled,
				"rb_magnatune_username_changed_cb" : account_details_changed,
				"rb_magnatune_password_changed_cb" : account_details_changed
			}

			builder = gtk.Builder()
			builder.add_from_file(self.find_file("magnatune-prefs.ui"))

			# FIXME this bit should be in builder too  (what?)
			dialog = builder.get_object('preferences_dialog')

			# Set the names of the radio buttons so we can tell which one has been clicked
			for name in ("no_account_radio", "stream_account_radio", "download_account_radio"):
				builder.get_object(name).set_name(name)

			builder.get_object("audio_combobox").set_active(self.format_list.index(self.client.get_string(self.gconf_keys['format'])))

			builder.connect_signals(self.configure_callback_dic)
			dialog.connect("response", close_button_pressed)

		keyring.find_items(keyring.ITEM_GENERIC_SECRET, {'rhythmbox-plugin': 'magnatune'}, got_items)
		return dialog
