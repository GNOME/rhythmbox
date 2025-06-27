/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2024 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef __RB_PLAYER_GST_MULTI_H__
#define __RB_PLAYER_GST_MULTI_H__

#include <glib-object.h>
#include <gst/gst.h>

#include "rb-player.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYER_GST_MULTI     (rb_player_gst_multi_get_type ())
#define RB_PLAYER_GST_MULTI(obj)     (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PLAYER_GST_MULTI, RBPlayerGstMulti))
#define RB_IS_PLAYER_GST_MULTI(obj)  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PLAYER_GST_MULTI))

typedef struct _RBPlayerGstMultiPrivate RBPlayerGstMultiPrivate;
typedef struct _RBPlayerGstMulti RBPlayerGstMulti;
typedef struct _RBPlayerGstMultiClass RBPlayerGstMultiClass;

struct _RBPlayerGstMultiClass
{
	GObjectClass obj_class;

	void (*prepare_source) (RBPlayerGstMulti *player,
				const char *stream_uri,
				GstElement *source);
	gboolean (*can_reuse_stream) (RBPlayerGstMulti *player,
				      const char *new_uri,
				      const char *stream_uri,
				      GstElement *source);
	void (*reuse_stream) (RBPlayerGstMulti *player,
			      const char *new_uri,
			      const char *stream_uri,
			      GstElement *source);
};

struct _RBPlayerGstMulti
{
	GObject obj;
	RBPlayerGstMultiPrivate *priv;
};

RBPlayer*	rb_player_gst_multi_new (GError **error);
GType		rb_player_gst_multi_get_type (void);

G_END_DECLS

#endif /* __RB_PLAYER_GST_MULTI_H__ */
