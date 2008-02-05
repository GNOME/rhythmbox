# Copyright 2007, James Livingston  <doclivingston@gmail.com>
# Copyright 2007, Frank Scholz <coherence@beebits.net>

import rb, rhythmdb
import gobject, gtk

from coherence import __version_info__ as coherence_version

from coherence import log

class UpnpSource(rb.BrowserSource,log.Loggable):

    logCategory = 'rb_media_store'

    __gproperties__ = {
        'plugin': (rb.Plugin, 'plugin', 'plugin', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
        'client': (gobject.TYPE_PYOBJECT, 'client', 'client', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
        'usn': (gobject.TYPE_PYOBJECT, 'usn', 'usn', gobject.PARAM_WRITABLE|gobject.PARAM_CONSTRUCT_ONLY),
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
        elif property.name == 'usn':
            self.__usn = value
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
        if coherence_version < (0,5,1):
            d = self.__client.content_directory.browse(id, browse_flag='BrowseDirectChildren', backward_compatibility=False)
        else:
            d = self.__client.content_directory.browse(id, browse_flag='BrowseDirectChildren', process_result=False, backward_compatibility=False)
        d.addCallback(self.process_media_server_browse, self.__usn)


    def state_variable_change(self, variable, usn=None):
        print "%s changed from %s to %s" % (variable.name, variable.old_value, variable.value)
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
                    print "we have a change in %s, container needs a reload" % container
                    self.load_db(container)


    def new_process_media_server_browse(self, results, usn):
        for item in results:
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


    def old_process_media_server_browse(self, results, usn):
        for k,v in results.iteritems():
            if k == 'items':
                for id, values in v.iteritems():
                    if values['upnp_class'].startswith('object.container'):
                        self.load_db(id)
                    if values['upnp_class'].startswith('object.item.audioItem'):
                        # (url, [method, something which is in asterix,  format, semicolon delimited key=value map of something])
                        resources = [(k, v.split(':')) for (k, v) in values['resources'].iteritems()]
                        # break data into map
                        for r in resources:
                            if r[1][3] is not '*':
                                r[1][3] = dict([v.split('=') for v in r[1][3].split(';')])
                            else:
                                r[1][3] = dict()

                        url = None
                        for r in resources:
                            if r[1][3].has_key('DLNA.ORG_CI') and r[1][3]['DLNA.ORG_CI'] is not '1':
                                url = r[0]
                                break

                        if url is None:
                            # use transcoded format, since we can't find a normal one
                            url = resources[0][0]

                        entry = self.__db.entry_lookup_by_location (url)
                        if entry == None:
                            entry = self.__db.entry_new(self.__entry_type, url)

                        self.__db.set(entry, rhythmdb.PROP_TITLE, values['title'])

                        self.__db.commit()

gobject.type_register(UpnpSource)
