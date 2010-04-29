# -*- coding: utf-8 -*-

# __init__.py
#
# Copyright (C) 2007 - Guillaume Desmottes
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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Parts from "Magnatune Rhythmbox plugin" (stolen from rhythmbox's __init__.py)
#     Copyright (C), 2006 Adam Zimmerman <adam_zimmerman@sfu.ca>

import rhythmdb, rb
import gobject
import gtk

from JamendoSource import JamendoSource
from JamendoConfigureDialog import JamendoConfigureDialog

popup_ui = """
<ui>
  <popup name="JamendoSourceViewPopup">
    <menuitem name="AddToQueueLibraryPopup" action="AddToQueue"/>
    <menuitem name="JamendoDownloadAlbum" action="JamendoDownloadAlbum"/>
    <menuitem name="JamendoDonateArtist" action="JamendoDonateArtist"/>
    <separator/>
    <menuitem name="BrowseGenreLibraryPopup" action="BrowserSrcChooseGenre"/>
    <menuitem name="BrowseArtistLibraryPopup" action="BrowserSrcChooseArtist"/>
    <menuitem name="BrowseAlbumLibraryPopup" action="BrowserSrcChooseAlbum"/>
    <separator/>
    <menuitem name="PropertiesLibraryPopup" action="MusicProperties"/>
  </popup>
</ui>
"""

class JamendoEntryType(rhythmdb.EntryType):
	def __init__(self):
		rhythmdb.EntryType.__init__(self, name='jamendo')

	def can_sync_metadata(self, entry):
		return True

class Jamendo(rb.Plugin):
	#
	# Core methods
	#

	def __init__(self):
		rb.Plugin.__init__(self)

	def activate(self, shell):
		self.db = shell.get_property("db")

		self.entry_type = JamendoEntryType()
		self.db.register_entry_type(self.entry_type)

		theme = gtk.icon_theme_get_default()
		rb.append_plugin_source_path(theme, "/icons/")

		width, height = gtk.icon_size_lookup(gtk.ICON_SIZE_LARGE_TOOLBAR)
		icon = rb.try_load_icon(theme, "jamendo", width, 0)

		group = rb.rb_source_group_get_by_name ("stores")
		self.source = gobject.new (JamendoSource,
					   shell=shell,
					   entry_type=self.entry_type,
					   plugin=self,
					   icon=icon,
					   source_group=group)
		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_source(self.source, None) # Add the source to the list

		# Add button
		manager = shell.get_player().get_property('ui-manager')
		action = gtk.Action('JamendoDownloadAlbum', _('_Download Album'),
				_("Download this album using BitTorrent"),
				'gtk-save')
		action.connect('activate', lambda a: shell.get_property("selected-source").download_album())
		self.action_group = gtk.ActionGroup('JamendoPluginActions')
		self.action_group.add_action(action)
		
		# Add Button for Donate
		action = gtk.Action('JamendoDonateArtist', _('_Donate to Artist'),
				_("Donate Money to this Artist"),
				'gtk-jump-to')
		action.connect('activate', lambda a: shell.get_property("selected-source").launch_donate())
		self.action_group.add_action(action)

		manager.insert_action_group(self.action_group, 0)
		self.ui_id = manager.add_ui_from_string(popup_ui)
		manager.ensure_update()

		self.pec_id = shell.get_player().connect('playing-song-changed', self.playing_entry_changed)

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

	def create_configure_dialog(self, dialog=None):
		if not dialog:
			builder_file = self.find_file("jamendo-prefs.ui")
			dialog = JamendoConfigureDialog (builder_file).get_dialog()
		dialog.present()
		return dialog

	def playing_entry_changed (self, sp, entry):
		self.source.playing_entry_changed (entry)

