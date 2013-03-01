/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2009 Bastien Nocera <hadess@hadess.net>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_PLAYER_GST_HELPER_H
#define __RB_PLAYER_GST_HELPER_H

#include <gst/gst.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include <backends/rb-player.h>
#include <metadata/rb-metadata.h>

G_BEGIN_DECLS

GstElement *	rb_player_gst_try_audio_sink (const char *plugin_name, const char *name);

GstElement *	rb_player_gst_find_element_with_property (GstElement *element, const char *property);

GdkPixbuf *	rb_gst_process_embedded_image 	(const GstTagList *taglist,
						 const char *tag);

gboolean	rb_gst_process_tag_string	(const GstTagList *taglist,
						 const char *tag,
						 RBMetaDataField *field,
						 GValue *value);

int		rb_gst_error_get_error_code	(const GError *error);

/* tee and filter support */

GstElement *	rb_gst_create_filter_bin (void);

gboolean	rb_gst_add_filter (RBPlayer *player, GstElement *filterbin, GstElement *element, gboolean use_pad_block);
gboolean	rb_gst_remove_filter (RBPlayer *player, GstElement *filterbin, GstElement *element, gboolean use_pad_block);

gboolean	rb_gst_add_tee (RBPlayer *player, GstElement *tee, GstElement *element, gboolean use_pad_block);
gboolean	rb_gst_remove_tee (RBPlayer *player, GstElement *tee, GstElement *element, gboolean use_pad_block);

G_END_DECLS

#endif /* __RB_PLAYER_GST_HELPER_H */
