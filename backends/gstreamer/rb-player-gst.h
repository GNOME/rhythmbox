/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __RB_PLAYER_GST_H__
#define __RB_PLAYER_GST_H__

#include <glib-object.h>
#include <gst/gst.h>

#include "rb-player.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYER_GST            (rb_player_gst_get_type ())
#define RB_PLAYER_GST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PLAYER, RBPlayerGst))
#define RB_IS_PLAYER_GST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PLAYER))

typedef struct _RBPlayerGstPrivate RBPlayerGstPrivate;
typedef struct _RBPlayerGst RBPlayerGst;
typedef struct _RBPlayerGstClass RBPlayerGstClass;

struct _RBPlayerGstClass
{
	GObjectClass obj_class;

	void (*prepare_source) (RBPlayerGst *player,
				const char *stream_uri,
				GstElement *source);
	gboolean (*can_reuse_stream) (RBPlayerGst *player,
				      const char *new_uri,
				      const char *stream_uri,
				      GstElement *source);
	void (*reuse_stream) (RBPlayerGst *player,
			      const char *new_uri,
			      const char *stream_uri,
			      GstElement *source);
};

struct _RBPlayerGst
{
	GObject obj;
	RBPlayerGstPrivate *priv;
};

RBPlayer*	rb_player_gst_new (GError **error);
GType		rb_player_gst_get_type (void);

G_END_DECLS

#endif /* __RB_PLAYER_GST_H__ */
