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

import re, os
import urllib.request, urllib.parse
import xml.dom.minidom as dom
import json

from mako.template import Template

import rb
import LastFM

from gi.repository import WebKit
from gi.repository import GObject, Gtk
from gi.repository import RB

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class ArtistTab (GObject.GObject):
    
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

        self.button     = Gtk.ToggleButton (label=_("Artist"))
        self.datasource = ds
        self.view       = view
        self.artist     = None
        self.active     = False

        self.button.show()
        self.button.set_relief (Gtk.ReliefStyle.NONE)
        self.button.set_focus_on_click(False)
        self.button.connect ('clicked', 
            lambda button : self.emit('switch-tab', 'artist'))
        buttons.pack_start (self.button, True, True, 0)

    def activate (self):
        print("activating Artist Tab")
        self.button.set_active(True)
        self.active = True
        self.reload ()

    def deactivate (self):
        print("deactivating Artist Tab")
        self.button.set_active(False)
        self.active = False

    def reload (self):
        entry = self.sp.get_playing_entry ()
        if entry is None:
            print("Nothing playing")
            return None
        artist = entry.get_string (RB.RhythmDBPropType.ARTIST)

        if self.active and self.artist != artist:
            self.datasource.fetch_artist_data (artist)
            self.view.loading (artist)
        else:
            self.view.load_view()
        self.artist = artist

class ArtistView (GObject.GObject):

    def __init__ (self, shell, plugin, webview, ds):
        GObject.GObject.__init__ (self)
        self.webview  = webview
        self.ds       = ds
        self.shell    = shell
        self.plugin   = plugin
        self.file     = ""

        plugindir = plugin.plugin_info.get_data_dir()
        self.basepath = "file://" + urllib.request.pathname2url (plugindir)

        self.load_tmpl ()
        self.connect_signals ()

    def load_view (self):
        self.webview.load_string (self.file, 'text/html', 'utf-8', self.basepath)

    def loading (self, current_artist):
        self.loading_file = self.loading_template.render (
            artist   = current_artist,
            info     = _("Loading biography for %s") % current_artist,
            song     = "",
            basepath = self.basepath)
        self.webview.load_string (self.loading_file, 'text/html', 'utf-8', self.basepath)

    def load_tmpl (self):
        self.path = rb.find_plugin_file(self.plugin, 'tmpl/artist-tmpl.html')
        self.loading_path = rb.find_plugin_file (self.plugin, 'tmpl/loading.html')
        self.template = Template (filename = self.path)
        self.loading_template = Template (filename = self.loading_path)
        self.styles = self.basepath + '/tmpl/main.css'

    def connect_signals (self):
        self.air_id  = self.ds.connect ('artist-info-ready', self.artist_info_ready)

    def artist_info_ready (self, ds):
        # Can only be called after the artist-info-ready signal has fired.
        # If called any other time, the behavior is undefined
        try:
            info = ds.get_artist_info ()
            small, med, big = info['images'] or (None, None, None)
            summary, full_bio = info['bio'] or (None, None)
            self.file = self.template.render (artist     = ds.get_current_artist (),
                                              error      = ds.get_error (),
                                              image      = med,
                                              fullbio    = full_bio,
                                              shortbio   = summary,
                                              datasource = LastFM.datasource_link (self.basepath),
                                              stylesheet = self.styles )
            self.load_view ()
        except Exception as e:
            print("Problem in info ready: %s" % e)
    

class ArtistDataSource (GObject.GObject):
    
    __gsignals__ = {
        'artist-info-ready'       : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, ()),
        'artist-similar-ready'    : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, ()),
        'artist-top-tracks-ready' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, ()),
        'artist-top-albums-ready' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_NONE, ()),
    }

    def __init__ (self, info_cache, ranking_cache):
        GObject.GObject.__init__ (self)

        self.current_artist = None
        self.error = None
        self.artist = {
            'info' : {
                'data'      : None, 
                'signal'    : 'artist-info-ready', 
                'function'  : 'getinfo',
                'cache'     : info_cache,
                'parsed'    : False,
            },
            
            # nothing uses this
            #'similar' : {
            #    'data'      : None, 
            #    'signal'    : 'artist-similar-ready', 
            #    'function'  : 'getsimilar',
            #    'cache'     : info_cache,
            #    'parsed'    : False,
            #},

            # or this
            #'top_albums' : {
            #    'data'      : None, 
            #    'signal'    : 'artist-top-albums-ready',
            #    'function'  : 'gettopalbums',
            #    'cache'     : ranking_cache,
            #    'parsed'    : False,
            #},
        }
       
    def fetch_artist_data (self, artist): 
        """
        Initiate the fetching of all artist data. Fetches artist info, similar
        artists, artist top albums and top tracks. Downloads XML files from last.fm
        and saves as parsed DOM documents in self.artist dictionary. Must be called
        before any of the get_* methods.
        """
        self.current_artist = artist
        if LastFM.user_has_account() is False:
            self.error = LastFM.NO_ACCOUNT_ERROR
            self.emit ('artist-info-ready')
            return

        self.error = None
        artist = urllib.parse.quote_plus(artist)
        for key, value in self.artist.items():
            cachekey = "lastfm:artist:%sjson:%s" % (value['function'], artist)
            url = '%sartist.%s&artist=%s&api_key=%s&format=json' % (LastFM.URL_PREFIX,
                value['function'], artist, LastFM.API_KEY)
            print("fetching %s" % url)
            value['cache'].fetch(cachekey, url, self.fetch_artist_data_cb, value)

    def fetch_artist_data_cb (self, data, category):
        if data is None:
            print("no data fetched for artist %s" % category['function'])
            return

        try:
            category['data'] = json.loads(data.decode('utf-8'))
            category['parsed'] = False
            self.emit (category['signal'])
        except Exception as e:
            print("Error parsing artist %s: %s" % (category['function'], e))
            return False

    def get_current_artist (self):
        return self.current_artist

    def get_error (self):
        return self.error

    def get_top_albums (self):
        if not self.artist['top_albums']['parsed']:
            albums = []
            d = self.artist['top_albums']['data']
            for album in d['topalbums'].get('album', []):
                images = [img['#text'] for img in album.get('image', ())]
                albums.append((album.get('name'), images[:3]))

            self.artist['top_albums']['data'] = albums
            self.artist['top_albums']['parsed'] = True

        return self.artist['top_albums']['data']

    def get_similar_artists (self):
        """
        Returns a list of similar artists
        """
        data = self.artist['similar']['data']
        if data is None:
            return None

        if not self.artist['similar']['parsed']:
            lst = []
            for node in data['similarartists'].get('artist', []):
                image = [img['#text'] for img in node.get('image', [])]
                lst.append ((node.get('name'), node.get('match'), image[:1]))
            data = lst
            self.artist['similar']['parsed'] = True
            self.artist['similar']['data'] = data

        return data

    def get_artist_images (self):
        """
        Returns tuple of image url's for small, medium, and large images.
        """
        data = self.artist['info']['data']
        if data is None:
            return None

        images = [img['#text'] for img in data['artist'].get('image', ())]
        return images[:3]
        
    def get_artist_bio (self):
        """
        Returns tuple of summary and full bio
        """
        data = self.artist['info']['data']
        if data is None:
            return None

        if not self.artist['info']['parsed']:
            content = data['artist']['bio']['content']
            summary = data['artist']['bio']['summary']
            return summary, content

        return self.artist['info']['data']['bio']

    def get_artist_info (self):
        """
        Returns the dictionary { 'images', 'bio' }
        """
        if not self.artist['info']['parsed']:
            images = self.get_artist_images()
            bio = self.get_artist_bio()
            self.artist['info']['data'] = { 'images'   : images,
                                            'bio'      : bio }
            self.artist['info']['parsed'] = True

        return self.artist['info']['data']
