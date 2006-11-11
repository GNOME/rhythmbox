import rb, rhythmdb
from TrackListHandler import TrackListHandler
from BuyAlbumHandler import BuyAlbumHandler, MagnatunePurchaseError

import gobject
import gtk.glade
import gnomevfs, gnome, gconf
import xml
import urllib, zipfile

magnatune_partner_id = "zimmerman"

# URIs
magnatune_dir = gnome.user_dir_get() + "rhythmbox/magnatune/"
magnatune_song_info_uri = gnomevfs.URI("http://magnatune.com/info/song_info_xml.zip")
local_song_info_uri = gnomevfs.URI(magnatune_dir + "song_info.xml")
local_song_info_temp_uri = gnomevfs.URI(magnatune_dir + "song_info.xml.zip.tmp")


class MagnatuneSource(rb.BrowserSource):
	__gproperties__ = {
		'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
	}

	__client = gconf.client_get_default()
	
	
	def __init__(self):

		rb.BrowserSource.__init__(self, name=_("Magnatune"))

		# track data
		self.__sku_dict = {}
		self.__home_dict = {}

		# catalogue stuff
		self.__activated = False
		self.__notify_id = 0
		self.__update_id = 0
		self.__xfer_handle = None
		self.__info_screen = None
		self.__updating = True
		self.__has_loaded = False
		self.__load_handle = None
		self.__load_current_size = 0
		self.__load_total_size = 1
		
		self.__downloads = {} # keeps track of amount downloaded for each file
		self.__downloading = False # keeps track of whether we are currently downloading an album
		self.__download_progress = 0.0 # progress of current download(s)
		self.purchase_filesize = 0 # total amount of bytes to download
	
	def do_set_property(self, property, value):
		if property.name == 'plugin':
			self.__plugin = value

			# we have to wait until we get the plugin to do this
			circle_file_name = self.__plugin.find_file("magnatune_circle_small.png")
			width, height = gtk.icon_size_lookup(gtk.ICON_SIZE_LARGE_TOOLBAR)
			icon = gtk.gdk.pixbuf_new_from_file_at_size(circle_file_name, width, height)
			self.set_property("icon", icon)

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
				progress = 0.0
			return (_("Loading Magnatune catalogue"), None, progress)
		elif self.__downloading:
			progress = min (self.__download_progress, 1.0)
			return (_("Downloading Magnatune Album(s)"), None, progress)
		else:
			qm = self.get_property("query-model")
			return (qm.compute_status_normal("song", "songs"), None, 0.0)
	
	def do_impl_get_ui_actions(self):
		return ["MagnatunePurchaseAlbum",
			"MagnatuneArtistInfo",
			"MagnatuneCancelDownload"]

	def do_impl_activate(self):
		if not self.__activated:
			shell = self.get_property('shell')
			self.__db = shell.get_property('db')
			self.__entry_type = self.get_property('entry-type')

			self.__activated = True
			self.__show_loading_screen (True)
			self.__load_catalogue()

			# start our catalogue updates
			self.__update_id = gobject.timeout_add(6 * 60 * 60 * 1000, self.__update_catalogue)
			self.__update_catalogue()
			
			self.get_entry_view().set_sorting_type(self.__client.get_string("/apps/rhythmbox/plugins/magnatune/sorting"))

		rb.BrowserSource.do_impl_activate (self)
	
#	def do_impl_get_browser_key (self):
#		return "/apps/rhythmbox/plugins/magnatune/show_browser"
#
#	def do_impl_get_paned_key (self):
#		return "/apps/rhythmbox/plugins/magnatune/paned_position"
	
	def do_impl_delete_thyself(self):
		if self.__update_id != 0:
			gobject.source_remove (self.__update_id)
			self.__update_id = 0

		if self.__notify_id != 0:
			gobject.source_remove (self.__notify_id)
			self.__notify_id = 0

		if self.__xfer_handle is not None:
			self.__xfer_handle.close(lambda handle, exc: None) #FIXME: report it?
			self.__xfer_handle = None
		
		self.__client.set_string("/apps/rhythmbox/plugins/magnatune/sorting", self.get_entry_view().get_sorting_type())
		
		rb.BrowserSource.do_impl_delete_thyself (self)


	#
	# methods for use by plugin and UI
	#


	def display_artist_info(self):
		tracks = self.get_entry_view().get_selected_entries()
		urls = set([])

		for tr in tracks:
			url = self.__home_dict[self.__db.entry_get(tr, rhythmdb.PROP_LOCATION)]
			if url not in urls:
				gnomevfs.url_show(url)
				urls.add(url)

	def purchase_tracks(self):
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
			pay_combo = gladexml.get_widget("pay_combobox")
			format_combo = gladexml.get_widget("audio_combobox")
			text_label = gladexml.get_widget("info_label")

			pay_combo.set_active(self.__client.get_int(self.__plugin.gconf_keys['pay']) - 5)
			format_combo.set_active(self.__plugin.format_list.index(self.__client.get_string(self.__plugin.gconf_keys['format'])))
			text_label.set_text(_("Would you like to purchase the album '%s' by '%s'.") % (album, artist))
			
			if self.__client.get_bool(self.__plugin.gconf_keys['forget']):
				gladexml.get_widget("name_label").visible = True
				gladexml.get_widget("name_entry").visible = True
				gladexml.get_widget("cc_label").visible = True
				gladexml.get_widget("cc_entry").visible = True
				gladexml.get_widget("mm_label").visible = True
				gladexml.get_widget("mm_combobox").visible = True
				gladexml.get_widget("yy_label").visible = True
				gladexml.get_widget("yy_entry").visible = True
				gladexml.get_widget("name_label").show()
				gladexml.get_widget("name_entry").show()
				gladexml.get_widget("cc_label").show()
				gladexml.get_widget("cc_entry").show()
				gladexml.get_widget("mm_label").show()
				gladexml.get_widget("mm_combobox").show()
				gladexml.get_widget("yy_label").show()
				gladexml.get_widget("yy_entry").show()
			
			window = gladexml.get_widget("purchase_dialog")
			if window.run() == gtk.RESPONSE_ACCEPT:
				if self.__client.get_bool(self.__plugin.gconf_keys['forget']):
					self.__tmp_name = gladexml.get_widget("name_entry").get_text()
					self.__tmp_cc = gladexml.get_widget("cc_entry").get_text()
					self.__tmp_mm = gladexml.get_widget("mm_combobox").get_active_text()
					self.__tmp_yy = gladexml.get_widget("yy_entry").get_text()
				self.__purchase_album (sku, pay_combo.get_active() + 5, self.__plugin.format_list[format_combo.get_active()])

			window.destroy()

	#
	# internal catalogue downloading and loading
	#
	def __load_catalogue_read_cb (self, handle, data, exc_type, bytes_requested, parser):
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				# successfully loaded
				gtk.gdk.threads_enter()
				self.__show_loading_screen (False)
				
				in_progress_dir = gnomevfs.DirectoryHandle(gnomevfs.URI(magnatune_dir))
				in_progress = in_progress_dir.next()
				while True:
					if in_progress.name[0:12] == "in_progress_":
						in_progress = gnomevfs.read_entire_file(magnatune_dir + in_progress.name)
						for uri in in_progress.split("\n"):
							if uri == '':
								continue
							self.__download_album(gnomevfs.URI(uri))
					try:
						in_progress = in_progress_dir.next()
					except:
						break
				gtk.gdk.threads_leave()
			else:
				# error reading file
				raise exc_type
			
			parser.close()
			handle.close(lambda handle, exc: None) # FIXME: report it?
			self.__load_handle = None
			self.__updating = False
			self.__notify_status_changed()
 		else:

			parser.feed(data)
			handle.read(64 * 1024, self.__load_catalogue_read_cb, parser)

		self.__notify_status_changed()

	def __load_catalogue_open_cb (self, handle, exc_type):
		if exc_type:
			self.__load_handle = None
			self.__notify_status_changed()
			
			if gnomevfs.exists(local_song_info_uri):
				raise exc_type
			else:
				return

		parser = xml.sax.make_parser()
		parser.setContentHandler(TrackListHandler(self.__db, self.__entry_type, self.__sku_dict, self.__home_dict))
		handle.read (64 * 1024, self.__load_catalogue_read_cb, parser)

	def __load_catalogue(self):
		self.__notify_status_changed()
		self.__load_handle = gnomevfs.async.open (local_song_info_uri, self.__load_catalogue_open_cb)


	def __download_update_cb (self, _reserved, info, moving):
		if info.phase == gnomevfs.XFER_PHASE_COMPLETED:
			# done downloading, unzip to real location
			catalog = zipfile.ZipFile(local_song_info_temp_uri.path)
			out = create_if_needed(local_song_info_uri, gnomevfs.OPEN_WRITE)
			out.write(catalog.read("opt/magnatune/info/song_info.xml"))
			out.close()
			catalog.close()
			gnomevfs.unlink(local_song_info_temp_uri)
			self.__updating = False
			self.__load_catalogue()
		else:
			#print info
			pass

		return 1

	def __download_progress_cb (self, info, data):
		#if info.status == gnomevfs.XFER_PROGRESS_STATUS_OK:
		if True:
			self.__load_current_size = info.bytes_copied
			self.__load_total_size = info.bytes_total
			self.__notify_status_changed()
		else:
			print info
		return 1

	def __download_catalogue(self):
		self.__updating = True
		create_if_needed(local_song_info_temp_uri, gnomevfs.OPEN_WRITE).close()
		self.__xfer_handle = gnomevfs.async.xfer (source_uri_list = [magnatune_song_info_uri],
							  target_uri_list = [local_song_info_temp_uri],
							  xfer_options = gnomevfs.XFER_FOLLOW_LINKS_RECURSIVE,
							  error_mode = gnomevfs.XFER_ERROR_MODE_ABORT,
							  overwrite_mode = gnomevfs.XFER_OVERWRITE_MODE_REPLACE,
							  progress_update_callback = self.__download_update_cb,
							  update_callback_data = False,
							  progress_sync_callback = self.__download_progress_cb,
							  sync_callback_data = None)

	def __update_catalogue(self):
		def info_cb (handle, results):
			(remote_uri, remote_exc, remote_info) = results[0]
			(local_uri, local_exc, local_info) = results[1]

			if remote_exc:
				# error locating remote file
				print "error locating remote catalogue", remote_exc
			elif local_exc:
				if issubclass (local_exc, gnomevfs.NotFoundError):
					# we haven't got it yet
					print "no local copy of catalogue"
					self.__download_catalogue()
				else:
					# error locating local file
					print "error locating local catalogue", local_exc
					self.__download_catalogue()
			else:
				try:
					if remote_info.mtime > local_info.mtime:
						# newer version available
						self.__download_catalogue()
					else:
						# up to date
						pass
				except ValueError, e:
					# couldn't get the mtimes. download?
					print "error checking times", e
					self.__download_catalogue()
			return

		gnomevfs.async.get_file_info ((magnatune_song_info_uri, local_song_info_uri), info_cb)

	def __show_loading_screen(self, show):
		if self.__info_screen is None:
			# load the glade stuff
			gladexml = gtk.glade.XML(self.__plugin.find_file("magnatune-loading.glade"), root="magnatune_loading_vbox")
			self.__info_screen = gladexml.get_widget("magnatune_loading_vbox")
			self.pack_start(self.__info_screen)
			self.get_entry_view().set_no_show_all (True)
			self.__info_screen.set_no_show_all (True)

		self.__info_screen.set_property("visible", show)
		self.get_entry_view().set_property("visible", not show)

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
	def __purchase_album(self, sku, pay, format):
		ccnumber = self.__client.get_string(self.__plugin.gconf_keys['ccnumber'])
		ccyear = self.__client.get_string(self.__plugin.gconf_keys['ccyear'])
		ccmonth = self.__client.get_string(self.__plugin.gconf_keys['ccmonth'])
		name = self.__client.get_string(self.__plugin.gconf_keys['ccname'])
		email = self.__client.get_string(self.__plugin.gconf_keys['email'])
		
		if self.__client.get_bool(self.__plugin.gconf_keys['forget']):
			ccnumber = self.__tmp_cc
			ccyear = self.__tmp_yy
			ccmonth = self.__tmp_mm
			name = self.__tmp_name

		print "purchasing tracks:", sku, pay, format, ccnumber, ccyear, ccmonth, name, email

		try:
			self.__buy_track(sku, pay, format, name, email, ccnumber, ccyear, ccmonth)
		except MagnatunePurchaseError, e:
			error_dlg = gtk.Dialog(title="Error", flags=gtk.DIALOG_DESTROY_WITH_PARENT, buttons=(gtk.STOCK_OK, gtk.RESPONSE_OK))
			label = gtk.Label(_("An error occurred while trying to purchase the album.\nThe Magnatune server returned:\n%s") % str(e))
			error_dlg.vbox.pack_start(label)
			label.set_selectable(True)
			label.show()
			error_dlg.connect("response", lambda w, r: w.destroy())
			error_dlg.show()

	def __buy_track(self, sku, pay, format, name, email, ccnumber, ccyear, ccmonth): # http://magnatune.com/info/api#purchase
		url = "https://magnatune.com/buy/buy_dl_cc_xml?"
		url = url + urllib.urlencode({
						'id':	magnatune_partner_id,
						'sku':	sku,
						'amount': pay,
						'cc':	ccnumber,
						'yy':	ccyear,
						'mm':	ccmonth,
						'name': name,
						'email':email
					})

		self.__buy_album_handler = BuyAlbumHandler(format) # so we can get the url and auth info
		self.__auth_parser = xml.sax.make_parser()
		self.__auth_parser.setContentHandler(self.__buy_album_handler)
		
		self.__wait_dlg = gtk.Dialog(title="Authorizing Purchase", flags=gtk.DIALOG_NO_SEPARATOR|gtk.DIALOG_DESTROY_WITH_PARENT)
		lbl = gtk.Label("Authorizing purchase with the Magnatune server. Please wait...")
		self.__wait_dlg.vbox.pack_start(lbl)
		lbl.show()
		self.__wait_dlg.show()
		gnomevfs.async.open(gnomevfs.URI(url), self.__auth_open_cb)
	
	def __auth_open_cb(self, handle, exc_type):
		if exc_type:
			raise exc_type
		
		handle.read(64 * 1024, self.__auth_read_cb)
		
	
	def __auth_read_cb (self, handle, data, exc_type, bytes_requested):
		data = data.replace("<br>", "") # get rid of any stray <br> tags that will mess up the parser
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				# successfully loaded
				gtk.gdk.threads_enter()
				audio_dl_uri = gnomevfs.URI(self.__buy_album_handler.url)
				audio_dl_uri = gnomevfs.URI(self.__buy_album_handler.url[0:self.__buy_album_handler.url.rfind("/") + 1] + urllib.quote(audio_dl_uri.short_name))
				audio_dl_uri.user_name = str(self.__buy_album_handler.username) # URI objects don't like unicode strings
				audio_dl_uri.password = str(self.__buy_album_handler.password)
				
				in_progress = create_if_needed(gnomevfs.URI(magnatune_dir + "in_progress_" + audio_dl_uri.short_name), gnomevfs.OPEN_WRITE)
				in_progress.write(str(audio_dl_uri))
				in_progress.close()
				self.__download_album(audio_dl_uri)
				self.__wait_dlg.destroy()
				gtk.gdk.threads_leave()
			else:
				# error reading file
				raise exc_type
			
			self.__auth_parser.close()
			handle.close(lambda handle, exc: None) # FIXME: report it?
			
 		else:

			self.__auth_parser.feed(data)
			handle.read(64 * 1024, self.__auth_read_cb)
	
	def __download_album(self, audio_dl_uri):
			library_location = self.__client.get_list("/apps/rhythmbox/library_locations", gconf.VALUE_STRING)[0] # Just use the first library location
			to_file_uri = gnomevfs.URI(magnatune_dir + audio_dl_uri.short_name)
			
			shell = self.get_property('shell')
			manager = shell.get_player().get_property('ui-manager')
			manager.get_action("/MagnatuneSourceViewPopup/MagnatuneCancelDownload").set_sensitive(True)
			self.__downloading = True
			self.cancelled = False
			self.purchase_filesize += gnomevfs.get_file_info(audio_dl_uri).size
			create_if_needed(to_file_uri, gnomevfs.OPEN_WRITE).close()
			gnomevfs.async.xfer (source_uri_list = [audio_dl_uri],
							target_uri_list = [to_file_uri],
							xfer_options = gnomevfs.XFER_FOLLOW_LINKS_RECURSIVE,
							error_mode = gnomevfs.XFER_ERROR_MODE_ABORT,
							overwrite_mode = gnomevfs.XFER_OVERWRITE_MODE_REPLACE,
							progress_update_callback = self.__purchase_download_update_cb,
							update_callback_data = (to_file_uri, library_location, audio_dl_uri),
							progress_sync_callback = self.__purchase_download_progress_cb,
							sync_callback_data = (to_file_uri, audio_dl_uri))
	
	def __purchase_download_update_cb(self, _reserved, info, data):
		if (info.phase == gnomevfs.XFER_PHASE_COMPLETED):
			to_file_uri = data[0]
			library_location = data[1]
			audio_dl_uri = data[2]
			
			try:
				del self.__downloads[str(audio_dl_uri)]
			except:
				return 0
			self.purchase_filesize -= gnomevfs.get_file_info(audio_dl_uri).size
			album = zipfile.ZipFile(to_file_uri.path)
			for track in album.namelist():
				track_uri = gnomevfs.URI(library_location + "/" + track)
				out = create_if_needed(track_uri, gnomevfs.OPEN_WRITE)
				out.write(album.read(track))
				out.close()
			album.close()
			gnomevfs.unlink(gnomevfs.URI(magnatune_dir + "in_progress_" + to_file_uri.short_name))
			gnomevfs.unlink(to_file_uri)
			if self.purchase_filesize == 0:
				self.__downloading = False
			self.__db.add_uri("file://" + urllib.quote(track_uri.dirname))
		return 1
	
	def __purchase_download_progress_cb(self, info, data):
		to_file_uri = data[0]
		audio_dl_uri = data[1]
		
		if self.cancelled:
			try:
				del self.__downloads[str(audio_dl_uri)]
				self.purchase_filesize -= gnomevfs.get_file_info(audio_dl_uri).size
				gnomevfs.unlink(gnomevfs.URI(magnatune_dir + "in_progress_" + to_file_uri.short_name))
				gnomevfs.unlink(to_file_uri)
			except: # this may get run more than once
				pass
			if self.purchase_filesize == 0:
				self.__downloading = False
			return 0
		
		self.__downloads[str(audio_dl_uri)] = info.bytes_copied
		purchase_downloaded = 0
		for i in self.__downloads.values():
			purchase_downloaded += i
		self.__download_progress = purchase_downloaded / float(self.purchase_filesize)
		self.__notify_status_changed()
		return 1
	
	def cancel_downloads(self):
		self.cancelled = True
		shell = self.get_property('shell')
		manager = shell.get_player().get_property('ui-manager')
		manager.get_action("/MagnatuneSourceViewPopup/MagnatuneCancelDownload").set_sensitive(False)

gobject.type_register(MagnatuneSource)


def create_if_needed(uri, mode):
	if not gnomevfs.exists(uri):
		for directory in URIIterator(uri):
			if not gnomevfs.exists(directory):
				gnomevfs.make_directory(directory, 0755)
		out = gnomevfs.create(uri, open_mode=mode)
	else:
		out = gnomevfs.open(uri, open_mode=mode)
	return out



class URIIterator:
	def __init__(self, uri):
		self.uri_list = uri.dirname.split("/")[1:] # dirname starts with /
		self.counter = 0
	def __iter__(self):
		return self
	def next(self):
		if self.counter == len(self.uri_list) + 1:
			raise StopIteration
		value = "file://"
		for i in range(self.counter):
			value += "/" + self.uri_list[i]
		self.counter += 1
		return gnomevfs.URI(value)
