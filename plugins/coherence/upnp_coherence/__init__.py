# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright 2008, Frank Scholz <coherence@beebits.net>
# Copyright 2008, James Livingston <doclivingston@gmail.com>
#
# Licensed under the MIT license
# http://opensource.org/licenses/mit-license.php


import rhythmdb, rb
import gobject, gtk

import coherence.extern.louie as louie

from coherence import log

# For the icon
import os.path, urllib, gnomevfs, gtk.gdk

class CoherencePlugin(rb.Plugin,log.Loggable):

    logCategory = 'rb_coherence_plugin'

    def __init__(self):
        rb.Plugin.__init__(self)
        self.coherence = None

    def activate(self, shell):
        from twisted.internet import gtk2reactor
        try:
            gtk2reactor.install()
        except AssertionError, e:
            # sometimes it's already installed
            print e

        self.coherence = self.get_coherence()
        if self.coherence is None:
            print "Coherence is not installed or too old, aborting"
            return

        print "coherence UPnP plugin activated"
        self.shell = shell
        self.sources = {}

        # Set up our icon
        the_icon = None
        face_path = os.path.join(os.path.expanduser('~'), ".face")
        if os.path.exists(face_path):
            url = "file://" + urllib.pathname2url(face_path)
            mimetype = gnomevfs.get_mime_type(url)
            pixbuf = gtk.gdk.pixbuf_new_from_file(face_path)
            width = "%s" % pixbuf.get_width()
            height = "%s" % pixbuf.get_height()
            depth = '24'
            the_icon = {
                'url':url,
                'mimetype':mimetype,
                'width':width,
                'height':height,
                'depth':depth
                }
        else:
            the_icon = None

        # create our own media server
        from coherence.upnp.devices.media_server import MediaServer
        from MediaStore import MediaStore
        if the_icon:
            server = MediaServer(self.coherence, MediaStore, no_thread_needed=True, db=self.shell.props.db, plugin=self, icon=the_icon)
        else:
            server = MediaServer(self.coherence, MediaStore, no_thread_needed=True, db=self.shell.props.db, plugin=self)

        self.uuid = str(server.uuid)

        if self.coherence_version >= (0,5,2):
            # create our own media renderer
            # but only if we have a matching Coherence package installed
            from coherence.upnp.devices.media_renderer import MediaRenderer
            from MediaPlayer import RhythmboxPlayer
            if the_icon:
                MediaRenderer(self.coherence, RhythmboxPlayer, no_thread_needed=True, shell=self.shell, icon=the_icon)
            else:
                MediaRenderer(self.coherence, RhythmboxPlayer, no_thread_needed=True, shell=self.shell)

        # watch for media servers
        louie.connect(self.detected_media_server,
                'Coherence.UPnP.ControlPoint.MediaServer.detected',
                louie.Any)
        louie.connect(self.removed_media_server,
                'Coherence.UPnP.ControlPoint.MediaServer.removed',
                louie.Any)


    def deactivate(self, shell):
        print "coherence UPnP plugin deactivated"
        if self.coherence is None:
            return

        self.coherence.shutdown()

        louie.disconnect(self.detected_media_server,
                'Coherence.UPnP.ControlPoint.MediaServer.detected',
                louie.Any)
        louie.disconnect(self.removed_media_server,
                'Coherence.UPnP.ControlPoint.MediaServer.removed',
                louie.Any)

        del self.shell
        del self.coherence

        for usn, source in self.sources.iteritems():
            source.delete_thyself()
        del self.sources

        # uninstall twisted reactor? probably not, since other things may have used it


    def get_coherence (self):
        coherence_instance = None
        required_version = (0, 5, 7)

        try:
            from coherence.base import Coherence
            from coherence import __version_info__
        except ImportError, e:
            print "Coherence not found"
            return None

        if __version_info__ < required_version:
            required = '.'.join([str(i) for i in required_version])
            found = '.'.join([str(i) for i in __version_info__])
            print "Coherence %s required. %s found. Please upgrade" % (required, found)
            return None

        self.coherence_version = __version_info__

        coherence_config = {
            #'logmode': 'info',
            'controlpoint': 'yes',
            'plugins': {},
        }
        coherence_instance = Coherence(coherence_config)

        return coherence_instance

    def removed_media_server(self, udn):
        print "upnp server went away %s" % udn
        if self.sources.has_key(udn):
            self.sources[udn].delete_thyself()
            del self.sources[udn]

    def detected_media_server(self, client, udn):
        print "found upnp server %s (%s)"  %  (client.device.get_friendly_name(), udn)
        self.warning("found upnp server %s (%s)"  %  (client.device.get_friendly_name(), udn))
        if client.device.get_id() == self.uuid:
            """ don't react on our own MediaServer"""
            return

        db = self.shell.props.db
        group = rb.rb_source_group_get_by_name ("shared")
        entry_type = db.entry_register_type("CoherenceUpnp:" + client.device.get_id()[5:])

        from UpnpSource import UpnpSource
        source = gobject.new (UpnpSource,
                    shell=self.shell,
                    entry_type=entry_type,
                    source_group=group,
                    plugin=self,
                    client=client,
                    udn=udn)

        self.sources[udn] = source

        self.shell.append_source (source, None)
