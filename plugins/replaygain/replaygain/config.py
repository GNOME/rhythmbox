# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2010 Jonathan Matthew
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
#

import gobject
import rb
from gi.repository import Gtk
from gi.repository import Gio
from gi.repository import RB

# modes
REPLAYGAIN_MODE_RADIO = 0
REPLAYGAIN_MODE_ALBUM = 1

# number of samples to keep for calculating the average gain
# to apply for tracks that aren't tagged
AVERAGE_GAIN_SAMPLES = 10

class ReplayGainConfigDialog(Gtk.Dialog):
	def __init__(self, plugin):
		Gtk.Dialog.__init__(self)
		self.set_border_width(12)

		self.settings = Gio.Settings("org.gnome.rhythmbox.plugins.replaygain")

		ui_file = plugin.find_file("replaygain-prefs.ui")
		self.builder = Gtk.Builder()
		self.builder.add_from_file(ui_file)

		content = self.builder.get_object("replaygain-prefs")
		self.get_content_area().add(content)

		self.add_action_widget(Gtk.Button(stock=Gtk.STOCK_CLOSE), 0)
		self.show_all()

		label = self.builder.get_object("headerlabel")
		label.set_markup("<b>%s</b>" % label.get_text())
		label.set_use_markup(True)

		combo = self.builder.get_object("replaygainmode")
		combo.props.id_column = 1
		self.settings.bind("mode", combo, "active-id", Gio.SettingsBindFlags.DEFAULT)

		preamp = self.builder.get_object("preamp")
		self.settings.bind("preamp", preamp.props.adjustment, "value", Gio.SettingsBindFlags.GET)
		preamp.connect("value-changed", self.preamp_changed_cb)

		preamp.add_mark(-15.0, Gtk.PositionType.BOTTOM, _("-15.0 dB"))
		preamp.add_mark(0.0, Gtk.PositionType.BOTTOM, _("0.0 dB"))
		preamp.add_mark(15.0, Gtk.PositionType.BOTTOM, _("15.0 dB"))

		limiter = self.builder.get_object("limiter")
		self.settings.bind("limiter", limiter, "active", Gio.SettingsBindFlags.DEFAULT)

	def preamp_changed_cb(self, preamp):
		RB.settings_delayed_sync(self.settings, self.sync_preamp, preamp)

	def sync_preamp(self, settings, preamp):
		v = preamp.get_value()
		print "preamp gain changed to %f" % v
		settings['preamp'] = v

gobject.type_register(ReplayGainConfigDialog)
