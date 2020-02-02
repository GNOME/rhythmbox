# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Adam Zimmerman  <adam_zimmerman@sfu.ca>
# Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
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
import sys
import xml
import urllib.parse, urllib.request
import threading
import zipfile

import rb
from gi.repository import RB
from gi.repository import GObject, Gtk, Gdk, Gio, GLib

from TrackListHandler import TrackListHandler
from DownloadAlbumHandler import DownloadAlbumHandler, MagnatuneDownloadError
import MagnatuneAccount

import gettext
gettext.install('rhythmbox', RB.locale_dir())

magnatune_partner_id = "rhythmbox"

# URIs
magnatune_song_info_uri = "http://magnatune.com/info/song_info_xml.zip"
magnatune_changed_uri = "http://magnatune.com/info/changed.txt"
magnatune_member_signup_uri = "http://magnatune.com/member/signup?"
magnatune_api_download_uri = "http://%s:%s@download.magnatune.com/buy/membership_free_dl_xml?"

magnatune_in_progress_dir = Gio.file_new_for_path(RB.user_data_dir()).resolve_relative_path('magnatune')
magnatune_cache_dir = Gio.file_new_for_path(RB.user_cache_dir()).resolve_relative_path('magnatune')

magnatune_song_info = os.path.join(magnatune_cache_dir.get_path(), 'song_info.xml')
magnatune_song_info_temp = os.path.join(magnatune_cache_dir.get_path(), 'song_info.zip.tmp')
magnatune_changes = os.path.join(magnatune_cache_dir.get_path(), 'changed.txt')


class MagnatuneSource(RB.BrowserSource):
	def __init__(self):
		RB.BrowserSource.__init__(self)
		self.hate = self

		self.__popup = None
		self.__settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.magnatune")
		# source state
		self.__activated = False
		self.__db = None
		self.__info_screen = None # the loading screen

		# track data
		self.__sku_dict = {}
		self.__home_dict = {}
		self.__art_dict = {}

		# catalogue stuff
		self.__has_loaded = False # whether the catalog has been loaded yet
		self.__update_id = 0 # GLib.idle_add id for catalog updates
		self.__catalogue_loader = None
		self.__catalogue_check = None
		self.__load_progress = None
		self.__download_progress = None

		# album download stuff
		self.__downloads = {} # keeps track of download progress for each file
		self.__copies = {} # keeps copy objects for each file

		self.__art_store = RB.ExtDB(name="album-art")

	#
	# RBSource methods
	#

	def do_show_entry_popup(self):
		if self.__popup is None:
			builder = Gtk.Builder()
			builder.add_from_file(rb.find_plugin_file(self.props.plugin, "magnatune-popup.ui"))
			self.__popup = builder.get_object("magnatune-popup")

		menu = Gtk.Menu.new_from_model(self.__popup)
		menu.attach_to_widget(self, None)
		menu.popup(None, None, None, None, 3, Gtk.get_current_event_time())


	def do_selected(self):
		if not self.__activated:
			shell = self.props.shell
			self.__db = shell.props.db
			self.__entry_type = self.props.entry_type

			if not magnatune_in_progress_dir.query_exists(None):
				magnatune_in_progress_path = magnatune_in_progress_dir.get_path()
				os.mkdir(magnatune_in_progress_path, 0o700)

			if not magnatune_cache_dir.query_exists(None):
				magnatune_cache_path = magnatune_cache_dir.get_path()
				os.mkdir(magnatune_cache_path, 0o700)

			self.__activated = True
			self.__show_loading_screen(True)

			# start our catalogue updates
			self.__update_id = GLib.timeout_add_seconds(6 * 60 * 60, self.__update_catalogue)
			self.__update_catalogue()

	def do_can_delete(self):
		return False

	def do_pack_content(self, content):
		self.__paned_box = Gtk.VBox(homogeneous=False, spacing=5)
		self.pack_start(self.__paned_box, True, True, 0)
		self.__paned_box.pack_start(content, True, True, 0)


	def do_delete_thyself(self):
		if self.__update_id != 0:
			GLib.source_remove(self.__update_id)
			self.__update_id = 0

		if self.__catalogue_loader is not None:
			self.__catalogue_loader.cancel()
			self.__catalogue_loader = None

		if self.__catalogue_check is not None:
			self.__catalogue_check.cancel()
			self.__catalogue_check = None

		RB.BrowserSource.do_delete_thyself(self)

	#
	# methods for use by plugin and UI
	#

	def display_artist_info(self):
		screen = self.props.shell.props.window.get_screen()
		tracks = self.get_entry_view().get_selected_entries()
		if len(tracks) == 0:
			return

		tr = tracks[0]
		sku = self.__sku_dict[tr.get_string(RB.RhythmDBPropType.LOCATION)]
		url = self.__home_dict[sku]
		Gtk.show_uri(screen, url, Gdk.CURRENT_TIME)


	def download_redirect(self):
		screen = self.props.shell.props.window.get_screen()
		tracks = self.get_entry_view().get_selected_entries()
		if len(tracks) == 0:
			return

		url = magnatune_member_signup_uri + urllib.parse.urlencode({ 'ref': magnatune_partner_id })
		Gtk.show_uri(screen, url, Gdk.CURRENT_TIME)


	def download_album(self):
		if self.__settings['account-type'] != 'download':
			# The user doesn't have a download account, so redirect them to the member signup page
			self.download_redirect()
			return

		try:
			# Just use the first library location
			library = Gio.Settings.new("org.gnome.rhythmbox.rhythmdb")
			library_location = library['locations'][0]
		except IndexError as e:
			RB.error_dialog(title = _("Couldn't download album"),
				        message = _("You must have a library location set to download an album."))
			return

		tracks = self.get_entry_view().get_selected_entries()
		skus = []

		for track in tracks:
			sku = self.__sku_dict[track.get_string(RB.RhythmDBPropType.LOCATION)]
			if sku in skus:
				continue
			skus.append(sku)
			self.__auth_download(sku)

	#
	# internal catalogue downloading and loading
	#

	def __update_catalogue(self):
		def update_cb(remote_changes):
			self.__catalogue_check = None
			try:
				f = open(magnatune_changes, 'rt')
				local_changes = f.read().strip()
			except:
				local_changes = ""

			remote_changes = remote_changes.strip().decode("iso-8859-1")
			print("local checksum %s, remote checksum %s" % (local_changes, remote_changes))
			if local_changes != remote_changes:
				try:
					f = open(magnatune_changes, 'wt')
					f.write(remote_changes + "\n")
					f.close()
				except Exception as e:
					print("unable to write local change id: %s" % str(e))

				download_catalogue()
			elif self.__has_loaded is False:
				load_catalogue()

		def download_catalogue():
			def find_song_info(catalogue):
				for info in catalogue.infolist():
					if info.filename.endswith("song_info.xml"):
						return info.filename;
				return None

			def download_progress(copy, complete, total, self):
				self.__load_progress.props.task_progress = min(float(complete) / total, 1.0)

			def download_finished(copy, success, self):
				if not success:
					print("catalog download failed")
					print(copy.get_error())
					return

				print("catalog download successful")
				# done downloading, unzip to real location
				catalog_zip = zipfile.ZipFile(magnatune_song_info_temp)
				catalog = open(magnatune_song_info, 'wb')
				filename = find_song_info(catalog_zip)
				if filename is None:
					RB.error_dialog(title=_("Unable to load catalog"),
							message=_("Rhythmbox could not understand the Magnatune catalog, please file a bug."))
					return
				catalog.write(catalog_zip.read(filename))
				catalog.close()
				catalog_zip.close()

				df = Gio.file_new_for_path(magnatune_song_info_temp)
				df.delete(None)
				self.__catalogue_loader = None

				self.__load_progress.props.task_outcome = RB.TaskOutcome.COMPLETE

				load_catalogue()

			try:
				df = Gio.file_new_for_path(magnatune_song_info_temp)
				df.delete(None)
			except:
				pass

			self.__load_progress = RB.TaskProgressSimple.new()
			self.__load_progress.props.task_label = _("Loading Magnatune catalog")
			self.props.shell.props.task_list.add_task(self.__load_progress)

			self.__catalog_loader = RB.AsyncCopy()
			self.__catalog_loader.set_progress(download_progress, self)
			self.__catalog_loader.start(magnatune_song_info_uri, magnatune_song_info_temp, download_finished, self)

		def load_catalogue():

			def catalogue_chunk_cb(loader, chunk, total, parser):
				if chunk is None:
					self.__load_progress.props.task_outcome = RB.TaskOutcome.COMPLETE
					error = loader.get_error()
					if error:
						# report error somehow?
						print("error loading catalogue: %s" % error)

					try:
						parser.close()
					except xml.sax.SAXParseException as e:
						# there isn't much we can do here
						print("error parsing catalogue: %s" % e)

					self.__show_loading_screen(False)
					self.__catalogue_loader = None

					# restart in-progress downloads
					# (doesn't really belong here)
					for f in magnatune_in_progress_dir.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NONE, None):
						name = f.get_name()
						if not name.startswith("in_progress_"):
							continue
						(result, uri, etag) = magnatune_in_progress_dir.resolve_relative_path(name).load_contents(None)
						uri = uri.decode('utf-8')

						print("restarting download from %s" % uri)
						self.__download_album(uri, name[12:])
				else:
					# hack around some weird chars that show up in the catalogue for some reason
					data = chunk.get_data().decode('utf-8', errors='replace')
					data = data.replace("\x19", "'")
					data = data.replace("\x13", "-")

					# argh.
					data = data.replace("Rock & Roll", "Rock &amp; Roll")

					try:
						parser.feed(data)
					except xml.sax.SAXParseException as e:
						print("error parsing catalogue: %s" % e)

					load_size['size'] += len(data)
					self.__load_progress.props.task_progress = min(float(load_size['size']) / total, 1.0)


			self.__has_loaded = True
			self.__load_progress = RB.TaskProgressSimple.new()
			self.__load_progress.props.task_label = _("Loading Magnatune catalog")
			self.props.shell.props.task_list.add_task(self.__load_progress)

			load_size = {'size': 0}

			parser = xml.sax.make_parser()
			parser.setContentHandler(TrackListHandler(self.__db, self.__entry_type, self.__sku_dict, self.__home_dict, self.__art_dict))

			self.__catalogue_loader = RB.ChunkLoader()
			self.__catalogue_loader.set_callback(catalogue_chunk_cb, parser)
			self.__catalogue_loader.start(magnatune_song_info, 64*1024)


		self.__catalogue_check = rb.Loader()
		self.__catalogue_check.get_url(magnatune_changed_uri, update_cb)


	def __show_loading_screen(self, show):
		if self.__info_screen is None:
			# load the builder stuff
			builder = Gtk.Builder()
			builder.add_from_file(rb.find_plugin_file(self.props.plugin, "magnatune-loading.ui"))
			self.__info_screen = builder.get_object("magnatune_loading_scrolledwindow")
			self.pack_start(self.__info_screen, True, True, 0)
			self.get_entry_view().set_no_show_all(True)
			self.__info_screen.set_no_show_all(True)

		self.__info_screen.set_property("visible", show)
		self.__paned_box.set_property("visible", not show)

	def __notify_status_changed(self):
		pass

	#
	# internal purchasing code
	#

	def __auth_download(self, sku): # http://magnatune.com/info/api

		def auth_data_cb(data, userpass):
			(username, password) = userpass
			dl_album_handler = DownloadAlbumHandler(self.__settings['format'])
			auth_parser = xml.sax.make_parser()
			auth_parser.setContentHandler(dl_album_handler)

			if data is None:
				# hmm.
				return

			try:
				data = data.decode("utf-8")
				data = data.replace("<br>", "") # get rid of any stray <br> tags that will mess up the parser
				data = data.replace(" & ", " &amp; ") # clean up some missing escaping
				# print data
				auth_parser.feed(data)
				auth_parser.close()

				# process the URI: add authentication info, quote the filename component for some reason
				parsed = urllib.parse.urlparse(dl_album_handler.url)
				netloc = "%s:%s@%s" % (username, password, parsed.hostname)

				spath = os.path.split(urllib.request.url2pathname(parsed.path))
				basename = spath[1]
				path = urllib.request.pathname2url(os.path.join(spath[0], urllib.parse.quote(basename)))

				authed = (parsed[0], netloc, path) + parsed[3:]
				audio_dl_uri = urllib.parse.urlunparse(authed)

				print("download uri for %s is %s" % (sku, audio_dl_uri))
				self.__download_album(audio_dl_uri, sku)

			except MagnatuneDownloadError as e:
				RB.error_dialog(title = _("Download Error"),
						message = _("An error occurred while trying to authorize the download.\nThe Magnatune server returned:\n%s") % str(e))
			except Exception as e:
				sys.excepthook(*sys.exc_info())
				RB.error_dialog(title = _("Error"),
						message = _("An error occurred while trying to download the album.\nThe error text is:\n%s") % str(e))

		print("downloading album: " + sku)
		account = MagnatuneAccount.instance()
		(account_type, username, password) = account.get()
		url_dict = {
			'id':	magnatune_partner_id,
			'sku':	sku
		}
		url = magnatune_api_download_uri % (username, password)
		url = url + urllib.parse.urlencode(url_dict)

		l = rb.Loader()
		l.get_url(url, auth_data_cb, (username, password))


	def __download_album(self, audio_dl_uri, sku):
		def update_progress(self):
			if len(self.__downloads) == 0:
				self.__download_progress.props.task_outcome = RB.TaskOutcome.COMPLETE
				self.__download_progress = None
			else:
				complete, total = map(sum, zip(*self.__downloads.values()))
				if total > 0:
					self.__download_progress.props.task_progress = min(float(complete) / total, 1.0)

		def download_progress(copy, complete, total, self):
			self.__downloads[audio_dl_uri] = (complete, total)
			update_progress(self)

		def download_finished(copy, success, self):
			del self.__downloads[audio_dl_uri]
			del self.__copies[audio_dl_uri]

			print("download of %s finished: %s" % (audio_dl_uri, success))
			if success:
				threading.Thread(target=unzip_album).start()
			else:
				remove_download_files()

			update_progress(self)


		def unzip_album():
			# just use the first library location
			library = Gio.Settings.new("org.gnome.rhythmbox.rhythmdb")
			library_location = Gio.file_new_for_uri(library['locations'][0])

			print("unzipping %s" % dest.get_path())
			album = zipfile.ZipFile(dest.get_path())
			for track in album.namelist():
				track_uri = library_location.resolve_relative_path(track).get_uri()
				print("zip file entry: %s => %s" % (track, track_uri))

				track_uri = RB.sanitize_uri_for_filesystem(track_uri)
				RB.uri_create_parent_dirs(track_uri)

				track_out = Gio.file_new_for_uri(track_uri).replace(None, False, Gio.FileCreateFlags.NONE, None)
				if track_out is not None:
					track_out.write(album.read(track), None)
					track_out.close(None)
					print("adding %s to library" % track_uri)
					self.__db.add_uri(track_uri)

			album.close()
			remove_download_files()

		def remove_download_files():
			print("removing download files")
			in_progress.delete(None)
			dest.delete(None)

		in_progress = magnatune_in_progress_dir.resolve_relative_path("in_progress_" + sku)
		dest = magnatune_in_progress_dir.resolve_relative_path(sku)

		in_progress.replace_contents(audio_dl_uri.encode('utf-8'),
					     None,
					     False,
					     Gio.FileCreateFlags.PRIVATE|Gio.FileCreateFlags.REPLACE_DESTINATION,
					     None)

		if self.__download_progress is None:
			self.__download_progress = RB.TaskProgressSimple.new()
			self.__download_progress.props.task_label = _("Downloading from Magnatune")
			self.__download_progress.connect('cancel-task', self.cancel_downloads)
			self.props.shell.props.task_list.add_task(self.__download_progress)

		# one async copy object per download
		if audio_dl_uri not in self.__copies:
			dl = RB.AsyncCopy()
			dl.set_progress(download_progress, self)
			dl.start(audio_dl_uri, dest.get_uri(), download_finished, self)
			self.__downloads[audio_dl_uri] = (0, 0) # (current, total)
			self.__copies[audio_dl_uri] = dl

	def cancel_downloads(self, task):
		for download in self.__copies.values():
			download.cancel()

		task.props.task_outcome = RB.TaskOutcome.CANCELLED

	def playing_entry_changed(self, entry):
		if not self.__db or not entry:
			return
		if entry.get_entry_type() != self.__db.entry_type_get_by_name("magnatune"):
			return

		sku = self.__sku_dict[entry.get_string(RB.RhythmDBPropType.LOCATION)]
		key = RB.ExtDBKey.create_storage("album", entry.get_string(RB.RhythmDBPropType.ALBUM))
		key.add_field("artist", entry.get_string(RB.RhythmDBPropType.ARTIST))
		self.__art_store.store_uri(key, RB.ExtDBSourceType.EMBEDDED, self.__art_dict[sku])

GObject.type_register(MagnatuneSource)
