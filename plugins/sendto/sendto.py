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
from gi.repository import Gio, GObject, GLib, Peas
from gi.repository import RB

import shlex
import gettext
gettext.install('rhythmbox', RB.locale_dir())

class SendToPlugin (GObject.Object, Peas.Activatable):
	__gtype_name__ = 'SendToPlugin'

	object = GObject.property (type = GObject.Object)

	def __init__(self):
		GObject.Object.__init__(self)

	def do_activate(self):
		self.__action = Gio.SimpleAction(name='sendto')
		self.__action.connect('activate', self.send_to)

		app = Gio.Application.get_default()
		app.add_action(self.__action)

		item = Gio.MenuItem()
		item.set_label(_("Send to…"))
		item.set_detailed_action('app.sendto')
		app.add_plugin_menu_item('edit', 'sendto', item)
		app.add_plugin_menu_item('browser-popup', 'sendto', item)
		app.add_plugin_menu_item('playlist-popup', 'sendto', item)
		app.add_plugin_menu_item('queue-popup', 'sendto', item)

	def do_deactivate(self):
		shell = self.object
		app = Gio.Application.get_default()
		app.remove_action('sendto')
		app.remove_plugin_menu_item('edit', 'sendto')
		app.remove_plugin_menu_item('browser-popup', 'sendto')
		app.remove_plugin_menu_item('playlist-popup', 'sendto')
		app.remove_plugin_menu_item('queue-popup', 'sendto')
		del self.__action

	def send_to(self, action, data):
		shell = self.object
		page = shell.props.selected_page
		if not hasattr(page, "get_entry_view"):
			return

		entries = page.get_entry_view().get_selected_entries()
		cmdline = 'nautilus-sendto ' + " ".join(shlex.quote(entry.get_playback_uri()) for entry in entries)
		GLib.spawn_command_line_async(cmdline)
