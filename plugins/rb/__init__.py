# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright 2006, James Livingston <doclivingston@gmail.com>
# Copyright 2006, Ed Catmur <ed@catmur.co.uk>
# Copyright 2007, Jonathan Matthew
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

import sys
import os.path
import os
import time
import thread

from gi.repository import RB
from gi.repository import GConf

import rhythmdb

# rb classes
from Loader import Loader
from Loader import ChunkLoader
from Loader import UpdateCheck
from Coroutine import Coroutine
from URLCache import URLCache

def try_load_icon(theme, icon, size, flags):
	try:
		return theme.load_icon(icon, size, flags)
	except:
		return None

def append_plugin_source_path(theme, iconpath):
	# check for a Makefile.am in the dir the file was loaded from
	fr = sys._getframe(1)
	co = fr.f_code
	filename = co.co_filename

	# and if found, append the icon path
	dir = filename[:filename.rfind(os.sep)]
	if os.path.exists(dir + "/Makefile.am"):
		plugindir = dir[:dir.rfind(os.sep)]
		icondir = plugindir + iconpath
		theme.append_search_path(icondir)

def entry_equal(db, a, b):
	if (a is None and b is None):
		return True
	if (a is None or b is None):
		return False
	return db.entry_get(a, rhythmdb.PROP_LOCATION) == db.entry_get(b, rhythmdb.PROP_LOCATION)

def get_gconf_string_list(key):
	gconf = GConf.Client().get_default()
	l = gconf.get_without_default(key)
	if l is None or \
	    l.type != GConf.ValueType.LIST or \
	    l.get_list_type() != GConf.ValueType.STRING:
		return []
	sl = []
	for e in l.get_list():
		sl.append(e.get_string())

	return sl



class _rbdebugfile:
	def __init__(self, fn):
		self.fn = fn

	def write(self, data):
		if data == '\n':
			return
		fr = sys._getframe(1)

		co = fr.f_code
		filename = co.co_filename

		# strip off the cwd, for if running uninstalled
		cwd = os.getcwd()
		if cwd[-1] != os.sep:
			cwd += os.sep
		if filename[:len(cwd)] == cwd:
			filename = filename[len(cwd):]

		# add the class name to the method, if 'self' exists
		methodname = co.co_name
		if fr.f_locals.has_key('self'):
			methodname = '%s.%s' % (fr.f_locals['self'].__class__.__name__, methodname)

		ln = co.co_firstlineno + fr.f_lineno
		RB.debug_real (methodname, filename, ln, True, str(data))

	def close(self):         pass
	def flush(self):         pass
	def fileno(self):        return self.fn
	def isatty(self):        return 0
	def read(self, a):       return ''
	def readline(self):      return ''
	def readlines(self):     return []
	writelines = write
	def seek(self, a):       raise IOError, (29, 'Illegal seek')
	def tell(self):          raise IOError, (29, 'Illegal seek')
	truncate = tell

sys.stdout = _rbdebugfile(sys.stdout.fileno())
