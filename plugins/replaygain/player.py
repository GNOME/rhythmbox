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

import rb
import gi

gi.require_version("Gst", "1.0")
from gi.repository import RB
from gi.repository import GObject, Gio, Gst

import config

import gettext
gettext.install('rhythmbox', RB.locale_dir())

EPSILON = 0.001

class ReplayGainPlayer(object):
	def __init__(self, shell):
		# make sure the replaygain elements are available
		missing = []
		required = ("rgvolume", "rglimiter")
		for e in required:
			if Gst.ElementFactory.find(e) is None:
				missing.append(e)

		if len(missing) > 0:
			msg = _("The GStreamer elements required for ReplayGain processing are not available. The missing elements are: %s") % ", ".join(missing)
			RB.error_dialog(shell.props.window, _("ReplayGain GStreamer plugins not available"), msg)
			raise Exception(msg)

		self.shell_player = shell.props.shell_player
		self.player = self.shell_player.props.player
		self.settings = Gio.Settings.new("org.gnome.rhythmbox.plugins.replaygain")

		self.settings.connect("changed::limiter", self.limiter_changed_cb)

		self.previous_gain = []
		self.fallback_gain = 0.0

		# we use different means to hook into the playback pipeline depending on
		# the playback backend in use
		if GObject.signal_lookup("get-stream-filters", self.player):
			self.setup_xfade_mode()
			self.deactivate_backend = self.deactivate_xfade_mode
		else:
			self.setup_playbin_mode()
			self.deactivate_backend = self.deactivate_playbin_mode



	def deactivate(self):
		self.deactivate_backend()
		self.player = None
		self.shell_player = None


	def set_rgvolume(self, rgvolume):
		# set preamp level
		preamp = self.settings['preamp']
		rgvolume.props.pre_amp = preamp

		# set mode
		# there may eventually be a 'guess' mode here that tries to figure out
		# what to do based on the upcoming tracks
		mode = self.settings['mode']
		if mode == config.REPLAYGAIN_MODE_ALBUM:
			rgvolume.props.album_mode = 1
		else:
			rgvolume.props.album_mode = 0

		# set calculated fallback gain
		rgvolume.props.fallback_gain = self.fallback_gain

		print("updated rgvolume settings: preamp %f, album-mode %s, fallback gain %f" % (
			rgvolume.props.pre_amp, str(rgvolume.props.album_mode), rgvolume.props.fallback_gain))


	def update_fallback_gain(self, rgvolume):
		gain = rgvolume.props.target_gain - rgvolume.props.pre_amp
		# filter out bogus notifications
		if abs(gain - self.fallback_gain) < EPSILON:
			print("ignoring gain %f (current fallback gain)" % gain)
			return False
		if abs(gain) < EPSILON:
			print("ignoring zero gain (pretty unlikely)")
			return False

		# update the running average
		if len(self.previous_gain) == config.AVERAGE_GAIN_SAMPLES:
			self.previous_gain.pop(0)
		self.previous_gain.append(gain)
		self.fallback_gain = sum(self.previous_gain) / len(self.previous_gain)
		print("got target gain %f; running average of previous gain values is %f" % (gain, self.fallback_gain))
		return True



	### playbin mode (rgvolume ! rglimiter as global filter)

	def playbin_target_gain_cb(self, rgvolume, pspec):
		self.update_fallback_gain(rgvolume)

	def setup_playbin_mode(self):
		print("using output filter for rgvolume and rglimiter")
		self.rgfilter = Gst.Bin()

		self.rgvolume = Gst.ElementFactory.make("rgvolume", None)
		self.rgvolume.connect("notify::target-gain", self.playbin_target_gain_cb)
		self.rgfilter.add(self.rgvolume)

		self.rglimiter = Gst.ElementFactory.make("rglimiter", None)
		self.rgfilter.add(self.rglimiter)

		self.rgfilter.add_pad(Gst.GhostPad.new("sink", self.rgvolume.get_static_pad("sink")))
		self.rgfilter.add_pad(Gst.GhostPad.new("src", self.rglimiter.get_static_pad("src")))
		self.rgvolume.link(self.rglimiter)

		self.player.add_filter(self.rgfilter)

	def deactivate_playbin_mode(self):
		self.player.remove_filter(self.rgfilter)
		self.rgfilter = None


	### xfade mode (rgvolume as stream filter, rglimiter as global filter)

	def xfade_target_gain_cb(self, rgvolume, pspec):
		if self.update_fallback_gain(rgvolume) is  True:
			# we don't want any further notifications from this stream
			rgvolume.disconnect_by_func(self.xfade_target_gain_cb)

	def create_stream_filter_cb(self, player, uri):
		print("creating rgvolume instance for stream %s" % uri)
		rgvolume = Gst.ElementFactory.make("rgvolume", None)
		rgvolume.connect("notify::target-gain", self.xfade_target_gain_cb)
		self.set_rgvolume(rgvolume)
		return [rgvolume]

	def limiter_changed_cb(self, settings, key):
		if self.rglimiter is not None:
			limiter = settings['limiter']
			print("limiter setting is now %s" % str(limiter))
			self.rglimiter.props.enabled = limiter

	def setup_xfade_mode(self):
		print("using per-stream filter for rgvolume")
		self.stream_filter_id = self.player.connect("get-stream-filters", self.create_stream_filter_cb)

		# and add rglimiter as an output filter
		self.rglimiter = Gst.ElementFactory.make("rglimiter", None)
		self.player.add_filter(self.rglimiter)

	def deactivate_xfade_mode(self):
		self.player.disconnect(self.stream_filter_id)
		self.stream_filter_id = None
		self.player.remove_filter(self.rglimiter)
		self.rglimiter = None
