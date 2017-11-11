# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
# vim: set sts=0 ts=8 sw=8 tw=0 noet :
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

from gi.repository import Gio

# if libsecret isn't new enough, this will crash
from rb import rbconfig
Secret = None
if rbconfig.libsecret_enabled:
	# Till libsecret completely replaces gnome-keyring, we'll fall back to not
	# saving the password if libsecret can't be found. This code can be removed later.
	try:
		import gi
		gi.require_version('Secret', '1')
		from gi.repository import Secret
		# We need to be able to fetch passwords stored by libgnome-keyring, so we use
		# a schema with SECRET_SCHEMA_DONT_MATCH_NAME set.
		# See: http://developer.gnome.org/libsecret/unstable/migrating-schemas.html
		MAGNATUNE_SCHEMA = Secret.Schema.new("org.gnome.rhythmbox.plugins.magnatune",
							Secret.SchemaFlags.DONT_MATCH_NAME,
							{"rhythmbox-plugin": Secret.SchemaAttributeType.STRING})
	except ImportError:
		pass

if Secret is None:
	print ("You need to install libsecret and its introspection files to store your Magnatune password")

__instance = None


def instance():
	global __instance
	if __instance is None:
		__instance = MagnatuneAccount()
	return __instance

class MagnatuneAccount(object):
	def __init__(self):
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.magnatune")
		self.secret = None
		self.keyring_attributes = {"rhythmbox-plugin": "magnatune"}
		if Secret is None:
			print ("Account details will not be saved because libsecret was not found")
			return
		# Haha.
		self.secret_service = Secret.Service.get_sync(Secret.ServiceFlags.OPEN_SESSION, None)
		items = self.secret_service.search_sync(MAGNATUNE_SCHEMA,
							self.keyring_attributes,
							Secret.SearchFlags.LOAD_SECRETS,
							None)
		if not items:
			# The Python API doesn't seem to have a way to differentiate between errors and no results.
			print ("Couldn't find an existing keyring entry")
			return
		self.secret = items[0].get_secret().get().decode("utf-8")

	def get(self):
		if self.secret is None:
			return ('none', None, None)

		account_type = self.settings['account-type']
		try:
			(username, password) = self.secret.split("\n")
			return (account_type, username, password)
		except ValueError:
			return ('none', None, None)

	def update(self, username, password):
		secret = '\n'.join((username, password))
		if secret == self.secret:
			print ("Account details not changed")
			return
		self.secret = secret
		if Secret is None:
			print ("Account details were not saved because libsecret was not found")
			return
		result = Secret.password_store_sync(MAGNATUNE_SCHEMA,
							self.keyring_attributes,
							Secret.COLLECTION_DEFAULT,
							"Rhythmbox: Magnatune account information",
							secret, None)
		if not result:
			print ("Couldn't create keyring item!")
