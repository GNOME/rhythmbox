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

import os
import cgi
import urllib
from mako.template import Template
import xml.dom.minidom as dom

import LastFM

import rb
from gi.repository import RB
from gi.repository import GObject, Gtk
from gi.repository import WebKit

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class AlbumTab (GObject.GObject):

    __gsignals__ = {
        'switch-tab' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE,
                                (GObject.TYPE_STRING,))
    }
    
    def __init__ (self, shell, buttons, ds, view):
        GObject.GObject.__init__ (self)
        self.shell      = shell
        self.sp         = shell.props.shell_player
        self.db         = shell.props.db
        self.buttons    = buttons

        self.button     = Gtk.ToggleButton (label=_("Albums"))
        self.ds         = ds
        self.view       = view
        self.artist     = None
        self.active     = False

        self.button.show()
        self.button.set_relief (Gtk.ReliefStyle.NONE)
        self.button.set_focus_on_click(False)
        self.button.connect ('clicked', 
            lambda button: self.emit ('switch-tab', 'album'))
        buttons.pack_start (self.button, True, True, 0)

    def activate (self):
        self.button.set_active(True)
        self.active = True
        self.reload ()

    def deactivate (self):
        self.button.set_active(False)
        self.active = False

    def reload (self):
        entry = self.sp.get_playing_entry ()
        if entry is None:
            return None

        artist = entry.get_string (RB.RhythmDBPropType.ARTIST)
        album  = entry.get_string (RB.RhythmDBPropType.ALBUM)
        if self.active and artist != self.artist:
            self.view.loading(artist)
            self.ds.fetch_album_list (artist)
        else:
            self.view.load_view()

        self.artist = artist

class AlbumView (GObject.GObject):

    def __init__ (self, shell, plugin, webview, ds):
        GObject.GObject.__init__ (self)
        self.webview = webview
        self.ds      = ds
        self.shell   = shell
        self.plugin  = plugin
        self.file    = ""

        plugindir = plugin.plugin_info.get_data_dir()
        self.basepath = "file://" + urllib.pathname2url (plugindir)

        self.load_tmpl ()
        self.connect_signals ()

    def load_view (self):
        self.webview.load_string(self.file, 'text/html', 'utf-8', self.basepath)

    def connect_signals (self):
        self.ds.connect('albums-ready', self.album_list_ready)

    def loading (self, current_artist):
        self.loading_file = self.loading_template.render (
            artist   = current_artist,
            # Translators: 'top' here means 'most popular'.  %s is replaced by the artist name.
            info     = _("Loading top albums for %s") % current_artist,
            song     = "",
            basepath = self.basepath)
        self.webview.load_string (self.loading_file, 'text/html', 'utf-8', self.basepath)

    def load_tmpl (self):
        self.path = rb.find_plugin_file (self.plugin, 'tmpl/album-tmpl.html')
        self.loading_path = rb.find_plugin_file (self.plugin, 'tmpl/loading.html')
        self.album_template = Template (filename = self.path,
                                        module_directory = '/tmp/context')
        self.loading_template = Template (filename = self.loading_path, 
                                          module_directory = '/tmp/context')
        self.styles = self.basepath + '/tmpl/main.css'

    def album_list_ready (self, ds):
        self.file = self.album_template.render (error = ds.get_error(), 
                                                list = ds.get_top_albums(), 
                                                artist = ds.get_artist(),
                                                datasource = LastFM.datasource_link (self.basepath),
                                                stylesheet = self.styles)
        self.load_view ()


class AlbumDataSource (GObject.GObject):
    
    __gsignals__ = {
        'albums-ready' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, ())
    }

    def __init__ (self, info_cache, ranking_cache):
        GObject.GObject.__init__ (self)
        self.albums = None
        self.error = None
        self.artist = None
        self.max_albums_fetched = 8
        self.info_cache = info_cache
        self.ranking_cache = ranking_cache

    def extract (self, data, position):
        """
        Safely extract the data from an xml node. Returns data
        at position or None if position does not exist
        """
        try:
            return data[position].firstChild.data
        except Exception, e:
            return None

    def get_artist (self):
        return self.artist

    def get_error (self):
        return self.error

    def fetch_album_list (self, artist):
        if LastFM.user_has_account() is False:
            self.error = LastFM.NO_ACCOUNT_ERROR
            self.emit ('albums-ready')
            return

        self.artist = artist
        qartist = urllib.quote_plus (artist)
        self.error  = None
        url = "%sartist.gettopalbums&artist=%s&api_key=%s" % (LastFM.URL_PREFIX,
                                                              qartist,
                                                              LastFM.API_KEY)
        cachekey = 'lastfm:artist:gettopalbums:%s' % qartist
        self.ranking_cache.fetch(cachekey, url, self.parse_album_list, artist)

    def parse_album_list (self, data, artist):
        if data is None:
            print "Nothing fetched for %s top albums" % artist
            return

        try:
            parsed = dom.parseString (data)
        except Exception, e:
            print "Error parsing album list: %s" % e
            return False

        lfm = parsed.getElementsByTagName ('lfm')[0]
        if lfm.attributes['status'].value == 'failed':
            self.error = lfm.childNodes[1].firstChild.data
            self.emit ('albums-ready')
            return

        self.albums = []
        album_nodes = parsed.getElementsByTagName ('album') 
        print "num albums: %d" % len(album_nodes)
        if len(album_nodes) == 0:
            self.error = "No albums found for %s" % artist
            self.emit('albums-ready')
            return
            
        self.album_info_fetched = min (len (album_nodes) - 1, self.max_albums_fetched)

        for i, album in enumerate (album_nodes): 
            if i >= self.album_info_fetched:
                break

            album_name = self.extract(album.getElementsByTagName ('name'), 0)
            imgs = album.getElementsByTagName ('image')
            images = (self.extract(imgs, 0), self.extract(imgs, 1), self.extract(imgs, 2))
            self.albums.append ({'title' : album_name, 'images' : images })
            self.fetch_album_info (artist, album_name, i)

    def get_top_albums (self):
        return self.albums

    def fetch_album_info (self, artist, album, index):
        qartist = urllib.quote_plus (artist)
        qalbum = urllib.quote_plus (album.encode('utf-8'))
        cachekey = "lastfm:album:getinfo:%s:%s" % (qartist, qalbum)
        url = "%salbum.getinfo&artist=%s&album=%s&api_key=%s" % (LastFM.URL_PREFIX,
                                                                 qartist,
                                                                 qalbum,
                                                                 LastFM.API_KEY)
        self.info_cache.fetch(cachekey, url, self.fetch_album_tracklist, album, index)

    def fetch_album_tracklist (self, data, album, index):
        if data is None:
            self.assemble_info(None, None, None)

        try:
            parsed = dom.parseString (data)
            self.albums[index]['id'] = parsed.getElementsByTagName ('id')[0].firstChild.data
        except Exception, e:
            print "Error parsing album tracklist: %s" % e
            return False

        self.albums[index]['releasedate'] = self.extract(parsed.getElementsByTagName ('releasedate'),0)
        self.albums[index]['summary'] = self.extract(parsed.getElementsByTagName ('summary'), 0)

        cachekey = "lastfm:album:tracks:%s" % self.albums[index]['id']
        url = "%splaylist.fetch&playlistURL=lastfm://playlist/album/%s&api_key=%s" % (
                     LastFM.URL_PREFIX, self.albums[index]['id'], LastFM.API_KEY)

        self.info_cache.fetch(cachekey, url, self.assemble_info, album, index)

    def assemble_info (self, data, album, index):
        rv = True
        if data is None:
            print "nothing fetched for %s tracklist" % album
        else:
            try:
                parsed = dom.parseString (data)
                list = parsed.getElementsByTagName ('track')
                tracklist = []
                album_length = 0
                for i, track in enumerate(list):
                    title = track.getElementsByTagName ('title')[0].firstChild.data
                    duration = int(track.getElementsByTagName ('duration')[0].firstChild.data) / 1000
                    album_length += duration
                    tracklist.append ((i, title, duration))
                self.albums[index]['tracklist'] = tracklist
                self.albums[index]['duration']  = album_length
            except Exception, e:
                print "Error parsing album playlist: %s" % e
                rv = False

        self.album_info_fetched -= 1
        print "%s albums left to process" % self.album_info_fetched
        if self.album_info_fetched == 0:
            self.emit('albums-ready')

        return rv
