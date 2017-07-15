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

import os, re
import urllib.request

import rb
from gi.repository import Gtk, Gio, GObject, Peas
from gi.repository import RB
from gi.repository import Gst, GstPbutils

import LyricsParse
from LyricsConfigureDialog import LyricsConfigureDialog

import gettext
gettext.install('rhythmbox', RB.locale_dir())


LYRIC_TITLE_STRIP=["\(live[^\)]*\)", "\(acoustic[^\)]*\)", "\([^\)]*mix\)", "\([^\)]*version\)", "\([^\)]*edit\)", "\(feat[^\)]*\)"]
LYRIC_TITLE_REPLACE=[("/", "-"), (" & ", " and ")]
LYRIC_ARTIST_REPLACE=[("/", "-"), (" & ", " and ")]
STREAM_SONG_TITLE='rb:stream-song-title'

def create_lyrics_view():
	tview = Gtk.TextView()
	tview.set_wrap_mode(Gtk.WrapMode.WORD)
	tview.set_editable(False)
	tview.set_left_margin(6)

	tview.set_size_request (0, 0)
	sw = Gtk.ScrolledWindow()
	sw.add(tview)
	sw.set_policy(Gtk.PolicyType.NEVER, Gtk.PolicyType.AUTOMATIC)
	sw.set_shadow_type(Gtk.ShadowType.IN)

	vbox = Gtk.VBox(spacing=12)
	vbox.pack_start(sw, True, True, 0)
	
	return (vbox, tview.get_buffer(), tview)

def parse_song_data(db, entry):
	(artist, title) = get_artist_and_title(db, entry)

	# don't search for 'unknown' when we don't have the artist or title information
	if artist == _("Unknown"):
		artist = ""
	if title == _("Unknown"):
		title = ""

	# convert to lowercase
	artist = artist.lower()
	title = title.lower()
	
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

def get_artist_and_title(db, entry):
	stream_song_title = db.entry_request_extra_metadata(entry, STREAM_SONG_TITLE)
	if stream_song_title is not None:
		(artist, title) = extract_artist_and_title(stream_song_title)
	else:
		artist = entry.get_string(RB.RhythmDBPropType.ARTIST)
		title = entry.get_string(RB.RhythmDBPropType.TITLE)
	return (artist, title)

def extract_artist_and_title(stream_song_title):
	details = stream_song_title.split('-')
	if len(details) > 1:
		artist = details[0].strip()
		title = details[1].strip()
	else:
		details = stream_song_title.split('(')
		if len(details) > 1:
			title = details[0].strip()
			artist = details[1].strip(') ')
		else:
			title = stream_song_title
			artist = ""
	return (artist, title)
	
def build_cache_path(artist, title):
	settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.lyrics")
	folder = settings['folder']
	if folder is None or folder == "":
		folder = os.path.join(RB.user_cache_dir(), "lyrics")

	lyrics_folder = os.path.expanduser (folder)
	if not os.path.exists (lyrics_folder):
		os.mkdir (lyrics_folder)

	artist_folder = os.path.join(lyrics_folder, artist[:128])
	if not os.path.exists (artist_folder):
		os.mkdir (artist_folder)

	return os.path.join(artist_folder, title[:128] + '.lyric')

class LyricGrabber(object):
	"""
	Fetch lyrics from several sources.

	1. Local cache file
	2. Lyric tags in file meta data
	3. Online services
	"""
	def __init__(self, db, entry):
		self.db = db
		self.entry = entry

		(self.artist, self.title) = parse_song_data(self.db, self.entry)

		self.cache_path = build_cache_path(self.artist, self.title)

	def verify_lyric(self):
		return os.path.exists(self.cache_path)

	def search_lyrics(self, callback, cache_only=False):
		"""
		Fetch lyrics from cache.

		If no cache file exist, tag extraction is tried next.
		"""
		self.callback = callback

		status = self.verify_lyric()
		if status:
			f = open(self.cache_path, 'rt')
			text = f.read()
			f.close()
			self.callback(text)
		elif cache_only:
			self.callback(_("No lyrics found"))
		else:
			self.search_tags()

	def search_tags(self):
		"""
		Initiate fetching meta tags.

		Result will be handled in search_tags_result
		"""
		location = self.entry.get_playback_uri()
		self.discoverer = GstPbutils.Discoverer(timeout=Gst.SECOND*3)
		self.discoverer.connect('discovered', self.search_tags_result)
		self.discoverer.start()
		self.discoverer.discover_uri_async(location)

	def search_tags_result(self, discoverer, info, error):
		"""
		Extract lyrics from the file meta data (tags).

		If no lyrics tags are found, online services are tried next.

		Supported file formats and lyrics tags:
		- ogg/vorbis files with "LYRICS" and "SYNCLYRICS" tag
		"""
		tags = info.get_tags()
		if tags is None:
			self.search_online()
			return

		for i in range(tags.get_tag_size("extended-comment")):
			(exists, value) = tags.get_string_index("extended-comment", i)
			#ogg/vorbis unsynchronized lyrics
			if exists and value.startswith("LYRICS"):
				text = value.replace("LYRICS=", "")
				self.lyrics_found(text)
				return
			#ogg/vorbis synchronized lyrics
			elif exists and value.startswith("SYNCLYRICS"):
				text = value.replace("SYNCLYRICS=", "")
				self.lyrics_found(text)
				return

		self.search_online()

	def search_online(self):
		"""Initiate searching the online lyrics services"""
		if self.artist == "" and self.title == "":
			self.callback(_("No lyrics found"))
		else:
			parser = LyricsParse.Parser(self.artist, self.title)
			parser.get_lyrics(self.search_online_result)

	def search_online_result(self, text):
		"""Handle the result of searching online lyrics services"""
		if text is not None:
			self.lyrics_found(text)
		else:
			self.callback(_("No lyrics found"))


	def lyrics_found(self, text):
		f = open(self.cache_path, 'wt')
		f.write(text)
		f.close()

		self.callback(text)


class LyricPane(object):
	def __init__(self, db, song_info):
		self.db = db
		self.song_info = song_info
		self.entry = self.song_info.props.current_entry
		
		self.build_path()
		
		def save_lyrics(cache_path, text):
			f = open(cache_path, 'wt')
			f.write(text)
			f.close()
		
		def erase_lyrics(cache_path):
			f = open(cache_path, 'w')
			f.write("")
			f.close()
		
		def save_callback():
			buf = self.buffer
			startiter = buf.get_start_iter()
			enditer = buf.get_end_iter()
			text = buf.get_text(startiter, enditer, True)
			save_lyrics(self.cache_path, text)
			self.get_lyrics()
		
		def edit_callback(widget):
			if self.edit.get_active() == 1:
				self.tview.set_editable(True)
				self.edit.set_label(_("_Save"))
			else:
				if self.cache_path is not None:
					save_callback()
				self.tview.set_editable(False)
				self.edit.set_label(_("_Edit"))

		def discard_callback(widget):
			if self.cache_path is not None and os.path.exists(self.cache_path):
				os.remove(self.cache_path)
			self.get_lyrics()
		
		def clear_callback(widget):
			if self.cache_path is not None and os.path.exists (self.cache_path):
				erase_lyrics(self.cache_path)
			self.get_lyrics()
	   

		self.edit = Gtk.ToggleButton(label=_("_Edit"), use_underline=True)
		self.edit.connect('toggled', edit_callback)
		self.discard = Gtk.Button(label=_("_Search again"), use_underline=True)
		self.discard.connect('clicked', discard_callback)
		self.clear = Gtk.Button.new_from_stock(Gtk.STOCK_CLEAR)
		self.clear.connect('clicked', clear_callback)
		self.hbox = Gtk.ButtonBox(orientation=Gtk.Orientation.HORIZONTAL)
		self.hbox.set_spacing (6)
		self.hbox.set_layout(Gtk.ButtonBoxStyle.END)
		self.hbox.add(self.edit)
		self.hbox.add(self.clear)
		self.hbox.add(self.discard)
		self.hbox.set_child_secondary (self.clear, True)

		(self.view, self.buffer, self.tview) = create_lyrics_view()

		self.view.pack_start(self.hbox, False, False, 0)
		self.view.set_spacing(6)
		self.view.props.margin = 6
	
		self.view.show_all()
		self.page_num = song_info.append_page(_("Lyrics"), self.view)
		self.have_lyrics = 0
		self.visible = 0

		self.entry_change_id = song_info.connect('notify::current-entry', self.entry_changed)
		nb = self.view.get_parent()
		self.switch_page_id = nb.connect('switch-page', self.switch_page_cb)
		
		#self.get_lyrics()

	def build_path(self):
		(artist, title) = parse_song_data(self.db, self.entry)
		cache_path = build_cache_path(artist, title)
		self.cache_path = cache_path

	def entry_changed(self, pspec, duh):
		self.entry = self.song_info.props.current_entry
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

	def __got_lyrics(self, text):
		self.buffer.set_text(text, -1)

	def get_lyrics(self):
		if self.entry is None:
			return

		self.buffer.set_text(_("Searching for lyrics..."), -1);
		lyrics_grabber = LyricGrabber(self.db, self.entry)
		lyrics_grabber.search_lyrics(self.__got_lyrics)


class LyricWindow (Gtk.Window):

	def __init__(self, shell):
		Gtk.Window.__init__(self)
		self.shell = shell
		self.set_border_width(12)

		close = Gtk.Button.new_from_stock(Gtk.STOCK_CLOSE)
		close.connect('clicked', lambda w: self.destroy())
	
		(lyrics_view, buffer, tview) = create_lyrics_view()
		self.buffer = buffer
		bbox = Gtk.HButtonBox()
		bbox.set_layout(Gtk.ButtonBoxStyle.END)
		bbox.pack_start(close, True, True, 0)
		lyrics_view.pack_start(bbox, False, False, 0)

		sp = shell.props.shell_player
		self.ppc_id = sp.connect('playing-song-property-changed', self.playing_property_changed)
	
		self.add(lyrics_view)
		self.set_default_size(400, 300)
		self.show_all()

	def destroy(self):
		sp = self.shell.props.shell_player
		sp.disconnect (self.ppc_id)
		Gtk.Window.destroy(self)

	def playing_property_changed(self, player, uri, prop, old_val, new_val):
		if (prop == STREAM_SONG_TITLE):
			self.update_song_lyrics(player.get_playing_entry())

	
	def __got_lyrics(self, text):
		self.buffer.set_text (text, -1)

	def update_song_lyrics(self, entry):
		db = self.shell.props.db
		(artist, title) = get_artist_and_title(db, entry)
		self.set_title(title + " - " + artist + " - " + _("Lyrics"))
		lyrics_grabber = LyricGrabber(db, entry)
		lyrics_grabber.search_lyrics(self.__got_lyrics)


class LyricsDisplayPlugin(GObject.Object, Peas.Activatable):
	__gtype_name__ = 'LyricsDisplayPlugin'
	object = GObject.property(type=GObject.Object)

	def __init__ (self):
		GObject.Object.__init__ (self)
		self.window = None

	def do_activate (self):
		shell = self.object

		self.action = Gio.SimpleAction.new("view-lyrics", None)
		self.action.connect("activate", self.show_song_lyrics, shell)
		# set accelerator?
		window = shell.props.window
		window.add_action(self.action)

		app = shell.props.application
		item = Gio.MenuItem.new(label=_("Song Lyrics"), detailed_action="win.view-lyrics")
		app.add_plugin_menu_item("view", "view-lyrics", item)

		sp = shell.props.shell_player
		self.pec_id = sp.connect('playing-song-changed', self.playing_entry_changed)
		self.playing_entry_changed (sp, sp.get_playing_entry ())

		self.csi_id = shell.connect('create_song_info', self.create_song_info)

		db = shell.props.db
		self.lyric_req_id = db.connect_after ('entry-extra-metadata-request::rb:lyrics', self.lyrics_request)

	def do_deactivate (self):
		shell = self.object
			
		app = shell.props.application
		app.remove_plugin_menu_item("view", "view-lyrics")
		app.remove_action("view-lyrics")
		self.action = None

		sp = shell.props.shell_player
		sp.disconnect (self.pec_id)

		shell.disconnect (self.csi_id)

		shell.props.db.disconnect (self.lyric_req_id)

		if self.window is not None:
			self.window.destroy ()
			self.window = None


	def show_song_lyrics (self, action, parameter, shell):

		if self.window is not None:
			self.window.destroy ()
			self.window = None

		sp = shell.props.shell_player
		entry = sp.get_playing_entry ()

		if entry is not None:
			self.window = LyricWindow(shell)
			self.window.connect("destroy", self.window_deleted)
			self.window.update_song_lyrics(entry)

	def playing_entry_changed (self, sp, entry):
		if entry is not None:
			self.action.set_enabled (True)
			if self.window is not None:
				self.window.update_song_lyrics(entry)
		else:
			self.action.set_enabled (False)

	def window_deleted (self, window):
		print("lyrics window destroyed")
		self.window = None
	
	def create_song_info (self, shell, song_info, is_multiple):
		if is_multiple is False:
			x = LyricPane(shell.props.db, song_info)

	def lyrics_request (self, db, entry):
		def lyrics_results(text):
			if text:
				db.emit_entry_extra_metadata_notify (entry, 'rb:lyrics', text)

		lyrics_grabber = LyricGrabber(db, entry)
		lyrics_grabber.search_lyrics(lyrics_results)

