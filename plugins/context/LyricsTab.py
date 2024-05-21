# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 John Iacona
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

from html import escape
import urllib.request
import re, os

from mako.template import Template

import rb
from gi.repository import GObject, Gtk
from gi.repository import RB

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class LyricsTab (GObject.GObject):
    
    __gsignals__ = {
        'switch-tab' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE,
                                (GObject.TYPE_STRING,))
    }

    def __init__ (self, shell, toolbar, ds, view):
        GObject.GObject.__init__ (self)
        self.shell      = shell
        self.sp         = shell.props.shell_player
        self.db         = shell.props.db
        self.toolbar    = toolbar

        self.button     = Gtk.ToggleButton (label=_("Lyrics"))
        self.datasource = ds
        self.view       = view
        
        self.button.show()
        self.button.set_relief (Gtk.ReliefStyle.NONE)
        self.button.set_focus_on_click(False)
        self.button.connect ('clicked', 
            lambda button: self.emit('switch-tab', 'lyrics'))
        toolbar.pack_start (self.button, True, True, 0)

    def activate (self):
        print("activating Lyrics Tab")
        self.button.set_active(True)
        self.reload ()

    def deactivate (self):
        print("deactivating Lyrics Tab")
        self.button.set_active(False)

    def reload (self):
        entry = self.sp.get_playing_entry ()
        if entry is None:
            return
        self.datasource.fetch_lyrics (entry)
        self.view.loading (self.datasource.get_artist(), self.datasource.get_title())

class LyricsView (GObject.GObject):

    def __init__ (self, shell, plugin, webview, ds):
        GObject.GObject.__init__ (self)
        self.webview = webview
        self.ds      = ds
        self.shell   = shell
        self.plugin  = plugin
        self.file    = ""
        plugindir = plugin.plugin_info.get_data_dir()
        self.basepath = "file://" + urllib.request.pathname2url(plugindir)

        self.load_tmpl ()
        self.connect_signals ()

    def connect_signals (self):
        self.ds.connect ('lyrics-ready', self.lyrics_ready)

    def load_view (self):
        self.webview.load_string (self.file, 'text/html', 'utf-8', self.basepath)

    def loading (self, current_artist, song):
        self.loading_file = self.loading_template.render (
            artist   = current_artist,
            info     = _("Loading lyrics for %s by %s") % (song, current_artist),
            song     = song,
            basepath = self.basepath)
        self.webview.load_string (self.loading_file, 'text/html', 'utf-8', self.basepath)
        print("loading screen loaded")

    def load_tmpl (self):
        self.path = rb.find_plugin_file(self.plugin, 'tmpl/lyrics-tmpl.html')
        self.loading_path = rb.find_plugin_file (self.plugin, 'tmpl/loading.html')
        self.template = Template (filename = self.path)
        self.loading_template = Template (filename = self.loading_path)
        self.styles = self.basepath + '/tmpl/main.css'

    def lyrics_ready (self, ds, entry, lyrics):
        print("loading lyrics into webview")
        if lyrics is None:
            lyrics = _("Lyrics not found")
        else:
            lyrics = lyrics.strip()
            lyrics = escape (lyrics, True)
            lyrics = lyrics.replace ('\n', '<br />')

        # should include data source information here, but the lyrics plugin
        # doesn't expose that.
        self.file = self.template.render (artist     = ds.get_artist (),
                                          song       = ds.get_title (),
                                          lyrics     = lyrics,
                                          stylesheet = self.styles)
        self.load_view ()

class LyricsDataSource (GObject.GObject):
    
    __gsignals__ = {
        'lyrics-ready' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, (RB.RhythmDBEntry, GObject.TYPE_STRING,)),
    }

    def __init__ (self, db):
        GObject.GObject.__init__ (self)
        self.db = db
        self.db.connect ('entry-extra-metadata-notify::rb:lyrics', self.lyrics_notify)

    def lyrics_notify (self, db, entry, field, metadata):
        if rb.entry_equal(entry, self.entry):
            self.emit ('lyrics-ready', self.entry, metadata)

    def fetch_lyrics (self, entry):
        self.entry = entry
        lyrics = self.db.entry_request_extra_metadata(entry, "rb:lyrics")
        # it never responds synchronously at present, but maybe some day it will
        if lyrics is not None:
            self.emit ('lyrics-ready', self.entry, lyrics)

    def get_title (self):
        return self.entry.get_string (RB.RhythmDBPropType.TITLE)

    def get_artist (self):
        return self.entry.get_string (RB.RhythmDBPropType.ARTIST)

