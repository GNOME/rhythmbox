# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
# artdisplay.py
#
# created by James Livingston, Gareth Murphy
#
# Copyright (C) 2006 - James Livingston
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

from xml.dom import minidom
import rhythmdb, rb
import gtk, gobject
import os, re
import urllib
import locale

try:
	import gnomevfs
	use_gnomevfs = True
except:
	use_gnomevfs = False

LICENSE_KEY = "18C3VZN9HCECM5G3HQG2"
DEFAULT_LOCALE = "en_US"
ASSOCIATE = "webservices-20"
ART_FOLDER = '~/.gnome2/rhythmbox/covers'

class GnomeVFSAsyncSrc(object):  
    def __init__(self):
	    	self.chunk = 4096

    def read_cb(self, handle, buffer, exc_type, bytes_requested):
			if exc_type:
				if issubclass(exc_type, gnomevfs.EOFError):
					gobject.idle_add(self.callback, self.data)
					handle.close(lambda *args: None)
				else:
					gobject.idle_add(self.callback, None)
					handle.close(lambda *args: None)
				return
			
			self.data += buffer
			handle.read(self.chunk, self.read_cb)
    
    def open_cb(self, handle, exc_type):
			if exc_type:
				gobject.idle_add(self.callback, None)
				return
	    
			handle.read(self.chunk, self.read_cb)
    
    def get_url(self, url, callback):
			self.callback = callback
			self.url = url
			self.data = ""
			gnomevfs.async.open(url, self.open_cb)

class URLLibSrc(object):
    def get_url(self, url, callback):
			try:
				sock = urllib.urlopen(url)
				data = sock.read()
				sock.close()
				callback(data)
			except:
				callback(None)
				raise

class CoverArtDatabase (object):
	def __init__(self):
		self.set_search_engine(self.get_default_search_engine())
		if use_gnomevfs:
			self.image_loader = GnomeVFSAsyncSrc()
		else:
			self.image_loader = URLLibSrc()

	def get_default_search_engine (self):
		if use_gnomevfs:
			return AmazonCoverArtSearch (GnomeVFSAsyncSrc())
		else:
			return AmazonCoverArtSearch (URLLibSrc())

	def set_search_engine (self, search_engine):
		self.search_engine = search_engine

	def build_art_cache_filename (self, album, artist, extension):
		art_folder = os.path.expanduser (ART_FOLDER)
		if not os.path.exists (art_folder):
			os.mkdir (art_folder)
		if extension is None:
			extension = "jpg"

		# FIXME: the following block of code is messy and needs to be redone ASAP
		return art_folder + '/%s - %s.%s' % (artist.replace ('/', '-'), album.replace ('/', '-'), extension)	

	def get_pixbuf (self, db, entry, on_get_pixbuf_completed):
		if entry is None:
			on_get_pixbuf_completed (db, entry, None)
			return
            
		st_artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		st_album = db.entry_get (entry, rhythmdb.PROP_ALBUM)

		# Handle special case
		if st_album == "":
			st_album = "Unknown"
		if st_artist == "":
			st_artist = "Unknown"

		# If unknown artist and album there is no point continuing
		if st_album == "Unknown" and st_artist == "Unknown":
			on_get_pixbuf_completed (db, entry, None)
			return

		# replace quote characters
		for char in ["\"", "'"]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')

		art_location = self.build_art_cache_filename (st_album, st_artist, "jpg")
		blist_location = self.build_art_cache_filename (st_album, st_artist, "rb-blist")

		# Check local cache
		if os.path.exists (art_location):
			pixbuf = gtk.gdk.pixbuf_new_from_file (art_location)	
			on_get_pixbuf_completed (db, entry, pixbuf)
		# Check for unsuccessful previous image download to prevent overhead search
		elif os.path.exists (blist_location):
			on_get_pixbuf_completed (db, entry, None)
		else:
			# Otherwise spawn (online) search-engine search
			if entry != self.search_engine.entry:
				self.on_get_pixbuf_completed = on_get_pixbuf_completed
				self.search_engine.search (db, entry, self.on_search_engine_results)

	def on_search_engine_results (self, db, entry, results):
		if results is None:
			self.on_get_pixbuf_completed (db, entry, None)
			return

		# Get best match from results
		self.best_match = self.search_engine.get_best_match (results)

		# Attempt to download image for best match
		pic_url = str(self.best_match.ImageUrlLarge)
		self.image_version = "large";
		self.image_loader.get_url (pic_url, self.on_image_data_received)

	def on_image_data_received	 (self, image_data):
		if image_data is None:
			self.search_engine.search_next()
			return

		if len(image_data) < 1000:
			if self.image_version == "large":
				# Fallback and try to load medium one
				self.image_version = "medium"
				pic_url = str(self.best_match.ImageUrlMedium)
				self.image_loader.get_url (pic_url, self.on_image_data_received)
				return
			blist_location = self.build_art_cache_filename(self.search_engine.st_album, self.search_engine.st_artist, "rb-list")
			f = file (blist_location, 'w')
			f.close ()
			self.search_engine.search_next()
		else:
			art_location = self.build_art_cache_filename(self.search_engine.st_album, self.search_engine.st_artist, "jpg")
			f = file (art_location, 'wb')
			f.write (image_data)
			f.close ()
			pixbuf = gtk.gdk.pixbuf_new_from_file (art_location)
			self.on_get_pixbuf_completed (self.search_engine.db, self.search_engine.entry, pixbuf)

class Bag: pass

class AmazonCoverArtSearch (object):
	def __init__(self, loader):
		self.searching = False
		self.cancel = False
		self.loader = loader
		self._supportedLocales = {
			"en_US" : ("us", "xml.amazon.com"),
			"en_GB" : ("uk", "xml-eu.amazon.com"),
			"de" : ("de", "xml-eu.amazon.com"),
			"ja" : ("jp", "xml.amazon.co.jp")
		}
		self.db = None
		self.entry = None

	def __get_locale (self):
		default = locale.getdefaultlocale ()
		lc_id = DEFAULT_LOCALE
		if default[0] is not None:
			if self._supportedLocales.has_key (default[0]):
				lc_id = default[0]

		lc_host = self._supportedLocales[lc_id][1]
		lc_name = self._supportedLocales[lc_id][0]
		return ((lc_host, lc_name))

	def search (self, db, entry, on_search_completed_callback):
		self.searching = True
		self.cancel = False
		self.db = db
		self.entry = entry
		self.on_search_completed_callback = on_search_completed_callback
		self.keywords = []

		st_artist = db.entry_get (entry, rhythmdb.PROP_ARTIST)
		st_album = db.entry_get (entry, rhythmdb.PROP_ALBUM)

		# Tidy up

		# Replace quote characters
		for char in ["\"", "'"]:
			st_artist = st_artist.replace (char, '')
			st_album = st_album.replace (char, '')

		# Save current search's entry properties
		self.st_album = st_album
		self.st_artist = st_artist

		# Remove variants of Disc/CD [1-9] from album title before search
		for exp in ["\([Dd]isc *[1-9]+\)", "\([Cc][Dd] *[1-9]+\)"]:
			p = re.compile (exp)
			st_album = p.sub ('', st_album)

		st_album_no_vol = st_album
		for exp in ["\(*[Vv]ol.*[1-9]+\)*"]:
			p = re.compile (exp)
			st_album_no_vol = p.sub ('', st_album_no_vol)

		self.st_album_no_vol = st_album_no_vol

		# TODO: Improve to decrease wrong cover downloads, maybe add severity?
		# Assemble list of search keywords (and thus search queries)
		if st_album == "Unknown":
			self.keywords.append ("%s Best of" % (st_artist))
			self.keywords.append ("%s Greatest Hits" % (st_artist))
			self.keywords.append ("%s Essential" % (st_artist))
			self.keywords.append ("%s Collection" % (st_artist))
			self.keywords.append ("%s" % (st_artist))
		elif st_artist == "Unknown":
			self.keywords.append ("%s" % (st_album))
			if st_album_no_vol != st_artist:
				self.keywords.append ("%s" % (st_album_no_vol))
			self.keywords.append ("Various %s" % (st_album))
		else:
			self.keywords.append ("%s %s" % (st_artist, st_album))
			if st_album_no_vol != st_artist:
				self.keywords.append ("%s %s" % (st_artist, st_album_no_vol))
			self.keywords.append ("Various %s" % (st_album))
			self.keywords.append ("%s" % (st_artist))

		# Initiate asynchronous search
		self.search_next ();

	def __build_url (self, keyword):
		(lc_host, lc_name) = self.__get_locale ()

		url = "http://" + lc_host + "/onca/xml3?f=xml"
		url += "&t=%s" % ASSOCIATE
		url += "&dev-t=%s" % LICENSE_KEY
		url += "&type=%s" % 'lite'
		url += "&locale=%s" % lc_name
		url += "&mode=%s" % 'music'
		url += "&%s=%s" % ('KeywordSearch', urllib.quote (keyword))

		return url

	def search_next (self):
		self.searching = True
		
		if len(self.keywords)==0:
			keyword = None
		else:
			keyword = self.keywords.pop(0)

		if keyword is None:
			# No keywords left to search -> no results
			self.on_search_completed (None)
		else:
			# Retrieve search for keyword
			url = self.__build_url (keyword.strip())
			self.loader.get_url (url, self.on_search_response)

	def __unmarshal (self, element):
		rc = Bag ()
		if isinstance (element, minidom.Element) and (element.tagName == 'Details'):
			rc.URL = element.attributes["url"].value
		childElements = [e for e in element.childNodes if isinstance (e, minidom.Element)]
		if childElements:
			for child in childElements:
				key = child.tagName
				if hasattr (rc, key):
					if type (getattr (rc, key)) <> type([]):
						setattr (rc, key, [getattr(rc, key)])
					setattr (rc, key, getattr (rc, key) + [self.__unmarshal(child)])
				elif isinstance(child, minidom.Element) and (child.tagName == 'Details'):
					setattr (rc,key,[self.__unmarshal(child)])
				else:
					setattr (rc, key, self.__unmarshal(child))
		else:
			rc = "".join ([e.data for e in element.childNodes if isinstance (e, minidom.Text)])
			if element.tagName == 'SalesRank':
				rc = rc.replace ('.', '')
				rc = rc.replace (',', '')
				rc = int (rc)
		return rc

	def on_search_response (self, result_data):
		if result_data is None:
			self.search_next()
			return

		try:
			xmldoc = minidom.parseString (result_data)
		except:
			self.search_next()
			return
		
		data = self.__unmarshal (xmldoc).ProductInfo

		if hasattr(data, 'ErrorMsg'):
			# Search was unsuccessful, try next keyword
			self.search_next ()
		else:
			# We got some search results
			self.on_search_results (data.Details)

	def on_search_results (self, results):
		self.on_search_completed (results)

	def on_search_completed (self, result):
		self.on_search_completed_callback (self.db, self.entry, result)
		self.searching = False

	# TODO: Better overall matching algo - "==" is inaccurate on minor differences
	def get_best_match (self, search_results):
		# default to the first match as a fallback
		best_match = search_results[0]

		# search within the results to find the album name match
		for item in search_results:
			# make album title case insensitive and strip
			# leading and trailing space
			if item.ProductName.lower ().strip () == self.st_album.lower ().strip ():
				best_match = item
				break

		# TODO: Also validate <Artist/> tag if it matched search

		return best_match

class ArtDisplayPlugin (rb.Plugin):
	def __init__ (self):
		rb.Plugin.__init__ (self)
		
	def activate (self, shell):
		self.shell = shell
		sp = shell.get_player ()
		self.pec_id = sp.connect ('playing-song-changed', self.playing_entry_changed)
		self.pc_id = sp.connect ('playing-changed', self.playing_changed)
		self.art_widget = gtk.Image ()
		self.art_widget.set_padding (0, 5)
		shell.add_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.sa_id = self.art_widget.connect ('size-allocate', self.size_allocated)
		self.current_pixbuf = None
		self.art_db = CoverArtDatabase ()
		self.resize_id = 0
		self.resize_in_progress = False
		self.old_width = 0
		entry = sp.get_playing_entry ()
		self.playing_entry_changed (sp, entry)
	
	def deactivate (self, shell):
		self.shell = None
		sp = shell.get_player ()
		sp.disconnect (self.pec_id)
		sp.disconnect (self.pc_id)
		shell.remove_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_widget.disconnect(self.sa_id)
		self.art_widget = None
		self.current_pixbuf = None
		self.art_db = None
		self.action = None
		self.action_group = None

		if self.resize_id != 0:
			gobject.source_remove (self.resize_id)

	def playing_changed (self, sp, playing):
		self.set_entry(sp.get_playing_entry ())

	def playing_entry_changed (self, sp, entry):
		self.set_entry(entry)

	def size_allocated (self, widget, allocation):
		if self.old_width == allocation.width:
			return
		if self.resize_id == 0:
			self.resize_id = gobject.idle_add (self.do_resize)
		self.resize_in_progress = True
		self.old_width = allocation.width

	def do_resize(self):
		if self.resize_in_progress:
			self.resize_in_progress = False
			self.update_displayed_art (True)
			ret = True
		else:
			self.update_displayed_art (False)
			self.resize_id = 0
			ret = False
		return ret

	def set_entry (self, entry):
		db = self.shell.get_property ("db")

		# Intitates search in the database (which checks art cache, internet etc.)
		self.art_db.get_pixbuf(db, entry, self.on_get_pixbuf_completed)

	def on_get_pixbuf_completed(self, db, entry, pixbuf):
		# Set the pixbuf for the entry returned from the art db
		self.set_current_art (pixbuf)

	def set_current_art (self, pixmap):
		self.current_pixbuf = pixmap;
		self.update_displayed_art (False);

	def update_displayed_art (self, quick):
		if self.current_pixbuf is None:
			# don't use  Image.clear(), not pygtk 2.6-compatible
			self.art_widget.set_from_pixbuf (None)
			self.art_widget.hide ()
		else:
			width = self.art_widget.parent.allocation.width 
			height = self.current_pixbuf.get_height () * width / self.current_pixbuf.get_width ()
			if quick:
				mode = gtk.gdk.INTERP_NEAREST
			else:
				mode = gtk.gdk.INTERP_HYPER
			self.art_widget.set_from_pixbuf (self.current_pixbuf.scale_simple (width, height, mode))
			self.art_widget.show ()
