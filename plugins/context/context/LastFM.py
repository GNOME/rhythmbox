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

import gobject
import gconf

# utility things for dealing with last.fm

URL_PREFIX = 'http://ws.audioscrobbler.com/2.0/?method='

# this is probably john iacona's key
API_KEY = '27151108bfce62e12c1f6341437e0e83'

# this isn't particularly well worded; maybe that's a sign that it's too ugly
# maybe we could provide a form to fill in?
NO_ACCOUNT_ERROR = _("This information is only available to last.fm users. Please enter your account details in the last.fm plugin configuration.")

USERNAME_GCONF_KEY = "/apps/rhythmbox/audioscrobbler/username"

def user_has_account():
    username = gconf.client_get_default().get_string(USERNAME_GCONF_KEY)
    return (username is not None and username != "")

def datasource_link(path):
    return "<a href='http://last.fm/'><img src='%s/img/lastfm.png'></a>" % path

