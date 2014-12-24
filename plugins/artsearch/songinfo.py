# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2014  Jonathan Matthew
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

from gi.repository import RB, Gtk

class AlbumArtPage(object):
	def __init__(self, shell, song_info):
		self.visible = False
		self.art_key = None

		self.shell = shell
		self.song_info = song_info
		self.entry = song_info.props.current_entry
		self.art_store = RB.ExtDB(name="album-art")
		self.art_store.connect("added", self.art_added_cb)

		grid = Gtk.Grid(hexpand=True, vexpand=True, margin=6, row_spacing=6)

		self.image = RB.FadingImage(fallback="rhythmbox-missing-artwork", use_tooltip=False)
		self.image.props.hexpand = True
		self.image.props.vexpand = True
		grid.attach(self.image, 0, 0, 1, 1)

		buttons = Gtk.ButtonBox(orientation=Gtk.Orientation.HORIZONTAL)
		buttons.set_spacing(6)
		buttons.set_layout(Gtk.ButtonBoxStyle.CENTER)
		grid.attach(buttons, 0, 1, 1, 1)

		clear = Gtk.Button(label=_("Clear"), use_underline=True)
		clear.connect('clicked', self.clear_button_cb)
		buttons.add(clear)

		fetch = Gtk.Button(label=_("_Fetch"), use_underline=True)
		fetch.connect('clicked', self.fetch_button_cb)
		buttons.add(fetch)

		browse_file = Gtk.Button(label=_("_Browse"), use_underline=True)
		browse_file.connect('clicked', self.browse_button_cb)
		buttons.add(browse_file)

		self.page_num = song_info.append_page(_("Album Art"), grid)

		self.ec_id = song_info.connect("notify::current-entry", self.entry_changed_cb)
		self.sp_id = grid.get_parent().connect("switch-page", self.switch_page_cb)

	def art_update(self, key, data):
		db = self.shell.props.db
		if db.entry_matches_ext_db_key(self.entry, key):
			self.image.set_pixbuf(data)
			self.art_key = key

	def art_added_cb(self, db, key, filename, data):
		print("art added?")
		self.art_update(key, data)

	def art_request_cb(self, key, skey, filename, data):
		print("art request finished?")
		self.art_update(skey, data)

	def get_art(self, entry, user_explicit=False):
		self.image.start(100)
		key = entry.create_ext_db_key(RB.RhythmDBPropType.ALBUM)
		if user_explicit:
			key.add_info("user-explicit", "true")
		self.art_store.request(key, self.art_request_cb)

	def storage_key(self, entry):
		key = RB.ExtDBKey.create_storage("album", entry.get_string(RB.RhythmDBPropType.ALBUM))
		artist = entry.get_string(RB.RhythmDBPropType.ALBUM_ARTIST)
		if artist is None or artist == "" or artist == _("Unknown"):
			artist = entry.get_string(RB.RhythmDBPropType.ARTIST)
		key.add_field("artist", artist)
		return key

	def entry_changed_cb(self, pspec, duh):
		self.entry = self.song_info.props.current_entry

		db = self.shell.props.db
		if self.art_key and db.entry_matches_ext_db_key(self.entry, self.art_key):
			return

		self.art_key = None
		if self.visible:
			self.get_art(self.entry, False)

	def switch_page_cb(self, notebook, page, page_num):
		if self.art_key is not None:
			return

		self.visible = (page_num == self.page_num)
		if self.visible:
			self.get_art(self.entry, False)

	def clear_button_cb(self, button):
		key = self.storage_key(self.entry)
		self.art_store.store(key, RB.ExtDBSourceType.USER_EXPLICIT, None)

	def fetch_button_cb(self, button):
		if self.art_key is not None:
			self.art_store.delete(self.art_key)
		self.get_art(self.entry, True)

	def browse_file_response_cb(self, dialog, response):
		if response == Gtk.ResponseType.OK:
			key = self.storage_key(self.entry)
			self.art_store.store_uri(key, RB.ExtDBSourceType.USER_EXPLICIT, dialog.get_uri())

		dialog.destroy()

	def browse_button_cb(self, button):
		d = Gtk.FileChooserDialog(_("Select new artwork"), self.shell.props.window, Gtk.FileChooserAction.OPEN)
		d.add_button(_("_Cancel"), Gtk.ResponseType.CANCEL)
		d.add_button(_("_Select"), Gtk.ResponseType.OK)
		d.set_default_response(Gtk.ResponseType.OK)
		d.connect("response", self.browse_file_response_cb)
		d.show_all()
