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

import gobject
import gtk.glade
import gnomevfs, gnome, gconf
import xml
import gzip
import datetime

# URIs
jamendo_dir = gnome.user_dir_get() + "rhythmbox/jamendo/"
jamendo_song_info_uri = gnomevfs.URI("http://img.jamendo.com/data/dbdump_artistalbumtrack.xml.gz")
local_song_info_uri = gnomevfs.URI(jamendo_dir + "dbdump.xml")
local_song_info_temp_uri = gnomevfs.URI(jamendo_dir + "dbdump.xml.tmp")
mp32_uri = "http://api.jamendo.com/get2/bittorrent/file/plain/?type=archive&class=mp32&album_id="
ogg3_uri = "http://api.jamendo.com/get2/bittorrent/file/plain/?type=archive&class=ogg3&album_id="


#  MP3s for streaming : http://api.jamendo.com/get2/stream/track/redirect/?id={TRACKID}&streamencoding=mp31
# OGGs for streaming : http://api.jamendo.com/get2/stream/track/redirect/?id={TRACKID}&streamencoding=ogg2

# .torrent file for download (MP3 archive) : http://api.jamendo.com/get2/bittorrent/file/plain/?album_id={ALBUMID}&type=archive&class=mp32
# .torrent file for download (OGG archive) : http://api.jamendo.com/get2/bittorrent/file/plain/?album_id={ALBUMID}&type=archive&class=ogg3

# Album Covers are available here: http://api.jamendo.com/get2/image/album/redirect/?id={ALBUMID}&imagesize={100-600}

stream_url = "http://api.jamendo.com/get2/stream/track/redirect/?id=%s&streamencoding=ogg2"
artwork_url = "http://api.jamendo.com/get2/image/album/redirect/?id=%s&imagesize=200"
artist_url = "http://www.jamendo.com/get/artist/id/album/page/plain/"

genre_id3 = ["Blues","Classic Rock","Country","Dance","Disco","Funk","Grunge","Hip-Hop","Jazz","Metal","New Age","Oldies","Other","Pop","R&B","Rap","Reggae","Rock","Techno","Industrial","Alternative","Ska","Death Metal","Pranks","Soundtrack","Euro-Techno","Ambient","Trip-Hop","Vocal","Jazz+Funk","Fusion","Trance","Classical","Instrumental","Acid","House","Game","Sound Clip","Gospel","Noise","AlternRock","Bass","Soul","Punk","Space","Meditative","Instrumental Pop","Instrumental Rock","Ethnic","Gothic","Darkwave","Techno-Industrial","Electronic","Pop-Folk","Eurodance","Dream","Southern Rock","Comedy","Cult","Gangsta","Top 40","Christian Rap","Pop/Funk","Jungle","Native American","Cabaret","New Wave","Psychadelic","Rave","Showtunes","Trailer","Lo-Fi","Tribal","Acid Punk","Acid Jazz","Polka","Retro","Musical","Rock & Roll","Hard Rock","Folk","Folk-Rock","National Folk","Swing","Fast Fusion","Bebob","Latin","Revival","Celtic","Bluegrass","Avantgarde","Gothic Rock","Progressive Rock","Psychedelic Rock","Symphonic Rock","Slow Rock","Big Band","Chorus","Easy Listening","Acoustic","Humour","Speech","Chanson","Opera","Chamber Music","Sonata","Symphony","Booty Bass","Primus","Porn Groove","Satire","Slow Jam","Club","Tango","Samba","Folklore","Ballad","Power Ballad","Rhythmic Soul","Freestyle","Duet","Punk Rock","Drum Solo","Acapella","Euro-House","Dance Hall"]

class JamendoSource(rb.BrowserSource):
	__gproperties__ = {
		'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
	}

	def __init__(self):

		rb.BrowserSource.__init__(self, name=_("Jamendo"))

		self.__loader = rb.Loader()

		# catalogue stuff
		self.__db = None
		self.__saxHandler = None
		self.__activated = False
		self.__notify_id = 0
		self.__update_id = 0
		self.__xfer_handle = None
		self.__info_screen = None
		self.__updating = True
		self.__load_handle = None
		self.__load_current_size = 0
		self.__load_total_size = 0

	def do_set_property(self, property, value):
		if property.name == 'plugin':
			self.__plugin = value
		else:
			raise AttributeError, 'unknown property %s' % property.name

	def do_impl_get_browser_key (self):
		return "/apps/rhythmbox/plugins/jamendo/show_browser"

	def do_impl_get_paned_key (self):
		return "/apps/rhythmbox/plugins/jamendo/paned_position"

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
			return (_("Loading Jamendo catalogue"), None, progress)
		else:
			qm = self.get_property("query-model")
			return (qm.compute_status_normal("%d song", "%d songs"), None, 0.0)

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

		if self.__xfer_handle is not None:
			self.__xfer_handle.cancel()
			self.__xfer_handle = None

		gconf.client_get_default().set_string(JamendoConfigureDialog.gconf_keys['sorting'], self.get_entry_view().get_sorting_type())
		rb.BrowserSource.do_impl_delete_thyself (self)


	#
	# internal catalogue downloading and loading
	#
	def __load_catalogue_read_cb (self, handle, data, exc_type, bytes_requested, parser):
		if exc_type:
			if issubclass (exc_type, gnomevfs.EOFError):
				def finish_loadscreen():
					# successfully loaded
					gtk.gdk.threads_enter()
					self.__load_db ()
					self.__show_loading_screen (False)

					in_progress_dir = gnomevfs.DirectoryHandle(gnomevfs.URI(jamendo_dir))
					in_progress = in_progress_dir.next()
					while True:
						if in_progress.name[0:12] == "in_progress_":
							in_progress = gnomevfs.read_entire_file(jamendo_dir + in_progress.name)
							for uri in in_progress.split("\n"):
								if uri == '':
									continue
								self.__download_album(gnomevfs.URI(uri))
						try:
							in_progress = in_progress_dir.next()
						except:
							break
					gtk.gdk.threads_leave()
				gobject.idle_add (finish_loadscreen)
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
		self.__saxHandler = JamendoSaxHandler()
		parser.setContentHandler(self.__saxHandler)
		handle.read (64 * 1024, self.__load_catalogue_read_cb, parser)

	def __load_catalogue(self):
		self.__notify_status_changed()
		self.__load_handle = gnomevfs.async.open (local_song_info_uri, self.__load_catalogue_open_cb)


	def __download_update_cb (self, _reserved, info, moving):
		self.__load_current_size = info.bytes_copied
		self.__load_total_size = info.bytes_total
		self.__notify_status_changed()

		if info.phase == gnomevfs.XFER_PHASE_COMPLETED:
			self.__xfer_handle = None
			# done downloading, unzip to real location
			catalog = gzip.open(local_song_info_temp_uri.path)
			out = create_if_needed(local_song_info_uri, gnomevfs.OPEN_WRITE)
			out.write(catalog.read())
			out.close()
			catalog.close()
			gnomevfs.unlink(local_song_info_temp_uri)
			self.__updating = False
			self.__load_catalogue()
		else:
			#print info
			pass

		return 1

	def __download_catalogue(self):
		self.__updating = True
		create_if_needed(local_song_info_temp_uri, gnomevfs.OPEN_WRITE).close()
		self.__xfer_handle = gnomevfs.async.xfer (source_uri_list = [jamendo_song_info_uri],
							  target_uri_list = [local_song_info_temp_uri],
							  xfer_options = gnomevfs.XFER_FOLLOW_LINKS_RECURSIVE,
							  error_mode = gnomevfs.XFER_ERROR_MODE_ABORT,
							  overwrite_mode = gnomevfs.XFER_OVERWRITE_MODE_REPLACE,
							  progress_update_callback = self.__download_update_cb,
							  update_callback_data = False)

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

		gnomevfs.async.get_file_info ((jamendo_song_info_uri, local_song_info_uri), info_cb)

	def __show_loading_screen(self, show):
		if self.__info_screen is None:
			# load the glade stuff
			gladexml = gtk.glade.XML(self.__plugin.find_file("jamendo-loading.glade"), root="jamendo_loading_scrolledwindow")
			self.__info_screen = gladexml.get_widget("jamendo_loading_scrolledwindow")
			self.pack_start(self.__info_screen)
			self.get_entry_view().set_no_show_all (True)
			self.__info_screen.set_no_show_all (True)

		self.__info_screen.set_property("visible", show)
		self.__paned_box.set_property("visible", not show)

	def __load_db(self):
		artists = self.__saxHandler.artists

		nbAlbums = 0
		nbTracks = 0
		for artist_key in artists.keys():
			artist = artists[artist_key]
			for album_key in artist['ALBUMS'].keys():
				nbAlbums = nbAlbums + 1
				album = artist['ALBUMS'][album_key]
				for track_key in album['TRACKS'].keys():
					nbTracks = nbTracks + 1
					track = album['TRACKS'][track_key]
					track_id = track['id']
					stream = stream_url % (track_id)
					entry = self.__db.entry_lookup_by_location (stream)
					if entry == None:
						entry = self.__db.entry_new(self.__entry_type, stream)

					release_date = album['releasedate']
					if release_date:
						year = int(release_date[0:4])
						date = datetime.date(year, 1, 1).toordinal()
						self.__db.set(entry, rhythmdb.PROP_DATE, date)

					self.__db.set(entry, rhythmdb.PROP_TITLE, track['name'])
					self.__db.set(entry, rhythmdb.PROP_ARTIST, artist['name'])
					try:
						genre = genre_id3[int(album['id3genre'])]
					except Exception:
						genre = _('Unknown')
						
					self.__db.set(entry, rhythmdb.PROP_GENRE, genre)
					self.__db.set(entry, rhythmdb.PROP_ALBUM, album['name'])

					trackno = int(track['numalbum'])
					if trackno >= 0:
						self.__db.set(entry, rhythmdb.PROP_TRACK_NUMBER, trackno)
					try:
						self.__db.set(entry, rhythmdb.PROP_DURATION, int(track['lengths']))
					except Exception:
						# No length, nevermind
						length = 0
					
					# slight misuse, but this is far more efficient than having a python dict
					# containing this data.
					self.__db.set(entry, rhythmdb.PROP_MUSICBRAINZ_ALBUMID, album['id'])

		print "Nb artistes : " + str(len(artists))
		print "Nb albums : " + str(nbAlbums)
		print "Nb tracks : " + str(nbTracks)

		self.__db.commit()
		self.__saxHandler = None


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
			self.__loader.get_url(p2plink, self.__download_p2plink, albumid)

	def __download_p2plink (self, result, albumid):
		if result is None:
			emsg = _("Error looking up p2plink for album %s on jamendo.com") % (albumid)
			gtk.MessageDialog(None, 0, gtk.MESSAGE_INFO, gtk.BUTTONS_OK, emsg).run()
			return
		gnomevfs.url_show(result)
	
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
			self.__loader.get_url(url, self.__open_donate, artist)

	def __open_donate (self, result, artist):
		if result is None:
			emsg = _("Error looking up artist %s on jamendo.com") % (artist)
			gtk.MessageDialog(None, 0, gtk.MESSAGE_INFO, gtk.BUTTONS_OK, emsg).run()
			return
		gnomevfs.url_show(result + "donate/")

	def __p2plink_download_update_cb (self, _reserved, info, moving):
		if info.phase == gnomevfs.XFER_PHASE_COMPLETED:
			print info

		return 1

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
