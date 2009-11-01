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

import rb, rhythmdb
import gtk, gobject
import re, os
import cgi
import urllib
import xml.dom.minidom as dom
import LastFM

import webkit
from mako.template import Template
    
class ArtistTab (gobject.GObject):
    
    __gsignals__ = {
        'switch-tab' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE,
                                (gobject.TYPE_STRING,))
    }

    def __init__ (self, shell, buttons, ds, view):
        gobject.GObject.__init__ (self)
        self.shell      = shell
        self.sp         = shell.get_player ()
        self.db         = shell.get_property ('db') 
        self.buttons    = buttons

        self.button     = gtk.ToggleButton (_("Artist"))
        self.datasource = ds
        self.view       = view
        self.artist     = None
        self.active     = False

        self.button.show()
        self.button.set_relief( gtk.RELIEF_NONE ) 
        self.button.set_focus_on_click(False)
        self.button.connect ('clicked', 
            lambda button : self.emit('switch-tab', 'artist'))
        buttons.pack_start (self.button, True, True)

    def activate (self):
        print "activating Artist Tab"
        self.button.set_active(True)
        self.active = True
        self.reload ()

    def deactivate (self):
        print "deactivating Artist Tab"
        self.button.set_active(False)
        self.active = False

    def reload (self):
        entry = self.sp.get_playing_entry ()
        if entry is None:
            print "Nothing playing"
            return None
        artist = self.db.entry_get (entry, rhythmdb.PROP_ARTIST)

        if self.active and self.artist != artist:
            self.datasource.fetch_artist_data (artist)
            self.view.loading (artist)
        else:
            self.view.load_view()
        self.artist = artist

class ArtistView (gobject.GObject):

    def __init__ (self, shell, plugin, webview, ds):
        gobject.GObject.__init__ (self)
        self.webview  = webview
        self.ds       = ds
        self.shell    = shell
        self.plugin   = plugin
        self.file     = ""
        plugindir = os.path.split(plugin.find_file ('context.rb-plugin'))[0]
        self.basepath = "file://" + urllib.pathname2url (plugindir)

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
        self.path = self.plugin.find_file('tmpl/artist-tmpl.html')
        self.loading_path = self.plugin.find_file ('tmpl/loading.html')
        self.template = Template (filename = self.path, module_directory = '/tmp/context/')
        self.loading_template = Template (filename = self.loading_path, module_directory = '/tmp/context')
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
        except Exception, e:
            print "Problem in info ready: %s" % e
    

class ArtistDataSource (gobject.GObject):
    
    __gsignals__ = {
        'artist-info-ready'       : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
        'artist-similar-ready'    : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
        'artist-top-tracks-ready' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
        'artist-top-albums-ready' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, ()),
    }

    def __init__ (self, info_cache, ranking_cache):
        gobject.GObject.__init__ (self)

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
            
            'similar' : {
                'data'      : None, 
                'signal'    : 'artist-similar-ready', 
                'function'  : 'getsimilar',
                'cache'     : info_cache,
                'parsed'    : False,
            },

            'top_albums' : {
                'data'      : None, 
                'signal'    : 'artist-top-albums-ready',
                'function'  : 'gettopalbums',
                'cache'     : ranking_cache,
                'parsed'    : False,
            },

            'top_tracks' : {
                'data'      : None, 
                'signal'    : 'artist-top-tracks-ready',
                'function'  : 'gettoptracks',
                'cache'     : ranking_cache,
                'parsed'    : False,
            },
        }
       
    def extract (self, data, position):
        """
        Safely extract the data from an xml node. Returns data
        at position or None if position does not exist
        """
        
        try:
            return data[position].firstChild.data
        except Exception, e:
            return None

    def fetch_top_tracks (self, artist):
        if LastFM.user_has_account() is False:
            return

        artist = urllib.quote_plus (artist)
        function = self.artist['top_tracks']['function']
        cache = self.artist['top_tracks']['cache']
        cachekey = "lastfm:artist:%s:%s" % (function, artist)
        url = '%sartist.%s&artist=%s&api_key=%s' % (LastFM.URL_PREFIX,
            function, artist, LastFM.API_KEY)
        cache.fetch(cachekey, url, self.fetch_artist_data_cb, self.artist['top_tracks'])

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
        artist = urllib.quote_plus (artist)
        for key, value in self.artist.items():
            cachekey = "lastfm:artist:%s:%s" % (value['function'], artist)
            url = '%sartist.%s&artist=%s&api_key=%s' % (LastFM.URL_PREFIX,
                value['function'], artist, LastFM.API_KEY)
            value['cache'].fetch(cachekey, url, self.fetch_artist_data_cb, value)

    def fetch_artist_data_cb (self, data, category):
        if data is None:
            print "no data fetched for artist %s" % category['function']
            return

        try:
            category['data'] = dom.parseString (data)
            category['parsed'] = False
            self.emit (category['signal'])
        except Exception, e:
            print "Error parsing artist %s: %s" % (category['function'], e)
            return False

    def get_current_artist (self):
        return self.current_artist

    def get_error (self):
        return self.error

    def get_top_albums (self):
        if not self.artist['top_albums']['parsed']:
            albums = []
            for album in self.artist['top_albums']['data'].getElementsByTagName ('album'):
                album_name = self.extract(album.getElementsByTagName ('name'), 0)
                imgs = album.getElementsByTagName ('image') 
                images = self.extract(imgs, 0), self.extract(imgs, 1), self.extract(imgs,2)
                albums.append ((album_name, images))
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
            for node in data.getElementsByTagName ('artist'):
                artist = self.extract(node.getElementsByTagName('name'), 0)
                similar = self.extract(node.getElementsByTagName('match') ,0)
                image = self.extract(node.getElementsByTagName('image'), 0)
                lst.append ((artist, similar, image))
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

        images = data.getElementsByTagName ('image')
        return self.extract(images,0), self.extract(images,1), self.extract(images,2)
        
    def get_artist_bio (self):
        """
        Returns tuple of summary and full bio
        """
        data = self.artist['info']['data']
        if data is None:
            return None

        if not self.artist['info']['parsed']:
            content = self.extract(data.getElementsByTagName ('content'), 0)
            summary = self.extract(data.getElementsByTagName ('summary'), 0)
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

    def get_top_tracks (self):
        """
        Returns a list of the top track titles
        """
        data = self.artist['top_tracks']['data']
        if data is None:
            return None

        if not self.artist['top_tracks']['parsed']:
            tracks = []
            for track in data.getElementsByTagName ('track'):
                name = self.extract(track.getElementsByTagName('name'), 0)
                tracks.append (name)
            self.artist['top_tracks']['data'] = tracks
            self.artist['top_tracks']['parsed'] = True

        return self.artist['top_tracks']['data']
