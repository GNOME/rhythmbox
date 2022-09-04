# Copyright (c) 2018-2019, 2022 Philipp Wolfer <ph.wolfer@gmail.com>
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

import json
import logging
import os
from gi.repository import GLib
from client import Track

MAX_TRACKS_PER_IMPORT = 100
SUBMISSION_INTERVAL_SECONDS = 120

logger = logging.getLogger("listenbrainz")


class ListenBrainzQueue:

    def __init__(self, client):
        self.__client = client
        self.__queue = []

    def activate(self):
        self.__timeout_id = GLib.timeout_add_seconds(
            SUBMISSION_INTERVAL_SECONDS, self.submit_batch)

    def deactivate(self):
        GLib.source_remove(self.__timeout_id)

    def add(self, listened_at, track):
        try:
            # Try to submit immediately, and queue if it fails
            response = self.__client.listen(listened_at, track)
            if response.status in [401, 429] or response.status >= 500:
                self._append(listened_at, track)
        except Exception as e:
            logger.error("ListenBrainz exception %s: %s", type(e).__name__, e)
            self._append(listened_at, track)

    def load(self):
        cache_file = self.get_cache_file_path()
        if os.path.exists(cache_file):
            logger.debug("Loading queue from %s", cache_file)
            with open(cache_file) as f:
                self.__queue = json.load(f, object_hook=from_json)

    def save(self):
        cache_file = self.get_cache_file_path()
        cache_dir = os.path.dirname(cache_file)
        if not os.path.exists(cache_dir):
            os.makedirs(cache_dir)
        logger.debug("Saving queue to %s", cache_file)
        with open(cache_file, 'w') as f:
            json.dump(self.__queue, f, cls=QueueEncoder)

    def _append(self, listened_at, track):
        logger.debug("Queuing for later submission %s: %s", listened_at, track)
        self.__queue.append((listened_at, track))

    def submit_batch(self):
        if len(self.__queue) == 0:
            return True
        logger.debug("Submitting %d queued entries", len(self.__queue))
        try:
            tracks = self.__queue[0:MAX_TRACKS_PER_IMPORT]
            response = self.__client.import_tracks(tracks)
            if response.status != 200:
                return True
            if len(self.__queue) > MAX_TRACKS_PER_IMPORT:
                self.__queue = self.__queue[MAX_TRACKS_PER_IMPORT:]
            else:
                self.__queue = []
        except Exception as e:
            logger.error("ListenBrainz exception %s: %s", type(e).__name__, e)
        return True

    def get_cache_file_path(self):
        return os.path.join(GLib.get_user_cache_dir(), "rhythmbox",
                            "listenbrainz-queue.json")


class QueueEncoder(json.JSONEncoder):
    def default(self, o):
        if type(o) is Track:
            return o.to_dict()
        return super(json.JSONEncoder, self).default(o)


def from_json(json_object):
    if 'artist_name' in json_object:
        return Track.from_dict(json_object)
    return json_object
