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

from CoverArtDatabase import CoverArtDatabase


FADE_STEPS = 10
FADE_TOTAL_TIME = 1000

class ArtDisplayPlugin (rb.Plugin):
	def __init__ (self):
		rb.Plugin.__init__ (self)
		
	def activate (self, shell):
		self.shell = shell
		sp = shell.get_player ()
		self.pec_id = sp.connect ('playing-song-changed', self.playing_entry_changed)
		self.pc_id = sp.connect ('playing-changed', self.playing_changed)
		self.art_widget = gtk.Image ()
		self.art_widget.set_padding (0, 5)
		shell.add_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.sa_id = self.art_widget.connect ('size-allocate', self.size_allocated)
		self.current_pixbuf = None
		self.art_db = CoverArtDatabase ()
		self.resize_id = 0
		self.resize_in_progress = False
		self.old_width = 0
		self.fade_step = 0
		self.fade_id = 0
		self.current_entry = None
		entry = sp.get_playing_entry ()
		self.playing_entry_changed (sp, entry)
	
	def deactivate (self, shell):
		self.shell = None
		sp = shell.get_player ()
		sp.disconnect (self.pec_id)
		sp.disconnect (self.pc_id)
		shell.remove_widget (self.art_widget, rb.SHELL_UI_LOCATION_SIDEBAR)
		self.art_widget.disconnect(self.sa_id)
		self.art_widget = None
		self.current_pixbuf = None
		self.art_db = None
		self.action = None
		self.action_group = None

		if self.resize_id != 0:
			gobject.source_remove (self.resize_id)
		if self.fade_id != 0:
			gobject.source_remove (self.fade_id)

	def playing_changed (self, sp, playing):
		self.set_entry(sp.get_playing_entry ())

	def playing_entry_changed (self, sp, entry):
		self.set_entry(entry)

	def size_allocated (self, widget, allocation):
		if self.old_width == allocation.width:
			return
		if self.resize_id == 0:
			self.resize_id = gobject.idle_add (self.do_resize)
		self.resize_in_progress = True
		self.old_width = allocation.width

	def do_resize(self):
		if self.resize_in_progress:
			self.resize_in_progress = False
			self.update_displayed_art (True)
			ret = True
		else:
			self.update_displayed_art (False)
			self.resize_id = 0
			ret = False
		return ret

	def set_entry (self, entry):
		if entry != self.current_entry:
			db = self.shell.get_property ("db")

			# Intitates search in the database (which checks art cache, internet etc.)
			self.current_entry = entry
			self.art_db.get_pixbuf(db, entry, self.on_get_pixbuf_completed)

	def on_get_pixbuf_completed(self, entry, pixbuf):
		# Set the pixbuf for the entry returned from the art db
		if entry == self.current_entry:
			self.set_current_art (pixbuf)

	def fade_art (self, old_pixbuf, new_pixbuf):
		self.fade_step += 1.0 / FADE_STEPS

		if self.fade_step <= 0.999:
			# get pixbuf size
			ow = old_pixbuf.get_width ()
			nw = new_pixbuf.get_width ()
			oh = old_pixbuf.get_height ()
			nh = new_pixbuf.get_height ()

			# find scale, widget size and alpha
			ww = self.old_width 
			wh = ww * (self.fade_step * (float(nh)/nw) + (1 - self.fade_step) * (float(oh)/ow))
			sw = float(ww)/nw
			sh = float(wh)/nh
			alpha = int (self.fade_step * 255)

			self.current_pixbuf = old_pixbuf.scale_simple (int(ww), int(wh), gtk.gdk.INTERP_BILINEAR)
			new_pixbuf.composite (self.current_pixbuf, 0, 0, int(ww), int(wh), 0, 0, sw, sh, gtk.gdk.INTERP_BILINEAR, alpha)
			self.art_widget.set_from_pixbuf (self.current_pixbuf)
			self.art_widget.show ()
			return True
		else:
			self.current_pixbuf = new_pixbuf
			self.update_displayed_art (False);
			self.fade_id = 0
			return False

	def set_current_art (self, pixbuf):
		current_pb = self.art_widget.get_pixbuf ()
		if self.fade_id != 0:
			gobject.source_remove (self.fade_id)
			self.fade_id = 0

		visible = (self.art_widget.parent.allocation.width > 1) # don't fade if the user can't see
		if current_pb is not None and pixbuf is not None and visible:
			self.fade_step = 0.0
			self.fade_art (current_pb, pixbuf)
			self.fade_id = gobject.timeout_add ((FADE_TOTAL_TIME / FADE_STEPS), self.fade_art, current_pb, pixbuf)
		else:
			self.current_pixbuf = pixbuf;
			self.update_displayed_art (False);

	def update_displayed_art (self, quick):
		if self.current_pixbuf is None:
			# don't use  Image.clear(), not pygtk 2.6-compatible
			self.art_widget.set_from_pixbuf (None)
			self.art_widget.hide ()
		else:
			width = self.old_width 
			height = self.current_pixbuf.get_height () * width / self.current_pixbuf.get_width ()

			if width > 2 and height > 2:
				if quick:
					mode = gtk.gdk.INTERP_BILINEAR
				else:
					mode = gtk.gdk.INTERP_HYPER
				self.art_widget.set_from_pixbuf (self.current_pixbuf.scale_simple (width, height, mode))
			else:
				self.art_widget.set_from_pixbuf (None)
			self.art_widget.show ()

