# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*- 
#
# Copyright (C) 2006 - James Livingston
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

import rhythmdb, rb
import gtk, gobject
from warnings import warn

from CoverArtDatabase import CoverArtDatabase


FADE_STEPS = 10
FADE_TOTAL_TIME = 1000
ART_MISSING_ICON = 'rhythmbox-missing-artwork'
WORKING_DELAY = 500
THROBBER_RATE = 10
THROBBER = 'gnome-spinner'

def pixbuf_aspect (pixbuf):
	if pixbuf is None: return None
	return float (pixbuf.props.height) / pixbuf.props.width

def weighted_avg (a, b, weight):
	if a is None and b is None: return 1
	elif a is None: return b
	elif b is None: return a
	else: return a * (1 - weight) + b * weight

def merge_height (old_aspect, new_aspect, reserve_aspect, step, width):
	if old_aspect is None:
		if new_aspect is None:
			return width
		else:
			return int (new_aspect * width)
	elif new_aspect is None:
		new_aspect = reserve_aspect
	return int (width * weighted_avg (old_aspect, new_aspect, step))

def merge_pixbufs (old_pb, new_pb, reserve_pb, step, width, height, mode=gtk.gdk.INTERP_BILINEAR):
	if width <= 1 and height <= 1:
		return None
	if old_pb is None:
		if new_pb is None:
			return reserve_pb
		else:
			return new_pb.scale_simple (width, height, mode)
	elif step == 0.0:
		return old_pb.scale_simple (width, height, mode)
	elif new_pb is None:
		if reserve_pb is None:
			return None
		new_pb = reserve_pb
	sw, sh = (float (width)) / new_pb.props.width, (float (height)) / new_pb.props.height
	alpha = int (step * 255)
	ret = old_pb.scale_simple (width, height, mode)
	new_pb.composite (ret, 0, 0, width, height, 0, 0, sw, sh, mode, alpha)
	return ret

def merge_with_background (pixbuf, bgcolor):
	if pixbuf is None or not pixbuf.get_has_alpha ():
		return pixbuf
	width, height = pixbuf.props.width, pixbuf.props.height
	bg = ((bgcolor.red & 0xff00) << 16) | ((bgcolor.green & 0xff00) << 8) | (bgcolor.blue & 0xff00) | 0xff
	ret = gtk.gdk.Pixbuf (gtk.gdk.COLORSPACE_RGB, False, 8, width, height)
	ret.fill (bg)
	pixbuf.composite (ret, 0, 0, width, height, 0, 0, 1.0, 1.0, gtk.gdk.INTERP_NEAREST, 255)
	return ret

class FadingImage (gtk.Misc):
	__gsignals__ = { 'size-allocate': 'override' }
	def __init__ (self, missing_image):
		gobject.GObject.__init__ (self)
		self.sc_id = self.connect('screen-changed', self.screen_changed)
		self.ex_id = self.connect ('expose-event', self.expose)
		self.sr_id = self.connect ('size-request', self.size_request)
		self.resize_id, self.fade_id, self.anim_id = 0, 0, 0
		self.missing_image, self.size = missing_image, 100
		self.screen_changed (self, None)
		self.old_pixbuf, self.new_pixbuf = None, None
		self.merged_pixbuf, self.missing_pixbuf = None, None
		self.fade_step = 0.0
		self.anim, self.anim_frames, self.anim_size = None, None, 0

	def disconnect_handlers (self):
		for id in self.sc_id, self.ex_id, self.sr_id:
			self.disconnect(id)
		self.icon_theme.disconnect(self.tc_id)
		for id in self.resize_id, self.fade_id, self.anim_id:
			if id != 0:
				gobject.source_remove (id)

	def screen_changed (self, widget, old_screen):
		if old_screen:
			self.icon_theme.disconnect (self.tc_id)
		self.icon_theme = gtk.icon_theme_get_for_screen (self.get_screen ())
		self.tc_id = self.icon_theme.connect ('changed', self.theme_changed)
		self.theme_changed (self.icon_theme)

	def reload_anim_frames (self):
		icon_info = self.icon_theme.lookup_icon (THROBBER, -1, 0)
		size = icon_info.get_base_size ()
		icon = gtk.gdk.pixbuf_new_from_file (icon_info.get_filename ())
		self.anim_frames = [ # along, then down
				icon.subpixbuf (x * size, y * size, size, size)
				for y in range (int (icon.props.height / size))
				for x in range (int (icon.props.width / size))]
		self.anim_size = size

	def theme_changed (self, icon_theme):
		try:
			self.reload_anim_frames ()
		except Exception, e:
			warn ("Throbber animation not loaded: %s" % e, Warning)
		self.reload_util_pixbufs ()

	def reload_util_pixbufs (self):
		if self.size <= 1:
			return
		try:
			missing_pixbuf = self.icon_theme.load_icon (ART_MISSING_ICON, self.size, 0)
		except:
			try:
				missing_pixbuf = gtk.gdk.pixbuf_new_from_file_at_size (self.missing_image, self.size, self.size)
			except Exception, e:
				warn ("Missing artwork icon not found: %s" % e, Warning)
				return
		self.missing_pixbuf = merge_with_background (missing_pixbuf, self.style.bg[gtk.STATE_NORMAL])

	def do_size_allocate (self, allocation):
		self.allocation = allocation
		if self.resize_id == 0:
			self.resize_id = gobject.idle_add (self.after_resize)
		if self.size != allocation.width:
			self.size = allocation.width
			self.queue_resize ()
		elif self.window is not None:
			self.window.move_resize (allocation.x, allocation.y, allocation.width, allocation.height)
			self.queue_draw ()
			self.window.process_updates (True)

	def after_resize (self):
		self.reload_util_pixbufs ()
		self.merged_pixbuf = None
		self.queue_draw ()
		return False

	def size_request (self, widget, requisition):
		requisition.width = -1
		requisition.height = merge_height (pixbuf_aspect (self.old_pixbuf), pixbuf_aspect (self.new_pixbuf), 1.0, self.fade_step, self.size)

	def expose (self, widget, event):
		if not self.ensure_merged_pixbuf ():
			return False
		if self.merged_pixbuf.props.width != self.size:
			draw_pb = self.merged_pixbuf.scale_simple (self.size, self.size, gtk.gdk.INTERP_NEAREST)
		else:
			draw_pb = self.merged_pixbuf
		x, y, w, h = event.area
		event.window.draw_pixbuf (None, draw_pb, x, y, x, y, min (w, self.size - x), min (h, self.size - y))
		if self.anim:
			x, y, w, h = self.anim_rect ()
			event.window.draw_pixbuf (None, self.anim, max (0, -x), max (0, -y), max (0, x), max (0, y), w, h)
		return False

	def anim_rect (self):
		return gtk.gdk.Rectangle (
				(self.allocation.width - self.anim_size) / 2,
				(self.allocation.height - self.anim_size) / 2,
				min (self.anim_size, self.allocation.width),
				min (self.anim_size, self.allocation.height))

	def ensure_merged_pixbuf (self):
		if self.merged_pixbuf is None:
			self.merged_pixbuf = merge_pixbufs (self.old_pixbuf, self.new_pixbuf, self.missing_pixbuf, self.fade_step, self.allocation.width, self.allocation.height)
		return self.merged_pixbuf

	def render_overlay (self):
		ret = self.ensure_merged_pixbuf ()
		if ret and self.anim:
			if ret is self.missing_pixbuf: ret = ret.copy ()
			x, y, w, h = self.anim_rect ()
			self.anim.composite (ret, max (x, 0), max (y, 0), w, h, x, y, 1, 1, gtk.gdk.INTERP_BILINEAR, 255)
		return ret

	def fade_art (self, first_time):
		self.fade_step += 1.0 / FADE_STEPS
		if self.fade_step > 0.999:
			self.old_pixbuf = None
			self.fade_id = 0
		self.merged_pixbuf = None
		if first_time:
			self.fade_id = gobject.timeout_add ((FADE_TOTAL_TIME / FADE_STEPS), self.fade_art, False)
			return False
		self.queue_resize ()
		return (self.fade_step <= 0.999)

	def animation_advance (self, counter, first_time):
		self.anim = self.anim_frames[counter[0]]
		counter[0] = (counter[0] + 1) % len(self.anim_frames)
		x, y, w, h = self.anim_rect ()
		self.queue_draw_area (max (x, 0), max (y, 0), w, h)
		if first_time:
			self.anim_id = gobject.timeout_add (int (1000 / THROBBER_RATE), self.animation_advance, counter, False)
			return False
		return True

	def set_current_art (self, pixbuf, working):
		if self.props.visible and self.parent.allocation.width > 1:
			self.old_pixbuf = self.render_overlay ()
		else:
			self.old_pixbuf = None	# don't fade
		self.new_pixbuf = merge_with_background (pixbuf, self.style.bg[gtk.STATE_NORMAL])
		self.merged_pixbuf = None
		self.fade_step = 0.0
		self.anim = None
		if self.fade_id != 0:
			gobject.source_remove (self.fade_id)
			self.fade_id = 0
		if self.old_pixbuf is not None:
			self.fade_id = gobject.timeout_add (working and WORKING_DELAY or (FADE_TOTAL_TIME / FADE_STEPS), self.fade_art, working)
		if working and self.anim_id == 0 and self.anim_frames:
			self.anim_id = gobject.timeout_add (WORKING_DELAY, self.animation_advance, [0], True)
		if not working and self.anim_id != 0:
			gobject.source_remove (self.anim_id)
			self.anim_id = 0
		self.queue_resize ()
gobject.type_register (FadingImage)


class ArtDisplayWidget (FadingImage):
	def __init__ (self, missing_image):
		super (ArtDisplayWidget, self).__init__ (missing_image)
		self.set_padding (0, 5)
		self.current_entry, self.working = None, False
		self.current_pixbuf, self.current_uri = None, None
	
	def set (self, entry, pixbuf, uri, working):
		self.current_entry = entry
		self.current_pixbuf = pixbuf
		self.current_uri = uri
		self.set_current_art (pixbuf, working)


class ArtDisplayPlugin (rb.Plugin):
	def __init__ (self):
		rb.Plugin.__init__ (self)
		
	def activate (self, shell):
		self.shell = shell
		sp = shell.get_player ()
		self.pec_id = sp.connect ('playing-song-changed', self.playing_entry_changed)
		self.pc_id = sp.connect ('playing-changed', self.playing_changed)
		db = shell.get_property ("db")
		self.eemr_art_id = db.connect_after ('entry-extra-metadata-request::rb:coverArt', self.cover_art_request)
		self.eemn_art_id = db.connect_after ('entry-extra-metadata-notify::rb:coverArt', self.cover_art_notify)
		self.art_widget = ArtDisplayWidget (self.find_file (ART_MISSING_ICON + ".svg"))
		shell.add_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_db = CoverArtDatabase ()
		self.current_entry, self.current_pixbuf = None, None
		self.playing_entry_changed (sp, sp.get_playing_entry ())
	
	def deactivate (self, shell):
		self.shell = None
		sp = shell.get_player ()
		sp.disconnect (self.pec_id)
		sp.disconnect (self.pc_id)
		db = shell.get_property ("db")
		db.disconnect (self.eemr_art_id)
		db.disconnect (self.eemn_art_id)
		shell.remove_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_widget.disconnect_handlers ()
		self.art_widget = None
		self.art_db = None

	def playing_changed (self, sp, playing):
		self.set_entry(sp.get_playing_entry ())

	def playing_entry_changed (self, sp, entry):
		self.set_entry(entry)

	def set_entry (self, entry):
		if entry == self.current_entry:
			return
		db = self.shell.get_property ("db")

		self.art_widget.set (entry, None, None, True)
		self.art_widget.show ()
		# Intitates search in the database (which checks art cache, internet etc.)
		self.current_entry = entry
		self.current_pixbuf = None
		self.art_db.get_pixbuf(db, entry, self.on_get_pixbuf_completed)

	def on_get_pixbuf_completed(self, entry, pixbuf, uri):
		# Set the pixbuf for the entry returned from the art db
		if entry != self.current_entry:
			return
		self.current_pixbuf = pixbuf
		self.art_widget.set (entry, pixbuf, uri, False)
		if pixbuf:
			db = self.shell.get_property ("db")
			# This might be from a playing-changed signal, 
			# in which case consumers won't be ready yet.
			def idle_emit_art():
				db.emit_entry_extra_metadata_notify (entry, "rb:coverArt", pixbuf)
				return False
			gobject.idle_add(idle_emit_art)

	def cover_art_request (self, db, entry):
		if entry == self.current_entry:
			return self.current_pixbuf

	def cover_art_notify (self, db, entry, field, metadata):
		if entry != self.current_entry:
			return
		if not isinstance (metadata, gtk.gdk.Pixbuf):
			return
		self.art_db.cancel_get_pixbuf (entry)
		if self.current_pixbuf == metadata:
			return
		self.art_widget.set (entry, metadata, None, False)

