# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
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

from gi.repository import Gio, GnomeKeyring

__instance = None

def instance():
	global __instance
	if __instance is None:
		__instance = MagnatuneAccount()
	return __instance

class MagnatuneAccount(object):
	def __init__(self):
		self.keyring_item = None
		self.settings = Gio.Settings("org.gnome.rhythmbox.plugins.magnatune")

		self.keyring_attributes = GnomeKeyring.attribute_list_new()
		GnomeKeyring.attribute_list_append_string(self.keyring_attributes,
							  "rhythmbox-plugin",
							  "magnatune")
		(result, items) = GnomeKeyring.find_items_sync(GnomeKeyring.ItemType.GENERIC_SECRET,
							       self.keyring_attributes)
		if result == GnomeKeyring.Result.OK and len(items) != 0:
			(result, item) = GnomeKeyring.item_get_info_sync(None, items[0].item_id)
			if result == GnomeKeyring.Result.OK:
				self.keyring_item = item
			else:
				print "Couldn't get keyring item: " + GnomeKeyring.result_to_message(result)
		else:
			print "couldn't search keyring items: " + GnomeKeyring.result_to_message(result)

	def get(self):
		if self.keyring_item is None:
			return ('none', None, None)

		account_type = self.settings['account-type']
		try:
			(username, password) = self.keyring_item.get_secret().split("\n")
			return (account_type, username, password)
		except ValueError:
			return ('none', None, None)

	def update(self, username, password):
		secret = '\n'.join((username, password))
		if self.keyring_item is not None:
			if secret == self.keyring_item.get_secret():
				print "account details not changed"
				return

		(result, id) = GnomeKeyring.item_create_sync(None,
							     GnomeKeyring.ItemType.GENERIC_SECRET,
							     "Rhythmbox: Magnatune account information",
							     self.keyring_attributes,
							     secret,
							     True)
		if result == GnomeKeyring.Result.OK:
			if self.keyring_item is None:
				(result, item) = GnomeKeyring.item_get_info_sync(None, id)
				if result == GnomeKeyring.Result.OK:
					self.keyring_item = item
				else:
					print "couldn't fetch keyring itme: " + GnomeKeyring.result_to_message(result)
		else:
			print "couldn't create keyring item: " + GnomeKeyring.result_to_message(result)
