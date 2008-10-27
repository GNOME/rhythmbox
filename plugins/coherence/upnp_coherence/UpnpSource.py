# Licensed under the MIT license
# http://opensource.org/licenses/mit-license.php
#
# Copyright 2007, James Livingston  <doclivingston@gmail.com>
# Copyright 2007,2008 Frank Scholz <coherence@beebits.net>

import rb, rhythmdb
import gobject, gtk

from coherence import __version_info__ as coherence_version

from coherence import log
from coherence.upnp.core import DIDLLite

class UpnpSource(rb.BrowserSource,log.Loggable):

    logCategory = 'rb_media_store'

    __gproperties__ = {
        'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
        'client': (gobject.TYPE_PYOBJECT, 'client', 'client', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
        'udn': (gobject.TYPE_PYOBJECT, 'udn', 'udn', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
    }

    def __init__(self):
        rb.BrowserSource.__init__(self)
        self.__db = None
        self.__activated = False
        self.container_watch = []
        if coherence_version < (0,5,1):
            self.process_media_server_browse = self.old_process_media_server_browse
        else:
            self.process_media_server_browse = self.new_process_media_server_browse

    def do_set_property(self, property, value):
        if property.name == 'plugin':
            self.__plugin = value
        elif property.name == 'client':
            self.__client = value
            self.props.name = self.__client.device.get_friendly_name()
        elif property.name == 'udn':
            self.__udn = value
        else:
            raise AttributeError, 'unknown property %s' % property.name


    def do_impl_activate(self):
        if not self.__activated:
            print "activating upnp source"
            self.__activated = True

            shell = self.get_property('shell')
            self.__db = shell.get_property('db')
            self.__entry_type = self.get_property('entry-type')

            # load upnp db
            self.load_db(0)
            self.__client.content_directory.subscribe_for_variable('ContainerUpdateIDs', self.state_variable_change)
            self.__client.content_directory.subscribe_for_variable('SystemUpdateID', self.state_variable_change)


    def load_db(self, id):
        d = self.__client.content_directory.browse(id, browse_flag='BrowseDirectChildren', process_result=False, backward_compatibility=False)
        d.addCallback(self.process_media_server_browse, self.__udn)


    def state_variable_change(self, variable, udn=None):
        self.info("%s changed from >%s< to >%s<", variable.name, variable.old_value, variable.value)
        if variable.old_value == '':
            return

        if variable.name == 'SystemUpdateID':
            self.load_db(0)
        elif variable.name == 'ContainerUpdateIDs':
            changes = variable.value.split(',')
            while len(changes) > 1:
                container = changes.pop(0).strip()
                update_id = changes.pop(0).strip()
                if container in self.container_watch:
                    self.info("we have a change in %r, container needs a reload", container)
                    self.load_db(container)


    def new_process_media_server_browse(self, results, udn):
        didl = DIDLLite.DIDLElement.fromString(results['Result'])
        for item in didl.getItems():
            self.info("process_media_server_browse %r %r", item.id, item)
            if item.upnp_class.startswith('object.container'):
                self.load_db(item.id)
            if item.upnp_class.startswith('object.item.audioItem'):

                url = None
                duration = None
                size = None
                bitrate = None

                for res in item.res:
                    remote_protocol,remote_network,remote_content_format,remote_flags = res.protocolInfo.split(':')
                    self.info("%r %r %r %r",remote_protocol,remote_network,remote_content_format,remote_flags)
                    if remote_protocol == 'http-get':
                        url = res.data
                        duration = res.duration
                        size = res.size
                        bitrate = res.bitrate
                        break

                if url is not None:
                    self.info("url %r %r",url,item.title)

                    entry = self.__db.entry_lookup_by_location (url)
                    if entry == None:
                        entry = self.__db.entry_new(self.__entry_type, url)

                    self.__db.set(entry, rhythmdb.PROP_TITLE, item.title)
                    try:
                        if item.artist is not None:
                            self.__db.set(entry, rhythmdb.PROP_ARTIST, item.artist)
                    except AttributeError:
                        pass
                    try:
                        if item.album is not None:
                            self.__db.set(entry, rhythmdb.PROP_ALBUM, item.album)
                    except AttributeError:
                        pass

                    try:
                        self.info("%r %r", item.title,item.originalTrackNumber)
                        if item.originalTrackNumber is not None:
                            self.__db.set(entry, rhythmdb.PROP_TRACK_NUMBER, int(item.originalTrackNumber))
                    except AttributeError:
                        pass

                    if duration is not None:
                        h,m,s = duration.split(':')
                        seconds = int(h)*3600 + int(m)*60 + int(s)
                        self.info("%r %r:%r:%r %r", duration, h, m , s, seconds)
                        self.__db.set(entry, rhythmdb.PROP_DURATION, seconds)

                    if size is not None:
                        self.__db.set(entry, rhythmdb.PROP_FILE_SIZE,int(size))

                    self.__db.commit()

gobject.type_register(UpnpSource)
