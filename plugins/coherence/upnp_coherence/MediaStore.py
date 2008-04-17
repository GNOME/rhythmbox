# Licensed under the MIT license
# http://opensource.org/licenses/mit-license.php
#
# Copyright 2007, James Livingston  <doclivingston@gmail.com>
# Copyright 2007, Frank Scholz <coherence@beebits.net>

import rhythmdb
import louie
import urllib
from coherence.upnp.core import DIDLLite

from coherence import log

ROOT_CONTAINER_ID = 0
AUDIO_CONTAINER = 10
AUDIO_ALL_CONTAINER_ID = 11
AUDIO_ARTIST_CONTAINER_ID = 12
AUDIO_ALBUM_CONTAINER_ID = 13

CONTAINER_COUNT = 10000

TRACK_COUNT = 1000000

# most of this class is from Coherence, originally under the MIT licence

class Container(log.Loggable):

    logCategory = 'rb_media_store'

    def __init__(self, id, parent_id, name, children_callback=None):
        self.id = id
        self.parent_id = parent_id
        self.name = name
        self.mimetype = 'directory'
        self.item = DIDLLite.Container(id, parent_id,self.name)
        self.update_id = 0
        self.item.childCount = 0
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

        self.info("Container get_children %r (%r,%r)", children, start, request_count)
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
        self.item.childCount = self.get_child_count()
        return self.item

    def get_name(self):
        return self.name

    def get_id(self):
        return self.id


class Album(log.Loggable):

    logCategory = 'rb_media_store'

    def __init__(self, store, title, id):
        self.id = id
        self.title = title
        self.store = store

        query = self.store.db.query_new()
        self.store.db.query_append(query,[rhythmdb.QUERY_PROP_EQUALS, rhythmdb.PROP_TYPE, self.store.db.entry_type_get_by_name('song')],
                                      [rhythmdb.QUERY_PROP_EQUALS, rhythmdb.PROP_ALBUM, self.title])
        self.tracks_per_album_query = self.store.db.query_model_new(query)
        #self.tracks_per_album_query.set_sort_order(rhythmdb.rhythmdb_query_model_track_sort_func)
        self.store.db.do_full_query_async_parsed(self.tracks_per_album_query, query)

    def get_children(self,start=0,request_count=0):
        children = []

        def track_sort(x,y):
            entry = self.store.db.entry_lookup_by_id (x.id)
            x_track = self.store.db.entry_get (entry, rhythmdb.PROP_TRACK_NUMBER)
            entry = self.store.db.entry_lookup_by_id (y.id)
            y_track = self.store.db.entry_get (entry, rhythmdb.PROP_TRACK_NUMBER)
            return cmp(x_track,y_track)

        def collate (model, path, iter):
            self.info("Album get_children %r %r %r" %(model, path, iter))
            id = model.get(iter, 0)[0]
            children.append(Track(self.store,id))

        self.tracks_per_album_query.foreach(collate)

        children.sort(cmp=track_sort)

        if request_count == 0:
            return children[start:]
        else:
            return children[start:request_count]

    def get_child_count(self):
        return len(self.get_children())

    def get_item(self):
        item = DIDLLite.MusicAlbum(self.id, AUDIO_ALBUM_CONTAINER_ID, self.title)
        return item

    def get_id(self):
        return self.id

    def get_name(self):
        return self.title

    def get_cover(self):
        return self.cover


class Artist(log.Loggable):

    logCategory = 'rb_media_store'

    def __init__(self, store, name, id):
        self.id = id
        self.name = name
        self.store = store

        query = self.store.db.query_new()
        self.store.db.query_append(query,[rhythmdb.QUERY_PROP_EQUALS, rhythmdb.PROP_TYPE, self.store.db.entry_type_get_by_name('song')],
                                      [rhythmdb.QUERY_PROP_EQUALS, rhythmdb.PROP_ARTIST, self.name])
        qm = self.store.db.query_model_new(query)
        self.store.db.do_full_query_async_parsed(qm, query)

        self.albums_per_artist_query = self.store.db.property_model_new(rhythmdb.PROP_ALBUM)
        self.albums_per_artist_query.props.query_model = qm

    def get_children(self,start=0,request_count=0):
        children = []

        def collate (model, path, iter):
            name = model.get(iter, 0)[0]
            priority = model.get(iter, 1)[0]
            self.info("get_children collate %r %r", name, priority)
            if priority is False:
                try:
                    album = self.store.albums[name]
                    children.append(album)
                except:
                    self.warning("hmm, a new album %r, that shouldn't happen", name)

        self.albums_per_artist_query.foreach(collate)

        if request_count == 0:
            return children[start:]
        else:
            return children[start:request_count]

    def get_child_count(self):
        return len(self.get_children())

    def get_item(self):
        item = DIDLLite.MusicArtist(self.id, AUDIO_ARTIST_CONTAINER_ID, self.name)
        return item

    def get_id(self):
        return self.id

    def get_name(self):
        return self.name


class Track(log.Loggable):

    logCategory = 'rb_media_store'

    def __init__(self, store, id):
        self.store = store
        if type(id) == int:
            self.id = id
        else:
            self.id = self.store.db.entry_get (id, rhythmdb.PROP_ENTRY_ID)

    def get_children(self, start=0, request_count=0):
        return []

    def get_child_count(self):
        return 0

    def get_item(self):

        self.info("Track get_item %r" %(self.id))

        host = ""

        # load common values
        entry = self.store.db.entry_lookup_by_id(self.id)
        # Bitrate is in bytes/second, not kilobits/second
        bitrate = self.store.db.entry_get(entry, rhythmdb.PROP_BITRATE) * 1024 / 8
        # Duration is in HH:MM:SS format
        seconds = self.store.db.entry_get(entry, rhythmdb.PROP_DURATION)
        hours = seconds / 3600
        seconds = seconds - hours * 3600
        minutes = seconds / 60
        seconds = seconds - minutes * 60
        duration = ("%02d:%02d:%02d") % (hours, minutes, seconds)

        location = self.get_path(entry)
        mimetype = self.store.db.entry_get(entry, rhythmdb.PROP_MIMETYPE)
        # This isn't a real mime-type
        if mimetype == "application/x-id3":
            mimetype = "audio/mpeg"
        size = self.store.db.entry_get(entry, rhythmdb.PROP_FILE_SIZE)

        # create item
        item = DIDLLite.MusicTrack(self.id + TRACK_COUNT)
        item.album = self.store.db.entry_get(entry, rhythmdb.PROP_ALBUM)
        item.artist = self.store.db.entry_get(entry, rhythmdb.PROP_ARTIST)
        #item.date =
        item.genre = self.store.db.entry_get(entry, rhythmdb.PROP_GENRE)
        item.originalTrackNumber = str(self.store.db.entry_get (entry, rhythmdb.PROP_TRACK_NUMBER))
        item.title = self.store.db.entry_get(entry, rhythmdb.PROP_TITLE) # much nicer if it was entry.title

        #cover = self.store.db.entry_request_extra_metadata(entry, "rb:coverArt")
        #self.warning("cover for %r is %r", item.title, cover)
        #item.albumArtURI = ## can we somehow store art in the upnp share??

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
        return self.store.db.entry_get(entry, rhythmdb.PROP_TITLE)

    def get_url(self):
        return self.store.urlbase + str(self.id + TRACK_COUNT)

    def get_path(self, entry = None):
        if entry is None:
            entry = self.store.db.entry_lookup_by_id (self.id)
        uri = self.store.db.entry_get(entry, rhythmdb.PROP_LOCATION)
        self.warning("Track get_path uri = %r", uri)
        location = None
        if uri.startswith("file://"):
            location = unicode(urllib.unquote(uri[len("file://"):]))
            self.warning("Track get_path location = %r", location)

        return location

class MediaStore(log.Loggable):

    logCategory = 'rb_media_store'
    implements = ['MediaServer']

    def __init__(self, server, **kwargs):
        print "creating UPnP MediaStore"
        self.server = server
        self.db = kwargs['db']
        self.plugin = kwargs['plugin']

        self.update_id = 0

        self.next_id = CONTAINER_COUNT
        self.albums = None
        self.artists = None
        self.tracks = None

        self.urlbase = kwargs.get('urlbase','')
        if( len(self.urlbase) > 0 and self.urlbase[len(self.urlbase)-1] != '/'):
            self.urlbase += '/'

        self.name = "Rhythmbox on %s" % self.server.coherence.hostname

        query = self.db.query_new()
        self.info(query)
        self.db.query_append(query, [rhythmdb.QUERY_PROP_EQUALS, rhythmdb.PROP_TYPE, self.db.entry_type_get_by_name('song')])
        qm = self.db.query_model_new(query)
        self.db.do_full_query_async_parsed(qm, query)

        self.album_query = self.db.property_model_new(rhythmdb.PROP_ALBUM)
        self.album_query.props.query_model = qm

        self.artist_query = self.db.property_model_new(rhythmdb.PROP_ARTIST)
        self.artist_query.props.query_model = qm

        self.containers = {}
        self.containers[ROOT_CONTAINER_ID] = \
                Container( ROOT_CONTAINER_ID,-1, "Rhythmbox on %s" % self.server.coherence.hostname)

        self.containers[AUDIO_ALL_CONTAINER_ID] = \
                Container( AUDIO_ALL_CONTAINER_ID,ROOT_CONTAINER_ID, 'All tracks',
                          children_callback=self.children_tracks)
        self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ALL_CONTAINER_ID])

        self.containers[AUDIO_ALBUM_CONTAINER_ID] = \
                Container( AUDIO_ALBUM_CONTAINER_ID,ROOT_CONTAINER_ID, 'Albums',
                          children_callback=self.children_albums)
        self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ALBUM_CONTAINER_ID])

        self.containers[AUDIO_ARTIST_CONTAINER_ID] = \
                Container( AUDIO_ARTIST_CONTAINER_ID,ROOT_CONTAINER_ID, 'Artists',
                          children_callback=self.children_artists)
        self.containers[ROOT_CONTAINER_ID].add_child(self.containers[AUDIO_ARTIST_CONTAINER_ID])

        louie.send('Coherence.UPnP.Backend.init_completed', None, backend=self)

    def get_by_id(self,id):

        self.info("looking for id %r", id)
        id = int(id)
        if id < TRACK_COUNT:
            item = self.containers[id]
        else:
            item = Track(self, (id - TRACK_COUNT))

        return item

    def get_next_container_id(self):
        ret = self.next_id
        self.next_id += 1
        return ret

    def upnp_init(self):
        if self.server:
            self.server.connection_manager_server.set_variable(0, 'SourceProtocolInfo', [
                #'internal:%s:*:*' % self.name,
                'http-get:*:audio/mpeg:*',
            ])

    def children_tracks(self):
        tracks = []

        def track_cb (entry):
            if self.db.entry_get (entry, rhythmdb.PROP_HIDDEN):
                return
            id = self.db.entry_get (entry, rhythmdb.PROP_ENTRY_ID)
            track = Track(self, id)
            tracks.append(track)

        self.db.entry_foreach_by_type (self.db.entry_type_get_by_name('song'), track_cb)
        return tracks

    def children_albums(self):
        albums =  {}

        self.info('children_albums')

        def album_sort(x,y):
            r = cmp(x.title,y.title)
            self.info("sort %r - %r = %r", x.title, y.title, r)
            return r

        def collate (model, path, iter):
            name = model.get(iter, 0)[0]
            priority = model.get(iter, 1)[0]
            self.info("children_albums collate %r %r", name, priority)
            if priority is False:
                id = self.get_next_container_id()
                album = Album(self, name, id)
                self.containers[id] = album
                albums[name] = album

        if self.albums is None:
            self.album_query.foreach(collate)
            self.albums = albums

        albums = self.albums.values() #.sort(cmp=album_sort)
        albums.sort(cmp=album_sort)
        return albums

    def children_artists(self,killbug=False):
        artists = []

        self.info('children_artists')

        def collate (model, path, iter):
            name = model.get(iter, 0)[0]
            priority = model.get(iter, 1)[0]
            if priority is False:
                id = self.get_next_container_id()
                artist = Artist(self,name, id)
                self.containers[id] = artist
                artists.append(artist)

        if self.artists is None:
            self.artist_query.foreach(collate)
            self.artists = artists

        return self.artists
