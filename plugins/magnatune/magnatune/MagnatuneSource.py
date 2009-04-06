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

import rb, rhythmdb
from TrackListHandler import TrackListHandler
from BuyAlbumHandler import BuyAlbumHandler, MagnatunePurchaseError

import os
import gobject
import gtk.glade
import gnome, gconf
import xml
import urllib
import urlparse
import zipfile

has_gnome_keyring = False

#try:
#	import gnomekeyring
#	has_gnome_keyring = True
#except:
#	pass


magnatune_partner_id = "rhythmbox"

# URIs
magnatune_song_info_uri = "http://magnatune.com/info/song_info_xml.zip"

magnatune_in_progress_dir = os.path.join(rb.user_data_dir(), 'magnatune')
magnatune_cache_dir = os.path.join(rb.user_cache_dir(), 'magnatune')

magnatune_song_info = os.path.join(magnatune_cache_dir, 'song_info.xml')
magnatune_song_info_temp = os.path.join(magnatune_cache_dir, 'song_info.zip.tmp')


ALBUM_ART_URL = 'http://www.magnatune.com/music/%s/%s/cover.jpg'

class MagnatuneSource(rb.BrowserSource):
	__gproperties__ = {
		'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
	}

	__client = gconf.client_get_default()


	def __init__(self):

		rb.BrowserSource.__init__(self, name=_("Magnatune"))
		self.__db = None

		# track data
		self.__sku_dict = {}
		self.__home_dict = {}
		self.__buy_dict = {}
		self.__art_dict = {}

		# catalogue stuff
		self.__activated = False
		self.__notify_id = 0
		self.__update_id = 0
		self.__catalogue_loader = None
		self.__catalogue_check = None
		self.__info_screen = None
		self.__updating = True
		self.__has_loaded = False
		self.__load_current_size = 0
		self.__load_total_size = 0

		self.__downloads = {} # keeps track of amount downloaded for each file
		self.__downloading = False # keeps track of whether we are currently downloading an album
		self.__download_progress = 0.0 # progress of current download(s)
		self.purchase_filesize = 0 # total amount of bytes to download



	def do_set_property(self, property, value):
		if property.name == 'plugin':
			self.__plugin = value
		else:
			raise AttributeError, 'unknown property %s' % property.name

	#
	# RBSource methods
	#

	def do_impl_show_entry_popup(self):
		self.show_source_popup ("/MagnatuneSourceViewPopup")

	def do_impl_get_status(self):
		if self.__updating:
			if self.__load_total_size > 0:
				progress = min (float(self.__load_current_size) / self.__load_total_size, 1.0)
			else:
				progress = -1.0
			return (_("Loading Magnatune catalog"), None, progress)
		elif self.__downloading:
			progress = min (self.__download_progress, 1.0)
			return (_("Downloading Magnatune Album(s)"), None, progress)
		else:
			qm = self.get_property("query-model")
			return (qm.compute_status_normal("%d song", "%d songs"), None, 0.0)

	def do_impl_get_ui_actions(self):
		return ["MagnatunePurchaseAlbum",
			"MagnatunePurchaseCD",
			"MagnatuneArtistInfo",
			"MagnatuneCancelDownload"]

	def do_impl_activate(self):
		if not self.__activated:
			shell = self.get_property('shell')
			self.__db = shell.get_property('db')
			self.__entry_type = self.get_property('entry-type')

			# move files from old ~/.gnome2 paths
			if os.path.exists(magnatune_in_progress_dir) is False:
				self.__move_data_files()

			self.__activated = True
			self.__show_loading_screen (True)

			# start our catalogue updates
			self.__update_id = gobject.timeout_add(6 * 60 * 60 * 1000, self.__update_catalogue)
			self.__update_catalogue()

			self.get_entry_view().set_sorting_type(self.__client.get_string("/apps/rhythmbox/plugins/magnatune/sorting"))

		rb.BrowserSource.do_impl_activate (self)

	def do_impl_get_browser_key (self):
		return "/apps/rhythmbox/plugins/magnatune/show_browser"

	def do_impl_get_paned_key (self):
		return "/apps/rhythmbox/plugins/magnatune/paned_position"

	def do_impl_pack_paned (self, paned):
		self.__paned_box = gtk.VBox(False, 5)
		self.pack_start(self.__paned_box)
		self.__paned_box.pack_start(paned)


	def do_impl_delete_thyself(self):
		if self.__update_id != 0:
			gobject.source_remove (self.__update_id)
			self.__update_id = 0

		if self.__notify_id != 0:
			gobject.source_remove (self.__notify_id)
			self.__notify_id = 0

		if self.__catalogue_loader is not None:
			self.__catalogue_loader.cancel()
			self.__catalogue_loader = None

		if self.__catalogue_check is not None:
			self.__catalogue_check.cancel()
			self.__catalogue_check = None

		self.__client.set_string("/apps/rhythmbox/plugins/magnatune/sorting", self.get_entry_view().get_sorting_type())

		rb.BrowserSource.do_impl_delete_thyself (self)

	#
	# methods for use by plugin and UI
	#

	def display_artist_info(self):
		tracks = self.get_entry_view().get_selected_entries()
		urls = set([])

		for tr in tracks:
			sku = self.__sku_dict[self.__db.entry_get(tr, rhythmdb.PROP_LOCATION)]
			url = self.__home_dict[sku]
			if url not in urls:
				rb.show_uri(url)
				urls.add(url)

	def buy_cd(self):
		tracks = self.get_entry_view().get_selected_entries()
		urls = set([])

		for tr in tracks:
			sku = self.__sku_dict[self.__db.entry_get(tr, rhythmdb.PROP_LOCATION)]
			url = self.__buy_dict[sku]
			if url not in urls:
				rb.show_uri(url)
				urls.add(url)

	def radio_toggled(self, gladexml):
		gc = gladexml.get_widget("radio_gc").get_active()
		gladexml.get_widget("remember_cc_details").set_sensitive(not gc)
		gladexml.get_widget("name_entry").set_sensitive(not gc)
		gladexml.get_widget("cc_entry").set_sensitive(not gc)
		gladexml.get_widget("mm_entry").set_sensitive(not gc)
		gladexml.get_widget("yy_entry").set_sensitive(not gc)
		
		gladexml.get_widget("gc_entry").set_sensitive(gc)
		if not gc:
			gladexml.get_widget("gc_entry").set_text("")
	
	def purchase_album(self):
		try:
			library_location = self.__client.get_list("/apps/rhythmbox/library_locations", gconf.VALUE_STRING)[0] # Just use the first library location
		except IndexError, e:
			rb.error_dialog(title = _("Couldn't purchase album"),
				        message = _("You must have a library location set to purchase an album."))
			return

		tracks = self.get_entry_view().get_selected_entries()
		skus = []

		for track in tracks:
			sku = self.__sku_dict[self.__db.entry_get(track, rhythmdb.PROP_LOCATION)]
			if sku in skus:
				continue
			skus.append(sku)
			artist = self.__db.entry_get(track, rhythmdb.PROP_ARTIST)
			album = self.__db.entry_get(track, rhythmdb.PROP_ALBUM)

			gladexml = gtk.glade.XML(self.__plugin.find_file("magnatune-purchase.glade"))
			cb_dict = {"rb_magnatune_on_radio_cc_toggled_cb":lambda w:self.radio_toggled(gladexml)}
			gladexml.signal_autoconnect(cb_dict)
			
			gladexml.get_widget("gc_entry").set_sensitive(False)
			gladexml.get_widget("pay_combobox").set_active(self.__client.get_int(self.__plugin.gconf_keys['pay']) - 5)
			gladexml.get_widget("audio_combobox").set_active(self.__plugin.format_list.index(self.__client.get_string(self.__plugin.gconf_keys['format'])))
			gladexml.get_widget("info_label").set_markup(_("Would you like to purchase the album <i>%(album)s</i> by '%(artist)s'?") % {"album":album, "artist":artist})
			gladexml.get_widget("remember_cc_details").props.visible = has_gnome_keyring

			try:
				(ccnumber, ccyear, ccmonth, name, email) = self.__plugin.get_cc_details()
				gladexml.get_widget("cc_entry").set_text(ccnumber)
				gladexml.get_widget("yy_entry").set_text(ccyear)
				gladexml.get_widget("mm_entry").set_active(ccmonth-1)
				gladexml.get_widget("name_entry").set_text(name)
				gladexml.get_widget("email_entry").set_text(email)

				gladexml.get_widget("remember_cc_details").set_active(True)
			except Exception, e:
				print e

				gladexml.get_widget("cc_entry").set_text("")
				gladexml.get_widget("yy_entry").set_text("")
				gladexml.get_widget("mm_entry").set_active(0)
				gladexml.get_widget("name_entry").set_text("")
				gladexml.get_widget("email_entry").set_text("")

				gladexml.get_widget("remember_cc_details").set_active(False)

			window = gladexml.get_widget("purchase_dialog")
			if window.run() == gtk.RESPONSE_ACCEPT:
				amount = gladexml.get_widget("pay_combobox").get_active() + 5
				format = self.__plugin.format_list[gladexml.get_widget("audio_combobox").get_active()]
				ccnumber = gladexml.get_widget("cc_entry").get_text()
				ccyear = gladexml.get_widget("yy_entry").get_text()
				ccmonth = str(gladexml.get_widget("mm_entry").get_active() + 1).zfill(2)
				name = gladexml.get_widget("name_entry").get_text()
				email = gladexml.get_widget("email_entry").get_text()
				gc = gladexml.get_widget("radio_gc").get_active()
				gc_text = gladexml.get_widget("gc_entry").get_text()

				if gladexml.get_widget("remember_cc_details").props.active:
					self.__plugin.store_cc_details(ccnumber, ccyear, ccmonth, name, email)
				else:
					self.__plugin.clear_cc_details()

				self.__buy_album (sku, amount, format, ccnumber, ccyear, ccmonth, name, email, gc, gc_text)

			window.destroy()

	#
	# internal catalogue downloading and loading
	#

	def __catalogue_chunk_cb(self, result, total, parser):
		if not result or isinstance (result, Exception):
			if result:
				# report error somehow?
				print "error loading catalogue: %s" % result

			try:
				parser.close ()
			except xml.sax.SAXParseException, e:
				# there isn't much we can do here
				print "error parsing catalogue: %s" % e

			self.__show_loading_screen (False)
			self.__updating = False
			self.__catalogue_loader = None

			# restart in-progress downloads
			# (doesn't really belong here)
			inprogress = os.listdir(magnatune_in_progress_dir)
			inprogress = filter(lambda x: x.startswith("in_progress_"), inprogress)
			for ip in inprogress:
				for uri in open(ip).readlines():
					print "restarting download from %s" % uri
					self.__download_album(uri)

		else:
			# hack around some weird chars that show up in the catalogue for some reason
			result = result.replace("\x19", "'")
			result = result.replace("\x13", "-")

			try:
				parser.feed(result)
			except xml.sax.SAXParseException, e:
				print "error parsing catalogue: %s" % e

			self.__load_current_size += len(result)
			self.__load_total_size = total

		self.__notify_status_changed()


	def __load_catalogue(self):
		self.__notify_status_changed()
		self.__has_loaded = True

		parser = xml.sax.make_parser()
		parser.setContentHandler(TrackListHandler(self.__db, self.__entry_type, self.__sku_dict, self.__home_dict, self.__buy_dict, self.__art_dict))
		
		self.__catalogue_loader = rb.ChunkLoader()
		self.__catalogue_loader.get_url_chunks(magnatune_song_info, 64*1024, True, self.__catalogue_chunk_cb, parser)



	def __find_song_info(self, catalogue):
		for info in catalogue.infolist():
			if info.filename.endswith("song_info.xml"):
				return info.filename;
		return None


	def __download_catalogue_chunk_cb (self, result, total, out):
		if not result:
			# done downloading, unzip to real location
			out.close()

			catalog = zipfile.ZipFile(magnatune_song_info_temp)
			out = open(magnatune_song_info, 'w')
			filename = self.__find_song_info(catalog)
			if filename is None:
				rb.error_dialog(title=_("Unable to load catalog"),
						message=_("Rhythmbox could not understand the Magnatune catalog, please file a bug."))
				return
			out.write(catalog.read(filename))
			out.close()
			catalog.close()

			os.unlink(magnatune_song_info_temp)
			self.__updating = False
			self.__catalogue_loader = None
			self.__load_catalogue()

		elif isinstance(result, Exception):
			# complain
			pass
		else:
			out.write(result)
			self.__load_current_size += len(result)
			self.__load_total_size = total

		self.__notify_status_changed()


	def __download_catalogue(self):
		self.__updating = True

		out = open(magnatune_song_info_temp, 'w')

		self.__catalogue_loader = rb.ChunkLoader()
		self.__catalogue_loader.get_url_chunks(magnatune_song_info_uri, 4*1024, True, self.__download_catalogue_chunk_cb, out)


	def __update_catalogue(self):
		def update_cb (result):
			self.__catalogue_check = None
			if result is True:
				self.__download_catalogue()
			elif self.__has_loaded is False:
				self.__load_catalogue()

		self.__catalogue_check = rb.UpdateCheck()
		self.__catalogue_check.check_for_update(magnatune_song_info, magnatune_song_info_uri, update_cb)


	def __show_loading_screen(self, show):
		if self.__info_screen is None:
			# load the glade stuff
			gladexml = gtk.glade.XML(self.__plugin.find_file("magnatune-loading.glade"), root="magnatune_loading_scrolledwindow")
			self.__info_screen = gladexml.get_widget("magnatune_loading_scrolledwindow")
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

	#
	# internal purchasing code
	#
	def __buy_album(self, sku, pay, format, ccnumber, ccyear, ccmonth, name, email, gc, gc_text): # http://magnatune.com/info/api#purchase
		print "purchasing tracks:", sku, pay, format, name, email
		url_dict = {
						'id':	magnatune_partner_id,
						'sku':	sku,
						'amount': pay,
						'name': name,
						'email':email
			}
		if gc:
			url_dict['gc'] = gc
		else:
			url_dict['cc'] = ccnumber
			url_dict['yy'] = ccyear
			url_dict['mm'] = ccmonth
		url = "https://magnatune.com/buy/buy_dl_cc_xml?"
		url = url + urllib.urlencode(url_dict)

		self.__wait_dlg = gtk.Dialog(title=_("Authorizing Purchase"), flags=gtk.DIALOG_NO_SEPARATOR|gtk.DIALOG_DESTROY_WITH_PARENT)
		lbl = gtk.Label(_("Authorizing purchase with the Magnatune server. Please wait..."))
		self.__wait_dlg.vbox.pack_start(lbl)
		lbl.show()
		self.__wait_dlg.show()

		l = rb.Loader()
		l.get_url (url, self.__auth_data_cb, format)



	def __auth_data_cb (self, data, format):

		buy_album_handler = BuyAlbumHandler(format)
		auth_parser = xml.sax.make_parser()
		auth_parser.setContentHandler(buy_album_handler)

		if data is None:
			# hmm.
			return

		self.__wait_dlg.destroy()
		try:
			data = data.replace("<br>", "") # get rid of any stray <br> tags that will mess up the parser
			# print data
			auth_parser.feed(data)
			auth_parser.close()

			# process the URI: add authentication info, quote the filename component for some reason

			parsed = urlparse.urlparse(buy_album_handler.url)
			netloc = "%s:%s@%s" % (str(buy_album_handler.username), str(buy_album_handler.password), parsed.hostname)

			spath = os.path.split(urllib.url2pathname(parsed.path))
			basename = spath[1]
			path = urllib.pathname2url(os.path.join(spath[0], urllib.quote(basename)))

			authed = (parsed[0], netloc, path) + parsed[3:]
			audio_dl_uri = urlparse.urlunparse(authed)

			in_progress = open(os.path.join(magnatune_in_progress_dir, "in_progress_" + basename), 'w')
			in_progress.write(str(audio_dl_uri))
			in_progress.close()

			self.__download_album(audio_dl_uri)

		except MagnatunePurchaseError, e:
			rb.error_dialog(title = _("Purchase Error"),
					message = _("An error occurred while trying to purchase the album.\nThe Magnatune server returned:\n%s") % str(e))

		except Exception, e:
			rb.error_dialog(title = _("Error"),
					message = _("An error occurred while trying to purchase the album.\nThe error text is:\n%s") % str(e))




	def __download_album(self, audio_dl_uri):

		fullpath = urlparse.urlparse(audio_dl_uri).path
		basename = os.split(fullpath)[1]
		destpath = os.path.join(magnatune_in_progress_dir, basename)

		shell = self.get_property('shell')
		manager = shell.get_player().get_property('ui-manager')
		manager.get_action("/MagnatuneSourceViewPopup/MagnatuneCancelDownload").set_sensitive(True)
		self.__downloading = True
		self.cancelled = False

		self.__downloads[audio_dl_uri] = 0

		# no way to resume downloads, sadly
		out = open(destpath, 'w')

		dl = rb.ChunkLoader()
		dl.get_url_chunks(audio_dl_uri, 4*1024, True, self.__download_album_chunk, (audio_dl_uri, destpath, out))


	def __remove_download_files (self, dest):
		sp = os.path.split(dest)
		inprogress = os.path.join(sp[0], "in_progress_" + sp[1])
		os.unlink(inprogress)
		os.unlink(dest)


	def __download_finished (self, total, audio_dl_uri, dest, out):
		try:
			del self.__downloads[audio_dl_uri]
		except:
			return 0

		out.close()
		self.purchase_filesize -= total

		# just use the first library location
		# not quite prepared to use gio here directly yet, so we can only deal with
		# local libraries here.
		library_location = self.__client.get_list("/apps/rhythmbox/library_locations", gconf.VALUE_STRING)[0]
		if library_location.startswith("file://"):
			urlpath = urlparse.urlparse(library_location).path
			library_dir = urllib.url2pathname(urlpath)
		else:
			library_dir = rb.music_dir ()

		album = zipfile.ZipFile(dest)
		for track in album.namelist():
			track_uri = "file://" + urllib.pathname2url(os.path.join(library_dir, track))

			track_uri = rb.sanitize_uri_for_filesystem(track_uri)
			rb.uri_create_parent_dirs(track_uri)

			track_path = urllib.url2pathname(urlparse.urlparse(track_uri).path)

			track_out = open(track_path, 'w')
			track_out.write(album.read(track))
			track_out.close()

		album.close()

		self.__remove_download_files (dest)

		if self.purchase_filesize == 0:
			self.__downloading = False

		self.__db.add_uri(os.path.split(track_path)[0])


	def __download_album_chunk(self, result, total, (audio_dl_uri, dest, out)):

		if not result:
			self.__download_finished (total, audio_dl_uri, dest, out)
		elif isinstance(result, Exception):
			# probably report this somehow?
			pass
		elif self.cancelled:
			try:
				del self.__downloads[audio_dl_uri]
				self.purchase_filesize -= total

				self.__remove_download_files (dest)

			except:
				pass

			if self.purchase_filesize == 0:
				self.__downloading = False

			return False
		else:
			if self.__downloads[audio_dl_uri] == 0:
				self.purchase_filesize += total

			out.write(result)
			self.__downloads[audio_dl_uri] += len(result)

			self.__download_progress = sum(self.__downloads.values()) / float(self.purchase_filesize)
			self.__notify_status_changed()


	def cancel_downloads(self):
		self.cancelled = True
		shell = self.get_property('shell')
		manager = shell.get_player().get_property('ui-manager')
		manager.get_action("/MagnatuneSourceViewPopup/MagnatuneCancelDownload").set_sensitive(False)

	def playing_entry_changed (self, entry):
		if not self.__db or not entry:
			return

		if entry.get_entry_type() != self.__db.entry_type_get_by_name("MagnatuneEntryType"):
			return

		gobject.idle_add (self.emit_cover_art_uri, entry)

	def emit_cover_art_uri (self, entry):
		sku = self.__sku_dict[self.__db.entry_get(entry, rhythmdb.PROP_LOCATION)]
		url = self.__art_dict[sku]
		self.__db.emit_entry_extra_metadata_notify (entry, 'rb:coverArt-uri', url)
		return False

	def __move_data_files (self):
		# create cache and data directories
		# (we know they don't already exist, and we know the parent dirs do)
		os.mkdir(magnatune_in_progress_dir, 0700)
		if os.path.exists(magnatune_cache_dir) is False:
			os.mkdir(magnatune_cache_dir, 0700)

		# move song info to cache dir
		old_magnatune_dir = os.path.join(rb.dot_dir(), 'magnatune')
		if os.path.exists(old_magnatune_dir) is False:
			print "old magnatune directory does not exist"
			return

		old_song_info = os.path.join(old_magnatune_dir, 'song_info.xml')
		if os.path.exists(old_song_info):
			print "moving existing song_info.xml to cache dir"
			os.rename(old_song_info, magnatune_song_info)
		else:
			print "no song_info.xml found (%s)" % old_song_info

		# move in progress downloads to data dir
		otherfiles = os.listdir(old_magnatune_dir)
		for f in otherfiles:
			print "moving file %s to new in-progress dir" % f
			os.rename(os.path.join(old_magnatune_dir, f),
				  os.path.join(magnatune_in_progress_dir, f))


gobject.type_register(MagnatuneSource)

