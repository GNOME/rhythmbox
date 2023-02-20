# Copyright (c) 2018-2019, 2022-2023 Philipp Wolfer <ph.wolfer@gmail.com>
#
# Permission is hereby granted, free of charge, to any person obtaining
# a copy of this software and associated documentation files (the
# "Software"), to deal in the Software without restriction, including
# without limitation the rights to use, copy, modify, merge, publish,
# distribute, sublicense, and/or sell copies of the Software, and to
# permit persons to whom the Software is furnished to do so, subject to
# the following conditions:
#
# The above copyright notice and this permission notice shall be
# included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
# EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
# MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
# NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
# LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
# OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
# WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

import logging
import re
import sys
import threading
import time
from gi.repository import GObject
from gi.repository import Peas
from gi.repository import RB
from client import ListenBrainzClient, Track
from queue import ListenBrainzQueue
from settings import ListenBrainzSettings, load_settings


UNSUPPORTED_TYPES = ('podcast-post')
RE_MBID = re.compile(r"[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}")


logging.basicConfig(stream=sys.stdout, level=logging.DEBUG)
logger = logging.getLogger("listenbrainz")


class ListenBrainzPlugin(GObject.Object, Peas.Activatable):
    __gtype_name = 'ListenBrainzPlugin'
    object = GObject.property(type=GObject.GObject)

    def __init__(self):
        GObject.Object.__init__(self)
        self.settings = None
        self.__client = None
        self.__current_entry = None
        self.__current_start_time = 0
        self.__current_elapsed = 0
        self.__lock = threading.Lock()

    def do_activate(self):
        logger.debug("activating ListenBrainz plugin")
        self.settings = load_settings()
        self.__client = ListenBrainzClient(logger=logger)
        self.settings.connect("changed::user-token",
                              self.on_user_token_changed)
        self.on_user_token_changed(self.settings)
        self.__current_entry = None
        self.__current_start_time = 0
        self.__current_elapsed = 0
        self.__queue = ListenBrainzQueue(self.__client)
        with self.__lock:
            try:
                self.__queue.load()
            except Exception as e:
                _handle_exception(e)
        self.__queue.activate()
        shell_player = self.object.props.shell_player
        shell_player.connect("playing-song-changed",
                             self.on_playing_song_changed)
        shell_player.connect("elapsed-changed", self.on_elapsed_changed)

    def do_deactivate(self):
        logger.debug("deactivating ListenBrainz plugin")
        shell_player = self.object.props.shell_player
        shell_player.disconnect_by_func(self.on_playing_song_changed)
        shell_player.disconnect_by_func(self.on_elapsed_changed)
        self.settings.disconnect_by_func(self.on_user_token_changed)
        self.__queue.deactivate()
        with self.__lock:
            self.__queue.submit_batch()
            self.__queue.save()

    def on_playing_song_changed(self, player, entry):
        logger.debug("playing-song-changed: %r, %r", player, entry)

        self._submit_current_entry()

        self.__current_entry = entry
        self.__current_elapsed = 0

        if not _can_be_listened(entry):
            self.__current_entry = None
            return

        self.__current_start_time = int(time.time())
        track = _entry_to_track(entry)
        threading.Thread(
            target=self._run_in_thread,
            args=[self.__client.playing_now, track]
            ).start()

    def on_elapsed_changed(self, player, elapsed):
        # logger.debug("elapsed-changed: %r, %i" % (player, elapsed))
        if player.get_playing_entry() == self.__current_entry:
            self.__current_elapsed += 1

    def on_user_token_changed(self, settings, key="user-token"):
        self.__client.user_token = settings.get_string("user-token")

    def _submit_current_entry(self):
        if self.__current_entry is not None:
            duration = self.__current_entry.get_ulong(
                            RB.RhythmDBPropType.DURATION)
            elapsed = self.__current_elapsed
            logger.debug("Elapsed: %s / %s", elapsed, duration)
            if elapsed >= 240 or (duration and elapsed >= duration / 2):
                track = _entry_to_track(self.__current_entry)
                threading.Thread(
                    target=self._run_in_thread,
                    args=[self.__queue.add, self.__current_start_time, track]
                    ).start()

    def _run_in_thread(self, callable, *args):
        with self.__lock:
            try:
                callable(*args)
            except Exception as e:
                _handle_exception(e)


def _can_be_listened(entry):
    if entry is None:
        return False

    entry_type = entry.get_entry_type()
    category = entry_type.get_property("category")
    title = entry.get_string(RB.RhythmDBPropType.TITLE)
    error = entry.get_string(RB.RhythmDBPropType.PLAYBACK_ERROR)

    if category != RB.RhythmDBEntryCategory.NORMAL:
        logger.debug('Cannot submit %r: Category "%s"' %
                     (title, category.value_name))
        return False

    type_name = entry_type.get_name()
    if type_name in UNSUPPORTED_TYPES or _is_unidentified_audiocd(entry):
        logger.debug('Cannot submit listen %r: Entry type "%s"' %
                     (title, type_name))
        return False

    if error is not None:
        logger.debug("Cannot submit %r: Playback error %s" %
                     (entry.get_string(RB.RhythmDBPropType.LOCATION), error))
        return False

    return True


def _is_unidentified_audiocd(entry):
    entry_type = entry.get_entry_type()
    return (entry_type.get_name().startswith("audiocd")
            and not entry.get_string(RB.RhythmDBPropType.MB_ALBUMID))


def _handle_exception(e):
    logger.error("ListenBrainz exception %s: %s", type(e).__name__, e)


def _entry_to_track(entry):
    artist = entry.get_string(RB.RhythmDBPropType.ARTIST)
    title = entry.get_string(RB.RhythmDBPropType.TITLE)
    album = entry.get_string(RB.RhythmDBPropType.ALBUM)
    track_number = entry.get_ulong(RB.RhythmDBPropType.TRACK_NUMBER)
    mb_track_id = entry.get_string(RB.RhythmDBPropType.MB_TRACKID)
    mb_album_id = entry.get_string(RB.RhythmDBPropType.MB_ALBUMID)
    mb_artist_id = entry.get_string(RB.RhythmDBPropType.MB_ARTISTID)
    duration = entry.get_ulong(RB.RhythmDBPropType.DURATION)

    additional_info = {
        "release_mbid": _validate_mbid(mb_album_id),
        "recording_mbid": _validate_mbid(mb_track_id),
        "artist_mbids": [mb_artist_id] if _validate_mbid(mb_artist_id) else [],
        "tracknumber": track_number or None,
        "duration": duration,
        "media_player": "Rhythmbox",
        "submission_client": "ListenBrainz plugin",
    }

    entry_type = entry.get_entry_type().get_name()
    if entry_type != "song" and not entry_type.startswith("audiocd"):
        additional_info["music_service_name"] = _cleanup_source(entry_type)

    return Track(artist, title, album, additional_info)


def _validate_mbid(mbid):
    return mbid if RE_MBID.match(mbid) else None


def _cleanup_source(source):
    if source.startswith("grilo:grl-"):
        return source[10:]
    return source


GObject.type_register(ListenBrainzSettings)
