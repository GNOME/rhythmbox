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

from JamendoSource import JamendoSource
from JamendoConfigureDialog import JamendoConfigureDialog

import rb
from gi.repository import GObject, Gtk, Gio, Peas
from gi.repository import RB

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

class JamendoEntryType(RB.RhythmDBEntryType):
	def __init__(self):
		RB.RhythmDBEntryType.__init__(self, name="jamendo")

	def do_can_sync_metadata(self, entry):
		return True

	def do_sync_metadata(self, entry, changes):
		return

class Jamendo(GObject.GObject, Peas.Activatable):
	__gtype_name__ = 'Jamendo'
	object = GObject.property(type=GObject.GObject)

	#
	# Core methods
	#

	def do_activate(self):
		shell = self.object
		self.db = shell.props.db

		self.entry_type = JamendoEntryType()
		self.db.register_entry_type(self.entry_type)

		theme = Gtk.IconTheme.get_default()
		rb.append_plugin_source_path(theme, "/icons/")

		what, width, height = Gtk.icon_size_lookup(Gtk.IconSize.LARGE_TOOLBAR)
		icon = rb.try_load_icon(theme, "jamendo", width, 0)

		group = RB.DisplayPageGroup.get_by_id ("stores")
		settings = Gio.Settings("org.gnome.rhythmbox.plugins.jamendo")
		self.source = GObject.new (JamendoSource,
					   shell=shell,
					   entry_type=self.entry_type,
					   plugin=self,
					   pixbuf=icon,
					   settings=settings.get_child("source"))
		shell.register_entry_type_for_source(self.source, self.entry_type)
		shell.append_display_page(self.source, group)

		# Add button
		manager = shell.props.ui_manager
		action = Gtk.Action(name='JamendoDownloadAlbum', label=_('_Download Album'),
				tooltip=_("Download this album using BitTorrent"),
				stock_id='gtk-save')
		action.connect('activate', lambda a: shell.props.selected_page.download_album())
		self.action_group = Gtk.ActionGroup('JamendoPluginActions')
		self.action_group.add_action(action)
		
		# Add Button for Donate
		action = Gtk.Action(name='JamendoDonateArtist', label=_('_Donate to Artist'),
				tooltip=_("Donate Money to this Artist"),
				stock_id='gtk-jump-to')
		action.connect('activate', lambda a: shell.props.selected_page.launch_donate())
		self.action_group.add_action(action)

		manager.insert_action_group(self.action_group, 0)
		self.ui_id = manager.add_ui_from_string(popup_ui)
		manager.ensure_update()

		self.pec_id = shell.props.shell_player.connect('playing-song-changed', self.playing_entry_changed)

	def do_deactivate(self):
		shell = self.object
		manager = shell.props.ui_manager
		manager.remove_ui (self.ui_id)
		manager.remove_action_group(self.action_group)
		self.action_group = None

		shell.props.shell_player.disconnect (self.pec_id)

		self.db.entry_delete_by_type(self.entry_type)
		self.db.commit()
		self.db = None
		self.entry_type = None

		self.source.delete_thyself()
		self.source = None

	def playing_entry_changed (self, sp, entry):
		self.source.playing_entry_changed (entry)
