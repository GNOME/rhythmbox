# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 Jonathan Matthew
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
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import os
import os.path
import time
import errno

import rb
from gi.repository import RB

SECS_PER_DAY = 86400

class URLCache(object):
    def __init__(self, name, path, refresh=-1, discard=-1, lifetime=-1):
        """
        Creates a new cache.  'name' is a symbolic name for the cache.
        'path' is either an absolute path to the cache directory, or a
        path relative to the user cache directory.
        'refresh' is the length of time for which cache entries are always
        considered valid.  'lifetime' is the maximum time an entry can live
        in the cache.  'discard' is the length of time for which a cache entry
        can go unused before being discarded.  These are all specified in days,
        with -1 meaning unlimited.
        """
        self.name = name
        if path.startswith("/"):
            self.path = path
        else:
            self.path = os.path.join(RB.user_cache_dir(), path)

        self.refresh = refresh
        self.discard = discard
        self.lifetime = lifetime

    def clean(self):
        """
        This sweeps all entries stored in the cache, removing entries that
        are past the cache lifetime limit, or have not been used for longer
        than the cache discard time.  This should be called on plugin activation,
        and perhaps periodically (infrequently) after that.
        """
        now = time.time()
        if os.path.exists(self.path) == False:
            print("cache directory %s does not exist" % self.path)
            return
            

        print("cleaning cache directory %s" % self.path)
        for f in os.listdir(self.path):
            try:
                path = os.path.join(self.path, f)
                stat = os.stat(path)

                if self.lifetime != -1:
                    if stat.st_ctime + (self.lifetime * SECS_PER_DAY) < now:
                        print("removing stale cache file %s:%s: age %s (past lifetime limit)" % (self.name, f, int(now - stat.st_ctime)))
                        os.unlink(path)
                        continue

                if self.discard != -1:
                    # hmm, noatime mounts will break this, probably
                    if stat.st_atime + (self.discard * SECS_PER_DAY) < now:
                        print("removing stale cache file %s:%s: age %s (past discard limit)" % (self.name, f, int(now - stat.st_atime)))
                        os.unlink(path)
                        continue

            except Exception as e:
                print("error while checking cache entry %s:%s: %s" % (self.name, f, str(e)))
        print("finished cleaning cache directory %s" % self.path)

    def cachefile(self, key):
        """
        Constructs the full path of the file used to store a given cache key.
        """
        fkey = key.replace('/', '_')
        return os.path.join(self.path, fkey)

    def check(self, key, can_refresh=True):
        """
        Checks for a fresh cache entry with a given key.
        If can_refresh is True, only cache entries that are within the
        refresh time will be considered.
        If can_refresh is False, cache entries that are older than the
        refresh time, but not past the lifetime limit or discard period,
        will also be considered.
        The intent is to allow older cache entries to be used if a network
        connection is not available or if the origin site is down.

        If successful, this returns the name of the file storing the cached data.
        Otherwise, it returns None.
        """
        now = time.time()
        try:
            path = self.cachefile(key)
            stat = os.stat(path)

            # check freshness
            stale = False
            if can_refresh and self.refresh != -1:
                if stat.st_ctime + (self.refresh * SECS_PER_DAY) < now:
                    stale = True
            
            if self.lifetime != -1:
                if stat.st_ctime + (self.lifetime * SECS_PER_DAY) < now:
                    stale = True

            if stale:
                print("removing stale cache entry %s:%s" % (self.name, key))
                os.unlink(path)
                return None

            return path

        except Exception as e:
            if hasattr(e, 'errno') is False or (e.errno != errno.ENOENT):
                print("error checking cache for %s:%s: %s" % (self.name, key, e))
            return None


    def store(self, key, data):
        """
        Stores an entry in the cache.
        """
        try:
            # construct cache filename
            if not os.path.exists(self.path):
                os.makedirs(self.path, mode=0o700)
            path = self.cachefile(key)

            # consider using gio set contents async?
            f = open(path, 'wb')
            f.write(data)
            f.close()

            print("stored cache data %s:%s" % (self.name, key))
        except Exception as e:
            print("exception storing cache data %s:%s: %s" % (self.name, key, e))
    

    def __fetch_cb(self, data, url, key, callback, args):
        if data is None:
            cachefile = self.check(key, False)
            if cachefile is not None:
                f = open(cachefile, 'rb')
                data = f.read()
                f.close()
                if callback(data, *args) is False:
                    print("cache entry %s:%s invalidated by callback" % (self.name, key))
                    os.unlink(cachefile)
            else:
                callback(None, *args)
        else:
            if callback(data, *args) is False:
                print("cache entry %s:%s invalidated by callback" % (self.name, key))
            else:
                self.store(key, data)

    def fetch(self, key, url, callback, *args):
        """
        Retrieve the specified URL, satisfying the request from the cache
        if possible, and refreshing the cache if necessary.

        The callback function may return False to indicate that the data
        passed to it is invalid.  Generally this should only happen if the
        data cannot be parsed and it is likely that a later attempt to fetch
        from the origin site will result in valid data.
        """
        # check if we've got a fresh entry in the cache
        print("fetching cache entry %s:%s [%s]" % (self.name, key, url))
        cachefile = self.check(key, True)
        if cachefile is not None:
            # could use a loader here, maybe
            f = open(cachefile, 'rb')
            data = f.read()
            f.close()
            if callback(data, *args) is not False:
                return

            print("cache entry %s:%s invalidated by callback" % (self.name, key))
            os.unlink(cachefile)

        ld = rb.Loader()
        ld.get_url(url, self.__fetch_cb, url, key, callback, args)


# vim: set ts=4 sw=4 expandtab :
