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

#ifndef __RB_PLAYER_GST_H__
#define __RB_PLAYER_GST_H__

#include <glib-object.h>

#include "rb-player.h"

G_BEGIN_DECLS

#define RB_TYPE_PLAYER_GST            (rb_player_gst_get_type ())
#define RB_PLAYER_GST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_PLAYER, RBPlayerGst))
#define RB_IS_PLAYERER_GST(obj)       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_PLAYER))

typedef struct _RBPlayerGstPrivate RBPlayerGstPrivate;

typedef struct
{
	GObjectClass obj_class;
} RBPlayerGstClass;

typedef struct
{
	GObject obj;
	RBPlayerGstPrivate *priv;
} RBPlayerGst;

RBPlayer*	rb_player_gst_new (GError **error);
GType		rb_player_gst_get_type (void);

G_END_DECLS

#endif /* __RB_PLAYER_GST_H__ */
