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

import gi
gi.require_version('Soup', '3.0')
from gi.repository import Gio, GLib, GObject, Soup
import datetime
import json
import time
import sys

from rbconfig import rhythmbox_version

from urllib.parse import quote_plus

SLOW_TIMEOUT = 600
FAST_TIMEOUT = 10

def gpodder_url_path(url, path):
	return GLib.build_pathv('/', [url.get_path(), path])


class GPodderLoginStatus(GObject.GEnum):
	NOT_LOGGED_IN = 0
	SUCCESS = 1
	ERROR = 2
	FAILED = 3

class GPodderSync (GObject.Object):
	def __init__(self, base_url, device_id, device_name, state_file, handler):
		super().__init__()
		self.cancellable = Gio.Cancellable()
		self.handler = handler

		self.soup = Soup.Session()
		self.soup.set_user_agent("Rhythmbox/" + rhythmbox_version)
		self.soup.add_feature(Soup.CookieJar())

		self.device_id = device_id
		self.device_name = device_name
		self.base_url = base_url
		self.api_url = None
		self.api_url_expire = 0

		self.timeout_id = -1
		self.actions = []

		self.state_file = state_file
		self.sync_state = {}
		self.load_sync_state()
		if 'episode-actions' not in self.sync_state:
			self.sync_state['episode-actions'] = []

		self.username = None
		self.password = None
		self.login_status_id = GPodderLoginStatus.NOT_LOGGED_IN
		self.login_status_message = None
		self.sync_subscriptions = False
		self.sync_episodes = False

	def set_sync_subscriptions(self, enable):
		self.sync_subscriptions = enable
		self.set_actions()
		self.schedule()

	def set_sync_episodes(self, enable):
		self.sync_episodes = enable
		self.set_actions()
		self.schedule()

	def set_url(self, base_url):
		if base_url == self.base_url:
			return

		self.api_url = None
		self.api_url_expire = 0


	def set_account(self, username, password):
		if (self.username == username) and (self.password == password):
			return
		if self.username is not None and self.username != username:
			self.sync_state = {}

		self.username = username
		self.password = password
		self.login_status_id = GPodderLoginStatus.NOT_LOGGED_IN
		self.login_status_message = ""
		self.notify("login-status")
		self.schedule()

	def cancel(self):
		if self.timeout_id != -1:
			GLib.source_remove(self.timeout_id)
			self.timeout_id = -1
		self.cancellable.cancel()

	@GObject.Property
	def login_status(self):
		return self.login_status_id

	def get_login_status_message(self):
		return self.login_status_message

	def get_last_sync(self):
		if 'last-sync-time' not in self.sync_state:
			return (None, None)
		else:
			return (self.sync_state['last-sync-time'], self.sync_state['last-sync-success'])

	def needs_sync(self):
		if self.sync_subscriptions:
			su = self.sync_state.get("subscription-updates")
			if su is not None and (len(su.get("add", [])) + len(su.get("remove", [])) != 0):
				return True

		if self.sync_episodes:
			eu = self.sync_state.get("episode-actions")
			if eu is not None and len(eu) > 0:
				return True

		return False


	def schedule(self):
		if self.timeout_id != -1:
			GLib.source_remove(self.timeout_id)

		if self.username is None or (self.sync_subscriptions is False and self.sync_episodes is False):
			return

		# should back off on failures here probably
		if self.needs_sync():
			interval = FAST_TIMEOUT
		else:
			interval = SLOW_TIMEOUT

		self.timeout_id = GLib.timeout_add_seconds(interval, self.run)

	def reschedule(self):
		if self.timeout_id == -1:
			return

		self.schedule()

	def set_actions(self):
		self.actions = [
			self.refresh_client_config,
			self.login
		]
		if self.sync_state.get('device-data') is None:
			self.actions.append(self.update_device_data)

		if self.sync_subscriptions:
			self.actions.append(self.send_subscription_updates)
			self.actions.append(self.get_subscription_updates)

		if self.sync_episodes:
			self.actions.append(self.send_episode_actions)
			self.actions.append(self.get_episode_actions)


	def run(self):
		success = True
		try:
			while len(self.actions) > 0:
				func = self.actions[0]
				self.actions = self.actions[1:]
				if func() == True:
					self.timeout_id = -1
					return False
		except Exception as e:
			print("sync failed: {}".format(str(e)))
			success = False

		self.sync_state['last-sync-time'] = int(time.time())
		self.sync_state['last-sync-success'] = success
		self.save_sync_state()
		self.handler.sync_finished()

		self.set_actions()
		self.schedule()
		return False

	def test_login(self):
		# what do we do if we're already syncing here?

		self.actions = [
			self.refresh_client_config,
			self.login
		]
		if self.timeout_id != -1:
			GLib.source_remove(self.timeout_id)
			self.timeout_id = -1

		self.run()


	def clear_sync_state(self):
		self.sync_state = {}
		try:
			os.unlink(self.state_file)
		except Exception as e:
			print("unable to clear sync state: {}".format(str(e)))

	def load_sync_state(self):
		try:
			with open(self.state_file) as s:
				self.sync_state = json.load(s)
		except Exception as e:
			print("unable to load sync state: {}".format(str(e)))

	def save_sync_state(self):
		try:
			with open(self.state_file, 'w') as s:
				json.dump(self.sync_state, s)
		except Exception as e:
			print("unable to save sync state: {}".format(str(e)))


	def http_request_cb(self, session, result, data):
		message = session.get_async_result_message(result)
		status = message.get_status()
		body = None
		try:
			bytes = session.send_and_read_finish(result)
			if bytes:
				body = bytes.get_data()
			if status != 200:
				print("non-200 response: " + body.decode('utf-8'))
				body = message.get_reason_phrase()
		except Exception as e:
			status = -1
			body = str(e)
			sys.excepthook(*sys.exc_info())
		finally:
			(callback, args) = data

		try:
			v = callback(body, status, *args)
		except Exception as e:
			sys.excepthook(*sys.exc_info())

	def http_request(self, method, url, body, body_content_type, callback, *data):
		try:
			req = Soup.Message.new(method, url)
			if body is not None:
				bst = Gio.MemoryInputStream.new_from_bytes(GLib.Bytes.new(body))
				req.set_request_body(body_content_type, bst, len(body))
			self.soup.send_and_read_async(req, GLib.PRIORITY_DEFAULT, self.cancellable, self.http_request_cb, (callback, data))
		except Exception as e:
			sys.excepthook(*sys.exc_info())
			callback(None, -1, *data)

	def api_request(self, method, path, query, body, body_content_type, callback, *data):
		userinfo = "{}:{}".format(self.username, self.password)
		url = GLib.uri_join(GLib.UriFlags.NONE, self.api_url.get_scheme(), userinfo, self.api_url.get_host(), self.api_url.get_port(), gpodder_url_path(self.api_url, path), query, None)
		self.http_request(method, url, body, body_content_type, callback, data)


	### GET client configuration

	def client_config_cb(self, data, status, junk):
		found = False
		if status == 200 and data is not None:
			try:
				j = json.loads(data)
				self.api_url = GLib.uri_parse(j['mygpo']['baseurl'], GLib.UriFlags.NONE)
				age = int(j['update_timeout'])
				found = True
				print("got api base url {} from client config".format(j['mygpo']['baseurl']))
			except Exception as e:
				print("couldn't parse client config: " + str(e))

		if found is False:
			print("using default base url")
			self.api_url = GLib.uri_parse(self.base_url, GLib.UriFlags.NONE)
			age = 3600

		self.api_url_expire = time.time() + age
		self.run()

	def refresh_client_config(self):
		if self.api_url_expire > time.time():
			return False

		print("refreshing client config from {}".format(self.base_url))
		burl = GLib.uri_parse(self.base_url, GLib.UriFlags.NONE)
		ccurl = GLib.uri_join(GLib.UriFlags.NONE, burl.get_scheme(), None, burl.get_host(), burl.get_port(), gpodder_url_path(burl, "/clientconfig.json"), None, None)
		self.http_request("GET", ccurl, None, None, self.client_config_cb, None)
		return True

	### POST login

	def login_cb(self, result, status, data):
		if status == -1:
			print("unable to log in")
			self.login_status_id = GPodderLoginStatus.ERROR
			self.login_status_message = result
			self.notify("login-status")

		elif status != 200:
			print("login failed")
			self.login_status_id = GPodderLoginStatus.FAILED
			self.login_status_message = result
			self.notify("login-status")

		else:
			print("login successful")
			self.login_status_id = GPodderLoginStatus.SUCCESS
			self.login_status_message = ""
			self.notify("login-status")
			self.run()

	def login(self):
		if self.username is None:
			self.login_status_id = GPodderLoginStatus.NOT_LOGGED_IN
			self.login_status_message = ""
			self.notify("login-status")
			return False

		path = "/api/2/auth/{}/login.json".format(self.username)
		self.api_request("POST", path, None, None, None, self.login_cb, None)
		return True


	### POST device data

	def device_data_update_cb(self, result, status, data):
		if result is None or status != 200:
			print("failed to update device data")
		else:
			print("device data updated")
			self.sync_state['device-data'] = True
			self.run()

	def update_device_data(self):
		device = {
			'caption': self.device_name,
			'type': 'desktop'
		}

		path = "/api/2/devices/{}/{}.json".format(self.username, self.device_id)
		data = json.dumps(device).encode('utf-8')
		print("sending device data")
		self.api_request('POST', path, None, data, "application/json", self.device_data_update_cb, None)
		return True

	### POST episode actions

	def episode_update_cb(self, result, status, data):
		if result is None or status != 200:
			print("failed to update episode actions")
		else:
			(nevents,) = data
			print("sent {} episode actions successfully".format(nevents))
			actions = self.sync_state.get("episode-actions")
			synced = actions[:nevents]
			self.sync_state['episode-actions'] = actions[nevents:]

			self.save_sync_state()

		self.run()

	def send_episode_actions(self):
		eu = self.sync_state.get("episode-actions")
		if eu is None or len(eu) == 0:
			return False

		path = "/api/2/episodes/{}.json".format(self.username)
		data = json.dumps(eu).encode('utf-8')
		print("sending {} episode actions".format(len(eu)))
		self.api_request('POST', path, None, data, "application/json", self.episode_update_cb, len(eu))
		return True

	def add_episode_action(self, action):
		ts = datetime.datetime.now(tz=datetime.timezone.utc)
		action['timestamp'] = ts.isoformat()
		action['device-id'] = self.device_id

		# maybe do some duplicate checking?
		self.sync_state['episode-actions'].append(action)
		self.save_sync_state()
		self.reschedule()


	### POST subscription changes

	def subscription_update_cb(self, result, status, data):
		if result is None or status != 200:
			print("failed to update subscriptions")
		else:
			print("updated subscriptions successfully")

			del self.sync_state["subscription-updates"]
			self.save_sync_state()

		self.run()

	def send_subscription_updates(self):
		if self.sync_state.get("subscription-updates") is None:
			return False

		path = "/api/2/subscriptions/{}/{}.json".format(self.username, self.device_id)
		data = json.dumps(self.sync_state["subscription-updates"]).encode('utf-8')
		self.api_request("POST", path, None, data, "application/json", self.subscription_update_cb, None)
		return True

	def add_subscription_update(self, added, url):
		if added:
			(addto, removefrom) = ("add", "remove")
		else:
			(addto, removefrom) = ("remove", "add")

		if self.sync_state.get("subscription-updates") is None:
			self.sync_state["subscription-updates"] = { addto: [url], removefrom: [] }
		else:
			upd = self.sync_state["subscription-updates"]
			if url not in upd[addto]:
				upd[addto].append(url)

			if url in upd[removefrom]:
				upd[removefrom].remove(url)

		self.save_sync_state()
		self.reschedule()

	def add_all_feeds(self):
		lastupdatetime = self.sync_state.get("subscription-timestamp", "0")
		return (lastupdatetime == "0")

	### GET episode actions

	def episode_actions_cb(self, data, status, thing):
		if data is None or status != 200:
			print("failed to fetch episode actions")
			self.run()
			return

		latest = int(self.sync_state.get("episode-timestamp", "0"))
		try:
			j = json.loads(data)
			for a in j.get('actions', []):
				if a.get('device') == self.device_id:
					print("ignoring our own action: {} {}".format(a.get('episode'), a.get('action')))
					continue

				print("syncing episode action: {} {}".format(a.get('episode'), a.get('action')))

				dts = datetime.datetime.now(datetime.timezone.utc)

				ts = a.get('timestamp')
				if ts is not None:
					dts = datetime.datetime.fromisoformat(ts).replace(tzinfo=datetime.timezone.utc)
					if dts.timestamp() > latest:
						latest = int(dts.timestamp())

				self.handler.episode_action_cb(a, int(dts.timestamp()))

			# start just after the latest event we got, rather than using the timestamp returned by the server.
			# play events (the only ones we really care about) are timestamped at the time playback stopped, not
			# when the server received them, and there may be a significant gap between those times.
			# this doesn't guarantee we'll see all play events if there are multiple other sync clients, but
			# it's better than nothing

			self.sync_state["episode-timestamp"] = str(latest + 1)
			self.save_sync_state()
		except Exception as e:
			sys.excepthook(*sys.exc_info())

		self.run()

	def get_episode_actions(self):
		lastupdatetime = self.sync_state.get("episode-timestamp", "0")
		path = "/api/2/episodes/{}.json".format(self.username)
		query = "since={}".format(lastupdatetime)

		print("retrieving episode actions since {}".format(lastupdatetime))
		self.api_request("GET", path, query, None, None, self.episode_actions_cb, None)
		return True


	### GET subscription changes

	def subscription_updates_cb(self, data, status, thing):
		if data is None or status != 200:
			print("failed to fetch subscription updates")
			self.run()
			return

		try:
			j = json.loads(data)
			for a in j.get('add', []):
				print("adding podcast feed {}".format(a))
				self.handler.subscription_added_cb(a)

			for d in j.get('remove', []):
				print("removing podcast feed {}".format(d))
				self.handler.subscription_removed_cb(a)

			self.sync_state["subscription-timestamp"] = j.get("timestamp", "0")
			self.save_sync_state()
		except Exception as e:
			sys.excepthook(*sys.exc_info())

		self.run()


	def get_subscription_updates(self):
		lastupdatetime = self.sync_state.get("subscription-timestamp", "0")
		path = '/api/2/subscriptions/{}/{}.json'.format(self.username, self.device_id)
		query = "since={}".format(lastupdatetime)

		print("retrieving subscription updates since {}".format(lastupdatetime))
		self.api_request("GET", path, query, None, None, self.subscription_updates_cb, None)
		return True

