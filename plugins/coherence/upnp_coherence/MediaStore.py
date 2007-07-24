# Copyright 2007, James Livingston  <doclivingston@gmail.com>

import rhythmdb
import louie
from coherence.upnp.core import DIDLLite


ROOT_CONTAINER_ID = 0
AUDIO_CONTAINER = 10
AUDIO_ALL_CONTAINER_ID = 11
AUDIO_ARTIST_CONTAINER_ID = 12
AUDIO_ALBUM_CONTAINER_ID = 13

CONTAINER_COUNT = 1000


# this class is from Coherence, originally under the MIT licence
# Copyright 2007, Frank Scholz <coherence@beebits.net>
class Container(object):
    def __init__(self, id, parent_id, name, children_callback=None):
        self.id = id
        self.parent_id = parent_id
        self.name = name
        self.mimetype = 'directory'
        self.item = DIDLLite.StorageFolder(id, parent_id,self.name)
        self.update_id = 0
        if children_callback != None:
            self.children = children_callback
        else:
            self.children = []

    def add_child(self, child):
        self.children.append(child)
        self.item.childCount += 1

    def get_children(self,start=0,request_count=0):
        if callable(self.children):
            children = self.children()
        else:
            children = self.children
        if request_count == 0:
            return children[start:]
        else:
            return children[start:request_count]

    def get_child_count(self):
        if callable(self.children):
            return len(self.children())
        else:
            return len(self.children)

    def get_item(self):
        return self.item

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id


class Track:
	def __init__(self, store, id):
		self.id = id
		self.store = store

	def get_children(self, start=0, request_count=0):
		return []

	def get_child_count(self):
		return 0

	def get_item(self):
		host = ""

		# load common values
		entry = self.store.db.entry_lookup_by_id (self.id)
		bitrate = self.store.db.entry_get (entry, rhythmdb.PROP_BITRATE)
		duration = self.store.db.entry_get (entry, rhythmdb.PROP_DURATION)
		location = self.store.db.entry_get (entry, rhythmdb.PROP_LOCATION)
		mimetype = self.store.db.entry_get (entry, rhythmdb.PROP_MIMETYPE)
		size = self.store.db.entry_get (entry, rhythmdb.PROP_FILE_SIZE)

		# create item
		item = DIDLLite.MusicTrack(self.id + CONTAINER_COUNT)
		item.album = self.store.db.entry_get (entry, rhythmdb.PROP_ALBUM)
		##item.albumArtURI = ## can we somehow store art in the upnp share??
		item.artist = self.store.db.entry_get (entry, rhythmdb.PROP_ARTIST)
		##item.date =
		item.genre = self.store.db.entry_get (entry, rhythmdb.PROP_GENRE)
		item.originalTrackNumber = str(self.store.db.entry_get (entry, rhythmdb.PROP_TRACK_NUMBER))
		item.title = self.store.db.entry_get (entry, rhythmdb.PROP_TITLE) # much nicer if it was entry.title
		item.res = []

		# add internal resource
		#res = DIDLLite.Resource(location, 'internal:%s:%s:*' % (host, mimetype))
		#res.size = size
		#res.duration = duration
		#res.bitrate = bitrate
		#item.res.append(res)

		# add http resource
		res = DIDLLite.Resource(self.get_url(), 'http-get:*:%s:*' % mimetype)
		if size > 0:
			res.size = size
		if duration > 0:
			res.duration = str(duration)
		if bitrate > 0:
			res.bitrate = str(bitrate)
		item.res.append(res)

		return item

	def get_id(self):
		return self.id

	def get_name(self):
		entry = self.store.db.entry_lookup_by_id (self.id)
		return self.store.db.entry_get (entry, rhythmdb.PROP_TITLE)

	def get_url(self):
		return self.store.urlbase + str(self.id + CONTAINER_COUNT)


class MediaStore: 
	implements = ['MediaServer']

	def __init__(self, server, **kwargs):
		print "creating UPnP MediaStore"
		self.server = server
		self.db = kwargs['db']
		self.plugin = kwargs['plugin']

		self.urlbase = kwargs.get('urlbase','')
		if( len(self.urlbase) > 0 and self.urlbase[len(self.urlbase)-1] != '/'):
			self.urlbase += '/'

		self.name = self.server.coherence.hostname

		self.containers = {}
		self.containers[ROOT_CONTAINER_ID] = \
		        Container( ROOT_CONTAINER_ID,-1, self.server.coherence.hostname)

		self.containers[AUDIO_ALL_CONTAINER_ID] = \
		        Container( AUDIO_ALL_CONTAINER_ID,ROOT_CONTAINER_ID, 'All tracks',
		                  children_callback=self.children_tracks)
		self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ALL_CONTAINER_ID])

		#self.containers[AUDIO_ALBUM_CONTAINER_ID] = \
		#        Container( AUDIO_ALBUM_CONTAINER_ID,ROOT_CONTAINER_ID, 'Albums',
		#                  children_callback=self.children_albums)
		#self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ALBUM_CONTAINER_ID])

		#self.containers[AUDIO_ARTIST_CONTAINER_ID] = \
		#        Container( AUDIO_ARTIST_CONTAINER_ID,ROOT_CONTAINER_ID, 'Artists',
		#                  children_callback=self.children_artists)
		#self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ARTIST_CONTAINER_ID])

		louie.send('Coherence.UPnP.Backend.init_completed', None, backend=self)

	def get_by_id(self,id):
		print "getting resource id " + str(id)
		if id.startswith('artist_all_tracks_'):
			return self.containers[id]

		id = int(id)
		if id < 1000:
			item = self.containers[id]
		else:
			item = Track(self, (id - CONTAINER_COUNT))

		return item

	def upnp_init(self):
		if self.server:
		    self.server.connection_manager_server.set_variable(0, 'SourceProtocolInfo', [
				#'internal:%s:*:*' % self.name,
				'http-get:*:audio/mpeg:*',
			])

	def children_tracks(self):
		tracks = []

		def track_cb (entry):
			id = self.db.entry_get (entry, rhythmdb.PROP_ENTRY_ID)
			tracks.append(Track(self, id))
		self.db.entry_foreach_by_type (self.db.entry_type_get_by_name('song'), track_cb)

		return tracks

	def children_albums(self):
		return []

	def children_artists(self):
		return []

