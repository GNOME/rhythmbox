# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright 2005 Eduardo Gonzalez
# Copyright (C) 2006 Jonathan Matthew
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
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
# TODO: 
# - multiple lyrics sites (lyrc.com.ar, etc.)
# - handle multiple results usefully
# - save lyrics to disk?
# - check that the lyrics returned even remotely match the request?
# - share URL grabbing code with other plugins

import os 
import gtk, gobject
import urllib
import re
from xml.dom import minidom
import rb
import rhythmdb

ui_str = """
<ui>
  <menubar name="MenuBar">
    <menu name="ViewMenu" action="View">
      <menuitem name="ViewSongLyrics" action="ViewSongLyrics"/>
    </menu>
  </menubar>
</ui>
"""

LYRICS_FOLDER="~/.lyrics"
LYRIC_TITLE_STRIP=["\(live[^\)]*\)", "\(acoustic[^\)]*\)", "\([^\)]*mix\)", "\([^\)]*version\)", "\([^\)]*edit\)", "\(feat[^\)]*\)"]
LYRIC_TITLE_REPLACE=[("/", "-"), (" & ", " and ")]
LYRIC_ARTIST_REPLACE=[("/", "-"), (" & ", " and ")]


def create_lyrics_view():
    view = gtk.TextView()
    view.set_wrap_mode(gtk.WRAP_WORD)
    view.set_editable(False)

    sw = gtk.ScrolledWindow()
    sw.add(view)
    sw.set_policy(gtk.POLICY_NEVER, gtk.POLICY_AUTOMATIC)
    sw.set_shadow_type(gtk.SHADOW_IN)

    vbox = gtk.VBox(spacing=12)
    vbox.pack_start(sw, expand=True)
    return (vbox, view.get_buffer())

class LyricWindow(gtk.Window):
    def __init__(self, db, entry):
        gtk.Window.__init__(self)
        self.set_border_width(12)

	title = db.entry_get(entry, rhythmdb.PROP_TITLE)
	artist = db.entry_get(entry, rhythmdb.PROP_ARTIST)
        self.set_title(title + " - " + artist + " - Lyrics")

	close = gtk.Button(stock=gtk.STOCK_CLOSE)
	close.connect('clicked', lambda w: self.destroy())

	(lyrics_view, buffer) = create_lyrics_view()
	self.buffer = buffer
        bbox = gtk.HButtonBox()
        bbox.set_layout(gtk.BUTTONBOX_END)
        bbox.pack_start(close)
        lyrics_view.pack_start(bbox, expand=False)

        self.buffer.set_text(_("Searching for lyrics..."))
        self.add(lyrics_view)
        self.set_default_size(400, 300)
        self.show_all()

class LyricPane(object):
    def __init__(self, db, song_info):
    	(self.view, self.buffer) = create_lyrics_view()
	self.view.show_all()
	self.page_num = song_info.append_page(_("Lyrics"), self.view)
	self.db = db
	self.song_info = song_info
	self.have_lyrics = 0
	self.visible = 0
        self.entry = self.song_info.get_property("current-entry")

	self.entry_change_id = song_info.connect('notify::current-entry', self.entry_changed)
	nb = self.view.get_parent()
	self.switch_page_id = nb.connect('switch-page', self.switch_page_cb)

    def entry_changed(self, pspec, duh):
        self.entry = self.song_info.get_property("current-entry")
	self.have_lyrics = 0
	if self.visible != 0:
	    self.get_lyrics()

    def switch_page_cb(self, notebook, page, page_num):
        if self.have_lyrics != 0:
	    return

        if page_num != self.page_num:
	    self.visible = 0
	    return

	self.visible = 1
	self.get_lyrics()

    def get_lyrics(self):
        if self.entry is None:
	    return

	self.buffer.set_text(_("Searching for lyrics..."));
	lyrics_grabber = LyricGrabber()
	lyrics_grabber.get_lyrics(self.db, self.entry, self.buffer.set_text)
	
	

class LyricGrabber(object):
    def __init__(self):
    	self.loader = rb.Loader ()

    def _build_cache_path(self, artist, title):
        lyrics_folder = os.path.expanduser (LYRICS_FOLDER)
        if not os.path.exists (lyrics_folder):
            os.mkdir (lyrics_folder)

	artist_folder = lyrics_folder + '/' + artist[:128]
	if not os.path.exists (artist_folder):
	    os.mkdir (artist_folder)

	return artist_folder + '/' + title[:128] + '.lyric'

    def get_lyrics(self, db, entry, callback):
	self.callback = callback
        artist = db.entry_get(entry, rhythmdb.PROP_ARTIST).lower()
        title = db.entry_get(entry, rhythmdb.PROP_TITLE).lower()

	# replace ampersands and the like
	for exp in LYRIC_ARTIST_REPLACE:
		p = re.compile (exp[0])
		artist = p.sub(exp[1], artist)
	for exp in LYRIC_TITLE_REPLACE:
		p = re.compile (exp[0])
		title = p.sub(exp[1], title)

        # strip things like "(live at Somewhere)", "(accoustic)", etc
        for exp in LYRIC_TITLE_STRIP:
            p = re.compile (exp)
            title = p.sub ('', title)

	# compress spaces
	title = title.strip()
	artist = artist.strip()

	self.cache_path = self._build_cache_path(artist, title)

	if os.path.exists (self.cache_path):
            self.loader.get_url(self.cache_path, callback)
            return;
        
	url = "http://api.leoslyrics.com/api_search.php?auth=Rhythmbox&artist=%s&songtitle=%s" % (
		urllib.quote(artist.encode('utf-8')),
		urllib.quote(title.encode('utf-8')))
	self.loader.get_url(url, self.search_results)

    def search_results(self, data):
    	if data is None:
	    self.callback("Server did not respond.")
	    return

	try:
	    xmldoc = minidom.parseString(data).documentElement
	except:
	    self.callback("Couldn't parse search results.")
	    return
	
	result_code = xmldoc.getElementsByTagName('response')[0].getAttribute('code')
	if result_code != '0':
	    self.callback("Server is busy, try again later.")
	    xmldoc.unlink()
	    return
	
	# We don't really need the top 100 matches, so I'm limiting it to ten
	matches = xmldoc.getElementsByTagName('result')[:10]
	#songs = map(lambda x:
	#	    x.getElementsByTagName('name')[0].firstChild.nodeValue
	#	    + " - " +
	#	    x.getElementsByTagName('title')[0].firstChild.nodeValue,
	#	    matches)
	hids = map(lambda x: x.getAttribute('hid'), matches)
	#exacts = map(lambda x: x.getAttribute('exactMatch'), matches)

	if len(hids) == 0:
	    # FIXME show other matches
	    self.callback("Unable to find lyrics for this track.")
	    xmldoc.unlink()
	    return
	
	#songlist = []
	#for i in range(len(hids)):
	#    songlist.append((songs[i], hids[i], exacts[i]))

	xmldoc.unlink()
	url = "http://api.leoslyrics.com/api_lyrics.php?auth=Rhythmbox&hid=%s" % (urllib.quote(hids[0].encode('utf-8')))
	self.loader.get_url(url, self.lyrics)


    def lyrics(self, data):
        if data is None:
	    self.callback("Unable to find lyrics for this track.")
	    return

	try:
	    xmldoc = minidom.parseString(data).documentElement
	except:
	    self.callback("Unable to parse the lyrics returned.")
	    return

	text = xmldoc.getElementsByTagName('title')[0].firstChild.nodeValue
	text += ' - ' + xmldoc.getElementsByTagName('artist')[0].getElementsByTagName('name')[0].firstChild.nodeValue + '\n\n'
	text += xmldoc.getElementsByTagName('text')[0].firstChild.nodeValue
	xmldoc.unlink()

	text += "\n\n"+_("Lyrics provided by leoslyrics.com")


        f = file (self.cache_path, 'w')
        f.write (text)
        f.close ()

	self.callback(text)



class LyricsDisplayPlugin(rb.Plugin):

    def __init__ (self):
	rb.Plugin.__init__ (self)
	self.window = None

    def activate (self, shell):
	self.action = gtk.Action ('ViewSongLyrics', _('Song L_yrics'),
				  _('Display lyrics for the playing song'),
				  'rb-song-lyrics')
	self.activate_id = self.action.connect ('activate', self.show_song_lyrics, shell)

	self.action_group = gtk.ActionGroup ('SongLyricsPluginActions')
	self.action_group.add_action (self.action)
    	
    	uim = shell.get_ui_manager ()
	uim.insert_action_group (self.action_group, 0)
	self.ui_id = uim.add_ui_from_string (ui_str)
	uim.ensure_update ()

	sp = shell.get_player ()
	self.pec_id = sp.connect('playing-song-changed', self.playing_entry_changed)
	self.playing_entry_changed (sp, sp.get_playing_entry ())

	self.csi_id = shell.connect('create_song_info', self.create_song_info)

    def deactivate (self, shell):
    	
	uim = shell.get_ui_manager()
	uim.remove_ui (self.ui_id)
	uim.remove_action_group (self.action_group)

	self.action_group = None
	self.action = None

	sp = shell.get_player ()
	sp.disconnect (self.pec_id)

	if self.window is not None:
	    self.window.destroy ()


    def playing_entry_changed (self, sp, entry):
    	if entry is not None:
	    self.action.set_sensitive (True)
	else:
	    self.action.set_sensitive (False)

    def show_song_lyrics (self, action, shell):

	if self.window is not None:
	    self.window.destroy ()

	db = shell.get_property ("db")
	sp = shell.get_player ()
	entry = sp.get_playing_entry ()

	if entry is None:
	    return

	self.window = LyricWindow(db, entry)
	lyrics_grabber = LyricGrabber()
	lyrics_grabber.get_lyrics(db, entry, self.window.buffer.set_text)
	
    def create_song_info (self, shell, song_info, is_multiple):

	if is_multiple is False:
	    x = LyricPane(shell.get_property ("db"), song_info)

