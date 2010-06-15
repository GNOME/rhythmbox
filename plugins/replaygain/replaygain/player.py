# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2010 Jonathan Matthew
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

import gtk
import gconf
import gst
import gobject
import rhythmdb, rb

import config

EPSILON = 0.001

class ReplayGainPlayer(object):
	def __init__(self, shell):
		# make sure the replaygain elements are available
		missing = []
		required = ("rgvolume", "rglimiter")
		for e in required:
			if gst.element_factory_find(e) is None:
				missing.append(e)

		if len(missing) > 0:
			msg = _("The GStreamer elements required for ReplayGain processing are not available. The missing elements are: %s") % ", ".join(missing)
			rb.error_dialog(shell.props.window, _("ReplayGain GStreamer plugins not available"), msg)
			raise Exception(msg)

		self.shell_player = shell.props.shell_player
		self.player = self.shell_player.props.player
		self.gconf = gconf.client_get_default()

		self.gconf.add_dir(config.GCONF_DIR, preload=False)
		self.gconf.notify_add(config.GCONF_KEYS['limiter'], self.limiter_changed_cb)

		self.previous_gain = []
		self.fallback_gain = 0.0
		self.resetting_rgvolume = False

		# we use different means to hook into the playback pipeline depending on
		# the playback backend in use
		if gobject.signal_lookup("get-stream-filters", self.player):
			self.setup_xfade_mode()
			self.deactivate_backend = self.deactivate_xfade_mode
		else:
			self.setup_playbin2_mode()
			self.deactivate_backend = self.deactivate_playbin2_mode



	def deactivate(self):
		self.deactivate_backend()
		self.player = None
		self.shell_player = None


	def set_rgvolume(self, rgvolume):
		# set preamp level
		preamp = self.gconf.get_float(config.GCONF_KEYS['preamp'])
		rgvolume.props.pre_amp = preamp

		# set mode
		# there may eventually be a 'guess' mode here that tries to figure out
		# what to do based on the upcoming tracks
		mode = self.gconf.get_int(config.GCONF_KEYS['mode'])
		if mode == config.REPLAYGAIN_MODE_ALBUM:
			rgvolume.props.album_mode = 1
		else:
			rgvolume.props.album_mode = 0

		# set calculated fallback gain
		rgvolume.props.fallback_gain = self.fallback_gain

		print "updated rgvolume settings: preamp %f, album-mode %s, fallback gain %f" % (
			rgvolume.props.pre_amp, str(rgvolume.props.album_mode), rgvolume.props.fallback_gain)


	def update_fallback_gain(self, rgvolume):
		gain = rgvolume.props.target_gain - rgvolume.props.pre_amp
		# filter out bogus notifications
		if abs(gain - self.fallback_gain) < EPSILON:
			print "ignoring gain %f (current fallback gain)" % gain
			return False
		if abs(gain) < EPSILON:
			print "ignoring zero gain (pretty unlikely)"
			return False

		# update the running average
		if len(self.previous_gain) == config.AVERAGE_GAIN_SAMPLES:
			self.previous_gain.pop(0)
		self.previous_gain.append(gain)
		self.fallback_gain = sum(self.previous_gain) / len(self.previous_gain)
		print "got target gain %f; running average of previous gain values is %f" % (gain, self.fallback_gain)
		return True



	### playbin2 mode (rgvolume ! rglimiter as global filter)

	def playbin2_uri_notify_cb(self, playbin, pspec):
		self.got_replaygain = False

	def playbin2_notify_cb(self, player, pspec):
		playbin = player.props.playbin
		playbin.connect("notify::uri", self.playbin2_uri_notify_cb)


	def playbin2_target_gain_cb(self, rgvolume, pspec):
		#if self.resetting_rgvolume is True:
		#	return

		if self.update_fallback_gain(rgvolume) == True:
			self.got_replaygain = True
		# do something clever probably

	def rgvolume_reset_done(self, pad, blocked, rgvolume):
		print "rgvolume reset done"
		self.set_rgvolume(rgvolume)

	def rgvolume_blocked_cb(self, pad, blocked, rgvolume):
		print "bouncing rgvolume state to reset tags"
		# somehow need to decide whether we've already got a gain value for the new track
		#self.resetting_rgvolume = True
		rgvolume.set_state(gst.STATE_READY)
		rgvolume.set_state(gst.STATE_PLAYING)
		#self.resetting_rgvolume = False
		pad.set_blocked_async(False, self.rgvolume_reset_done, rgvolume)

	def playing_entry_changed(self, player, entry):
		if entry is None:
			return

		if self.got_replaygain is False:
			print "blocking rgvolume to reset it"
			pad = self.rgvolume.get_static_pad("sink").get_peer()
			pad.set_blocked_async(True, self.rgvolume_blocked_cb, self.rgvolume)
		else:
			print "no need to reset rgvolume"

	def setup_playbin2_mode(self):
		print "using output filter for rgvolume and rglimiter"
		self.rgvolume = gst.element_factory_make("rgvolume")
		self.rgvolume.connect("notify::target-gain", self.playbin2_target_gain_cb)
		self.rglimiter = gst.element_factory_make("rglimiter")

		# on track changes, we need to reset the rgvolume state, otherwise it
		# carries over the tags from the previous track
		self.pec_id = self.shell_player.connect('playing-song-changed', self.playing_entry_changed)

		# watch playbin2's uri property to see when a new track is opened
		playbin = self.player.props.playbin
		if playbin is None:
			self.player.connect("notify::playbin", self.playbin2_notify_cb)
		else:
			playbin.connect("notify::uri", self.playbin2_uri_notify_cb)

		# try to work around bug #621632
		if gobject.pygobject_version > (2,21,1):
			print "working around pygobject/gst-python refcount bug.."
			self.player.add_filter(self.rgvolume)
			self.player.add_filter(self.rglimiter)
			self.rgfilter = None
		else:
			self.rgfilter = gst.Bin()
			self.rgfilter.add(self.rgvolume, self.rglimiter)
			self.rgvolume.link(self.rglimiter)
			self.rgfilter.add_pad(gst.GhostPad("sink", self.rgvolume.get_static_pad("sink")))
			self.rgfilter.add_pad(gst.GhostPad("src", self.rglimiter.get_static_pad("src")))
			self.player.add_filter(self.rgfilter)

	def deactivate_playbin2_mode(self):
		if self.rgfilter == None:
			self.player.remove_filter(self.rglimiter)
			self.player.remove_filter(self.rgvolume)
		else:
			self.player.remove_filter(self.rgfilter)
			self.rgfilter = None

		self.shell_player.disconnect(self.pec_id)
		self.pec_id = None



	### xfade mode (rgvolume as stream filter, rglimiter as global filter)

	def xfade_target_gain_cb(self, rgvolume, pspec):
		if self.update_fallback_gain(rgvolume) is  True:
			# we don't want any further notifications from this stream
			rgvolume.disconnect_by_func(self.xfade_target_gain_cb)

	def create_stream_filter_cb(self, player, uri):
		print "creating rgvolume instance for stream %s" % uri
		rgvolume = gst.element_factory_make("rgvolume")
		rgvolume.connect("notify::target-gain", self.xfade_target_gain_cb)
		self.set_rgvolume(rgvolume)
		return [rgvolume]

	def limiter_changed_cb(self, client, id, entry, d):
		if self.rglimiter is not None:
			limiter = self.gconf.get_bool(config.GCONF_KEYS['limiter'])
			print "limiter setting is now %s" % str(limiter)
			self.rglimiter.props.enabled = limiter

	def setup_xfade_mode(self):
		print "using per-stream filter for rgvolume"
		self.stream_filter_id = self.player.connect("get-stream-filters", self.create_stream_filter_cb)

		# and add rglimiter as an output filter
		self.rglimiter = gst.element_factory_make("rglimiter")
		self.player.add_filter(self.rglimiter)

	def deactivate_xfade_mode(self):
		self.player.disconnect(self.stream_filter_id)
		self.stream_filter_id = None
		self.player.remove_filter(self.rglimiter)
		self.rglimiter = None
