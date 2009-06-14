# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2009 - Jonathan Matthew
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

#
# string matching algorithm described here: http://www.catalysoft.com/articles/StrikeAMatch.html
#

def pairs(str):
	pairs = []
	for word in str.split(' '):
		wpairs = map(lambda x: word[x:x+2], range(len(word)-1))
		pairs.extend(wpairs)

	return pairs

def string_match(a, b):
	apairs = pairs(a.lower())
	bpairs = pairs(b.lower())

	intersection = 0
	union = len(apairs) + len(bpairs)
	for p in apairs:
		try:
			bpairs.remove(p)
			intersection = intersection + 1
		except ValueError:
			pass

	return float(intersection*2) / float(union)

