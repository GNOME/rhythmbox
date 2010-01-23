# __init__.py
#
# Copyright (C) 2010 - Filipp Ivanov
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

import rb
import gtk
import glib

ui_definition = """
<ui>
    <popup name="BrowserSourceViewPopup">
        <menuitem name="SendToLibraryPopup" action="SendTo" />
    </popup>

    <popup name="PlaylistViewPopup">
        <menuitem name="SendToPlaylistPopup" action="SendTo" />
    </popup>

    <popup name="QueuePlaylistViewPopup">
        <menuitem name="SendToQueuePlaylistPopup" action="SendTo" />
    </popup>
</ui>"""

class SendToPlugin (rb.Plugin):
    def __init__(self):
        rb.Plugin.__init__(self)

    def activate(self, shell):
        self.__action = gtk.Action('SendTo', _('Send to...'),
                                _('Send files by mail, instant message...'), '')
        self.__action.connect('activate', self.send_to, shell)

        self.__action_group = gtk.ActionGroup('SendToActionGroup')
        self.__action_group.add_action(self.__action)
        shell.get_ui_manager().insert_action_group(self.__action_group)

        self.__ui_id = shell.get_ui_manager().add_ui_from_string(ui_definition)

    def deactivate(self, shell):
        shell.get_ui_manager().remove_action_group(self.__action_group)
        shell.get_ui_manager().remove_ui(self.__ui_id)
        shell.get_ui_manager().ensure_update()

        del self.__action_group
        del self.__action

    def send_to(self, action, shell):
        entries = shell.props.selected_source.get_entry_view().get_selected_entries()

        cmdline = ['nautilus-sendto'] + [entry.get_playback_uri() for entry in entries]
	glib.spawn_async(argv=cmdline, flags=glib.SPAWN_SEARCH_PATH)
