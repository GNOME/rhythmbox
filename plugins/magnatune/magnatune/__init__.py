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

import urllib
import zipfile
import sys, os.path
import xml
import datetime
import string

from MagnatuneSource import MagnatuneSource

has_gnome_keyring = False
#try:
#	import gnomekeyring
#	has_gnome_keyring = True
#except:
#	pass	

popup_ui = """
<ui>
  <popup name="MagnatuneSourceViewPopup">
    <menuitem name="AddToQueueLibraryPopup" action="AddToQueue"/>
    <menuitem name="MagnatunePurchaseAlbum" action="MagnatunePurchaseAlbum"/>
    <menuitem name="MagnatunePurchaseCD" action="MagnatunePurchaseCD"/>
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

keyring_attributes = {"name": "rb-magnatune-cc-data"}

class Magnatune(rb.Plugin):
	client = gconf.client_get_default()

	format_list = ['ogg', 'flac', 'wav', 'mp3-vbr', 'mp3-cbr']

	gconf_keys = {
		'format': "/apps/rhythmbox/plugins/magnatune/format",
		'pay': "/apps/rhythmbox/plugins/magnatune/pay",
		'ccauthtoken': "/apps/rhythmbox/plugins/magnatune/ccauthtoken"
	}


	#
	# Core methods
	#
	
	def __init__(self):
		rb.Plugin.__init__(self)

	def activate(self, shell):
		self.shell = shell # so the source can update the progress bar
		self.db = shell.get_property("db")
		self.keyring = None

		self.entry_type = self.db.entry_register_type("MagnatuneEntryType")
		# allow changes which don't do anything
		self.entry_type.can_sync_metadata = True
		self.entry_type.sync_metadata = None

		theme = gtk.icon_theme_get_default()
		rb.append_plugin_source_path(theme, "/icons")

		width, height = gtk.icon_size_lookup(gtk.ICON_SIZE_LARGE_TOOLBAR)
		icon = rb.try_load_icon(theme, "magnatune", width, 0)

		group = rb.rb_source_group_get_by_name ("stores")
		self.source = gobject.new (MagnatuneSource,
 					   shell=shell,
 					   entry_type=self.entry_type,
 					   source_group=group,
					   icon=icon,
 					   plugin=self)

		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_source(self.source, None) # Add the source to the list
		
		manager = shell.get_player().get_property('ui-manager')
		# Add the popup menu actions
		action = gtk.Action('MagnatunePurchaseAlbum', _('Purchase Album'),
				_("Purchase this album from Magnatune"),
				'gtk-save')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").purchase_album())
		self.action_group = gtk.ActionGroup('MagnatunePluginActions')
		self.action_group.add_action(action)
		action = gtk.Action('MagnatunePurchaseCD', _('Purchase Physical CD'),
				_("Purchase a physical CD from Magnatune"),
				'gtk-cdrom')
		action.connect('activate', lambda a: self.shell.get_property("selected-source").buy_cd())
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
		manager.remove_ui (self.ui_id)
		manager.remove_action_group(self.action_group)
		self.action_group = None

		shell.get_player().disconnect (self.pec_id)

		self.db.entry_delete_by_type(self.entry_type)
		self.db.commit()
		self.db = None
		self.entry_type = None
		self.source.delete_thyself()
		self.source = None
		self.shell = None
		self.keyring = None

	def playing_entry_changed (self, sp, entry):
		self.source.playing_entry_changed (entry)

	def get_keyring(self):
		if self.keyring is None:
			self.keyring = gnomekeyring.get_default_keyring_sync()
		return self.keyring

	def store_cc_details(self, *details):
		if has_gnome_keyring:
			print "storing CC details"
			try:
				id = gnomekeyring.item_create_sync(self.get_keyring(),
						gnomekeyring.ITEM_GENERIC_SECRET,
						"Magnatune credit card info", keyring_attributes,
						string.join (details, '\n'), True)
			except Exception, e:
				print e

	def clear_cc_details(self):
		if has_gnome_keyring:
			print "clearing CC details"
			try:
				ids = gnomekeyring.find_items_sync (gnomekeyring.ITEM_GENERIC_SECRET, keyring_attributes)
				gnomekeyring.item_delete_sync (self.get_keyring(), id[0])
			except Exception, e:
				print e
	
	def get_cc_details(self):
		if has_gnome_keyring:
			print "getting CC details"
			try:
				ids = gnomekeyring.find_items_sync (gnomekeyring.ITEM_GENERIC_SECRET, keyring_attributes)
				data =  gnomekeyring.item_get_info_sync(self.get_keyring(), ids[0]).get_secret()
				return string.split(data, "\n")
			except Exception, e:
				print e
		return ("", "", 0, "", "")
	
	def create_configure_dialog(self, dialog=None):
		if dialog == None:
			def fill_cc_details():
				try:
					(ccnumber, ccyear, ccmonth, name, email) = self.get_cc_details()
					builder.get_object("cc_entry").set_text(ccnumber)
					builder.get_object("yy_entry").set_text(ccyear)
					builder.get_object("mm_entry").set_active(int(ccmonth)-1)
					builder.get_object("name_entry").set_text(name)
					builder.get_object("email_entry").set_text(email)
					builder.get_object("remember_cc_details").set_active(True)
				except Exception, e:
					print e

					builder.get_object("cc_entry").set_text("")
					builder.get_object("yy_entry").set_text("")
					builder.get_object("mm_entry").set_active(0)
					builder.get_object("name_entry").set_text("")
					builder.get_object("email_entry").set_text("")
					builder.get_object("remember_cc_details").set_active(False)

			def update_expired():
				mm = builder.get_object("mm_entry").get_active() + 1
				yy = 0
				try:
					yy = int(builder.get_object("yy_entry").get_text())
				except Exception, e:
					print e
					builder.get_object("cc_expired_label").hide()
					return

				if yy < (datetime.date.today().year % 100):
					builder.get_object("cc_expired_label").show()
				elif (yy == (datetime.date.today().year % 100) and mm < datetime.date.today().month):
					builder.get_object("cc_expired_label").show()
				else:
					builder.get_object("cc_expired_label").hide()
			
			def remember_checkbox_toggled (button):
				print "remember CC details toggled " + str(button.get_active())
				builder.get_object("cc_entry").set_sensitive(button.get_active())
				builder.get_object("mm_entry").set_sensitive(button.get_active())
				builder.get_object("yy_entry").set_sensitive(button.get_active())
				builder.get_object("name_entry").set_sensitive(button.get_active())
				builder.get_object("email_entry").set_sensitive(button.get_active())

				if not button.get_active():
					try:
						self.clear_cc_details ()
					except Exception, e:
						print e
#				fill_cc_details()


			# shit
			self.configure_callback_dic = {
#				"rb_magnatune_yy_entry_changed_cb" : lambda w: update_expired(),
#				"rb_magnatune_mm_entry_changed_cb" : lambda w: update_expired(),
#				"rb_magnatune_name_entry_changed_cb" : lambda w: None,
#				"rb_magnatune_cc_entry_changed_cb" : lambda w: None,
#				"rb_magnatune_email_entry_changed_cb" : lambda w: None,
				"rb_magnatune_pay_combobox_changed_cb" : lambda w: self.client.set_int(self.gconf_keys['pay'], w.get_active() + 5),
				"rb_magnatune_audio_combobox_changed_cb" : lambda w: self.client.set_string(self.gconf_keys['format'], self.format_list[w.get_active()]),
				"rb_magnatune_remember_cc_details_toggled_cb" : remember_checkbox_toggled
			}

			builder = gtk.Builder()
			builder.add_from_file(self.find_file("magnatune-prefs.ui"))
			# gladexml.signal_autoconnect(self.configure_callback_dic)

			# FIXME this bit should be in builder too  (what?)
			dialog = builder.get_object('preferences_dialog')
			def dialog_response (dialog, response):
				if builder.get_object("remember_cc_details").get_active():
					ccnumber = builder.get_object("cc_entry").get_text()
					ccyear = builder.get_object("yy_entry").get_text()
					ccmonth = str(builder.get_object("mm_entry").get_active() + 1)
					name = builder.get_object("name_entry").get_text()
					email = builder.get_object("email_entry").get_text()
					self.store_cc_details(ccnumber, ccyear, ccmonth, name, email)
				dialog.hide()
			dialog.connect("response", dialog_response)
			
			builder.get_object("cc_details_box").props.visible = has_gnome_keyring
			builder.get_object("pay_combobox").set_active(self.client.get_int(self.gconf_keys['pay']) - 5)
			builder.get_object("audio_combobox").set_active(self.format_list.index(self.client.get_string(self.gconf_keys['format'])))
			fill_cc_details()

		dialog.present()
		return dialog
