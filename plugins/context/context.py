# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 John Iacona
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

# vim:shiftwidth=4:softtabstop=4:expandtab

import ContextView as cv

from gi.repository import GObject, Peas
from gi.repository import RB

class ContextPlugin(GObject.Object, Peas.Activatable):
    __gtype_name__ = 'ContextPlugin'
    object = GObject.property(type=GObject.Object)

    def __init__ (self):
        GObject.Object.__init__ (self)

    def do_activate (self):
        self.context_view = cv.ContextView (self.object, self)

    def do_deactivate(self):
        self.context_view.deactivate(self.object)
        del self.context_view
