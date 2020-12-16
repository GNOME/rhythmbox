# -.- coding: utf-8 -.-

# Copyright © 2009 Markus Korn <thekorn@gmx.de>
# Copyright © 2010 Laszlo Pandy <laszlok2@gmail.com>
# Copyright © 2011 Michal Hruby <michal.mhr@gmail.com>
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

# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import gi
import rb
import time

gi.require_version('Peas', '1.0')
gi.require_version('RB', '3.0')
gi.require_version('Zeitgeist', '2.0')

from gi.repository import GObject, Gio, GLib, Peas, Zeitgeist
from gi.repository import RB

try:
    logger = Zeitgeist.Log.new()

except RuntimeError as e:
    print("Unable to connect to Zeitgeist, won't send events. Reason: '%s'" % e)
    logger = None

class ZeitgeistPlugin(GObject.Object, Peas.Activatable):
    __gtype_name__ = 'ZeitgeistPlugin'
    object = GObject.property(type=GObject.Object)

    def __init__(self):
        GObject.Object.__init__(self)

    def do_activate(self):
        print("Loading Zeitgeist plugin...")

        if logger is not None:
            shell = self.object
            shell_player = shell.props.shell_player
            self.__psc_id = shell_player.connect("playing-song-changed", self.playing_song_changed)

            backend_player = shell_player.props.player
            self.__eos_id = backend_player.connect("eos", self.on_backend_eos)

            self.__manual_switch = True
            self.__current_song = None

            event = Zeitgeist.Event.new()
            event.set_property("interpretation", "Source Registration")
            event.set_property("manifestation", Zeitgeist.USER_ACTIVITY)
            event.set_property("actor", "application://org.gnome.Rhythmbox3.desktop")

            datasource = Zeitgeist.DataSource.new()
            datasource.set_unique_id("org.gnome.Rhythmbox3,dataprovider")
            datasource.set_name("Rhythmbox")
            datasource.set_description("Play and organize your music collection")
            datasource.set_event_templates([event,])
            datasource.set_enabled(True)
            datasource.set_timestamp(int(time.time()*1000))

            # Register Rhythmbox as a data source with Zeitgeist
            # engine.
            registry = Zeitgeist.DataSourceRegistry.new()
            registry.register_data_source(datasource)

            # Save references as register_data_source is async.
            self.event = event
            self.datasource = datasource
            self.registry = registry

    @staticmethod
    def get_song_info(db, entry):
        # we don't want the PROP_MEDIA_TYPE, as it doesn't contain mimetype
        # of the audio file itself
        song = {
            "album": entry.get_string(RB.RhythmDBPropType.ALBUM),
            "artist": entry.get_string(RB.RhythmDBPropType.ARTIST),
            "title":  entry.get_string(RB.RhythmDBPropType.TITLE),
            "location": entry.get_playback_uri(),
        }
        return song


    def on_backend_eos(self, backend_player, stream_data, eos_early):
        # EOS signal means that the song changed because the song is over.
        # ie. the user did not explicitly change the song.
        self.__manual_switch = False

    def playing_song_changed(self, shell, entry):
        if self.__current_song is not None:
            self.send_to_zeitgeist_async(self.__current_song, Zeitgeist.LEAVE_EVENT)

        if entry is not None:
            self.send_to_zeitgeist_async(entry, Zeitgeist.ACCESS_EVENT)

        self.__current_song = entry
        GLib.idle_add(self.reset_manual_switch)

    def reset_manual_switch(self):
        """
        After the eos signal has fired, and after the zeitgeist events have
        been sent asynchronously, reset the manual_switch variable.
        """
        self.__manual_switch = True

    def send_to_zeitgeist_async(self, entry, event_type):
        """
        We do async here because the "eos" signal is fired
        *after* the "playing-song-changed" signal.
        We don't know if the song change was manual or automatic
        until we get get the eos signal. If the mainloop goes to
        idle, it means there are no more signals scheduled, so we
        will have already received the eos if it was coming.
        """
        shell = self.object
        db = shell.props.db

        GLib.idle_add(self.send_to_zeitgeist, db, entry, event_type)

    def send_to_zeitgeist(self, db, entry, event_type):
        song = self.get_song_info(db, entry)

        if self.__manual_switch:
            manifest = Zeitgeist.USER_ACTIVITY
        else:
            manifest = Zeitgeist.SCHEDULED_ACTIVITY

        def file_info_complete(obj, res, user_data):
            try:
                fi = obj.query_info_finish(res)
            except:
                return

            uri_mimetype = fi.get_content_type()

            subject = Zeitgeist.Subject.new()
            subject.set_property("uri", song["location"])
            subject.set_property("interpretation", str(Zeitgeist.AUDIO))
            subject.set_property("manifestation", str(Zeitgeist.FILE_DATA_OBJECT))
            subject.set_property("origin", song["location"].rpartition("/")[0])
            subject.set_property("mimetype", uri_mimetype)
            subject.set_property("text", " - ".join([song["title"], song["artist"], song["album"]]))

            event = Zeitgeist.Event.new()
            event.set_property("timestamp", int(time.time()*1000))
            event.set_property("interpretation", str(event_type))
            event.set_property("manifestation", str(manifest))
            event.set_property("actor", "application://org.gnome.Rhythmbox3.desktop")
            event.add_subject(subject)

            logger.insert_event(event)

        f = Gio.file_new_for_uri(song["location"])
        f.query_info_async(Gio.FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
                           Gio.FileQueryInfoFlags.NONE,
                           GLib.PRIORITY_DEFAULT,
                           None,
                           file_info_complete,
                           None)

    def do_deactivate(self):
        print("Deactivating Zeitgeist plugin...")
        if logger is not None:
            shell = self.object
            shell_player = shell.props.shell_player
            shell_player.disconnect(self.__psc_id)
            self.__psc_id = None

            backend_player = shell_player.props.player
            backend_player.disconnect(self.__eos_id)
            self.__eos_id = None

            self.__current_song = None
