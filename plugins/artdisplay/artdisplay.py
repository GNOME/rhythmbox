# -*- Mode: python; coding: utf-8; tab-width: 8; indent-tabs-mode: t; -*-
#
# Copyright (C) 2006 - James Livingston
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

from warnings import warn

import rb
from gi.repository import GObject, Gtk, Gdk, GdkPixbuf, Gio, Peas
from gi.repository import RB

import gettext
gettext.install('rhythmbox', RB.locale_dir())

import urllib

FADE_STEPS = 10
FADE_TOTAL_TIME = 1000
ART_MISSING_ICON = 'rhythmbox-missing-artwork'
WORKING_DELAY = 500
THROBBER_RATE = 10
THROBBER = 'process-working'
ASPECT_RATIO_MIN = 0.9
ASPECT_RATIO_MAX = 1.1

def merge_pixbufs (old_pb, new_pb, reserve_pb, step, width, height, mode=GdkPixbuf.InterpType.BILINEAR):
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

def merge_with_background (pixbuf, bgcolor, pad_if_not_near_square):

	if pixbuf is None:
		return pixbuf
	has_alpha = pixbuf.get_has_alpha ()
	width, height = pixbuf.props.width, pixbuf.props.height
	if pad_if_not_near_square and (height < width * ASPECT_RATIO_MIN or
				       height > width * ASPECT_RATIO_MAX):
		rw, rh = max (width, height), max (width, height)
		left, top = (rw - width) // 2, (rh - height) // 2
	else:
		if not has_alpha:
			return pixbuf
		rw, rh, left, top = width, height, 0, 0
	ret = GdkPixbuf.Pixbuf.new (GdkPixbuf.Colorspace.RGB, False, 8, rw, rh)
	ret.fill ((int(bgcolor.red * 255) << 24) | (int(bgcolor.green * 255) << 16) | (int(bgcolor.blue * 255) << 8) | int(bgcolor.alpha * 255))
	if has_alpha:
		pixbuf.composite (ret, left, top, width, height, left, top, 1.0, 1.0, GdkPixbuf.InterpType.NEAREST, 255)
	else:
		pixbuf.copy_area (0, 0, width, height, ret, left, top)
	return ret

class FadingImage (Gtk.Misc):
	__gsignals__ = {
		'get-max-size' : (GObject.SIGNAL_RUN_LAST, GObject.TYPE_INT, ())
	}
	def __init__ (self, missing_image):
		Gtk.Misc.__init__(self)
		self.sc_id = self.connect('screen-changed', self.screen_changed)
		self.sa_id = self.connect('size-allocate', self.size_allocate_cb)
		self.resize_id, self.fade_id, self.anim_id = 0, 0, 0
		self.missing_image = missing_image
		self.size = 100
		self.screen_changed (self, None)
		self.old_pixbuf, self.new_pixbuf = None, None
		self.merged_pixbuf, self.missing_pixbuf = None, None
		self.fade_step = 0.0
		self.anim, self.anim_frames, self.anim_size = None, None, 0

	def disconnect_handlers (self):
		self.disconnect(self.sc_id)
		self.disconnect(self.sa_id)
		self.icon_theme.disconnect(self.tc_id)
		for id in self.resize_id, self.fade_id, self.anim_id:
			if id != 0:
				GObject.source_remove (id)

	def screen_changed (self, widget, old_screen):
		if old_screen:
			self.icon_theme.disconnect (self.tc_id)
		self.icon_theme = Gtk.IconTheme.get_for_screen (self.get_screen ())
		self.tc_id = self.icon_theme.connect ('changed', self.theme_changed)
		self.theme_changed (self.icon_theme)

	def reload_anim_frames (self):
		icon_info = self.icon_theme.lookup_icon (THROBBER, -1, 0)
		size = icon_info.get_base_size ()
		icon = GdkPixbuf.Pixbuf.new_from_file (icon_info.get_filename ())
		self.anim_frames = [ # along, then down
				icon.new_subpixbuf (x * size, y * size, size, size)
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
				missing_pixbuf = GdkPixbuf.Pixbuf.new_from_file_at_size (self.missing_image, self.size, self.size)
			except Exception, e:
				warn ("Missing artwork icon not found: %s" % e, Warning)
				return

		bgcolor = self.get_style_context().get_background_color(Gtk.StateType.NORMAL)
		self.missing_pixbuf = merge_with_background (missing_pixbuf, bgcolor, False)

	def size_allocate_cb (self, widget, allocation):
		if self.resize_id == 0:
			self.resize_id = GObject.idle_add (self.after_resize)

		max_size = self.emit ('get-max-size')
		self.size = min (self.get_allocated_width (), max_size)

	def after_resize (self):
		self.reload_util_pixbufs ()
		self.merged_pixbuf = None
		self.resize_id = 0
		self.queue_draw ()
		return False

	def do_get_request_mode(self):
		return Gtk.SizeRequestMode.HEIGHT_FOR_WIDTH

	def do_get_preferred_width(self):
		# maybe set minimum width here?
		return (0, 0)

	def do_get_preferred_height_for_width(self, width):
		max_size = self.emit ('get-max-size')
		size = min(self.get_allocated_width(), max_size)
		return (size, size)

	def do_draw (self, cr):
		if not self.ensure_merged_pixbuf ():
			return False

		if self.merged_pixbuf.props.width != self.size:
			draw_pb = self.merged_pixbuf.scale_simple (self.size, self.size, GdkPixbuf.InterpType.NEAREST)
		else:
			draw_pb = self.merged_pixbuf

		# center the image if we're wider than we are tall
		pad = (self.get_allocation().width - self.size) / 2

		left = pad
		right = pad + self.size
		top = 0
		bottom = self.size
		if right > left and bottom > top:
			Gdk.cairo_set_source_pixbuf(cr, draw_pb, pad, 0)
			cr.rectangle(left, top, right - left, bottom - top)
			cr.fill()

		if self.anim:
			x, y, w, h = self.anim_rect ()
			Gdk.cairo_set_source_pixbuf(cr, self.anim, max(0, x), max(0, y))
			cr.rectangle(max(0, x), max(0, y), w, h)
			cr.fill()

		return False

	def anim_rect (self):
		alloc_width = self.get_allocated_width()
		alloc_height = self.get_allocated_height()
		return ((alloc_width - self.anim_size) / 2,
			(alloc_height - self.anim_size) / 2,
			min (self.anim_size, alloc_width),
			min (self.anim_size, alloc_height))

	def ensure_merged_pixbuf (self):
		if self.merged_pixbuf is None:
			alloc_width = self.get_allocated_width()
			alloc_height = self.get_allocated_height()
			self.merged_pixbuf = merge_pixbufs (self.old_pixbuf, self.new_pixbuf, self.missing_pixbuf, self.fade_step, alloc_width, alloc_height)
		return self.merged_pixbuf

	def render_overlay (self):
		ret = self.ensure_merged_pixbuf ()
		if ret and self.anim:
			if ret is self.missing_pixbuf: ret = ret.copy ()
			x, y, w, h = self.anim_rect ()
			self.anim.composite (ret, max (x, 0), max (y, 0), w, h, x, y, 1, 1, GdkPixbuf.InterpType.BILINEAR, 255)
		return ret

	def fade_art (self, first_time):
		self.fade_step += 1.0 / FADE_STEPS
		if self.fade_step > 0.999:
			self.old_pixbuf = None
			self.fade_id = 0
		self.merged_pixbuf = None
		if first_time:
			self.fade_id = GObject.timeout_add ((FADE_TOTAL_TIME / FADE_STEPS), self.fade_art, False)
			return False
		self.queue_resize ()
		return (self.fade_step <= 0.999)

	def animation_advance (self, counter, first_time):
		self.anim = self.anim_frames[counter[0]]
		counter[0] = (counter[0] + 1) % len(self.anim_frames)
		x, y, w, h = self.anim_rect ()
		self.queue_draw_area (max (x, 0), max (y, 0), w, h)
		if first_time:
			self.anim_id = GObject.timeout_add (int (1000 / THROBBER_RATE), self.animation_advance, counter, False)
			return False
		return True

	def set_current_art (self, pixbuf, working):
		if self.props.visible and self.get_allocated_width() > 1:
			self.old_pixbuf = self.render_overlay ()
		else:
			self.old_pixbuf = None	# don't fade

		bgcolor = self.get_style_context().get_background_color(Gtk.StateType.NORMAL)
		self.new_pixbuf = merge_with_background (pixbuf, bgcolor, True)
		self.merged_pixbuf = None
		self.fade_step = 0.0
		self.anim = None
		if self.fade_id != 0:
			GObject.source_remove (self.fade_id)
			self.fade_id = 0
		if self.old_pixbuf is not None:
			self.fade_id = GObject.timeout_add (working and WORKING_DELAY or (FADE_TOTAL_TIME / FADE_STEPS), self.fade_art, working)
		if working and self.anim_id == 0 and self.anim_frames:
			self.anim_id = GObject.timeout_add (WORKING_DELAY, self.animation_advance, [0], True)
		if not working and self.anim_id != 0:
			GObject.source_remove (self.anim_id)
			self.anim_id = 0
		self.queue_resize ()

GObject.type_register (FadingImage)


class ArtDisplayWidget (FadingImage):

	def __init__ (self, missing_image):
		super (ArtDisplayWidget, self).__init__ (missing_image)
		self.set_padding (0, 5)
		self.qt_id = self.connect ('query-tooltip', self.query_tooltip)
		self.props.has_tooltip = True
		self.current_entry, self.working = None, False
		self.current_pixbuf, self.current_uri = None, None

	def disconnect_handlers (self):
 		super (ArtDisplayWidget, self).disconnect_handlers ()

	def query_tooltip (self, widget, x, y, keyboard_mode, tooltip):
		if (self.tooltip_image, self.tooltip_text) != (None, None):
			tooltip.set_text(self.tooltip_text)
			tooltip.set_icon(self.tooltip_image)
			return True
		else:
			return False

	def set (self, entry, pixbuf, uri, tooltip_image, tooltip_text, working):
		self.current_entry = entry
		self.current_pixbuf = pixbuf
		self.current_uri = uri
		self.set_current_art (pixbuf, working)

		self.tooltip_image = None
		if not self.current_entry:
			self.tooltip_text = None
		elif working:
			self.tooltip_text = _("Searching...")
		elif (tooltip_image, tooltip_text) != (None, None):
			self.tooltip_image = tooltip_image
			self.tooltip_text = tooltip_text
		else:
			self.tooltip_text = None


GObject.type_register (ArtDisplayWidget)


class ArtDisplayPlugin (GObject.GObject, Peas.Activatable):
	__gtype_name__ = 'ArtDisplayPlugin'
	object = GObject.property(type=GObject.GObject)

	def __init__ (self):
		GObject.GObject.__init__ (self)

	def do_activate (self):
		shell = self.object
		sp = shell.props.shell_player
		self.player_cb_ids = (
			sp.connect ('playing-song-changed', self.playing_entry_changed),
			sp.connect ('playing-changed', self.playing_changed)
		)
		self.art_store = RB.ExtDB(name="album-art")
		self.art_widget = ArtDisplayWidget (rb.find_plugin_file (self, ART_MISSING_ICON + ".svg"))
		self.art_widget.connect ('get-max-size', self.get_max_art_size)
		self.art_widget.connect ('button-press-event', self.on_button_press)
		self.art_container = Gtk.VBox ()
		self.art_container.pack_start (self.art_widget, True, True, 6)
		shell.add_widget (self.art_container, RB.ShellUILocation.SIDEBAR, False, True)
		self.current_entry, self.current_pixbuf = None, None
		self.playing_entry_changed (sp, sp.get_playing_entry ())

	def do_deactivate (self):

		shell = self.object
		sp = shell.props.shell_player
		for id in self.player_cb_ids:
			sp.disconnect (id)
		self.player_cb_ids = ()

		shell.remove_widget (self.art_container, RB.ShellUILocation.SIDEBAR)
		self.art_widget.disconnect_handlers ()
		self.art_widget = None
		self.art_container = None

	def playing_changed (self, sp, playing):
		self.set_entry(sp.get_playing_entry ())

	def playing_entry_changed (self, sp, entry):
		self.set_entry(entry)

	def set_entry (self, entry):
		if rb.entry_equal(entry, self.current_entry):
			return

		self.art_widget.set (entry, None, None, None, None, True)
		self.art_container.show_all ()
		self.current_entry = entry
		self.current_pixbuf = None

		if entry is not None:
			key = entry.create_ext_db_key (RB.RhythmDBPropType.ALBUM)
			self.art_store.request(key, self.art_store_request_cb, entry)

	def art_store_request_cb(self, key, filename, data, entry):
		if rb.entry_equal(entry, self.current_entry) is False:
			# track changed while we were searching
			return

		if isinstance(data, GdkPixbuf.Pixbuf):
			self.current_pixbuf = data
			uri = "file://" + urllib.pathname2url(filename)
			self.art_widget.set (entry, self.current_pixbuf, uri, None, None, False)
		else:
			self.art_widget.set (entry, None, None, None, None, False)

	def get_max_art_size (self, widget):
		# limit the art image to a third of the window height to prevent it from
		# forcing the window to resize, obscuring everything else, and so on.
		shell = self.object
		(width, height) = shell.props.window.get_size()
		return height / 3

	def on_button_press (self, widget, event):
		# on double clicks, open the cover image (if there is one) in the default
		# image viewer

		doubleclick = Gdk.EventType._2BUTTON_PRESS
		if event.type != doubleclick or event.button != 1:
			return

		if self.art_widget.current_uri is None:
			return

		f = Gio.file_new_for_uri(self.art_widget.current_uri)
		shell = self.object
		Gtk.show_uri(shell.props.window.get_screen(), f.get_uri(), event.time)
