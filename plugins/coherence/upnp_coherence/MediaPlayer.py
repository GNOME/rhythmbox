# Licensed under the MIT license
# http://opensource.org/licenses/mit-license.php

# Copyright 2008, Frank Scholz <coherence@beebits.net>

import urllib

import rhythmdb

from coherence.upnp.core.soap_service import errorCode
from coherence.upnp.core import DIDLLite

import coherence.extern.louie as louie

from coherence.extern.simple_plugin import Plugin

from coherence import log

TRACK_COUNT = 1000000

class RhythmboxPlayer(log.Loggable):

    """ a backend to the Rhythmbox

    """
    logCategory = 'rb_media_renderer'

    implements = ['MediaRenderer']
    vendor_value_defaults = {'RenderingControl': {'A_ARG_TYPE_Channel':'Master'},
                             'AVTransport': {'A_ARG_TYPE_SeekMode':('ABS_TIME','REL_TIME')}}
    vendor_range_defaults = {'RenderingControl': {'Volume': {'maximum':100}}}

    def __init__(self, device, **kwargs):
        self.warning("__init__ RhythmboxPlayer %r", kwargs)
        self.shell = kwargs['shell']
        self.server = device

        self.player = None
        self.metadata = None
        self.name = "Rhythmbox on %s" % self.server.coherence.hostname

        self.player = self.shell.get_player()
        louie.send('Coherence.UPnP.Backend.init_completed', None, backend=self)

        self.playing = False
        self.state = None
        self.duration = None
        self.volume = 1.0
        self.muted_volume = None
        self.view = []
        self.tags = {}

    def __repr__(self):
        return str(self.__class__).split('.')[-1]

    def volume_changed(self, player, parameter):
        self.volume = self.player.props.volume
        self.warning('volume_changed to %r', self.volume)
        if self.volume > 0:
            rcs_id = self.server.connection_manager_server.lookup_rcs_id(self.current_connection_id)
            self.server.rendering_control_server.set_variable(rcs_id, 'Volume', self.volume*100)

    def playing_song_changed(self, player, entry):
        self.warning("playing_song_changed %r", entry)
        if self.server != None:
            connection_id = self.server.connection_manager_server.lookup_avt_id(self.current_connection_id)
        if entry == None:
            self.update('STOPPED')
            self.playing = False
            #self.entry = None
            self.metadata = None
            self.duration = None
        else:
            self.id = self.shell.props.db.entry_get (entry, rhythmdb.PROP_ENTRY_ID)
            bitrate = self.shell.props.db.entry_get(entry, rhythmdb.PROP_BITRATE) * 1024 / 8
            # Duration is in HH:MM:SS format
            seconds = self.shell.props.db.entry_get(entry, rhythmdb.PROP_DURATION)
            hours = seconds / 3600
            seconds = seconds - hours * 3600
            minutes = seconds / 60
            seconds = seconds - minutes * 60
            self.duration = "%02d:%02d:%02d" % (hours, minutes, seconds)

            mimetype = self.shell.props.db.entry_get(entry, rhythmdb.PROP_MIMETYPE)
            # This isn't a real mime-type
            if mimetype == "application/x-id3":
                mimetype = "audio/mpeg"
            size = self.shell.props.db.entry_get(entry, rhythmdb.PROP_FILE_SIZE)

            # create item
            item = DIDLLite.MusicTrack(self.id + TRACK_COUNT)
            item.album = self.shell.props.db.entry_get(entry, rhythmdb.PROP_ALBUM)
            item.artist = self.shell.props.db.entry_get(entry, rhythmdb.PROP_ARTIST)
            item.genre = self.shell.props.db.entry_get(entry, rhythmdb.PROP_GENRE)
            item.originalTrackNumber = str(self.shell.props.db.entry_get (entry, rhythmdb.PROP_TRACK_NUMBER))
            item.title = self.shell.props.db.entry_get(entry, rhythmdb.PROP_TITLE) # much nicer if it was entry.title

            item.res = []

            uri = self.shell.props.db.entry_get(entry, rhythmdb.PROP_LOCATION)
            if uri.startswith("file://"):
                location = unicode(urllib.unquote(uri[len("file://"):]))

                # add a fake resource for the moment
                res = DIDLLite.Resource(location, 'http-get:*:%s:*' % mimetype)
                if size > 0:
                    res.size = size
                if self.duration > 0:
                    res.duration = self.duration
                if bitrate > 0:
                    res.bitrate = str(bitrate)
                item.res.append(res)

            elt = DIDLLite.DIDLElement()
            elt.addItem(item)
            self.metadata = elt.toString()
            self.entry = entry
            if self.server != None:
                self.server.av_transport_server.set_variable(connection_id, 'AVTransportURIMetaData',self.metadata)
                self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackMetaData',self.metadata)
            self.warning("playing_song_changed %r", self.metadata)
        if self.server != None:
            self.server.av_transport_server.set_variable(connection_id, 'RelativeTimePosition', '00:00:00')
            self.server.av_transport_server.set_variable(connection_id, 'AbsoluteTimePosition', '00:00:00')

    def playing_changed(self, player, state):
        self.warning("playing_changed", state)
        if state is True:
            transport_state = 'PLAYING'
        else:
            if self.playing is False:
                transport_state = 'STOPPED'
            else:
                transport_state = 'PAUSED_PLAYBACK'
        self.update(transport_state)
        try:
            position = player.get_playing_time()
        except:
            position = None
        try:
            duration = player.get_playing_song_duration()
        except:
            duration = None
        self.update_position(position,duration)
        self.warning("playing_changed %r %r ", position, duration)

    def elapsed_changed(self, player, time):
        self.warning("elapsed_changed %r %r", player, time)
        try:
            duration = player.get_playing_song_duration()
        except:
            duration = None
        self.update_position(time,duration)

    def update(self, state):

        self.warning("update %r", state)

        if state in ('STOPPED','READY'):
            transport_state = 'STOPPED'
        if state == 'PLAYING':
            transport_state = 'PLAYING'
        if state == 'PAUSED_PLAYBACK':
            transport_state = 'PAUSED_PLAYBACK'

        if self.state != transport_state:
            self.state = transport_state
            if self.server != None:
                connection_id = self.server.connection_manager_server.lookup_avt_id(self.current_connection_id)
                self.server.av_transport_server.set_variable(connection_id,
                                                             'TransportState',
                                                             transport_state)


    def update_position(self, position,duration):
        self.warning("update_position %r %r", position,duration)

        if self.server != None:
            connection_id = self.server.connection_manager_server.lookup_avt_id(self.current_connection_id)
            self.server.av_transport_server.set_variable(connection_id, 'CurrentTrack', 0)

        if position is not None:
            m,s = divmod( position, 60)
            h,m = divmod(m,60)
            if self.server != None:
                self.server.av_transport_server.set_variable(connection_id, 'RelativeTimePosition', '%02d:%02d:%02d' % (h,m,s))
                self.server.av_transport_server.set_variable(connection_id, 'AbsoluteTimePosition', '%02d:%02d:%02d' % (h,m,s))

        if duration <= 0:
            duration = None

        if duration is not None:
            m,s = divmod( duration, 60)
            h,m = divmod(m,60)

            if self.server != None:
                self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackDuration', '%02d:%02d:%02d' % (h,m,s))
                self.server.av_transport_server.set_variable(connection_id, 'CurrentMediaDuration', '%02d:%02d:%02d' % (h,m,s))

            if self.duration is None:
                if self.metadata is not None:
                    self.warning("update_position %r", self.metadata)
                    elt = DIDLLite.DIDLElement.fromString(self.metadata)
                    for item in elt:
                        for res in item.findall('res'):
                            res.attrib['duration'] = "%d:%02d:%02d" % (h,m,s)
                    self.metadata = elt.toString()

                    if self.server != None:
                        self.server.av_transport_server.set_variable(connection_id, 'AVTransportURIMetaData',self.metadata)
                        self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackMetaData',self.metadata)

                self.duration = duration

    def load( self, uri, metadata):
        self.warning("player load %r %r", uri, metadata)
        #self.shell.load_uri(uri,play=False)
        self.duration = None
        self.metadata = metadata
        self.tags = {}

        was_playing = self.playing

        if was_playing == True:
            self.stop()

        if len(metadata)>0:
            elt = DIDLLite.DIDLElement.fromString(metadata)
            if elt.numItems() == 1:
                item = elt.getItems()[0]

                if uri.startswith('track-'):
                    self.entry = self.shell.props.db.entry_lookup_by_id(int(uri[6:]))
                else:
                    self.entry = self.shell.props.db.entry_lookup_by_location(uri)
                self.warning("check for entry %r %r %r", self.entry,item.server_uuid,uri)
                if self.entry == None:
                    if item.server_uuid is not None:
                        entry_type = self.shell.props.db.entry_register_type("CoherenceUpnp:" + item.server_uuid)
                        self.entry = self.shell.props.db.entry_new(entry_type, uri)
                        self.warning("create new entry %r", self.entry)
                    else:
                        entry_type = self.shell.props.db.entry_register_type("CoherencePlayer")
                        self.entry = self.shell.props.db.entry_new(entry_type, uri)
                        self.warning("load and check for entry %r", self.entry)

                duration = None
                size = None
                bitrate = None
                for res in item.res:
                    if res.data == uri:
                        duration = res.duration
                        size = res.size
                        bitrate = res.bitrate
                        break

                self.shell.props.db.set(self.entry, rhythmdb.PROP_TITLE, item.title)
                try:
                    if item.artist is not None:
                        self.shell.props.db.set(self.entry, rhythmdb.PROP_ARTIST, item.artist)
                except AttributeError:
                    pass
                try:
                    if item.album is not None:
                        self.shell.props.db.set(self.entry, rhythmdb.PROP_ALBUM, item.album)
                except AttributeError:
                    pass

                try:
                    self.info("%r %r", item.title,item.originalTrackNumber)
                    if item.originalTrackNumber is not None:
                        self.shell.props.db.set(self.entry, rhythmdb.PROP_TRACK_NUMBER, int(item.originalTrackNumber))
                except AttributeError:
                    pass

                if duration is not None:
                    h,m,s = duration.split(':')
                    seconds = int(h)*3600 + int(m)*60 + int(s)
                    self.info("%r %r:%r:%r %r", duration, h, m , s, seconds)
                    self.shell.props.db.set(self.entry, rhythmdb.PROP_DURATION, seconds)

                if size is not None:
                    self.shell.props.db.set(self.entry, rhythmdb.PROP_FILE_SIZE,int(size))

        else:
            if uri.startswith('track-'):
                self.entry = self.shell.props.db.entry_lookup_by_id(int(uri[6:]))
            else:
                #self.shell.load_uri(uri,play=False)
                #self.entry = self.shell.props.db.entry_lookup_by_location(uri)
                entry_type = self.shell.props.db.entry_register_type("CoherencePlayer")
                self.entry = self.shell.props.db.entry_new(entry_type, uri)


        self.playing = False
        self.metadata = metadata

        connection_id = self.server.connection_manager_server.lookup_avt_id(self.current_connection_id)
        self.server.av_transport_server.set_variable(connection_id, 'CurrentTransportActions','Play,Stop,Pause,Seek')
        self.server.av_transport_server.set_variable(connection_id, 'NumberOfTracks',1)
        self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackURI',uri)
        self.server.av_transport_server.set_variable(connection_id, 'AVTransportURI',uri)
        self.server.av_transport_server.set_variable(connection_id, 'AVTransportURIMetaData',metadata)
        self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackURI',uri)
        self.server.av_transport_server.set_variable(connection_id, 'CurrentTrackMetaData',metadata)

        if was_playing == True:
            self.play()

    def start(self, uri):
        self.load(uri)
        self.play()

    def stop(self):
        self.warning("player stop")

        self.player.stop()
        self.playing = False
        #self.server.av_transport_server.set_variable( \
        #    self.server.connection_manager_server.lookup_avt_id(self.current_connection_id),\
        #                     'TransportState', 'STOPPED')

    def play(self):
        self.warning("player play")

        if self.playing == False:
            self.player.play_entry(self.entry)
            self.playing = True
        else:
            self.player.playpause()
        #self.server.av_transport_server.set_variable( \
        #    self.server.connection_manager_server.lookup_avt_id(self.current_connection_id),\
        #                     'TransportState', 'PLAYING')

    def pause(self):
        self.player.pause()
        #self.server.av_transport_server.set_variable( \
        #    self.server.connection_manager_server.lookup_avt_id(self.current_connection_id),\
        #                     'TransportState', 'PAUSED_PLAYBACK')

    def seek(self, location, old_state):
        """
        @param location:    simple number = time to seek to, in seconds
                            +nL = relative seek forward n seconds
                            -nL = relative seek backwards n seconds
        """
        self.warning("player seek %r", location)
        self.player.seek(location)
        self.server.av_transport_server.set_variable(0, 'TransportState', old_state)

    def mute(self):
        self.muted_volume = self.volume
        self.player.set_volume(0)
        rcs_id = self.server.connection_manager_server.lookup_rcs_id(self.current_connection_id)
        self.server.rendering_control_server.set_variable(rcs_id, 'Mute', 'True')

    def unmute(self):
        if self.muted_volume is not None:
            self.player.set_volume(self.muted_volume)
            self.muted_volume = None
        self.player.set_mute(False)
        rcs_id = self.server.connection_manager_server.lookup_rcs_id(self.current_connection_id)
        self.server.rendering_control_server.set_variable(rcs_id, 'Mute', 'False')

    def get_mute(self):
        return self.player.get_mute()

    def get_volume(self):
        self.volume = self.player.get_volume()
        self.warning("get_volume %r", self.volume)
        return self.volume * 100

    def set_volume(self, volume):
        self.warning("set_volume %r", volume)
        volume = int(volume)
        if volume < 0:
            volume=0
        if volume > 100:
            volume=100

        self.player.set_volume(float(volume/100.0))

    def upnp_init(self):
        self.player.connect ('playing-song-changed',
                                 self.playing_song_changed),
        self.player.connect ('playing-changed',
                                 self.playing_changed)
        self.player.connect ('elapsed-changed',
                                 self.elapsed_changed)
        self.player.connect("notify::volume", self.volume_changed)

        self.current_connection_id = None
        self.server.connection_manager_server.set_variable(0, 'SinkProtocolInfo',
                            ['rhythmbox:%s:audio/mpeg:*' % self.server.coherence.hostname,
                             'http-get:*:audio/mpeg:*'],
                            default=True)
        self.server.av_transport_server.set_variable(0, 'TransportState', 'NO_MEDIA_PRESENT', default=True)
        self.server.av_transport_server.set_variable(0, 'TransportStatus', 'OK', default=True)
        self.server.av_transport_server.set_variable(0, 'CurrentPlayMode', 'NORMAL', default=True)
        self.server.av_transport_server.set_variable(0, 'CurrentTransportActions', '', default=True)
        self.server.rendering_control_server.set_variable(0, 'Volume', self.get_volume())
        self.server.rendering_control_server.set_variable(0, 'Mute', self.get_mute())

    def upnp_Play(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        Speed = int(kwargs['Speed'])
        self.play()
        return {}

    def upnp_Pause(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        self.pause()
        return {}

    def upnp_Stop(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        self.stop()
        return {}

    def upnp_Seek(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        Unit = kwargs['Unit']
        Target = kwargs['Target']
        if Unit in ['ABS_TIME','REL_TIME']:
            old_state = self.server.av_transport_server.get_variable(0, 'TransportState')
            self.server.av_transport_server.set_variable(0, 'TransportState', 'TRANSITIONING')
            h,m,s = Target.split(':')
            seconds = int(h)*3600 + int(m)*60 + int(s)
            self.seek(seconds, old_state)
        return {}

    def upnp_SetAVTransportURI(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        CurrentURI = kwargs['CurrentURI']
        CurrentURIMetaData = kwargs['CurrentURIMetaData']
        local_protocol_infos=self.server.connection_manager_server.get_variable('SinkProtocolInfo').value.split(',')
        #print '>>>', local_protocol_infos
        if len(CurrentURIMetaData)==0:
            self.load(CurrentURI,CurrentURIMetaData)
            return {}
        else:
            elt = DIDLLite.DIDLElement.fromString(CurrentURIMetaData)
            #import pdb; pdb.set_trace()
            if elt.numItems() == 1:
                item = elt.getItems()[0]
                res = item.res.get_matching(local_protocol_infos, protocol_type='rhythmbox')
                if len(res) == 0:
                    res = item.res.get_matching(local_protocol_infos)
                if len(res) > 0:
                    res = res[0]
                    remote_protocol,remote_network,remote_content_format,_ = res.protocolInfo.split(':')
                    self.load(res.data,CurrentURIMetaData)
                    return {}
        return failure.Failure(errorCode(714))

    def upnp_SetMute(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        Channel = kwargs['Channel']
        DesiredMute = kwargs['DesiredMute']
        if DesiredMute in ['TRUE', 'True', 'true', '1','Yes','yes']:
            self.mute()
        else:
            self.unmute()
        return {}

    def upnp_SetVolume(self, *args, **kwargs):
        InstanceID = int(kwargs['InstanceID'])
        Channel = kwargs['Channel']
        DesiredVolume = int(kwargs['DesiredVolume'])
        self.set_volume(DesiredVolume)
        return {}
