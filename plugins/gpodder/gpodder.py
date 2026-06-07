# 
# Copyright (C) 2026 Jonathan Matthew
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

import rb
import gi
from gi.repository import Gio, GLib, GObject, Gtk, Peas, PeasGtk
from gi.repository import RB
import sys
import json
import random
import socket
import time
import datetime
import os.path

from sync import GPodderSync, GPodderLoginStatus

from rb import Loader
from rbconfig import rhythmbox_version

from urllib.parse import quote_plus

import gettext
gettext.install('rhythmbox', RB.locale_dir())

FEED_SYNCED_KEYWORD = 'gpodder-feed-synced'
EPISODE_DOWNLOADED_KEYWORD = 'gpodder-episode-downloaded'

from rb import rbconfig
if rbconfig.libsecret_enabled:
	try:
		gi.require_version('Secret', '1')
		from gi.repository import Secret

		GPODDER_SCHEMA = Secret.Schema.new("org.gnome.rhythmbox.plugins.gpodder",
						   Secret.SchemaFlags.NONE,
						   {"rhythmbox-plugin": Secret.SchemaAttributeType.STRING})
	except ImportError:
		pass

keyring_attributes = {"rhythmbox-plugin": "gpodder"}

gpodder_sync = None

def load_account():
	global keyring_attributes

	if Secret is None:
		return ('', '')

	svc = Secret.Service.get_sync(Secret.ServiceFlags.OPEN_SESSION, None)
	items = svc.search_sync(GPODDER_SCHEMA,
				keyring_attributes,
				Secret.SearchFlags.LOAD_SECRETS,
				None)
	if items:
		secret = items[0].get_secret().get().decode("utf-8")
		(username, password) = secret.split("\n")
		return (username, password)

	return ('', '')

def store_account_cb(source, result, unused):
	try:
		stored = Secret.password_store_finish(result)
	except Exception as e:
		print("unable to store account details: {}".format(str(e)))

def store_account(username, password):
	global keyring_attributes

	if Secret is None:
		print("can't save account details as secret support is missing")
		return

	label = "Rhythmbox: GPodder account information"
	secret = "\n".join((username, password))
	Secret.password_store(GPODDER_SCHEMA, keyring_attributes, None, label, secret, None, store_account_cb, None)


def get_feed_sync_url(entry):
	url = entry.get_string(RB.RhythmDBPropType.SUBTITLE)
	if url == "":
		url = entry.get_string(RB.RhythmDBPropType.LOCATION)
	return url


class GPodderPodcastSearch (RB.PodcastSearch):
	def __init__(self):
		self.cancellable = Gio.Cancellable()
		self.loader = Loader()

	def search_cb(self, data, max_results):
		if data is None:
			self.finished(False)
			return

		try:
			count = 0
			j = json.loads(data)
			for p in json.loads(data):
				print(p)
				channel = RB.podcast_parse_channel_new()
				channel.url = p.get("url")
				channel.title = p.get("title")
				channel.description = p.get("description")
				channel.img = p.get("logo_url")

				self.result(channel)
				count = count + 1
				if (count == max_results):
					break

			self.finished(True)
		except Exception as e:
			sys.excepthook(*sys.exc_info())
			self.finished(False)

	def do_start(self, text, max_results):
		# search requests always go to gpodder.net, regardless of the api url for sync
		url = 'https://gpodder.net/search.json?q=' + quote_plus(text)
		print("search url {}".format(url))
		self.loader.get_url(url, self.search_cb, max_results)

	def do_cancel(self):
		self.loader.cancel()

# since we don't update feeds as frequently as we receive episode actions from the sync server,
# we're likely to see actions for episodes that we haven't seen in the feed yet.
# to deal with this, we create gpodder-pending entries (using the episode url with 'gpodder+' as
# a prefix) and apply the actions to them, and then merge with the real episode entry when that
# turns up.  this is also how we handle matching actions up with entries via guid rather than url.
# since pending entries are a different entry type, they don't have the podcast specific properties,
# so we use the comment and title properties to store the feed url and guid.
#
# the merge process scans through all pending entries and tries to either find an episode entry
# with the right url, or do a rhythmdb query matching the feed url and the episode guid.
#
SCHEME_PREFIX = 'gpodder+'
class GPodderPendingEntryType(RB.RhythmDBEntryType):

	def __init__(self):
		RB.RhythmDBEntryType.__init__(self, name='gpodder-pending', save_to_disk=True)

	def get_pending_url(url):
		return SCHEME_PREFIX + url

	def get_episode_url(url):
		if url.startswith(SCHEME_PREFIX):
			return url[len(SCHEME_PREFIX):]
		return None

	def do_get_playback_uri(self, entry):
		return None

	def do_can_sync_metadata(self, entry):
		return False




class GPodderPlugin (GObject.Object, Peas.Activatable):
	__gtype_name__ = 'GPodderPlugin'
	object = GObject.property(type=GObject.Object)

	def __init__ (self):
		GObject.Object.__init__ (self)

	def message_dialog_response(self, dialog, rsp):
		dialog.destroy()

	def do_activate (self):
		global gpodder_sync

		shell = self.object
		self.db = shell.props.db
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.gpodder")
		self.secret = None
		self.podcast_manager = shell.props.podcast_manager


		app = shell.props.application

		action = Gio.SimpleAction.new("gpodder-sync-now", None)
		action.connect("activate", self.sync_now_action, None)
		app.add_action(action)

		action = Gio.SimpleAction.new("gpodder-sync-prefs", None)
		action.connect("activate", self.sync_prefs_action, None)
		app.add_action(action)

		self.pending_entry_type = GPodderPendingEntryType()
		self.db.register_entry_type(self.pending_entry_type)

		self.feed_synced_keyword = RB.RefString(FEED_SYNCED_KEYWORD)
		self.episode_downloaded_keyword = RB.RefString(EPISODE_DOWNLOADED_KEYWORD)
		self.adding_feeds = []
		self.removing_feeds = []
		self.updating_play_counts = {}
		self.apply_pending_iter = None
		self.apply_pending_query = False
		self.apply_pending_idle = 0

		if self.settings['device-id'] == "":
			random.seed()
			self.settings['device-id'] = 'rhythmbox-' + random.randbytes(16).hex()
			print("generated new device id {}".format(self.settings['device-id']))
		else:
			print("using device id {}".format(self.settings['device-id']))


		sync_state_path = os.path.join(RB.user_data_dir(), 'gpodder-sync')
		# Translators: do not translate {hostname} as it is a string substitution
		# marker (like %s) for the hostname.
		device_name = _("Rhythmbox on {hostname}").format(hostname=socket.gethostname())
		gpodder_sync = GPodderSync(self.settings['mygpo-url'], self.settings['device-id'], device_name, sync_state_path, self)
		self.build_sync_menu(remove=False)

		(username, password) = load_account()
		if username != "":
			print("using username {}".format(username))
			gpodder_sync.set_account(username, password)
		else:
			print("no login details")

		if self.settings['search']:
			self.podcast_manager.add_search(GPodderPodcastSearch.__gtype__)

		feedtype = self.db.entry_type_get_by_name("podcast-feed")
		self.feed_model = RB.RhythmDBQueryModel.new_for_entry_type(self.db, feedtype, True)
		self.feed_model.connect("entry-prop-changed", self.podcast_feed_changed, None)
		self.feed_model.connect("entry-removed", self.podcast_feed_removed, None)
		self.feed_model.connect("row-inserted", self.podcast_feed_added, None)

		posttype = self.db.entry_type_get_by_name("podcast-post")
		self.post_model = RB.RhythmDBQueryModel.new_for_entry_type(self.db, posttype, True)
		self.post_model.connect("entry-prop-changed", self.podcast_post_changed, None)

		self.pending_model = RB.RhythmDBQueryModel.new_for_entry_type(self.db, self.pending_entry_type, True)

		if self.settings['feed-sync']:

			# does this need to wait for both db load finishing and the query completing?
			if gpodder_sync.add_all_feeds():
				for row in self.feed_model:
					(entry, path) = list(row)
					gpodder_sync.add_subscription_update(True, get_feed_sync_url(entry))

			gpodder_sync.set_sync_subscriptions(True)

		if self.settings['episode-sync']:
			gpodder_sync.set_sync_episodes(True)

		gpodder_sync.schedule()

	def build_sync_menu(self, remove=False):
		global gpodder_sync

		(synctime, syncresult) = gpodder_sync.get_last_sync()
		syncmenu = Gio.Menu.new()

		if synctime is None:
			lastsynctime = _("Not synchronized")
			lastsyncresult = None
		else:
			# would be nice to use rb_utf_friendly_time() here, but we don't have a way
			# to update the menu item just before it gets displayed.
			st = datetime.datetime.fromtimestamp(synctime)
			lastsynctime = _("Last synced: %s") % (st.strftime("%c"))
			if syncresult:
				lastsyncresult = _("Last sync was successful")
			else:
				lastsyncresult = _("Last sync failed")

		app = self.object.props.application
		if remove:
			app.remove_plugin_menu_item("podcast-toolbar", "gpodder-sync")

		# the menu items we're using as labels here have a nonexistant action so they can't be clicked
		syncmenu.append_item(Gio.MenuItem.new(label=lastsynctime, detailed_action="app.gpodder-fake-action"))
		if lastsyncresult is not None:
			syncmenu.append_item(Gio.MenuItem.new(label=lastsyncresult, detailed_action="app.gpodder-fake-action"))
		syncmenu.append_item(Gio.MenuItem.new(label=_("Sync Now"), detailed_action="app.gpodder-sync-now"))
		syncmenu.append_item(Gio.MenuItem.new(label=_("Preferences"), detailed_action="app.gpodder-sync-prefs"))
		app.add_plugin_menu_item("podcast-toolbar", "gpodder-sync", Gio.MenuItem.new_submenu(label=_("Sync"), submenu=syncmenu))

	def sync_finished(self):
		self.build_sync_menu(True)

	def podcast_post_changed(self, model, entry, prop, old_value, new_value, data):

		if self.settings['episode-sync'] is False:
			return

		episode_url = entry.get_string(RB.RhythmDBPropType.MOUNTPOINT)
		if episode_url is None:
			episode_url = entry.get_string(RB.RhythmDBPropType.LOCATION)

		data = {
			'episode': episode_url,
			'podcast': entry.get_string(RB.RhythmDBPropType.SUBTITLE)
		}
		guid = entry.get_string(RB.RhythmDBPropType.PODCAST_GUID)
		if guid != "":
			data['guid'] = guid

		if prop == RB.RhythmDBPropType.STATUS:
			if new_value == 100:
				data['action'] = 'download'
		elif prop == RB.RhythmDBPropType.PLAY_COUNT:
			# check if this is a synced value we're updating
			synced_count = self.updating_play_counts.pop(episode_url, 0)

			if new_value > synced_count:
				data['action'] = 'play'
				# pretend the whole thing was played start to finish
				data['started'] = 0
				data['position'] = entry.get_ulong(RB.RhythmDBPropType.DURATION)
				data['total'] = entry.get_ulong(RB.RhythmDBPropType.DURATION)
			else:
				print("ignoring play count change on {} (from sync)".format(episode_url))
		elif prop == RB.RhythmDBPropType.MOUNTPOINT:
			if new_value == '' and old_value != new_value:
				data['action'] = 'delete'

		if data.get('action') is None:
			 return

		print("new episode action: {} {}".format(data['action'], data['episode']))
		gpodder_sync.add_episode_action(data)


	def podcast_feed_changed(self, model, entry, prop, old_value, new_value, data):

		feed_url = entry.get_string(RB.RhythmDBPropType.LOCATION)

		# the feed entry's last-seen time changes after a feed update,
		# which is a good time to try to apply pending episode actions.
		if self.settings['episode-sync'] and prop == RB.RhythmDBPropType.LAST_SEEN:
			self.apply_pending_actions_idle(True)

		# if the resolved url changes, delete and readd the feed
		if self.settings['feed-sync'] and prop == RB.RhythmDBPropType.SUBTITLE:
			if old_value == "":
				old_value = feed_url
			if new_value == "":
				new_value = feed_url

			if new_value != old_value:
				print("sync url for {} changed from {} to {}".format(feed_url, old_value, new_value))
				gpodder_sync.add_subscription_update(False, old_value)
				gpodder_sync.add_subscription_update(True, new_value)


	def podcast_feed_added(self, model, path, iter, data):

		if self.settings['feed-sync'] is False:
			return

		entry = model.iter_to_entry(iter)
		if self.db.entry_keyword_has(entry, self.feed_synced_keyword):
			return

		url = get_feed_sync_url(entry)
		if url in self.adding_feeds:
			self.adding_feeds.remove(url)
			return

		print("syncing addition of feed {}".format(url))
		gpodder_sync.add_subscription_update(True, url)
		self.db.entry_keyword_add(entry, self.feed_synced_keyword)
		self.db.commit()

	def podcast_feed_removed(self, model, entry, data):

		if self.settings['feed-sync'] is False:
			return

		url = get_feed_sync_url(entry)
		if url in self.removing_feeds:
			self.removing_feeds.remove(url)
			return

		print("syncing removal of feed {}".format(url))
		gpodder_sync.add_subscription_update(False, url)

	def find_feed_by_sync_url(self, feed):
		entry = self.db.entry_lookup_by_location(feed)
		if entry is not None:
			return entry

		for row in self.feed_model:
			(entry, path) = list(row)
			if entry.get_string(RB.RhythmDBPropType.SUBTITLE) == feed:
				return entry

		return None

	def subscription_added_cb(self, feed):
		entry = self.find_feed_by_sync_url(feed)
		if entry is not None:
			print("feed {} already exists".format(feed))
			return

		print("adding feed {}".format(feed))
		self.adding_feeds.append(feed)
		self.podcast_manager.subscribe_feed(feed, True)

	def subscription_removed_cb(self, feed):
		entry = self.find_feed_by_sync_url(feed)
		if entry is None:
			print("feed {} not present".format(feed))
			return

		if get_feed_sync_url(entry) != feed:
			print("not removing feed {} as sync url doesn't match".format(feed))
			return

		print("removing feed {}".format(feed))
		self.removing_feeds.append(feed)
		id_url = entry.get_string(RB.RhythmDBPropType.LOCATION)
		self.podcast_manager.remove_feed(id_url, False)


	def apply_pending_entry(self, entry, pending):
		playcount = pending.get_ulong(RB.RhythmDBPropType.PLAY_COUNT)
		lastplayed = pending.get_ulong(RB.RhythmDBPropType.LAST_PLAYED)
		downloaded = self.db.entry_keyword_has(pending, self.episode_downloaded_keyword)

		episode_url = entry.get_string(RB.RhythmDBPropType.LOCATION)
		print("applying pending sync actions to episode {}: play count {}, last played {}, downloaded {}".format(episode_url, playcount, lastplayed, downloaded))
		self.updating_play_counts[episode_url] = playcount
		self.db.entry_set(entry, RB.RhythmDBPropType.PLAY_COUNT, playcount)
		self.db.entry_set(entry, RB.RhythmDBPropType.LAST_PLAYED, lastplayed)
		if downloaded:
			self.db.entry_keyword_add(entry, self.episode_downloaded_keyword)

		self.db.entry_delete(pending)
		self.db.commit()

	def episode_actions_query_cb(self, results, pending):
		self.apply_pending_query = False

		res = results.get_results()
		guid = pending.get_string(RB.RhythmDBPropType.TITLE)
		if len(res) == 0:
			print("couldn't find missing entry")
		else:
			e = res[0]
			# might need to move this to the main thread?
			print("found matching entry {}".format(e.get_string(RB.RhythmDBPropType.LOCATION)))
			self.apply_pending_entry(e, pending)

		# have to use an idle handler here, but we don't want to restart processing
		if self.apply_pending_iter is not None:
			self.apply_pending_actions_idle(False)
		else:
			print("finished applying pending episode actions")

	def apply_pending_actions_step(self):
		while True:
			if self.apply_pending_iter is None:
				print("finished applying pending episode actions")
				return
			pending = self.pending_model.iter_to_entry(self.apply_pending_iter)
			self.apply_pending_iter = self.pending_model.iter_next(self.apply_pending_iter)

			episode_url = GPodderPendingEntryType.get_episode_url(pending.get_string(RB.RhythmDBPropType.LOCATION))
			entry = self.db.entry_lookup_by_location(episode_url)
			if entry is not None:
				self.apply_pending_entry(entry, pending)
				continue

			# query by feed+guid, as long as we have them
			guid = pending.get_string(RB.RhythmDBPropType.TITLE)
			feed_url = pending.get_string(RB.RhythmDBPropType.COMMENT)
			if guid == "" or feed_url == "":
				print("not enough information to query for missing episode {}".format(episode_url))
				# assume an entry with matching url will show up at some point
				continue

			# handle resolved feed urls
			feed_entry = self.find_feed_by_sync_url(feed_url)
			if feed_entry is None:
				print("couldn't find feed by url {}".format(feed_url))
			feed_url = feed_entry.get_string(RB.RhythmDBPropType.LOCATION)

			self.apply_pending_query = True
			print("querying for episode guid {} in feed {}".format(guid, feed_url))
			posttype = self.db.entry_type_get_by_name("podcast-post")
			q = GLib.PtrArray()
			self.db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.TYPE, posttype)
			self.db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.SUBTITLE, feed_url)
			self.db.query_append_params(q, RB.RhythmDBQueryType.EQUALS, RB.RhythmDBPropType.PODCAST_GUID, guid)

			rl = RB.RhythmDBQueryResultList()
			rl.connect("complete", self.episode_actions_query_cb, pending)
			self.db.do_full_query_async_parsed(rl, q)
			return

	def apply_pending_actions_idle_cb(self, data):
		self.apply_pending_idle = 0

		# if we're waiting for a query to finish, we'll restart after that
		if self.apply_pending_query is False:
			print("starting to apply pending actions")
			self.apply_pending_actions_step()
		return GLib.SOURCE_REMOVE

	def apply_pending_actions_idle(self, restart):
		if restart:
			self.apply_pending_iter = self.pending_model.get_iter_first()

		if self.apply_pending_idle == 0:
			self.apply_pending_idle = GLib.idle_add(self.apply_pending_actions_idle_cb, None)
		else:
			print("already processing")

	def episode_action_cb(self, action, when):

		episode = action.get('episode')
		guid = action.get('guid')
		feed = action.get('podcast')
		type = action.get('action')
		deviceid = action.get('device')
		if episode is None or feed is None or type is None or deviceid is None:
			print("not enough information in synced episode action")
			return

		if deviceid == self.settings['device-id']:
			return

		# only count an episode as played if we get a playback action that reaches the end
		if type == 'play':
			pos = action.get('position')
			end = action.get('total')
			if end is None or pos is None:
				print("ignoring play action with no position information")
				return
			if end > 0 and pos < end - 5:
				print("ignoring incomplete play action ({} of {})".format(pos, end))
				return

		# if the episode url matches an existing entry, we can apply actions directly
		# otherwise, create a pending action entry to be applied later.
		entry = self.db.entry_lookup_by_location(episode)
		if entry is None:
			pending_ep = GPodderPendingEntryType.get_pending_url(episode)
			entry = self.db.entry_lookup_by_location(pending_ep)
			if entry is None:
				print("creating pending episode entry for episode {} (feed {}, guid {})".format(episode, feed, guid))
				entry = RB.RhythmDBEntry.new(self.db, self.pending_entry_type, pending_ep)
				self.db.entry_set(entry, RB.RhythmDBPropType.COMMENT, feed)
				if guid is not None:
					self.db.entry_set(entry, RB.RhythmDBPropType.TITLE, guid)
			else:
				print("using existing pending episode entry for episode {} (feed {}, guid {})".format(episode, feed, guid))

		if type == 'play':
			count = entry.get_ulong(RB.RhythmDBPropType.PLAY_COUNT)
			print("changing play count for episode {} from {} to {}".format(episode, count, count + 1))
			self.db.entry_set(entry, RB.RhythmDBPropType.PLAY_COUNT, count + 1)
			self.updating_play_counts[episode] = count + 1

			last = entry.get_ulong(RB.RhythmDBPropType.LAST_PLAYED)
			if when > last:
				print("changing last played time for episode {} from {} to {}".format(episode, last, when))
				self.db.entry_set(entry, RB.RhythmDBPropType.LAST_PLAYED, when)
		elif type == 'download':
			print("marking episode {} as downloaded (from device {})".format(episode, deviceid))
			self.db.entry_keyword_add(entry, self.episode_downloaded_keyword)
		elif type == 'delete':
			print("removing downloaded mark from episode {} (from device {})".format(episode, deviceid))
			self.db.entry_keyword_remove(entry, self.episode_downloaded_keyword)
		elif type == 'new':
			# can't really remove what we did to the play count
			print("clearing keywords on episode {}".format(episode))
			self.db.entry_keyword_remove(entry, self.episode_downloaded_keyword)
		else:
			print("received unknown action {} for episode {}".format(type, episode))
			return

		self.db.commit()
		self.apply_pending_actions_idle(True)

	def sync_prefs_destroy(self, dialog, response):
		dialog.destroy()

	def sync_prefs_action (self, action, parameter, data):
		# this is mostly peas-gtk-plugin-manager.c:show_configure_cb()
		dialog = Gtk.Dialog.new()
		dialog.set_title(self.plugin_info.get_name())
		dialog.set_transient_for(self.object.props.window)
		dialog.set_modal(True)
		dialog.set_destroy_with_parent(True)
		dialog.add_button(_("Close"), Gtk.ResponseType.CLOSE)

		content = dialog.get_content_area()
		config = GPodderConfig()
		config.plugin_info = self.plugin_info
		settings = config.create_configure_widget()
		content.pack_start(settings, True, True, 0)

		dialog.show_all()
		dialog.connect("response", self.sync_prefs_destroy)

	def sync_now_action (self, action, parameter, data):
		global gpodder_sync
		gpodder_sync.run()

	def do_deactivate (self):
		shell = self.object
		gpodder_sync.cancel()
		# cancel password storing probably

		# disconnect stuff


class GPodderConfig(GObject.Object, PeasGtk.Configurable):
	__gtype_name__ = 'GPodderConfig'
	object = GObject.property(type=GObject.GObject)

	def __init__(self):
		GObject.GObject.__init__(self)
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.gpodder")


	def login_status_changed(self, obj, pspec):
		label = self.builder.get_object('login_status')
		status = gpodder_sync.props.login_status
		if status == GPodderLoginStatus.NOT_LOGGED_IN:
			label.set_text("")
		elif status == GPodderLoginStatus.SUCCESS:
			label.set_text(_("Login successful"))
		elif status == GPodderLoginStatus.ERROR:
			label.set_text(_("Error"))
		elif status == GPodderLoginStatus.FAILED:
			label.set_text(_("Login failed"))

		label = self.builder.get_object('login_status_message')
		message = gpodder_sync.get_login_status_message()
		if message is None:
			message = ""
		label.set_text(message)

	def settings_changed(self, settings, key):
		global gpodder_sync

		if key == 'mygpo-url':
			gpodder_sync.set_url(settings[key])

		# other things?

	def account_details_changed(self, entry, event):
		global gpodder_sync

		nu = self.builder.get_object('username_entry').get_text()
		np = self.builder.get_object('password_entry').get_text()
		gpodder_sync.set_account(nu, np)

		if gpodder_sync.add_all_feeds():
			for row in self.feed_model:
				(entry, path) = list(row)
				gpodder_sync.add_subscription_update(True, get_feed_sync_url(entry))

		store_account(nu, np)


	def login_clicked(self, button):
		global gpodder_sync
		gpodder_sync.test_login()


	def do_create_configure_widget(self):
		global gpodder_sync
		gpodder_sync.connect("notify::login-status", self.login_status_changed)

		ui_file = rb.find_plugin_file(self, "gpodder-prefs.ui")
		self.builder = Gtk.Builder()
		self.builder.add_from_file(ui_file)

		content = self.builder.get_object("gpodder-account-prefs")

		self.settings.connect('changed', self.settings_changed)

		self.settings.bind('mygpo-url', self.builder.get_object('server_url_entry'), 'text', Gio.SettingsBindFlags.DEFAULT)
		self.settings.bind('feed-sync', self.builder.get_object('sync_subscriptions'), 'active', Gio.SettingsBindFlags.DEFAULT)
		self.settings.bind('episode-sync', self.builder.get_object('sync_episode_actions'), 'active', Gio.SettingsBindFlags.DEFAULT)
		self.login_status_changed(None, None)

		(username, password) = load_account()
		self.builder.get_object('username_entry').set_text(username)
		self.builder.get_object('password_entry').set_text(password)
		self.builder.get_object('username_entry').connect('focus-out-event', self.account_details_changed)
		self.builder.get_object('password_entry').connect('focus-out-event', self.account_details_changed)

		self.test_login_button = self.builder.get_object('test_login_button')
		self.test_login_button.connect('clicked', self.login_clicked)

		return content

