# Copyright (c) 2018 Philipp Wolfer <ph.wolfer@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import rb
from gi.repository import Gio
from gi.repository import GObject
from gi.repository import Gtk
from gi.repository import PeasGtk


def load_settings():
    return Gio.Settings.new("org.gnome.rhythmbox.plugins.listenbrainz")


class ListenBrainzSettings(GObject.Object, PeasGtk.Configurable):
    __gtype_name__ = 'ListenBrainzSettings'
    object = GObject.property(type=GObject.Object)

    user_token_entry = GObject.Property(type=Gtk.Entry, default=None)

    def do_create_configure_widget(self):
        self.settings = load_settings()

        ui_file = rb.find_plugin_file(self, "settings.ui")
        self.builder = Gtk.Builder()
        self.builder.add_from_file(ui_file)

        content = self.builder.get_object("listenbrainz-settings")

        self.user_token_entry = self.builder.get_object("user-token")
        self.settings.bind("user-token", self.user_token_entry, "text", 0)

        return content
