# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Jonathan Matthew
# Copyright (C) 2007 James Livingston
# Copyright (C) 2007 Sirio Bolaños Puchet
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
# - check that the lyrics returned even remotely match the request?

import os, re
import gtk, gtk.glade
import gconf
import rhythmdb, rb

import LyricsParse
from LyricsConfigureDialog import LyricsConfigureDialog

ui_str = """
<ui>
  <menubar name="MenuBar">
	<menu name="ViewMenu" action="View">
	  <menuitem name="ViewSongLyrics" action="ViewSongLyrics"/>
	</menu>
  </menubar>
</ui>
"""

LYRIC_TITLE_STRIP=["\(live[^\)]*\)", "\(acoustic[^\)]*\)", "\([^\)]*mix\)", "\([^\)]*version\)", "\([^\)]*edit\)", "\(feat[^\)]*\)"]
LYRIC_TITLE_REPLACE=[("/", "-"), (" & ", " and ")]
LYRIC_ARTIST_REPLACE=[("/", "-"), (" & ", " and ")]

gconf_keys = {	'engines' : '/apps/rhythmbox/plugins/lyrics/engines',
		'folder': '/apps/rhythmbox/plugins/lyrics/folder'
	     }


def create_lyrics_view():
	tview = gtk.TextView()
	tview.set_wrap_mode(gtk.WRAP_WORD)
	tview.set_editable(False)

	sw = gtk.ScrolledWindow()
	sw.add(tview)
	sw.set_policy(gtk.POLICY_NEVER, gtk.POLICY_AUTOMATIC)
	sw.set_shadow_type(gtk.SHADOW_IN)

	vbox = gtk.VBox(spacing=12)
	vbox.pack_start(sw, expand=True)
	
	return (vbox, tview.get_buffer(), tview)

def parse_song_data(artist, title):
	
	# replace ampersands and the like
	for exp in LYRIC_ARTIST_REPLACE:
		artist = re.sub(exp[0], exp[1], artist)
	for exp in LYRIC_TITLE_REPLACE:
		title = re.sub(exp[0], exp[1], title)

	# strip things like "(live at Somewhere)", "(accoustic)", etc
	for exp in LYRIC_TITLE_STRIP:
		title = re.sub (exp, '', title)

	# compress spaces
	title = title.strip()
	artist = artist.strip()	
	
	return (artist, title)
	
def build_cache_path(artist, title):
	folder = gconf.client_get_default().get_string(gconf_keys['folder'])
	if folder is None:
		folder = "~/.lyrics"

	lyrics_folder = os.path.expanduser (folder)
	if not os.path.exists (lyrics_folder):
		os.mkdir (lyrics_folder)

	artist_folder = lyrics_folder + '/' + artist[:128]
	if not os.path.exists (artist_folder):
		os.mkdir (artist_folder)

	return artist_folder + '/' + title[:128] + '.lyric'

class LyricGrabber(object):
	def __init__(self, db, entry):
		self.loader = rb.Loader ()
		self.db = db
		self.entry = entry
		
		self.artist = self.db.entry_get(self.entry, rhythmdb.PROP_ARTIST).lower()
		self.title = self.db.entry_get(self.entry, rhythmdb.PROP_TITLE).lower()

		(self.artist, self.title) = parse_song_data(self.artist, self.title)

		self.cache_path = build_cache_path(self.artist, self.title)

	def verify_lyric(self):
		return os.path.exists(self.cache_path)
	  
	def search_lyrics(self, callback, cache_only=False):
		self.callback = callback
		
		status = self.verify_lyric()
		
		if status:
			self.loader.get_url(self.cache_path, callback)
		else:
			if cache_only:
				self.callback(_("No lyrics found"))
			else:
				def lyric_callback (text):
					if text is not None:
						f = file (self.cache_path, 'w')
						f.write (text)
						f.close ()
						self.callback(text)
					else:
						self.callback(_("No lyrics found"))

				parser = LyricsParse.Parser(gconf_keys, self.artist, self.title)
				parser.get_lyrics(lyric_callback)

class LyricPane(object):
	def __init__(self, db, song_info):
		self.db = db
		self.song_info = song_info
		self.entry = self.song_info.get_property("current-entry")
		
		self.build_path()
		
		def save_lyrics(cache_path, text):
			f = file (cache_path, 'w')
			f.write (text)
			f.close ()
		
		def erase_lyrics(cache_path):
			f = file (cache_path, 'w')
			f.write ("")
			f.close ()
		
		def save_callback():
			buf = self.buffer
			startiter = buf.get_start_iter()
			enditer = buf.get_end_iter()
			text = buf.get_text(startiter, enditer)
			save_lyrics(self.cache_path, text)
			self.get_lyrics()
		
		def edit_callback(widget):
			if self.edit.get_active() == 1:
				self.tview.set_editable(True)
				self.edit.set_label("_Save")
			else:
				if self.cache_path is not None:
					save_callback()
				self.tview.set_editable(False)
				self.edit.set_label("_Edit")

		def discard_callback(widget):
			if self.cache_path is not None and os.path.exists(self.cache_path):
				os.remove(self.cache_path)
			self.get_lyrics()
		
		def clear_callback(widget):
			if self.cache_path is not None and os.path.exists (self.cache_path):
				erase_lyrics(self.cache_path)
			self.get_lyrics()
	   

		self.edit = gtk.ToggleButton("_Edit")
		self.edit.connect('toggled', edit_callback)
		self.discard = gtk.Button("_Search again")
		self.discard.connect('clicked', discard_callback)
		self.clear = gtk.Button(stock=gtk.STOCK_CLEAR)
		self.clear.connect('clicked', clear_callback)
		self.hbox = gtk.HBox()
		self.hbox.pack_end(self.edit, expand=False)
		self.hbox.pack_end(self.clear, expand=False)
		self.hbox.pack_start(self.discard, expand=False)
		
		(self.view, self.buffer, self.tview) = create_lyrics_view()

		self.view.pack_start(self.hbox, expand=False, fill=False)
		self.view.set_spacing(2)
	
		self.view.show_all()
		self.page_num = song_info.append_page(_("Lyrics"), self.view)
		self.have_lyrics = 0
		self.visible = 0

		self.entry_change_id = song_info.connect('notify::current-entry', self.entry_changed)
		nb = self.view.get_parent()
		self.switch_page_id = nb.connect('switch-page', self.switch_page_cb)
		
		#self.get_lyrics()

	def build_path(self):

		artist = self.db.entry_get(self.entry, rhythmdb.PROP_ARTIST).lower()
		title = self.db.entry_get(self.entry, rhythmdb.PROP_TITLE).lower()
		(artist, title) = parse_song_data(artist, title)
		cache_path = build_cache_path(artist, title)
		self.cache_path = cache_path

	def entry_changed(self, pspec, duh):
		self.entry = self.song_info.get_property("current-entry")
		self.have_lyrics = 0
		if self.visible != 0:
			self.build_path()
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
		lyrics_grabber = LyricGrabber(self.db, self.entry)
		lyrics_grabber.search_lyrics(self.buffer.set_text)
			

class LyricWindow (gtk.Window):

	def __init__(self):
		gtk.Window.__init__(self)
		self.set_border_width(12)

		close = gtk.Button(stock=gtk.STOCK_CLOSE)
		close.connect('clicked', lambda w: self.destroy())
	
		(lyrics_view, buffer, tview) = create_lyrics_view()
		self.buffer = buffer
		bbox = gtk.HButtonBox()
		bbox.set_layout(gtk.BUTTONBOX_END)
		bbox.pack_start(close)
		lyrics_view.pack_start(bbox, expand=False)
	
		self.add(lyrics_view)
		self.set_default_size(400, 300)
		self.show_all()

	def s_title(self, title, artist):
		self.set_title(title + " - " + artist + " - Lyrics")

class LyricsDisplayPlugin(rb.Plugin):

	def __init__ (self):
		rb.Plugin.__init__ (self)
		self.window = None

	def activate (self, shell):
		self.shell = shell
		self.action = gtk.Action ('ViewSongLyrics', _('Song L_yrics'),
					  _('Display lyrics for the playing song'),
					  'rb-song-lyrics')
		self.activate_id = self.action.connect ('activate', self.show_song_lyrics, shell)
		
		self.action_group = gtk.ActionGroup ('SongLyricsPluginActions')
		self.action_group.add_action_with_accel (self.action, "<control>L")
		
		uim = shell.get_ui_manager ()
		uim.insert_action_group (self.action_group, 0)
		self.ui_id = uim.add_ui_from_string (ui_str)
		uim.ensure_update ()

		sp = shell.get_player ()
		self.pec_id = sp.connect('playing-song-changed', self.playing_entry_changed)
		self.current_entry = None
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
		shell.disconnect (self.csi_id)

		if self.window is not None:
			self.window.destroy ()
			self.window = None

	def create_configure_dialog(self, dialog=None):
		if not dialog:
			glade_file = self.find_file("lyrics-prefs.glade")
			dialog = LyricsConfigureDialog (glade_file, gconf_keys).get_dialog()
		dialog.present()
		return dialog
	
	def playing_entry_changed (self, sp, entry):
		if entry is not None:
			self.action.set_sensitive (True)
			self.update_song_lyrics(entry)
		else:
			self.action.set_sensitive (False)

	def update_song_lyrics(self, entry):
		if entry == self.current_entry:
			return
		
		db = self.shell.get_property ("db")
		
		if self.window is None:
			return

		title = db.entry_get(entry, rhythmdb.PROP_TITLE)
		artist = db.entry_get(entry, rhythmdb.PROP_ARTIST)

		self.window.s_title(title, artist)
		lyrics_grabber = LyricGrabber(db, entry)
		lyrics_grabber.search_lyrics(self.window.buffer.set_text)

	def show_song_lyrics (self, action, shell):

		if self.window is not None:
			self.window.destroy ()
			self.window = None

		db = shell.get_property ("db")
		sp = shell.get_player ()
		entry = sp.get_playing_entry ()

		if entry is None:
			return
		
		title = db.entry_get(entry, rhythmdb.PROP_TITLE)
		artist = db.entry_get(entry, rhythmdb.PROP_ARTIST)

		self.window = LyricWindow()
		self.window.s_title(title, artist)
		self.window.connect("destroy", self.window_deleted)
		lyrics_grabber = LyricGrabber(db, entry)
		lyrics_grabber.search_lyrics(self.window.buffer.set_text)

	def window_deleted (self, window):
		print "lyrics window destroyed"
		self.window = None
	
	def create_song_info (self, shell, song_info, is_multiple):

		if is_multiple is False:
			x = LyricPane(shell.get_property ("db"), song_info)
