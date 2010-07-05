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

import rhythmdb, rb
import gtk, gobject
import gio
from warnings import warn

from CoverArtDatabase import CoverArtDatabase


FADE_STEPS = 10
FADE_TOTAL_TIME = 1000
ART_MISSING_ICON = 'rhythmbox-missing-artwork'
WORKING_DELAY = 500
THROBBER_RATE = 10
THROBBER = 'process-working'
ASPECT_RATIO_MIN = 0.9
ASPECT_RATIO_MAX = 1.1

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
	ret = gtk.gdk.Pixbuf (gtk.gdk.COLORSPACE_RGB, False, 8, rw, rh)
	ret.fill (((bgcolor.red & 0xff00) << 16) | ((bgcolor.green & 0xff00) << 8) | (bgcolor.blue & 0xff00) | 0xff)
	if has_alpha:
		pixbuf.composite (ret, left, top, width, height, left, top, 1.0, 1.0, gtk.gdk.INTERP_NEAREST, 255)
	else:
		pixbuf.copy_area (0, 0, width, height, ret, left, top)
	return ret

class FadingImage (gtk.Misc):
	__gsignals__ = {
		'size-allocate': 'override',
		'get-max-size' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_INT, ())
	}
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
		self.missing_pixbuf = merge_with_background (missing_pixbuf, self.style.bg[gtk.STATE_NORMAL], False)

	def do_size_allocate (self, allocation):
		old_width = self.allocation.width
		self.allocation = allocation

		if self.resize_id == 0:
			self.resize_id = gobject.idle_add (self.after_resize)

		if old_width != allocation.width:
			max_size = self.emit ('get-max-size')
			self.size = min (allocation.width, max_size)
			self.queue_resize ()
		
		elif self.window is not None:
			self.window.move_resize (allocation.x, allocation.y, allocation.width, allocation.height)
			self.queue_draw ()
			self.window.process_updates (True)

	def after_resize (self):
		self.reload_util_pixbufs ()
		self.merged_pixbuf = None
		self.resize_id = 0
		self.queue_draw ()
		return False

	def size_request (self, widget, requisition):
		requisition.width, requisition.height = -1, self.size

	def expose (self, widget, event):
		if not self.ensure_merged_pixbuf ():
			return False

		if self.merged_pixbuf.props.width != self.size:
			draw_pb = self.merged_pixbuf.scale_simple (self.size, self.size, gtk.gdk.INTERP_NEAREST)
		else:
			draw_pb = self.merged_pixbuf

		# center the image if we're wider than we are tall
		x, y, w, h = event.area
		pad = (self.allocation.width - self.size) / 2

		left = max (x, pad)
		right = min (x + w, pad + self.size)
		top = y
		bottom = min (y + h, self.size)
		if right > left and bottom > top:
			event.window.draw_pixbuf (None, draw_pb, left-pad, top, left, top, right - left, bottom - top)

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
		self.new_pixbuf = merge_with_background (pixbuf, self.style.bg[gtk.STATE_NORMAL], True)
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
	__gsignals__ = {
			'pixbuf-dropped' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, (rhythmdb.Entry, gtk.gdk.Pixbuf)),
			'uri-dropped' : (gobject.SIGNAL_RUN_LAST, gobject.TYPE_NONE, (rhythmdb.Entry, gobject.TYPE_STRING))
			}

	def __init__ (self, missing_image):
		super (ArtDisplayWidget, self).__init__ (missing_image)
		self.set_padding (0, 5)
		self.ddg_id = self.connect ('drag-data-get', self.drag_data_get)
		self.ddr_id = self.connect ('drag-data-received', self.drag_data_received)
		self.qt_id = self.connect ('query-tooltip', self.query_tooltip)
		self.props.has_tooltip = True
		self.current_entry, self.working = None, False
		self.current_pixbuf, self.current_uri = None, None

	def disconnect_handlers (self):
 		super (ArtDisplayWidget, self).disconnect_handlers ()
		self.disconnect (self.ddg_id)
		self.disconnect (self.ddr_id)
	
	def update_dnd_targets (self):
		targets = None
		if self.current_entry:
			targets = gtk.target_list_add_image_targets (targets)
			targets = gtk.target_list_add_uri_targets (targets)
			targets = gtk.target_list_add_text_targets (targets)
		if targets:
			self.drag_dest_set (gtk.DEST_DEFAULT_ALL, targets, gtk.gdk.ACTION_COPY)
		else:
			self.drag_dest_unset ()

		targets = None
		if self.current_pixbuf:
			targets = gtk.target_list_add_image_targets (targets, writable=True)
		if self.current_uri:
			targets = gtk.target_list_add_uri_targets (targets)
		if targets:
			self.drag_source_set (gtk.gdk.BUTTON1_MASK, targets, gtk.gdk.ACTION_COPY)
		else:
			self.drag_source_unset ()

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
		self.update_dnd_targets ()

		self.tooltip_image = None
		if not self.current_entry:
			self.tooltip_text = None
		elif working:
			self.tooltip_text = _("Searching... drop artwork here")
		elif (tooltip_image, tooltip_text) != (None, None):
			self.tooltip_image = tooltip_image
			self.tooltip_text = tooltip_text
		else:
			self.tooltip_text = _("Drop artwork here")


	def drag_data_get (self, widget, drag_context, selection_data, info, timestamp):
		if self.current_pixbuf:
			selection_data.set_pixbuf (self.current_pixbuf)
		if self.current_uri:
			selection_data.set_uris ([self.current_uri])

	def drag_data_received (self, widget, drag_context, x, y, selection_data, info, timestamp):
		entry = self.current_entry
		pixbuf = selection_data.get_pixbuf ()
		uris = selection_data.get_uris ()
		text = selection_data.get_text ()
		if pixbuf:
			self.emit ('pixbuf-dropped', entry, pixbuf)
		elif uris:
			self.emit ('uri-dropped', entry, uris[0])
		elif text:
			self.emit ('uri-dropped', entry, text)
gobject.type_register (ArtDisplayWidget)


class ArtDisplayPlugin (rb.Plugin):
	def __init__ (self):
		rb.Plugin.__init__ (self)

	def activate (self, shell):
		self.shell = shell
		sp = shell.get_player ()
		self.player_cb_ids = (
			sp.connect ('playing-song-changed', self.playing_entry_changed),
			sp.connect ('playing-changed', self.playing_changed)
		)
		self.emitting_uri_notify = False
		db = shell.get_property ("db")
		self.db_cb_ids = (
			db.connect_after ('entry-extra-metadata-request::rb:coverArt', self.cover_art_request),
			db.connect_after ('entry-extra-metadata-notify::rb:coverArt', self.cover_art_notify),
			db.connect_after ('entry-extra-metadata-request::rb:coverArt-uri', self.cover_art_uri_request),
			db.connect_after ('entry-extra-metadata-notify::rb:coverArt-uri', self.cover_art_uri_notify),
			db.connect_after ('entry-extra-metadata-gather', self.cover_art_uri_gather),
		)
		self.art_widget = ArtDisplayWidget (self.find_file (ART_MISSING_ICON + ".svg"))
		self.art_widget.connect ('pixbuf-dropped', self.on_set_pixbuf)
		self.art_widget.connect ('uri-dropped', self.on_set_uri)
		self.art_widget.connect ('get-max-size', self.get_max_art_size)
		self.art_widget.connect ('button-press-event', self.on_button_press)
		self.art_container = gtk.VBox ()
		self.art_container.pack_start (self.art_widget, padding=6)
		shell.add_widget (self.art_container, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_db = CoverArtDatabase ()
		self.current_entry, self.current_pixbuf = None, None
		self.playing_entry_changed (sp, sp.get_playing_entry ())

	def deactivate (self, shell):
		self.shell = None

		sp = shell.get_player ()
		for id in self.player_cb_ids:
			sp.disconnect (id)
		self.player_cb_ids = ()

		db = shell.get_property ("db")
		for id in self.db_cb_ids:
			db.disconnect (id)
		self.db_cb_ids = ()

		shell.remove_widget (self.art_container, rb.SHELL_UI_LOCATION_SIDEBAR)
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

		self.art_widget.set (entry, None, None, None, None, True)
		self.art_container.show_all ()
		# Intitates search in the database (which checks art cache, internet etc.)
		self.current_entry = entry
		self.current_pixbuf = None
		self.art_db.get_pixbuf(db, entry, True, self.on_get_pixbuf_completed)

	def on_get_pixbuf_completed(self, entry, pixbuf, uri, tooltip_image, tooltip_text):
		# Set the pixbuf for the entry returned from the art db
		if entry == self.current_entry:
			self.current_pixbuf = pixbuf

			if tooltip_image is None:
				pb = None
			elif tooltip_image.startswith("/"):
				pb = gtk.gdk.pixbuf_new_from_file(tooltip_image)
			else:
				f = self.find_file(tooltip_image)
				pb = gtk.gdk.pixbuf_new_from_file(f)
			self.art_widget.set (entry, pixbuf, uri, pb, tooltip_text, False)

		if pixbuf:
			db = self.shell.get_property ("db")
			# This might be from a playing-changed signal,
			# in which case consumers won't be ready yet.
			def idle_emit_art():
				db.emit_entry_extra_metadata_notify (entry, "rb:coverArt", pixbuf)
				if uri:
					self.emitting_uri_notify = True
					db.emit_entry_extra_metadata_notify (entry, "rb:coverArt-uri", uri)
					self.emitting_uri_notify = False
				return False
			gobject.idle_add(idle_emit_art)

	def cover_art_request (self, db, entry):
		a = [None]
		def callback(entry, pixbuf, uri, tooltip_image, tooltip_text):
			a[0] = pixbuf
			self.on_get_pixbuf_completed(entry, pixbuf, uri, tooltip_image, tooltip_text)

		playing = (entry == self.current_entry)
		self.art_db.get_pixbuf(db, entry, playing, callback)

		# If callback was called synchronously we can return a pixmap
		return a[0]

	def cover_art_notify (self, db, entry, field, metadata):
		if entry != self.current_entry:
			return
		if not isinstance (metadata, gtk.gdk.Pixbuf):
			return
		self.art_db.cancel_get_pixbuf (entry)
		if self.current_pixbuf == metadata:
			return

		# cache the pixbuf so we can provide a url
		uri = self.art_db.cache_pixbuf (db, entry, metadata)
		self.art_widget.set (entry, metadata, uri, None, None, False)
		self.emitting_uri_notify = True
		db.emit_entry_extra_metadata_notify (entry, "rb:coverArt-uri", uri)
		self.emitting_uri_notify = False

	def cover_art_uri_notify (self, db, entry, field, metadata):
		if entry != self.current_entry:
			return

		if self.emitting_uri_notify:
			return

		if not metadata:
			print "got no-cover-art notification"
			self.art_widget.set (entry, None, None, False)
			db.emit_entry_extra_metadata_notify (entry, "rb:coverArt", None)
			return

		uri = str (metadata)
		def loader_cb (data):
			if data and len (data) >= 1000:
				pbl = gtk.gdk.PixbufLoader ()
				try:
					if pbl.write (data) and pbl.close ():
						pixbuf = pbl.get_pixbuf ()
						if pixbuf:
							self.art_db.cancel_get_pixbuf (entry)
							self.on_get_pixbuf_completed (entry, pixbuf, uri, None, None)
				except GError:
					pass

		print "got cover art URI notification: %s" % (uri)
		l = rb.Loader()
		l.get_url (uri, loader_cb)

	def cover_art_uri_request (self, db, entry):
		if entry == self.current_entry:
			return self.art_widget.current_uri

	def cover_art_uri_gather (self, db, entry, metadata):
		if entry == self.current_entry and self.art_widget.current_uri:
			metadata ['rb:coverArt-uri'] = self.art_widget.current_uri

	def on_set_pixbuf (self, widget, entry, pixbuf):
		db = self.shell.get_property ("db")
		self.art_db.set_pixbuf (db, entry, pixbuf, self.on_get_pixbuf_completed)

	def on_set_uri (self, widget, entry, uri):
		db = self.shell.get_property ("db")
		self.art_db.set_pixbuf_from_uri (db, entry, uri, self.on_get_pixbuf_completed)

	def get_max_art_size (self, widget):
		# limit the art image to a third of the window height to prevent it from
		# forcing the window to resize, obscuring everything else, and so on.
		(width, height) = self.shell.props.window.get_size()
		return height / 3

	def on_button_press (self, widget, event):
		# on double clicks, open the cover image (if there is one) in the default
		# image viewer
		if event.type != gtk.gdk._2BUTTON_PRESS or event.button != 1:
			return

		if self.art_widget.current_uri is None:
			return

		f = gio.File(self.art_widget.current_uri)
		gtk.show_uri(self.shell.props.window.get_screen(), f.get_uri(), event.time)
