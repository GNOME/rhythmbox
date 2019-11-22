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

from gi.repository import RB, Gtk

# rb classes
from Loader import Loader
from Coroutine import Coroutine
from URLCache import URLCache
import rbconfig

def try_load_icon(theme, icon, size, flags):
	try:
		return theme.load_icon(icon, size, flags)
	except:
		return None

def append_plugin_source_path(plugin, iconpath):
	theme = Gtk.IconTheme.get_default()

	# get plugin data dir
	datadir = plugin.plugin_info.get_data_dir()
	icondir = os.path.join(datadir, iconpath)
	if os.path.exists(icondir):
		theme.append_search_path(icondir)

	# where was the caller loaded from?
	fr = sys._getframe(1)
	co = fr.f_code
	filename = co.co_filename

	# if the calling plugin has an icons dir, add it to the search path
	plugindir = filename[:filename.rfind(os.sep)]
	icondir = os.path.join(plugindir, iconpath)
	if os.path.exists(icondir):
		theme.append_search_path(icondir)

def entry_equal(a, b):
	if (a is None and b is None):
		return True
	if (a is None or b is None):
		return False
	return a.get_string(RB.RhythmDBPropType.LOCATION) == b.get_string(RB.RhythmDBPropType.LOCATION)

def find_plugin_file(plugin, filename):
	info = plugin.plugin_info
	data_dir = info.get_data_dir()
	path = os.path.join(data_dir, filename)
	print("looking for " + filename + " in " + data_dir)
	if os.path.exists(path):
		return path

	return RB.file(filename)


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
		if 'self' in fr.f_locals:
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
	def seek(self, a):       raise IOError((29, 'Illegal seek'))
	def tell(self):          raise IOError((29, 'Illegal seek'))
	truncate = tell

sys.stdout = _rbdebugfile(sys.stdout.fileno())
