# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 Adam Zimmerman  <adam_zimmerman@sfu.ca>
# Copyright (C) 2006 James Livingston  <doclivingston@gmail.com>
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2, or (at your option)
# any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.

import gnomevfs
import xml.sax, xml.sax.handler

class BuyAlbumHandler(xml.sax.handler.ContentHandler): # Class to download the track, etc.

	format_map =	{
			'ogg'		:	'URL_OGGZIP',
			'flac'		:	'URL_FLACZIP',
			'wav'		:	'URL_WAVZIP',
			'mp3-cbr'	:	'URL_128KMP3ZIP',
			'mp3-vbr'	:	'URL_VBRZIP'
			}

	def __init__(self, format):
		xml.sax.handler.ContentHandler.__init__(self)
		self._format_tag = self.format_map[format] # format of audio to download

	def startElement(self, name, attrs):
		self._text = ""

	def endElement(self, name):
		if name == "ERROR": # Something went wrong. Display error message to user.
			raise MagnatunePurchaseError(self._text)
		elif name == "DL_USERNAME":
			self.username = self._text
		elif name == "DL_PASSWORD":
			self.password = self._text
		elif name == self._format_tag:
			self.url = self._text

	def characters(self, content):
		self._text = self._text + content

class MagnatunePurchaseError(Exception):
	pass
