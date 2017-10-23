# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2014 Jonathan Matthew
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

from gi.repository import Gtk, Gdk, GObject, Gio, GLib, Peas
from gi.repository import RB
import rb
import urllib.parse
import json
from datetime import datetime

import gettext
gettext.install('rhythmbox', RB.locale_dir())

# rhythmbox app registered with soundcloud with the account notverysmart@gmail.com
CLIENT_ID = 'e4ef6572c2baf401db2f64b4e0eae9ce'

class SoundCloudEntryType(RB.RhythmDBEntryType):
	def __init__(self):
		RB.RhythmDBEntryType.__init__(self, name='soundcloud')

	def do_get_playback_uri(self, entry):
		uri = entry.get_string(RB.RhythmDBPropType.MOUNTPOINT)
		return uri + "?client_id=" + CLIENT_ID

	def do_can_sync_metadata(self, entry):
		return False


class SoundCloudPlugin(GObject.Object, Peas.Activatable):
	__gtype_name = 'SoundCloudPlugin'
	object = GObject.property(type=GObject.GObject)

	def __init__(self):
		GObject.Object.__init__(self)

	def do_activate(self):
		shell = self.object

		rb.append_plugin_source_path(self, "icons")

		db = shell.props.db

		self.entry_type = SoundCloudEntryType()
		db.register_entry_type(self.entry_type)

		model = RB.RhythmDBQueryModel.new_empty(db)
		self.source = GObject.new (SoundCloudSource,
					   shell=shell,
					   name=_("SoundCloud"),
					   plugin=self,
					   query_model=model,
					   entry_type=self.entry_type,
					   icon=Gio.ThemedIcon.new("soundcloud-symbolic"))
		shell.register_entry_type_for_source(self.source, self.entry_type)
		self.source.setup()
		group = RB.DisplayPageGroup.get_by_id ("shared")
		shell.append_display_page(self.source, group)

	def do_deactivate(self):
		self.source.delete_thyself()
		self.source = None

class SoundCloudSource(RB.StreamingSource):
	def __init__(self, **kwargs):
		super(SoundCloudSource, self).__init__(kwargs)
		self.loader = None
		self.container_loader = None
		self.container_marker_path = None

		self.search_count = 1
		self.search_types = {
			'tracks': {
				'label': _("Search tracks"),
				'placeholder': _("Search tracks on SoundCloud"),
				'title': "",	# container view is hidden
				'endpoint': '/tracks.json',
				'containers': False
			},
			'sets': {
				'label': _("Search sets"),
				'placeholder': _("Search sets on SoundCloud"),
				'title': _("SoundCloud Sets"),
				'endpoint': '/playlists.json',
				'containers': True
			},
			'users': {
				'label': _("Search users"),
				'placeholder': _("Search users on SoundCloud"),
				'title': _("SoundCloud Users"),
				'endpoint': '/users.json',
				'containers': True
			},
			'groups': {
				'label': _("Search groups"),
				'placeholder': _("Search groups on SoundCloud"),
				'title': _("SoundCloud Groups"),
				'endpoint': '/groups.json',
				'containers': True
			},
		}

		self.container_types = {
			'user': {
				'attributes': ['username', 'kind', 'uri', 'permalink_url', 'avatar_url', 'description'],
				'tracks-url': '/tracks.json',
				'tracks-type': 'plain',
			},
			'playlist': {
				'attributes': ['title', 'kind', 'uri', 'permalink_url', 'artwork_url', 'description'],
				'tracks-url': '.json',
				'tracks-type': 'playlist',
			},
			'group': {
				'attributes': ['name', 'kind', 'uri', 'permalink_url', 'artwork_url', 'description'],
				'tracks-url': '/tracks.json',
				'tracks-type': 'plain',
			}
		}

	def hide_entry_cb(self, entry):
		shell = self.props.shell
		shell.props.db.entry_set(entry, RB.RhythmDBPropType.HIDDEN, True)

	def new_model(self):
		shell = self.props.shell
		plugin = self.props.plugin
		db = shell.props.db

		self.search_count = self.search_count + 1
		q = GLib.PtrArray()
		db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.TYPE, plugin.entry_type)
		db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.LAST_SEEN, self.search_count)
		model = RB.RhythmDBQueryModel.new_empty(db)

		db.do_full_query_async_parsed(model, q)
		self.props.query_model = model
		self.songs.set_model(model)

	def add_track(self, db, entry_type, item):
		if not item['streamable']:
			return

		uri = item['permalink_url']
		entry = db.entry_lookup_by_location(uri)
		if entry:
			db.entry_set(entry, RB.RhythmDBPropType.LAST_SEEN, self.search_count)
		else:
			entry = RB.RhythmDBEntry.new(db, entry_type, item['permalink_url'])
			db.entry_set(entry, RB.RhythmDBPropType.MOUNTPOINT, item['stream_url'])
			db.entry_set(entry, RB.RhythmDBPropType.ARTIST, item['user']['username'])
			db.entry_set(entry, RB.RhythmDBPropType.TITLE, item['title'])
			db.entry_set(entry, RB.RhythmDBPropType.LAST_SEEN, self.search_count)
			if item['genre'] is not None:
				db.entry_set(entry, RB.RhythmDBPropType.GENRE, item['genre'])
			db.entry_set(entry, RB.RhythmDBPropType.DURATION, item['duration']/1000)
			if item['bpm'] is not None:
				db.entry_set(entry, RB.RhythmDBPropType.BEATS_PER_MINUTE, item['bpm'])

			if item['description'] is not None:
				db.entry_set(entry, RB.RhythmDBPropType.COMMENT, item['description'])

			if item['artwork_url'] is not None:
				db.entry_set(entry, RB.RhythmDBPropType.MB_ALBUMID, item['artwork_url'])

			if item['release_year'] is not None:
				date = GLib.Date.new_dmy(item.get('release_day', 0), item.get('release_month', 0), item['release_year'])
				db.entry_set(entry, RB.RhythmDBPropType.DATE, date.get_julian())

			if item['created_at'] is not None:
				try:
					dt = datetime.strptime(item['created_at'], '%Y/%m/%d %H:%M:%S %z')
					db.entry_set(entry, RB.RhythmDBPropType.FIRST_SEEN, int(dt.timestamp()))
				except Exception as e:
					print(str(e))

		db.commit()

	def add_container(self, item):
		k = item['kind']
		if k not in self.container_types:
			return

		ct = self.container_types[k]
		self.containers.append([item.get(i) for i in ct['attributes']])

	def add_container_marker(self):
		iter = self.containers.append(['...', None, None, None, None, None])
		self.container_marker_path = self.containers.get_path(iter)

	def remove_container_marker(self):
		if self.container_marker_path is None:
			return

		iter = self.containers.get_iter(self.container_marker_path)
		self.containers.remove(iter)
		self.container_marker_path = None

	def search_tracks_api_cb(self, data):
		if data is None:
			return

		shell = self.props.shell
		db = shell.props.db
		entry_type = self.props.entry_type

		data = data.decode('utf-8')
		stuff = json.loads(data)
		for item in stuff['collection']:
			self.add_track(db, entry_type, item)

		if 'next_href' in stuff:
			self.more_tracks_url = stuff.get('next_href')
			self.fetch_more_button.set_sensitive(True)


	def search_containers_api_cb(self, data):
		if data is None:
			return

		self.remove_container_marker()
		self.container_loader = None

		entry_type = self.props.entry_type

		data = data.decode('utf-8')
		stuff = json.loads(data)
		for item in stuff['collection']:
			self.add_container(item)

		if 'next_href' in stuff:
			self.add_container_marker()
			self.more_containers_url = stuff.get('next_href')


	def resolve_api_cb(self, data):
		if data is None:
			return

		data = data.decode('utf-8')
		stuff = json.loads(data)

		if stuff['kind'] == 'track':
			shell = self.props.shell
			db = shell.props.db
			self.add_track(db, self.props.entry_type, stuff)
		else:
			self.add_container(stuff)
			# select, etc. too?

	def playlist_api_cb(self, data):
		if data is None:
			return

		shell = self.props.shell
		db = shell.props.db

		data = data.decode('utf-8')
		stuff = json.loads(data)
		for t in stuff['tracks']:
			self.add_track(db, self.props.entry_type, t)

	def cancel_request(self, cancel_containers=False):
		if self.loader:
			self.loader.cancel()
			self.loader = None

		if self.container_loader and cancel_containers:
			self.container_loader.cancel()
			self.container_loader = None

	def search_popup_cb(self, widget):
		self.search_popup.popup(None, None, None, None, 3, Gtk.get_current_event_time())

	def search_type_action_cb(self, action, parameter):
		print(parameter.get_string() + " selected")
		self.search_type = parameter.get_string()

		if self.search_entry.searching():
			self.do_search()

		st = self.search_types[self.search_type]
		self.search_entry.set_placeholder(st['placeholder'])

	def search_entry_cb(self, widget, term):
		self.search_text = term
		self.do_search()

	def show_more_cb(self, button):
		button.set_sensitive(False)
		if self.more_tracks_url:
			self.cancel_request(False)
			print("fetching more tracks")
			self.loader = rb.Loader()
			self.loader.get_url(self.more_tracks_url, self.search_tracks_api_cb)

	def do_search(self):
		self.cancel_request()

		base = 'https://api.soundcloud.com'
		self.new_model()
		self.containers.clear()
		self.more_tracks_url = None
		self.more_containers_url = None
		self.fetch_more_button.set_sensitive(False)
		term = self.search_text

		if term.startswith('https://soundcloud.com/') or term.startswith("http://soundcloud.com/"):
			# ignore the selected search type and try to resolve whatever the url is
			print("resolving " + term)
			self.scrolled.hide()
			url = base + '/resolve.json?url=' + term + '&client_id=' + CLIENT_ID
			self.loader = rb.Loader()
			self.loader.get_url(url, self.resolve_api_cb)
			return

		if self.search_type not in self.search_types:
			print("not sure how to search for " + self.search_type)
			return

		print("searching for " + self.search_type + " matching " + term)
		st = self.search_types[self.search_type]
		self.container_view.get_column(0).set_title(st['title'])

		url = base + st['endpoint'] + '?q=' + urllib.parse.quote(term) + '&linked_partitioning=1&limit=100&client_id=' + CLIENT_ID
		self.loader = rb.Loader()
		if st['containers']:
			self.scrolled.show()
			self.loader.get_url(url, self.search_containers_api_cb)
		else:
			self.scrolled.hide()
			self.loader.get_url(url, self.search_tracks_api_cb)


	def selection_changed_cb(self, selection):
		self.new_model()
		self.cancel_request()
		self.build_sc_menu()

		(model, aiter) = selection.get_selected()
		if aiter is None:
			return

		if self.container_marker_path is not None:
			apath = self.containers.get_path(aiter)
			if apath.compare(self.container_marker_path) == 0:
				print("marker row selected")
				return

		[itemtype, url] = model.get(aiter, 1, 2)
		if itemtype not in self.container_types:
			return

		print("loading %s %s" % (itemtype, url))
		ct = self.container_types[itemtype]
		trackurl = url + ct['tracks-url'] + '?linked_partitioning=1&client_id=' + CLIENT_ID

		self.loader = rb.Loader()
		if ct['tracks-type'] == 'playlist':
			self.loader.get_url(trackurl, self.playlist_api_cb)
		else:
			self.loader.get_url(trackurl, self.search_tracks_api_cb)

	def maybe_more_containers(self):
		self.more_containers_idle = 0
		if self.container_loader is not None:
			return False

		(start, end) = self.container_view.get_visible_range()
		if self.container_marker_path.compare(end) == 1:
			return False

		self.container_loader = rb.Loader()
		self.container_loader.get_url(self.more_containers_url, self.search_containers_api_cb)
		return False

	def scroll_adjust_changed_cb(self, adjust):
		if self.more_containers_url is None:
			return

		if self.more_containers_idle == 0:
			self.more_containers_idle = GLib.idle_add(self.maybe_more_containers)

	def sort_order_changed_cb(self, obj, pspec):
		obj.resort_model()

	def songs_selection_changed_cb(self, songs):
		self.build_sc_menu()

	def playing_entry_changed_cb(self, player, entry):
		self.build_sc_menu()
		if not entry:
			return
		if entry.get_entry_type() != self.props.entry_type:
			return

		au = entry.get_string(RB.RhythmDBPropType.MB_ALBUMID)
		if au:
			key = RB.ExtDBKey.create_storage("title", entry.get_string(RB.RhythmDBPropType.TITLE))
			key.add_field("artist", entry.get_string(RB.RhythmDBPropType.ARTIST))
			self.art_store.store_uri(key, RB.ExtDBSourceType.EMBEDDED, au)

	def open_uri_action_cb(self, action, param):
		shell = self.props.shell
		window = shell.props.window
		screen = window.get_screen()

		uri = param.get_string()
		Gtk.show_uri(screen, uri, Gdk.CURRENT_TIME)

	def build_sc_menu(self):
		menu = {}

		# playing track
		shell = self.props.shell
		player = shell.props.shell_player
		entry = player.get_playing_entry()
		if entry is not None and entry.get_entry_type() == self.props.entry_type:
			url = entry.get_string(RB.RhythmDBPropType.LOCATION)
			menu[url] = _("View '%(title)s' on SoundCloud") % {'title': entry.get_string(RB.RhythmDBPropType.TITLE) }
			# artist too?


		# selected track
		if self.songs.have_selection():
			entry = self.songs.get_selected_entries()[0]
			url = entry.get_string(RB.RhythmDBPropType.LOCATION)
			menu[url] = _("View '%(title)s' on SoundCloud") % {'title': entry.get_string(RB.RhythmDBPropType.TITLE) }
			# artist too?

		# selected container
		selection = self.container_view.get_selection()
		(model, aiter) = selection.get_selected()
		if aiter is not None:
			[name, url] = model.get(aiter, 0, 3)
			menu[url] = _("View '%(container)s' on SoundCloud") % {'container': name}

		if len(menu) == 0:
			self.sc_button.set_menu_model(None)
			self.sc_button.set_sensitive(False)
			return None

		m = Gio.Menu()
		for u in menu:
			i = Gio.MenuItem()
			i.set_label(menu[u])
			i.set_action_and_target_value("win.soundcloud-open-uri", GLib.Variant.new_string(u))
			m.append_item(i)
		self.sc_button.set_menu_model(m)
		self.sc_button.set_sensitive(True)

	def setup(self):
		shell = self.props.shell

		builder = Gtk.Builder()
		builder.add_from_file(rb.find_plugin_file(self.props.plugin, "soundcloud.ui"))

		self.scrolled = builder.get_object("container-scrolled")
		self.scrolled.set_no_show_all(True)
		self.scrolled.hide()

		self.more_containers_idle = 0
		adj = self.scrolled.get_vadjustment()
		adj.connect("changed", self.scroll_adjust_changed_cb)
		adj.connect("value-changed", self.scroll_adjust_changed_cb)

		self.search_entry = RB.SearchEntry(spacing=6)
		self.search_entry.props.explicit_mode = True

		self.fetch_more_button = Gtk.Button.new_with_label(_("Fetch more tracks"))
		self.fetch_more_button.connect("clicked", self.show_more_cb)

		action = Gio.SimpleAction.new("soundcloud-search-type", GLib.VariantType.new('s'))
		action.connect("activate", self.search_type_action_cb)
		shell.props.window.add_action(action)

		m = Gio.Menu()
		for st in sorted(self.search_types):
			i = Gio.MenuItem()
			i.set_label(self.search_types[st]['label'])
			i.set_action_and_target_value("win.soundcloud-search-type", GLib.Variant.new_string(st))
			m.append_item(i)

		self.search_popup = Gtk.Menu.new_from_model(m)

		action.activate(GLib.Variant.new_string("tracks"))

		grid = builder.get_object("soundcloud-source")

		self.search_entry.connect("search", self.search_entry_cb)
		self.search_entry.connect("activate", self.search_entry_cb)
		self.search_entry.connect("show-popup", self.search_popup_cb)
		self.search_entry.set_size_request(400, -1)

		searchbox = builder.get_object("search-box")
		searchbox.pack_start(self.search_entry, False, True, 0)
		searchbox.pack_start(self.fetch_more_button, False, True, 0)


		self.search_popup.attach_to_widget(self.search_entry, None)

		self.containers = builder.get_object("container-store")
		self.container_view = builder.get_object("containers")
		self.container_view.set_model(self.containers)

		action = Gio.SimpleAction.new("soundcloud-open-uri", GLib.VariantType.new('s'))
		action.connect("activate", self.open_uri_action_cb)
		shell.props.window.add_action(action)

		r = Gtk.CellRendererText()
		c = Gtk.TreeViewColumn("", r, text=0)
		self.container_view.append_column(c)

		self.container_view.get_selection().connect('changed', self.selection_changed_cb)

		self.songs = RB.EntryView(db=shell.props.db,
					  shell_player=shell.props.shell_player,
					  is_drag_source=True,
					  is_drag_dest=False)
		self.songs.append_column(RB.EntryViewColumn.TITLE, True)
		self.songs.append_column(RB.EntryViewColumn.ARTIST, True)
		self.songs.append_column(RB.EntryViewColumn.DURATION, True)
		self.songs.append_column(RB.EntryViewColumn.YEAR, False)
		self.songs.append_column(RB.EntryViewColumn.GENRE, False)
		self.songs.append_column(RB.EntryViewColumn.BPM, False)
		self.songs.append_column(RB.EntryViewColumn.FIRST_SEEN, False)
		self.songs.set_model(self.props.query_model)
		self.songs.connect("notify::sort-order", self.sort_order_changed_cb)
		self.songs.connect("selection-changed", self.songs_selection_changed_cb)

		paned = builder.get_object("paned")
		paned.pack2(self.songs)

		self.bind_settings(self.songs, paned, None, True)

		self.sc_button = Gtk.MenuButton()
		self.sc_button.set_relief(Gtk.ReliefStyle.NONE)
		img = Gtk.Image.new_from_file(rb.find_plugin_file(self.props.plugin, "powered-by-soundcloud.png"))
		self.sc_button.add(img)
		box = builder.get_object("soundcloud-button-box")
		box.pack_start(self.sc_button, True, True, 0)

		self.build_sc_menu()

		self.pack_start(grid, expand=True, fill=True, padding=0)
		grid.show_all()

		self.art_store = RB.ExtDB(name="album-art")
		player = shell.props.shell_player
		player.connect('playing-song-changed', self.playing_entry_changed_cb)

	def do_get_entry_view(self):
		return self.songs

	def do_get_playback_status(self, text, progress):
		return self.get_progress()

	def do_can_copy(self):
		return False

	def do_can_pause(self):
		return True

GObject.type_register(SoundCloudSource)
