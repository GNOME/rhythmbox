# -*- coding: utf-8 -*-

# JamendoSource.py
#
# Copyright (C) 2007 - Guillaume Desmottes
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
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

# Parts from "Magnatune Rhythmbox plugin" (stolen from rhythmbox's MagnatuneSource.py)
#     Copyright (C), 2006 Adam Zimmerman <adam_zimmerman@sfu.ca>

import rb, rhythmdb
from JamendoSaxHandler import JamendoSaxHandler
import JamendoConfigureDialog

import os
import gobject
import gtk
import gnome, gconf
import xml
import gzip
import datetime

# URIs

jamendo_song_info_uri = "http://img.jamendo.com/data/dbdump_artistalbumtrack.xml.gz"

mp32_uri = "http://api.jamendo.com/get2/bittorrent/file/plain/?type=archive&class=mp32&album_id="
ogg3_uri = "http://api.jamendo.com/get2/bittorrent/file/plain/?type=archive&class=ogg3&album_id="


#  MP3s for streaming : http://api.jamendo.com/get2/stream/track/redirect/?id={TRACKID}&streamencoding=mp31
# OGGs for streaming : http://api.jamendo.com/get2/stream/track/redirect/?id={TRACKID}&streamencoding=ogg2

# .torrent file for download (MP3 archive) : http://api.jamendo.com/get2/bittorrent/file/plain/?album_id={ALBUMID}&type=archive&class=mp32
# .torrent file for download (OGG archive) : http://api.jamendo.com/get2/bittorrent/file/plain/?album_id={ALBUMID}&type=archive&class=ogg3

# Album Covers are available here: http://api.jamendo.com/get2/image/album/redirect/?id={ALBUMID}&imagesize={100-600}

artwork_url = "http://api.jamendo.com/get2/image/album/redirect/?id=%s&imagesize=200"
artist_url = "http://www.jamendo.com/get/artist/id/album/page/plain/"

class JamendoSource(rb.BrowserSource):
	__gproperties__ = {
		'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
	}

	def __init__(self):

		rb.BrowserSource.__init__(self, name=_("Jamendo"))

		# catalogue stuff
		self.__db = None
		self.__saxHandler = None
		self.__activated = False
		self.__notify_id = 0
		self.__update_id = 0
		self.__info_screen = None
		self.__updating = True
		self.__load_current_size = 0
		self.__load_total_size = 0
		self.__db_load_finished = False

		self.__catalogue_loader = None
		self.__catalogue_check = None

		self.__jamendo_dir = rb.find_user_cache_file("jamendo")
		if os.path.exists(self.__jamendo_dir) is False:
			os.makedirs(self.__jamendo_dir, 0700)

		self.__local_catalogue_path = os.path.join(self.__jamendo_dir, "dbdump.xml")
		self.__local_catalogue_temp = os.path.join(self.__jamendo_dir, "dbdump.xml.tmp")

	def do_set_property(self, property, value):
		if property.name == 'plugin':
			self.__plugin = value
		else:
			raise AttributeError, 'unknown property %s' % property.name

	def do_impl_get_browser_key (self):
		return "/apps/rhythmbox/plugins/jamendo/show_browser"

	def do_impl_get_paned_key (self):
		return "/apps/rhythmbox/plugins/jamendo/paned_position"

	def do_impl_can_delete (self):
		return False

	def do_impl_pack_paned (self, paned):
		self.__paned_box = gtk.VBox(False, 5)
		self.pack_start(self.__paned_box)
		self.__paned_box.pack_start(paned)

	#
	# RBSource methods
	#

	def do_impl_show_entry_popup(self):
		self.show_source_popup ("/JamendoSourceViewPopup")

	def do_impl_get_ui_actions(self):
		return ["JamendoDownloadAlbum","JamendoDonateArtist"]


	def do_impl_get_status(self):
		if self.__updating:
			if self.__load_total_size > 0:
				progress = min (float(self.__load_current_size) / self.__load_total_size, 1.0)
			else:
				progress = -1.0
			return (_("Loading Jamendo catalog"), None, progress)
		else:
			qm = self.get_property("query-model")
			return (qm.compute_status_normal("%d song", "%d songs"), None, 2.0)

	def do_impl_activate(self):
		if not self.__activated:
			shell = self.get_property('shell')
			self.__db = shell.get_property('db')
			self.__entry_type = self.get_property('entry-type')

			self.__activated = True
			self.__show_loading_screen (True)

			# start our catalogue updates
			self.__update_id = gobject.timeout_add_seconds(6 * 60 * 60, self.__update_catalogue)
			self.__update_catalogue()

			sort_key = gconf.client_get_default().get_string(JamendoConfigureDialog.gconf_keys['sorting'])
			if not sort_key:
				sort_key = "Artist,ascending"
			self.get_entry_view().set_sorting_type(sort_key)

		rb.BrowserSource.do_impl_activate (self)

	def do_impl_delete_thyself(self):
		if self.__update_id != 0:
			gobject.source_remove (self.__update_id)
			self.__update_id = 0

		if self.__notify_id != 0:
			gobject.source_remove (self.__notify_id)
			self.__notify_id = 0

		if self.__catalogue_loader:
			self.__catalogue_loader.cancel()
			self.__catalogue_loader = None

		if self.__catalogue_check:
			self.__catalogue_check.cancel()
			self.__catalogue_check = None

		gconf.client_get_default().set_string(JamendoConfigureDialog.gconf_keys['sorting'], self.get_entry_view().get_sorting_type())
		rb.BrowserSource.do_impl_delete_thyself (self)


	#
	# internal catalogue downloading and loading
	#

	def __catalogue_chunk_cb(self, result, total):
		if not result or isinstance (result, Exception):
			if result:
				# report error somehow?
				print "error loading catalogue: %s" % result

			self.__parser.close()
			self.__db_load_finished = True
			self.__updating = False
			self.__saxHandler = None
			self.__show_loading_screen (False)
			self.__catalogue_loader = None
			return

		self.__parser.feed(result)
		self.__load_current_size += len(result)
		self.__load_total_size = total
		self.__notify_status_changed()

	def __load_catalogue(self):
		print "loading catalogue %s" % self.__local_catalogue_path
		self.__notify_status_changed()
		self.__db_load_finished = False

		self.__saxHandler = JamendoSaxHandler(self.__db, self.__entry_type)
		self.__parser = xml.sax.make_parser()
		self.__parser.setContentHandler(self.__saxHandler)

		self.__catalogue_loader = rb.ChunkLoader()
		self.__catalogue_loader.get_url_chunks(self.__local_catalogue_path, 64*1024, True, self.__catalogue_chunk_cb)


	def __download_catalogue_chunk_cb (self, result, total, out):
		if not result:
			# done downloading, unzip to real location
			out.close()
			catalog = gzip.open(self.__local_catalogue_temp)
			out = open(self.__local_catalogue_path, 'w')

			while True:
				s = catalog.read(4096)
				if s == "":
					break
				out.write(s)

			out.close()
			catalog.close()
			os.unlink(self.__local_catalogue_temp)

			self.__db_load_finished = True
			self.__show_loading_screen (False)
			self.__catalogue_loader = None

			self.__load_catalogue ()

		elif isinstance(result, Exception):
			# complain
			pass
		else:
			out.write(result)
			self.__load_current_size += len(result)
			self.__load_total_size = total

		self.__notify_status_changed()

	def __download_catalogue(self):
		print "downloading catalogue"
		self.__updating = True
		out = open(self.__local_catalogue_temp, 'w')

		self.__catalogue_loader = rb.ChunkLoader()
		self.__catalogue_loader.get_url_chunks(jamendo_song_info_uri, 4*1024, True, self.__download_catalogue_chunk_cb, out)

	def __update_catalogue(self):
		def update_cb (result):
			self.__catalogue_check = None
			if result is True:
				self.__download_catalogue()
			elif self.__db_load_finished is False:
				self.__load_catalogue()

		self.__catalogue_check = rb.UpdateCheck()
		self.__catalogue_check.check_for_update(self.__local_catalogue_path, jamendo_song_info_uri, update_cb)


	def __show_loading_screen(self, show):
		if self.__info_screen is None:
			# load the builder stuff
			builder = gtk.Builder()
			builder.add_from_file(self.__plugin.find_file("jamendo-loading.ui"))

			self.__info_screen = builder.get_object("jamendo_loading_scrolledwindow")
			self.pack_start(self.__info_screen)
			self.get_entry_view().set_no_show_all (True)
			self.__info_screen.set_no_show_all (True)

		self.__info_screen.set_property("visible", show)
		self.__paned_box.set_property("visible", not show)


	def __notify_status_changed(self):
		def change_idle_cb():
			self.notify_status_changed()
			self.__notify_id = 0
			return False

		if self.__notify_id == 0:
			self.__notify_id = gobject.idle_add(change_idle_cb)


	# Download album
	def download_album (self):
		tracks = self.get_entry_view().get_selected_entries()
		format = gconf.client_get_default().get_string(JamendoConfigureDialog.gconf_keys['format'])
		if not format or format not in JamendoConfigureDialog.format_list:
			format = 'ogg3'

		#TODO: this should work if the album was selected in the browser
		#without any track selected
		if len(tracks) == 1:
			track = tracks[0]
			albumid = self.__db.entry_get(track, rhythmdb.PROP_MUSICBRAINZ_ALBUMID)

			formats = {}
			formats["mp32"] = mp32_uri + albumid
			formats["ogg3"] = ogg3_uri + albumid

			p2plink = formats[format]
			l = rb.Loader()
			l.get_url(p2plink, self.__download_p2plink, albumid)

	def __download_p2plink (self, result, albumid):
		if result is None:
			emsg = _("Error looking up p2plink for album %s on jamendo.com") % (albumid)
			gtk.MessageDialog(None, 0, gtk.MESSAGE_INFO, gtk.BUTTONS_OK, emsg).run()
			return

		gtk.show_uri(self.props.shell.props.window.get_screen(), result, gtk.gdk.CURRENT_TIME)

	# Donate to Artist
	def launch_donate (self):
		tracks = self.get_entry_view().get_selected_entries()

		#TODO: this should work if the artist was selected in the browser
		#without any track selected
		if len(tracks) == 1:
			track = tracks[0]
			# The Album ID can be used to lookup the artist, and issue a clean redirect.
			albumid = self.__db.entry_get(track, rhythmdb.PROP_MUSICBRAINZ_ALBUMID)
			artist = self.__db.entry_get(track, rhythmdb.PROP_ARTIST)
			url = artist_url + albumid.__str__() + "/"

			l = rb.Loader()
			l.get_url(url, self.__open_donate, artist)

	def __open_donate (self, result, artist):
		if result is None:
			emsg = _("Error looking up artist %s on jamendo.com") % (artist)
			gtk.MessageDialog(None, 0, gtk.MESSAGE_INFO, gtk.BUTTONS_OK, emsg).run()
			return
		gtk.show_uri(self.props.shell.props.window.get_screen(), result + "donate/", gtk.gdk.CURRENT_TIME)

	def playing_entry_changed (self, entry):
		if not self.__db or not entry:
			return

		if entry.get_entry_type() != self.__db.entry_type_get_by_name("JamendoEntryType"):
			return

		gobject.idle_add(self.emit_cover_art_uri, entry)

	def emit_cover_art_uri (self, entry):
		stream = self.__db.entry_get (entry, rhythmdb.PROP_LOCATION)
		albumid = self.__db.entry_get (entry, rhythmdb.PROP_MUSICBRAINZ_ALBUMID)
		url = artwork_url % albumid

		self.__db.emit_entry_extra_metadata_notify (entry, "rb:coverArt-uri", str(url))
		return False

gobject.type_register(JamendoSource)

