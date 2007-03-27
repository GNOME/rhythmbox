/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2006 James Livingston <jrl@ids.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __RB_PLAYER_GST_XFADE_H__
#define __RB_PLAYER_GST_XFADE_H__

#include <glib-object.h>

#include <gst/gst.h>

#include "rb-player.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYER_GST_XFADE      (rb_player_gst_xfade_get_type ())
#define RB_PLAYER_GST_XFADE(obj)      (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PLAYER_GST_XFADE, RBPlayerGstXFade))
#define RB_IS_PLAYER_GST_XFADE(obj)   (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PLAYER_GST_XFADE))

typedef struct _RBPlayerGstXFadePrivate RBPlayerGstXFadePrivate;

typedef struct
{
	GObject obj;
	RBPlayerGstXFadePrivate *priv;
} RBPlayerGstXFade;

typedef struct
{
	GObjectClass obj_class;

	gboolean (*can_reuse_stream) (RBPlayerGstXFade *player,
				      const char *new_uri,
				      const char *stream_uri,
				      GstElement *stream_bin);
	void (*reuse_stream) (RBPlayerGstXFade *player,
			      const char *new_uri,
			      const char *stream_uri,
			      GstElement *stream_bin);
} RBPlayerGstXFadeClass;

RBPlayer*	rb_player_gst_xfade_new (GError **error);
GType		rb_player_gst_xfade_get_type (void);

G_END_DECLS

#endif /* __RB_PLAYER_GST_XFADE_H__ */
