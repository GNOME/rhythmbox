# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2026 Jonathan Matthew
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

import gi
gi.require_version('Peas', '1.0')
gi.require_version('PeasGtk', '1.0')
gi.require_version('RB', '3.0')
from gi.repository import GLib, GLibUnix, GObject, Gio, Peas, PeasGtk, Gtk
from gi.repository import RB
import rb

import sys
import os
import json
import re
import time
import struct
import datetime

import gettext
gettext.install('rhythmbox', RB.locale_dir())

class MPDAck(object):
	IDLE = -1
	ASYNC = -2

	OK = 0

	NOT_LIST = 1
	ARG = 2
	PASSWORD = 3
	PERMISSION = 4
	UNKNOWN = 5

	NO_EXIST = 50
	PLAYLIST_MAX = 51
	SYSTEM = 52
	PLAYLIST_LOAD = 53
	UPDATE_ALREADY = 54
	PLAYER_SYNC = 55
	EXIST = 56

class MPDError(Exception):
	def __init__(cmd, idx, err, msg):
		super().__init__()
		self.cmd = cmd
		self.idx = idx
		self.err = err
		self.msg = msg

string_props = {
	'Title': RB.RhythmDBPropType.TITLE,
	'TitleSort': RB.RhythmDBPropType.TITLE_SORTNAME,
	'Artist': RB.RhythmDBPropType.ARTIST,
	'ArtistSort': RB.RhythmDBPropType.MB_ARTISTSORTNAME,
	'Album': RB.RhythmDBPropType.ALBUM,
	'AlbumSort': RB.RhythmDBPropType.ALBUM_SORTNAME,
	'AlbumArtist': RB.RhythmDBPropType.ALBUM_ARTIST,
	'AlbumArtistSort': RB.RhythmDBPropType.ALBUM_ARTIST_SORTNAME,
	'Genre': RB.RhythmDBPropType.GENRE,
	'Composer': RB.RhythmDBPropType.COMPOSER,
	'ComposerSort': RB.RhythmDBPropType.COMPOSER_SORTNAME,
	'Comment': RB.RhythmDBPropType.COMMENT,
	'MUSICBRAINZ_TRACKID': RB.RhythmDBPropType.MB_TRACKID,
	'MUSICBRAINZ_ARTISTID': RB.RhythmDBPropType.MB_ARTISTID,
	'MUSICBRAINZ_ALBUMARTISTID': RB.RhythmDBPropType.MB_ALBUMARTISTID,
	'MUSICBRAINZ_ALBUMID': RB.RhythmDBPropType.MB_ALBUMID,
}
string_fold_props = {
	'Title': RB.RhythmDBPropType.TITLE_FOLDED,
	'Artist': RB.RhythmDBPropType.ARTIST_FOLDED,
	'Album': RB.RhythmDBPropType.ALBUM_FOLDED,
	'AlbumArtist': RB.RhythmDBPropType.ALBUM_ARTIST_FOLDED,
	'Genre': RB.RhythmDBPropType.GENRE_FOLDED,
	'Composer': RB.RhythmDBPropType.COMPOSER_FOLDED
}
ulong_props = {
	'Id': RB.RhythmDBPropType.ENTRY_ID,
	'Date': RB.RhythmDBPropType.YEAR,
	'Time': RB.RhythmDBPropType.DURATION,
	'duration': RB.RhythmDBPropType.DURATION,
	'Track': RB.RhythmDBPropType.TRACK_NUMBER,
	'Disc': RB.RhythmDBPropType.DISC_NUMBER,
}

class QueryResultsStats(GObject.Object, RB.RhythmDBQueryResults):
	def __init__(self, callback, data):
		super().__init__()
		self.callback = callback
		self.data = data
		self.idle = 0
		self.songs = 0
		self.duration = 0
		self.artists = set()
		self.albums = set()

	def cancel(self):
		if self.idle != 0:
			GLib.source_remove(self.idle)
			self.idle = 0

	def do_add_results(self, entries):
		for e in entries:
			self.songs += 1
			self.duration += e.get_ulong(RB.RhythmDBPropType.DURATION)
			self.artists.add(e.get_string(RB.RhythmDBPropType.ARTIST))
			self.albums.add(e.get_string(RB.RhythmDBPropType.ALBUM))

	def complete_callback(self):
		self.idle = 0
		self.callback(self, self.data)
		return GLib.SOURCE_REMOVE

	def do_query_complete(self):
		self.idle = GLib.idle_add(self.complete_callback)

	def do_set_query(self, query):
		pass

GObject.type_register(QueryResultsStats)

class QueryResultsGroupbyTree(GObject.Object, RB.RhythmDBQueryResults):
	def __init__(self, tag, groupby, callback, data):
		super().__init__()
		self.groupby = groupby
		self.tag = tag
		self.results = {}
		self.callback = callback
		self.data = data
		self.idle = 0

	def cancel(self):
		if self.idle != 0:
			GLib.source_remove(self.idle)
			self.idle = 0

	def get_entry_property(self, entry, prop):
		global string_props, ulong_props
		if prop in string_props:
			return entry.get_string(string_props[prop])
		if prop in ulong_props:
			return entry.get_ulong(ulong_props[prop])
		raise Exception("grouping by unknown property {}".format(prop))

	def do_add_results(self, entries):
		print("adding {} entries".format(len(entries)))
		try:
			for e in entries:
				r = self.results
				for g in self.groupby:
					v = self.get_entry_property(e, g)

					if v in r:
						r = r[v]
					else:
						nr = {}
						r[v] = nr
						r = nr

				duration = e.get_ulong(RB.RhythmDBPropType.DURATION)
				v = self.get_entry_property(e, self.tag)
				if v in r:
					(count, playtime) = r[v]
					r[v] = (count + 1, playtime + duration)
				else:
					r[v] = (1, duration)

		except Exception as e:
			print("error processing query results: {}".format(str(e)))

	def complete_callback(self):
		self.idle = 0
		self.callback(self.results, self.data)
		return GLib.SOURCE_REMOVE

	def do_query_complete(self):
		self.idle = GLib.idle_add(self.complete_callback)

	def do_set_query(self, query):
		pass

GObject.type_register(QueryResultsGroupbyTree)

class MDNSService(object):
	def __init__(self, name, label, port):
		self.name = name
		self.label = label
		self.port = port
		self.proxy = None
		self.entry_group = None

	def available(self):
		try:
			if self.proxy is None:
				self.proxy = Gio.DBusProxy.new_for_bus_sync(Gio.BusType.SYSTEM,
				                                            0,
				                                            None,
				                                            'org.freedesktop.Avahi',
				                                            '/',
				                                            'org.freedesktop.Avahi.Server2')
			return True
		except Exception as e:
			print("unable to create proxy for avahi: {}".format(str(e)))
			return False

	def publish(self):
		if self.available() is False:
			return

		try:
			if self.entry_group is None:
				path = self.proxy.EntryGroupNew()
				self.entry_group = Gio.DBusProxy.new_for_bus_sync(Gio.BusType.SYSTEM,
				                                                  0,
				                                                  None,
				                                                  'org.freedesktop.Avahi',
				                                                  path,
				                                                  'org.freedesktop.Avahi.EntryGroup')
			self.entry_group.AddService('(iiussssqaay)', -1, -1, 0, self.name, self.label, "", "", self.port, [])
			self.entry_group.Commit()
		except Exception as e:
			print("unable to publish mdns record for MPD server: {}".format(str(e)))

	def unpublish(self):
		try:
			if self.entry_group is not None:
				self.entry_group.Reset()
		except Exception as e:
			print("unable to unpublish mdns record for MPD server: {}".format(str(e)))


class MPDCommands(object):
	# argument types
	# - ranges (start[:end])
	# - numbers (mostly integers, sometimes real)
	# - relative positions (+/- integer)
	# - uris
	# - filters
	# - list of idle things

	def _check_range(val, min, max):
		if min is not None and val < min:
			raise Exception("value {} is less than {}".format(val, min))
		if max is not None and val > max:
			raise Exception("value {} is greater than {}".format(val, max))

	def parse_integer(args, min, max):
		val = int(args[0])
		MPDCommands._check_range(val, min, max)
		return (val, args[1:])

	def parse_range(args, min, max):
		bits = args[0].split(b':')
		if len(bits) > 2:
			raise Exception("badly formatted range: {}".format(args[0]))
		rmin = int(bits[0])
		MPDCommands._check_range(rmin, min, None)
		if len(bits) == 2:
			rmax = int(bits[1])
			MPDCommands._check_range(rmax, None, max)
		else:
			rmax = None
		return (rmin, rmax, args[1:])

	def parse_float(args, min, max):
		val = float(args[0])
		MPDCommands._check_range(val, min, max)
		return (val, args[1:])

	def parse_relative(args, min, max):
		arg = args[0]
		if arg[0] in (ord('+'), ord('-')):
			val = MPDCommands.parse_integer([arg[1:]], min, max)
			return (chr(arg[0]), val[0], args[1:])
		else:
			val = MPDCommands.parse_integer(args, min, max)
			return (None, val[0], args[1:])

	def parse_uri(args):
		uri = args[0]
		# use GUri to re-escape parts of the uri to match rhythmdb
		euri = GLib.Uri.parse(uri.decode('utf-8'), GLib.UriFlags.NONE).to_string()
		return (euri, args[1:])

	def empty_filter(db):
		q = GLib.PtrArray()
		songtype = db.entry_type_get_by_name("song")
		db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.TYPE, songtype)
		return q

	def parse_filter(args, db, search):
		# only handle the [tag] [value] form
		if len(args) < 2:
			return (None, None)
		# not doing proper filters yet
		if args[0][0] == ord('('):
			return (None, None)

		q = MPDCommands.empty_filter(db)

		while len(args) > 1:
			mpdtag = args[0].decode('ascii')
			val = args[1].decode('utf-8')
			args = args[2:]

			if mpdtag == 'any':
				db.query_append_params(q, RB.RhythmDBQueryType.FUZZY_MATCH, RB.RhythmDBPropType.SEARCH_MATCH, val)
			elif mpdtag == 'file':
				db.query_append_params(q, RB.RhythmDBQueryType.FUZZY_MATCH, RB.RhythmDBPropType.LOCATION, val)
			else:
				ftag = MPDCommands.fix_property_name(mpdtag)
				if ftag in string_props:
					if search and ftag in string_fold_props:
						db.query_append_params(q, RB.RhythmDBQueryType.FUZZY_MATCH, string_fold_props[ftag], val)
					elif search:
						db.query_append_params(q, RB.RhythmDBQueryType.FUZZY_MATCH, string_props[ftag], val)
					else:
						db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, string_props[ftag], val)
				elif ftag in ulong_props:
					db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, ulong_props[ftag], int(val))
				else:
					raise Exception("querying on unsupported tag {}".format(str(mpdtag)))

		return (q, args)

	def parse_find_args(args, pos=False):
		position = None
		window = None
		sort = None

		if pos and len(args) > 2 and args[-2] == b'position':
			position = int(args[-1])
			args = args[0:-2]

		if len(args) > 2 and args[-2] == b'window':
			window = args[-1]
			args = args[0:-2]

		if len(args) > 2 and args[-2] == b'sort':
			sort = args[-1].decode('ascii')
			args = args[0:-2]

		return (args, sort, window, position)

	def parse_idle(args):
		# could check that the idle names are valid
		# but it doesn't really matter
		return set([a.decode('utf-8') for a in args])

	def format_bool(val):
		if val:
			return "1"
		else:
			return "0"

	def fix_property_name(prop):
		if prop in string_props or prop in ulong_props:
			return prop

		lprop = prop.lower()
		for p in string_props:
			if p.lower() == lprop:
				return p
		for p in ulong_props:
			if p.lower() == lprop:
				return p
		raise Exception("unable to fix property name {}".format(prop))

	def format_entry_uri(entry):
		uri = entry.get_string(RB.RhythmDBPropType.LOCATION)
		try:
			return GLib.Uri.unescape_string(uri)
		except UnicodeDecodeError as e:
			# not sure
			print("bad utf-8 uri: {}".format(uri))
			return uri

	def format_entry_string(entry, prop):
		v = entry.get_string(prop)
		if v.isprintable():
			return v
		return ''.join(map(lambda c: c.isprintable() and c or " ", v))

	def entry_details(entry, pos=None):

		e = []
		if entry is None:
			return e

		e.append("file: {}".format(MPDCommands.format_entry_uri(entry)))
		for (k, p) in string_props.items():
			e.append("{}: {}".format(k, MPDCommands.format_entry_string(entry, p)))
		for (k, p) in ulong_props.items():
			e.append("{}: {}".format(k, entry.get_ulong(p)))

		if pos is not None:
			e.append("Pos: {}".format(pos))

		dt = datetime.datetime.utcfromtimestamp(entry.get_ulong(RB.RhythmDBPropType.MTIME))
		e.append("Last-Modified: {}".format(dt.strftime("%FT%TZ")))
		return e

	def mpd_queue_len(client):
		p = client.get_player()
		l = 0

		if p.get_playing_entry() is not None and client.playing_from_queue() is False:
			l += 1

		return l + len(client.get_queue())

	def mpd_queue_iter(client):
		p = client.get_player()
		pentry = p.get_playing_entry()
		idx = 0
		if pentry is not None and client.playing_from_queue() is False:
			yield (idx, pentry)
			idx += 1

		queue = client.get_queue()
		for e in queue:
			(entry, _) = e
			yield (idx, entry)
			idx += 1

	def queue_insert_pos(client, rel, pos, relto):
		p = client.get_player()
		qlen = len(client.get_queue())
		adj = 0

		if p.get_playing_entry() is not None and client.playing_from_queue() is False:
			adj = -1
			qlen += 1

		if rel is None:
			return max(pos + adj, 0)
		elif rel == '+':
			return min(relto + pos + adj, qlen)
		elif rel == '-':
			return max(relto - pos + adj, 0)
		else:
			return qlen

	def cmd_tagtypes(client, args):
		if len(args) == 0 or args[0] == 'available':
			tags = list(string_props.keys()) + list(ulong_props.keys())
			lines = ["tagtype: " + t for t in tags]
			return (MPDAck.OK, "\n".join(lines))
		else:
			return (MPDAck.OK, "")

	def cmd_close(client, args):
		client.close()
		return (MPDAck.OK, "")

	def cmd_config(client, args):
		return (MPDAck.OK, "")

	def cmd_commands(client, args):
		authed = client.authenticated or client.server.settings['mpd-password'] == ""
		cmds = []
		for (cmd, (auth, handler)) in MPDCommands.commands.items():
			if auth is False or authed:
				cmds.append("command: {}".format(cmd))
		return (MPDAck.OK, "\n".join(cmds))

	def cmd_notcommands(client, args):
		authed = client.authenticated or client.server.settings['mpd-password'] == ""
		cmds = []
		for (cmd, (auth, handler)) in MPDCommands.commands.items():
			if auth is True and authed is False:
				cmds.append("command: {}".format(cmd))
		return (MPDAck.OK, "\n".join(cmds))

	def cmd_password(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")
		if args[0].decode('utf-8') == client.server.settings['mpd-password']:
			client.authenticated = True
			return (MPDAck.OK, "")
		else:
			print("password {} doesn't match {}".format(args[0].decode('utf-8'), client.server.settings['mpd-password']))
			return (MPDAck.PASSWORD, "bad password")

	def cmd_ping(client, args):
		return (MPDAck.OK, "")

	def cmd_addid(client, args):
		if len(args) < 1 or len(args) > 2:
			return (MPDAck.ARG, "wrong arguments")

		db = client.get_db()
		queue = client.get_queue()
		(uri, args) = MPDCommands.parse_uri(args)

		entry = db.entry_lookup_by_location(uri)
		if entry is None:
			return (MPDAck.NO_EXIST, "no entry found")

		if len(args) == 2:
			(pos, args) = MPDCommands.parse_integer(args, None, None)
			if pos < 1:
				pos = 0
			queue.add_entry(entry, pos)
		else:
			queue.add_entry(entry, -1)

		return (MPDAck.OK, "Id: {}".format(entry.get_ulong(RB.RhythmDBPropType.ENTRY_ID)))

	def cmd_add(client, args):
		rsp = MPDCommands.cmd_addid(client, args)
		if rsp[0] != MPDAck.OK:
			return rsp
		# eat the returned song id
		return (MPDAck.OK, "")

	def cmd_delete(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")

		(start, end, args) = MPDCommands.parse_range(args, 0, None)
		if end is None:
			end = start + 1

		queue = client.get_queue()
		for (idx, entry) in MPDCommands.mpd_queue_iter(client):
			if idx == end:
				break
			if idx >= start:
				queue.remove_entry(entry)

		return (MPDAck.OK, "")

	def cmd_deleteid(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")

		(id, args) = MPDCommands.parse_integer(args, 0, None)

		p = client.get_player()
		dentry = client.get_db().entry_lookup_by_id(id)
		if dentry is None:
			return (MPDAck.NO_EXIST, "can't find entry by id")

		if dentry == p.get_playing_entry():
			p.do_next()
			return (MPDAck.OK, "")

		queue = client.get_queue()
		if queue.remove_entry(dentry) is False:
			return (MPDAck.NO_EXIST, "entry not found in queue")

		return (MPDAck.OK, "")

	def cmd_move(client, args):		# move [<pos>|<range>] [<pos>|<relative>]
		if len(args) != 2:
			return (MPDAck.ARG, "wrong arguments")

		(mfrom, args) = MPDCommands.parse_integer(args, 0, None)
		(rel, to, args) = MPDCommands.parse_relative(args, None, None)

		queue = client.get_queue()
		p = client.get_player()

		pos = MPDCommands.queue_insert_pos(client, rel, to, mfrom)

		entry = p.get_playing_entry()
		if mfrom == 0 and entry is not None:
			if client.playing_from_queue():
				print("moving queue playing entry to {}".format(pos))
				queue.move_entry(entry, pos)
			else:
				print("moving playing entry to {}".format(pos))
				queue.add_entry(entry, pos)
				p.do_next()
		else:
			entry = None
			for (idx, e) in MPDCommands.mpd_queue_iter(client):
				if idx == mfrom:
					entry = e
					break
			if entry is None:
				return (MPDAck.ARG, "from position out of range")

			print("moving entry {} from {} to {}".format(entry.get_string(RB.RhythmDBPropType.LOCATION), mfrom, pos))
			queue.move_entry(entry, pos)

		return (MPDAck.OK, "")

	def cmd_moveid(client, args):		# moveid <songid> <pos>
		if len(args) != 2:
			return (MPDAck.ARG, "wrong arguments")

		(id, args) = MPDCommands.parse_integer(args, 0, None)
		(rel, to, args) = MPDCommands.parse_integer(args, 0, None)

		p = client.get_player()
		db = client.get_db()
		queue = client.get_queue()

		mfrom = None
		for (idx, e) in MPDCommands.mpd_queue_iter(client):
			if e.get_ulong(RB.RhythmDBPropType.ENTRY_ID) == id:
				mfrom = idx
				mentry = e
				break
		if mfrom is None:
			return (MPDAck.NO_EXIST, "can't find entry by id")

		pos = MPDCommands.queue_insert_pos(client, rel, to, mfrom)

		if client.playing_from_queue() is False and mentry == p.get_playing_entry():
			queue.add_entry(mentry, pos)
			p.do_next()
		else:
			queue.move_entry(mentry, pos)

		return (MPDAck.OK, "")

	def cmd_swap(client, args):		# swap [pos] [pos]
		if len(args) != 2:
			return (MPDAck.ARG, "wrong arguments")

		queue = client.get_queue()
		p = client.get_player()

		(swapfrom, args) = MPDCommands.parse_integer(args, 0, None)
		(swapto, args) = MPDCommands.parse_integer(args, 0, None)
		if swapfrom == swapto:
			return (MPDAck.OK, "")

		if swapto < swapfrom:
			t = swapto
			swapto = swapfrom
			swapfrom = t

		if client.playing_from_queue():
			if swapfrom == 0:
				# do something
				pass

			swapfrom -= 1
			swapto -= 1

		a = queue[swapfrom]
		b = queue[swapto]
		queue.move_entry(a, swapto)
		queue.move_entry(b, swapfrom)

		return (MPDAck.OK, "")

	def cmd_swapid(client, args):		# swapid [songid] [songid]
		return (MPDAck.OK, "")

	def cmd_shuffle(client, args):		# shuffle [start:end]
		if len(args) > 0:
			return (MPDAck.ARGS, "partial shuffle not supported")

		q = client.get_queue()
		q.shuffle_entries()
		return (MPDAck.OK, "")

	def cmd_clear(client, args):
		q = client.get_queue()
		for (entry, _) in q:
			q.remove_entry(entry)
		return (MPDAck.OK, "")

	def cmd_currentsong(client, args):
		p = client.get_player()
		entry = p.get_playing_entry()
		if entry is None:
			return (MPDAck.OK, "")

		data = MPDCommands.entry_details(entry)
		return (MPDAck.OK, "\n".join(data))

	def cmd_binarylimit(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")

		(blen, args) = MPDCommands.parse_integer(args, 0, None)
		client.binary_limit = blen
		return (MPDAck.OK, "")

	def cmd_albumart(client, args):
		if len(args) != 2:
			return (MPDAck.ARG, "wrong arguments")

		(uri, args) = MPDCommands.parse_uri(args)
		(offset, args) = MPDCommands.parse_integer(args, 0, None)

		entry = client.get_db().entry_lookup_by_location(uri)
		if entry is None:
			return (MPDAck.OK, "")

		key = entry.create_ext_db_key(RB.RhythmDBPropType.ALBUM)
		(filename, lkey) = client.get_art_store().lookup(key)
		if filename is None:
			return (MPDAck.OK, "")

		artcache = os.path.join(RB.user_cache_dir(), "album-art", "")
		if filename.startswith(artcache) is False or not os.path.exists(filename):
			return (MPDAck.OK, "")

		relname = filename[len(artcache):]
		try:
			fp = open(filename, 'rb')
			dsize = fp.seek(0, os.SEEK_END)
			fp.seek(offset)
			if client.binary_limit is None:
				d = fp.read()
			else:
				d = fp.read(client.binary_limit)

			data = []
			data.append("file: {}".format(relname))
			data.append("size: {}".format(dsize))
			data.append("binary: {}".format(len(d)))
			client.send(data)
			client.send(b"\n")
			client.send(d)
		except Exception as e:
			print("{}: error sending albumart response: {}".format(client.client_id, str(e)))

		return (MPDAck.OK, "")

	def cmd_readpicture(client, args):
		# our implementation of albumart will find embedded album art,
		# so we don't really need to do anything here
		return (MPDAck.OK, "")

	def cmd_getvol(client, args):
		p = client.get_player()
		return (MPDAck.OK, str(p.get_volume() * 100))

	def cmd_setvol(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")
		vol = int(args[0])
		if vol < 0 or vol > 100:
			return (MPDAck.ARG, "bad volume")
		p = client.get_player()
		p.set_volume(vol / 100)
		return (MPDAck.OK, "")

	def cmd_volume(client, args):
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")
		vol = int(args[0])
		p = client.get_player()
		p.set_volume_relative(vol)
		return (MPDAck.OK, "")

	def cmd_outputs(client, args):
		info = [
			"outputid: 0",
			"outputname: rhythmbox",
			"plugin: gstreamer",
			"outputenabled: 1"
		]
		return (MPDAck.OK, "\n".join(info))

	def format_tag_list(list, results, groupby, tag):
		for r in results.keys():
			if len(groupby) == 0:
				list.append("{}: {}".format(tag, r))
			else:
				list.append("{}: {}".format(groupby[0], r))
				MPDCommands.format_tag_list(list, results[r], groupby[1:], tag)

	def list_query_cb(results, data):
		(client, tag, groupby, window) = data

		lines = []
		MPDCommands.format_tag_list(lines, results, groupby, tag)
		client.response((MPDAck.OK, "\n".join(lines)))


	def cmd_list(client, args):
		window = None
		groupby = []
		tag = None
		filter = None

		if len(args) > 2 and args[-2] == b'window':
			window = args[-1]
			args = args[0:-2]

		while len(args) > 2 and args[-2] == b'group':
			groupby.append(args[-1].decode('ascii'))
			args = args[0:-2]
		groupby.reverse()

		if len(args) == 0:
			return (MPDAck.ARG, "wrong arguments")

		tag = args[0].decode('ascii')
		args = args[1:]

		db = client.get_db()
		if len(args) > 0:
			(filter, args) = MPDCommands.parse_filter(args, db, False)
			if filter is None:
				return (MPDAck.ARG, "unable to parse command")
		else:
			filter = MPDCommands.empty_filter(db)

		# account for clients who think property names are case insensitive
		cgroupby = [MPDCommands.fix_property_name(p) for p in groupby]
		ctag = MPDCommands.fix_property_name(tag)

		qrgbt = QueryResultsGroupbyTree(ctag, cgroupby, MPDCommands.list_query_cb, (client, ctag, cgroupby, window))
		db.do_full_query_async_parsed(qrgbt, filter)

		return (MPDAck.ASYNC, "")

	def cmd_listplaylist(client, args):
		return (MPDAck.OK, "")

	def cmd_listplaylistinfo(client, args):
		return (MPDAck.OK, "")

	def cmd_listplaylists(client, args):
		return (MPDAck.OK, "")

	def cmd_next(client, args):
		p = client.get_player()
		# return error if there's no next entry?
		p.do_next()
		return (MPDAck.OK, "")

	def cmd_previous(client, args):
		p = client.get_player()
		# return error if there's no prev entry?
		p.do_previous()
		return (MPDAck.OK, "")

	def cmd_pause(client, args):
		p = client.get_player()
		if len(args) == 0:
			p.playpause()
			return (MPDAck.OK, "")
		elif len(args) == 1:
			arg = int(args[0])
			if arg == 1:
				p.pause()
			elif arg == 0:
				p.play()
			else:
				return (MPDAck.ARG, "wrong arguments")

			return (MPDAck.OK, "")
		else:
			return (MPDAck.ARG, "wrong arguments")

	def cmd_seekcur(client, args):
		p = client.get_player()
		if len(args) != 1:
			return (MPDAck.ARG, "wrong arguments")

		(rel, val, _) = MPDCommands.parse_relative(args, None, None)
		if rel is None:
			p.set_playing_time(val)
		elif rel == '+':
			p.seek(val)
		elif rel == '-':
			p.seek(-val)
		else:
			return (MPDAck.ARG, "wrong arguments")
		return (MPDAck.OK, "")

	def cmd_seekid(client, args):
		p = client.get_player()
		if len(args) != 2:
			return (MPDAck.ARG, "wrong arguments")

		entry = p.get_playing_entry()
		(sid, args) = MPDCommands.parse_integer(args, 0, None)
		(pos, args) = MPDCommands.parse_integer(args, 0, None)
		if sid != entry.get_ulong(RB.RhythmDBPropType.ENTRY_ID):
			return (MPDAck.ARG, "entry not playing")

		p.set_playing_time(pos)
		return (MPDAck.OK, "")

	def cmd_stop(client, args):
		p = client.get_player()
		p.stop()
		return (MPDAck.OK, "")

	def cmd_play(client, args):
		p = client.get_player()
		if len(args) == 1:
			(pos, args) = MPDCommands.parse_integer(args, 0, None)
			if pos == 0:
				p.playpause()
				return (MPDAck.OK, "")

			q = MPDCommands.mpd_queue_iter(client)
			(_, entry) = q[pos]
			client.play_queue_entry(entry)
			return (MPDAck.OK, "")
		else:
			p.playpause()
			return (MPDAck.OK, "")

	def cmd_playid(client, args):
		p = client.get_player()
		if len(args) == 1:
			(id, args) = MPDCommands.parse_integer(args, 0, None)
			for (idx, e) in MPDCommands.mpd_queue_iter(client):
				if e.get_ulong(RB.RhythmDBPropType.ENTRY_ID) == id:
					client.play_queue_entry(e)
					return (MPDAck.OK, "")

			return (MPDAck.NO_EXIST, "entry not found in queue")
		else:
			p.play()
			return (MPDAck.OK, "")

	def cmd_playlist(client, args):
		uris = []
		for (idx, entry) in MPDCommands.mpd_queue_iter(client):
			uris.append("{}:{}".format(idx, MPDCommands.format_entry_uri(entry)))
		return (MPDAck.OK, "\n".join(uris))

	def cmd_playlistid(client, args):
		(id, args) = MPDCommands.parse_integer(args, 0, None)

		data = []
		for (idx, entry) in MPDCommands.mpd_queue_iter(client):
			if entry.get_ulong(RB.RhythmDBPropType.ENTRY_ID) == id:
				data = MPDCommands.entry_details(entry, idx)
				break

		return (MPDAck.OK, "\n".join(data))

	def cmd_playlistinfo(client, args):
		data = []

		if len(args) > 0:
			(rmin, rmax, _) = MPDCommands.parse_range(args, 0, None)
		else:
			(rmin, rmax) = (0, MPDCommands.mpd_queue_len(client))

		for (idx, entry) in MPDCommands.mpd_queue_iter(client):
			if idx == rmax:
				break
			if idx >= rmin:
				data.extend(MPDCommands.entry_details(entry, idx))

		return (MPDAck.OK, "\n".join(data))

	def cmd_plchanges(client, args):
		# extremely cheap
		return MPDCommands.cmd_playlistinfo(client, args[1:])

	def cmd_plchangesposid(client, args):
		data = []

		(ver, args) = MPDCommands.parse_integer(args, 0, None)
		if len(args) == 1:
			(rmin, rmax, args) = MPDCommands.parse_range(args, 0, None)
		else:
			rmin = 0
			rmax = MPDCommands.mpd_queue_len(client)

		for (idx, entry) in MPDCommands.mpd_queue_iter(client):
			if idx == rmax:
				break
			if idx >= rmin:
				data.append("cpos: {}".format(idx))
				data.append("Id: {}".format(entry.get_ulong(RB.RhythmDBPropType.ENTRY_ID)))

		return (MPDAck.OK, "\n".join(data))

	def cmd_status(client, args):
		p = client.get_player()
		# get queue
		status = []
		status.append("partition: default")
		(_, vol) = p.get_volume()
		status.append("volume: {:d}".format(int(vol * 100)))
		# don't use the actual repeat and shuffle settings here,
		# as we're only telling mpd clients about the play queue,
		# which is always played in order.
		status.append("repeat: 0")
		status.append("random: 0")
		status.append("single: 0")
		status.append("consume: 1")
		status.append("playlist: {}".format(client.playlist_version()))
		status.append("playlistlength: {}".format(MPDCommands.mpd_queue_len(client)))
		entry = p.get_playing_entry()
		if entry is None:
			status.append("state: stop")
		else:
			if p.props.playing:
				status.append("state: play")
			else:
				status.append("state: pause")
			# current song is effectively always the first item in the queue
			status.append("song: 0")
			status.append("songid: {}".format(entry.get_ulong(RB.RhythmDBPropType.ENTRY_ID)))
			try:
				(_, ptime) = p.get_playing_time()
			except gi.repository.GLib.GError:
				ptime = 0

			duration = entry.get_ulong(RB.RhythmDBPropType.DURATION)
			if duration > 0:
				status.append("time: {:d}:{:d}".format(ptime, duration))
				status.append("duration: {:.3f}".format(entry.get_ulong(RB.RhythmDBPropType.DURATION)))
			else:
				status.append("time: {:d}".format(ptime))

			status.append("elapsed: {:.3f}".format(ptime))

			# nextsong, nextsongid?

		return (MPDAck.OK, "\n".join(status))

	def stats_query_cb(results, client):

		now = time.time()
		uptime = now - client.server.start_time
		stats = [
			"songs: {}".format(results.songs),
			"db_playtime: {}".format(results.duration),
			"uptime: {}".format(int(uptime)),
			"playtime: {}".format(int(uptime)),		# could do better
			"db_update: {}".format(int(now)),
			"artists: {}".format(len(results.artists)),
			"albums: {}".format(len(results.albums))
		]
		client.response((MPDAck.OK, "\n".join(stats)))

	def cmd_stats(client, args):
		db = client.get_db()
		filter = MPDCommands.empty_filter(db)

		qrs = QueryResultsStats(MPDCommands.stats_query_cb, client)
		db.do_full_query_async_parsed(qrs, filter)

		return (MPDAck.ASYNC, "")

	def find_query_cb(results, client):
		r = []
		# need to apply sort and window here too
		for entry in results.get_results():
			r.extend(MPDCommands.entry_details(entry))
		client.response((MPDAck.OK, "\n".join(r)))

	def findadd_query_cb(results, client):
		# need to apply sort, window, position here too
		queue = client.get_queue()
		for entry in results.get_results():
			queue.add_entry(entry)
		client.response((MPDAck.OK, ""))

	def cmd_find(client, args):
		(args, sort, window, position) = MPDCommands.parse_find_args(args)
		db = client.get_db()
		(filter, args) = MPDCommands.parse_filter(args, db, False)
		if filter is None:
			return (MPDAck.ARG, "unable to parse command")

		rl = RB.RhythmDBQueryResultList()
		rl.connect("complete", MPDCommands.find_query_cb, client)
		db.do_full_query_async_parsed(rl, filter)

		return (MPDAck.ASYNC, "")

	def cmd_search(client, args):
		(args, sort, window, position) = MPDCommands.parse_find_args(args)
		db = client.get_db()
		(filter, args) = MPDCommands.parse_filter(args, db, True)
		if filter is None:
			return (MPDAck.ARG, "unable to parse command")

		rl = RB.RhythmDBQueryResultList()
		rl.connect("complete", MPDCommands.find_query_cb, client)
		db.do_full_query_async_parsed(rl, filter)

		return (MPDAck.ASYNC, "")

	def cmd_findadd(client, args):
		(args, sort, window, position) = MPDCommands.parse_find_args(args, True)
		db = client.get_db()
		(filter, args) = MPDCommands.parse_filter(args, db, False)
		if filter is None:
			return (MPDAck.ARG, "unable to parse command")

		rl = RB.RhythmDBQueryResultList()
		rl.connect("complete", MPDCommands.findadd_query_cb, client)
		db.do_full_query_async_parsed(rl, filter)

		return (MPDAck.ASYNC, "")

	def cmd_searchadd(client, args):
		(args, sort, window, position) = MPDCommands.parse_find_args(args, True)
		db = client.get_db()
		(filter, args) = MPDCommands.parse_filter(args, db, True)
		if filter is None:
			return (MPDAck.ARG, "unable to parse command")

		rl = RB.RhythmDBQueryResultList()
		rl.connect("complete", MPDCommands.findadd_query_cb, client)
		db.do_full_query_async_parsed(rl, filter)

		return (MPDAck.ASYNC, "")

	def count_query_cb(results, client):
		count = 0
		playtime = 0
		for entry in results.get_results():
			count += 1
			playtime += entry.get_ulong(RB.RhythmDBPropType.DURATION)
		client.response((MPDAck.OK, "songs: {}\nplaytime: {}\n".format(count, playtime)))

	def count_groupby_query_cb(results, data):
		(client, groupby) = data

		lines = []
		for r in results.keys():
			count = 0
			playtime = 0
			lines.append("{}: {}\nsongs: {}\nplaytime: {}]n".format(groupby, r, count, playtime))

		client.response((MPDAck.OK, "\n".join(lines)))

	def cmd_count(client, args):
		groupby = None
		if len(args) > 2 and args[-2] == b'group':
			g = args[-1].decode('ascii')
			groupby = MPDCommands.fix_property_name(g)

		db = client.get_db()
		(filter, args) = MPDCommands.parse_filter(args, db, False)
		if filter is None:
			return (MPDAck.ARG, "unable to parse command")

		if groupby is not None:
			qrgbt = QueryResultsGroupbyTree(groupby, [], MPDCommands.count_groupby_query_cb, (client, groupby))
			db.do_full_query_async_parsed(qrgbt, filter)
		else:
			rl = RB.RhythmDBQueryResultList()
			rl.connect("complete", MPDCommands.count_query_cb, client)
			db.do_full_query_async_parsed(rl, filter)

		return (MPDAck.ASYNC, "")

	def cmd_partition(client, args):
		return (MPDAck.OK, "")

	def cmd_subscribe(client, args):
		return (MPDAck.OK, "")

	def cmd_channels(client, args):
		return (MPDAck.OK, "")

	def cmd_sendmessage(client, args):
		return (MPDAck.OK, "")

	def cmd_lsinfo(client, args):
		return (MPDAck.OK, "")

	def cmd_urlhandlers(client, args):
		return (MPDAck.OK, "handler: file://")

	def cmd_decoders(client, args):
		# moderately accurate
		d = [
			"plugin: gstreamer",
			"suffix: mp3",
			"suffix: ogg",
			"suffix: flac",
			"suffix: m4a",
			"mime_type: audio/mpeg",
			"mime_type: audio/ogg",
			"mime_type: audio/x-flac"
		]
		return (MPDAck.OK, "\n".join(d))

	def cmd_replaygain_mode(client, args):
		return (MPDAck.OK, "")

	def cmd_replaygain_status(client, args):
		return (MPDAck.OK, "replay_gain_mode: off")

	commands = {
		# name			 auth   handler
		'add':			(True,  cmd_add),
		'addid':		(True,  cmd_addid),
		'albumart':		(True,  cmd_albumart),
		'binarylimit':		(True,  cmd_binarylimit),
		'channels':		(True,  cmd_channels),
		'clear':		(True,  cmd_clear),
		'close':		(False, cmd_close),
		'commands':		(False, cmd_commands),
		'config':		(False, cmd_config),
		'count':		(True,  cmd_count),
		'currentsong':		(True,  cmd_currentsong),
		'decoders':		(True,  cmd_decoders),
		'delete':		(True,  cmd_delete),
		'deleteid':		(True,  cmd_deleteid),
		'find':			(True,  cmd_find),
		'findadd':		(True,  cmd_findadd),
		'getvol':		(True,  cmd_getvol),
		'idle':			(False, None),
		'list':			(True,  cmd_list),
		'listplaylist':		(True,  cmd_listplaylist),
		'listplaylists':	(True,  cmd_listplaylists),
		'listplaylistinfo':	(True,  cmd_listplaylistinfo),
		'lsinfo':		(True,  cmd_lsinfo),
		'move':			(True,  cmd_move),
		'moveid':		(True,  cmd_moveid),
		'next':			(True,  cmd_next),
		'notcommands':		(False, cmd_notcommands),
		'outputs':		(True,  cmd_outputs),
		'partition':		(True,  cmd_partition),
		'password':		(False, cmd_password),
		'pause':		(True,  cmd_pause),
		'ping':			(False, cmd_ping),
		'play':			(True,  cmd_play),
		'playid':		(True,  cmd_playid),
		'playlist':		(True,  cmd_playlist),
		'playlistid':		(True,  cmd_playlistid),
		'playlistinfo':		(True,  cmd_playlistinfo),
		'plchanges':		(True,  cmd_plchanges),
		'plchangesposid':	(True,  cmd_plchangesposid),
		'previous':		(True,  cmd_previous),
		'readpicture':		(True,  cmd_readpicture),
		'replay_gain_mode':	(True,  cmd_replaygain_mode),
		'replay_gain_status':	(True,  cmd_replaygain_status),
		'search':		(True,  cmd_search),
		'searchadd':		(True,  cmd_searchadd),
		'seekcur':		(True,  cmd_seekcur),
		'seekid':		(True,  cmd_seekid),
		'sendmessage':		(True,  cmd_sendmessage),
		'setvol':		(True,  cmd_setvol),
		'shuffle':		(True,  cmd_shuffle),
		'stats':		(True,  cmd_stats),
		'status':		(True,  cmd_status),
		'stop':			(True,  cmd_stop),
		'subscribe':		(True,  cmd_subscribe),
		'swap':			(True,  cmd_swap),
		'swapid':		(True,  cmd_swapid),
		'tagtypes':		(True,  cmd_tagtypes),
		'urlhandlers':		(True,  cmd_urlhandlers),
		'volume':		(True,  cmd_volume),
	}

	def parse_line(line):
		bits = line.split(None, 1)
		cmd = bits[0]
		if len(bits) > 1:
			line = bits[1]
		else:
			line = ""

		words = []
		while len(line) > 0:
			if line[0] == ord('"'):
				pos = 1
				while line[pos] != ord('"'):
					if line[pos] == ord('\\'):
						pos = pos + 1
					pos = pos + 1
					if pos == len(line):
						raise Exception("no closing quote")

				if pos != len(line) - 1 and not bytes.isspace(line[pos+1:pos+2]):
					raise Exception("space must follow closing quote")

				words.append(line[1:pos])

				line = line[pos+1:].lstrip()
			else:
				bits = line.split(None, 1)
				words.append(bits[0])
				if len(bits) > 1:
					line = bits[1]
				else:
					line = ""
		return (cmd.decode('utf-8'), words)

	def process(client, cmd, args, idx):
		h = MPDCommands.commands.get(cmd, None)
		if h is None:
			return MPDCommands.format_error(cmd, MPDAck.ARG, idx, "unknown command")
		if client.server.settings['mpd-password'] != "":
			if h[0] and client.authenticated is False:
				return MPDCommands.format_error(cmd, MPDAck.PERMISSION, idx, "access denied")

		(err,msg) = h[1](client, args)
		if err not in (MPDAck.OK, MPDAck.ASYNC):
			return MPDCommands.format_error(cmd, err, idx, msg)
		else:
			return (err, msg)

	def format_error(cmd, err, idx, msg):
		return (err, b"ACK [%d@%d] {%s} %s\n" % (err, idx, cmd.encode('utf-8'), msg.encode('utf-8')))





class MPDClient(object):
	def __init__(self, server, conn):
		self.server = server
		self.connection = conn
		self.authenticated = False
		self.idling = None
		self.unidle = set()
		self.inbuffer = b''
		self.async_processing = False
		self.command_list_state = None
		self.binary_limit = None

		fd = conn.get_socket().get_fd()
		self.client_id = "fd:{}".format(fd)
		cond = GLib.IOCondition.IN | GLib.IOCondition.ERR
		self.insource = GLibUnix.fd_source_new(fd, cond)
		self.insource.set_callback(self.read_cb)
		self.insource.attach(None)

		self.outbuffer = None
		self.outsource = None

		self.send("OK MPD 0.24.0\n".encode('utf-8'))

	def close(self):
		self.connection.close_async(GLib.PRIORITY_DEFAULT, None, self.close_cb, None)

	def close_cb(self, conn, result, obj):
		try:
			conn.close_finish(result)
		except Exception as e:
			print("{}: couldn't close connection: {}".format(self.client_id, e))
		self.closed()

	def closed(self):
		self.server.connection_closed(self)


	def read_cb(self):
		try:
			# gio stream read methods don't work, so dig out the fd
			data = os.read(self.connection.get_socket().get_fd(), 80)
			if len(data) == 0:
				self.close()
				return False

			self.inbuffer = self.inbuffer + data
			self.process()
			return True
		except Exception as e:
			print("{}: error reading from connection: {}".format(self.client_id, e))
			sys.excepthook(*sys.exc_info())
			self.close()
			return False

	def response(self, rsp):
		wasasync = self.async_processing
		self.async_processing = False

		if rsp[0] != MPDAck.OK:
			# error response is already formatted bytes
			self.send(rsp[1])
			# error terminates command lists
			self.command_list_state = None
			return False

		if rsp[1] != "":
			# response is a string
			self.send(rsp[1].encode('utf-8'))
			self.send(b"\n")

		if self.command_list_state is None:
			self.send(b"OK\n")
		elif wasasync:
			self.process_list()

		return True

	def process(self):
		if self.async_processing:
			return

		if self.command_list_state is not None:
			self.process_list()
			return

		lines = self.inbuffer.split(b'\n', 1)
		if len(lines) == 1:
			# don't have a full command yet
			return

		if lines[0] in (b'command_list_begin', b'command_list_ok_begin'):
			morelines = lines[1].split(b'\n')

			# should handle cases where we get more lines than that
			if len(morelines) < 2 or morelines[-2] != b'command_list_end':
				# haven't got the whole list yet
				return

			list_ok = (lines[0] == b'command_list_ok_begin')
			all_ok = True

			self.inbuffer = b''
			self.command_list_state = (0, list_ok, morelines[:-2])
			self.process_list()

		elif len(lines[0]) > 800:
			raise Exception("{}: command too long".format(self.client_id))
		else:
			rsp = self.command(lines[0], 0)
			if rsp[0] == MPDAck.IDLE:
				# idling, no response yet
				pass
			elif rsp[0] == MPDAck.ASYNC:
				# no response yet, block command processing until done
				self.async_processing = True
			else:
				self.response(rsp)

			self.inbuffer = lines[1]

	def process_list(self):
		(idx, list_ok, lines) = self.command_list_state
		for line in lines[idx:]:
			rsp = self.command(line, idx)
			idx += 1
			if rsp[0] == MPDAck.ASYNC:
				self.command_list_state = (idx, list_ok, lines)
				self.async_processing = True
				return

			if self.response(rsp) is False:
				return

			if list_ok:
				self.send(b"list_OK\n")

		self.send(b"OK\n")
		self.command_list_state = None

	def command(self, line, idx):
		(cmd, args) = MPDCommands.parse_line(line)
		sarg = [x.decode('utf-8') for x in args]
		print("{}: command {}: {} [{}]".format(self.client_id, idx, cmd, " ".join(sarg)))

		try:
			# idle and noidle are special
			if cmd == 'noidle':
				self.idling = None
				return (MPDAck.OK, "")

			if self.idling is not None:
				return MPDCommands.format_error(cmd, MPDAck.ARG, idx, "idling")
			elif cmd == 'idle':
				self.idling = MPDCommands.parse_idle(args)
				if self.maybe_unidle() is True:
					return (MPDAck.OK, "")
				else:
					print("{}: idling ({})".format(self.client_id, str(self.idling)))
					return (MPDAck.IDLE, "")

			rsp = MPDCommands.process(self, cmd, args, idx)
			print("{}: returning [{}]".format(self.client_id, rsp[1]))
			return rsp
		except Exception as e:
			sys.excepthook(*sys.exc_info())
			return MPDCommands.format_error(cmd, MPDAck.ARG, idx, "{}: error processing command".format(self.client_id))




	def writable_cb(self):
		print("{}: output writable".format(self.client_id))
		self.try_send()

	def try_send(self):
		out = self.connection.get_output_stream()
		try:
			while len(self.outbuffer) > 0:
				data = self.outbuffer[0]
				written = out.write_nonblocking(data, None)
				if written != len(data):
					self.outbuffer[0] = data[written:]
					return

				self.outbuffer = self.outbuffer[1:]

			if self.outsource is not None:
				self.outsource.destroy()
				self.outsource = None

		except GLib.GError as ge:
			if ge.matches(Gio.io_error_quark(), Gio.IOErrorEnum.WOULD_BLOCK):
				if self.outsource is None:
					print("{}: output buffer full".format(self.client_id))
					self.outsource = out.create_source()
					self.outsource.set_callback(self.writable_cb)
					self.outsource.attach()
			else:
				print("{}: error writing to connection: {}".format(self.client_id, ge))
				self.close()


	def send(self, data):
		if isinstance(data, str):
			data = data.encode('utf-8')

		if self.outsource is not None:
			self.outbuffer.append(data)
			return

		self.outbuffer = [data]
		self.try_send()

	def maybe_unidle(self):
		if self.idling is None:
			return False

		if len(self.idling) == 0:
			changed = self.unidle
			self.unidle = set()
		else:
			changed = self.unidle.intersection(self.idling)
			self.unidle = self.unidle.difference(changed)

		if len(changed) == 0:
			return False

		rsp = "\n".join(["changed: {}".format(c) for c in changed]) + "\n"
		print("{}: unidling client: {}".format(self.client_id, rsp))
		self.idling = None
		self.send(rsp.encode('utf-8'))
		return True

	def dispatch(self, update):
		print("{}: dispatching {}".format(self.client_id, update))
		if update is not None:
			self.unidle.add(update)

		if self.maybe_unidle() is True:
			self.send(b"OK\n")

	def get_player(self):
		return self.server.shell_player

	def get_queue(self):
		return self.server.shell_player.props.queue_source.props.query_model

	def play_queue_entry(self, entry):
		p = self.server.shell_player
		p.play_entry(entry, p.props.queue_source)

	def playing_from_queue(self):
		return self.server.shell_player.props.playing_from_queue

	def playlist_version(self):
		return self.server.playlist_version

	def get_db(self):
		return self.server.db

	def get_art_store(self):
		return self.server.art_store

class MPDServerPlugin(GObject.Object, Peas.Activatable):
	__gtype_name = 'MPDServerPlugin'
	object = GObject.Property(type=GObject.GObject)


	def __init__(self):
		GObject.Object.__init__(self)
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.mpdserver")
		self.settings.connect("changed", self.settings_changed_cb)
		self.server = None
		self.mdns = None
		self.clients = []
		self.mpd_password = None
		self.playlist_version = 1
		self.start_time = time.time()

	def playing_song_changed_cb(self, player, entry):
		self.playlist_version += 1
		self.notify_clients("playlist")

	def playing_changed_cb(self, player, playing):
		self.notify_clients("player")

	def playing_song_property_changed_cb(self, player, uri, prop, oldvalue, newvalue):
		self.notify_clients("player")

	def playing_from_queue_changed_cb(self, pspec, data):
		self.playlist_version += 1
		self.notify_clients("playlist")

	def player_volume_changed_cb(self, pspec, data):
		self.notify_clients("player")

	def art_added_cb(self, store, key, filename, data):
		entry = self.shell_player.get_playing_entry()
		if entry is not None and self.db.entry_matches_ext_db_key(entry, key):
			self.notify_clients("player")

	def queue_row_inserted_cb(self, model, path, iter):
		self.playlist_version += 1
		self.notify_clients("playlist")

	def queue_entry_deleted_cb(self, model, entry):
		self.playlist_version += 1
		self.notify_clients("playlist")

	def queue_rows_reordered_cb(self, model, path, iter, order):
		self.playlist_version += 1
		self.notify_clients("playlist")

	def notify_clients(self, update):
		for c in self.clients:
			c.dispatch(update)

	def connection_incoming_cb(self, service, conn, obj):
		print("new connection")
		client = MPDClient(self, conn)
		self.clients.append(client)


	def connection_closed(self, client):
		print("connection closed")
		try:
			self.clients.remove(client)
		except Exception as e:
			pass

	def close_clients(self):
		cl = self.clients.copy()
		for c in cl:
			c.close()

	def start_service(self):
		self.server = Gio.SocketService()
		mode = self.settings['listen-mode']
		port = self.settings['listen-port']

		if self.mdns is None:
			self.mdns = MDNSService('Rhythmbox MPD server', '_mpd._tcp', port)
		else:
			self.mdns.port = port

		if mode == 'localhost':
			print("listening on localhost port {}".format(port))
			addr = Gio.InetSocketAddress.new_from_string("127.0.0.1", port)
			self.server.add_address(addr, Gio.SocketType.STREAM, Gio.SocketProtocol.TCP)
		elif mode == 'all':
			print("listening on port {}".format(port))
			self.server.add_inet_port(port)
			self.mdns.publish()
		elif mode == 'address':
			addrstr = self.settings['listen-address']
			print("listening on {} port {}".format(addrstr, port))
			addr = Gio.InetSocketAddress.new_from_string(addrstr, port)
			self.server.add_address(addr, Gio.SocketType.STREAM, Gio.SocketProtocol.TCP)
			self.mdns.publish()

		self.server.connect('incoming', self.connection_incoming_cb)

	def stop_service(self):
		self.mdns.unpublish()
		self.server.stop()
		self.server.close()
		self.server = None

	def settings_changed_cb(self, settings, key):
		if key in ('listen-port', 'listen-mode', 'listen-address'):
			# close listen socket, open new one
			self.stop_service()
			self.start_service()
		elif key == 'mpd-password':
			# all clients have to reauthenticate now
			# hope nothing bad happens
			for c in self.clients:
				c.authenticated = False

	def do_activate(self):
		shell = self.object
		self.db = shell.props.db

		self.artcache = os.path.join(RB.user_cache_dir(), "album-art", "")
		self.art_store = RB.ExtDB(name="album-art")
		self.art_store.connect("added", self.art_added_cb)

		self.shell_player = shell.props.shell_player
		self.shell_player.connect("playing-song-changed", self.playing_song_changed_cb)
		self.shell_player.connect("playing-song-property-changed", self.playing_song_property_changed_cb)
		self.shell_player.connect("playing-changed", self.playing_changed_cb)
		self.shell_player.connect("notify::playing-from-queue", self.playing_from_queue_changed_cb)
		self.shell_player.connect("notify::volume", self.player_volume_changed_cb)
		self.playing_song_changed_cb(self.shell_player, self.shell_player.get_playing_entry())

		queue = self.shell_player.props.queue_source.props.query_model
		queue.connect("row-inserted", self.queue_row_inserted_cb)
		queue.connect("post-entry-delete", self.queue_entry_deleted_cb)
		queue.connect("rows-reordered", self.queue_rows_reordered_cb)

		self.start_service()


	def do_deactivate(self):
		self.close_clients()
		self.stop_service()

class MPDServerConfig(GObject.Object, PeasGtk.Configurable):
	__gtype_name__ = 'MPDServerConfig'
	object = GObject.Property(type=GObject.GObject)

	def get_listen_mode(self):
		return self.listen_mode_name

	def set_listen_mode(self, mode):
		if hasattr(self, 'listen_mode_button') is False:
			return

		self.listen_mode_name = mode
		w = self.listen_mode_button.get(mode)
		if w is None:
			print("unknown listen mode {} set".format(mode))
			return

		w.set_active(True)

	listen_mode = GObject.Property(type=str, getter=get_listen_mode, setter=set_listen_mode)

	def mpdpassword_focus_out_cb(self, widget, event):
		pwd = widget.get_text()
		if pwd != self.settings['mpd-password']:
			print("changing mpd password to {}".format(pwd))
			self.settings['mpd-password'] = pwd

		return False

	def listenaddress_focus_out_cb(self, widget, event):
		addr = widget.get_text()
		if addr != self.settings['listen-address']:
			print("changing listen address to {}".format(addr))
			self.settings['listen-address'] = addr

		return False

	def portnumber_focus_out_cb(self, widget, event):
		port = int(widget.get_text())
		if port != self.settings['listen-port']:
			print("changing listen port to {}")
			self.settings['listen-port'] = port

		return False

	def settings_changed_cb(self, settings, key):
		if key == 'listen-port':
			self.portnumber_entry.set_text(str(self.settings['listen-port']))
		elif key == 'listen-address':
			self.listen_address_entry.set_text(self.settings['listen-address'])
		elif key == 'mpd-password':
			self.mpd_password_entry.set_text(self.settings['mpd-password'])
		elif key == 'publish-mdns':
			if self.settings['publish-mdns']:
				self.mdns.publish()
			else:
				self.mdns.unpublish()

	def mode_button_toggled_cb(self, widget):
		for (n, w) in self.listen_mode_button.items():
			if w == widget:
				self.listen_mode_name = n
				self.notify('listen_mode')

	def do_create_configure_widget(self):
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.mpdserver")
		self.settings.connect("changed", self.settings_changed_cb)

		ui_file = rb.find_plugin_file(self, "mpdserver-config.ui")
		self.builder = Gtk.Builder()
		self.builder.add_from_file(ui_file)

		content = self.builder.get_object("mpdserver-config")

		self.portnumber = self.builder.get_object("listen-port")
		self.portnumber.set_text(str(self.settings['listen-port']))
		self.portnumber.connect("focus-out-event", self.portnumber_focus_out_cb)

		self.listen_mode_button = {}
		for mode in ('localhost', 'all', 'address'):
			b = self.builder.get_object("listen-mode-{}".format(mode))
			self.listen_mode_button[mode] = b
			b.connect("toggled", self.mode_button_toggled_cb)
		self.settings.bind('listen-mode', self, 'listen_mode', 0)

		self.listen_address = self.builder.get_object("listen-address")
		self.listen_address.set_text(self.settings['listen-address'])
		self.listen_address.connect("focus-out-event", self.listenaddress_focus_out_cb)

		self.enable_mdns_button = self.builder.get_object("publish-mdns")
		self.settings.bind('publish-mdns', self.enable_mdns_button, 'active', 0)
		# make insensitive if avahi is not available?

		self.mpd_password = self.builder.get_object("mpd-password")
		self.mpd_password.set_text(self.settings['mpd-password'])
		self.mpd_password.connect("focus-out-event", self.mpdpassword_focus_out_cb)

		return content


GObject.type_register(MPDServerConfig)
