/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Interface to audio encoder backend  
 *
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_ENCODER_GST_H__
#define __RB_ENCODER_GST_H__

#include <glib-object.h>

#include "rb-encoder.h"

G_BEGIN_DECLS


#define RB_TYPE_ENCODER_GST            (rb_encoder_gst_get_type ())
#define RB_ENCODER_GST(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ENCODER, RBEncoderGst))
#define RB_IS_ENCODER_GST(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_ENCODER))

typedef struct _RBEncoderGstPrivate RBEncoderGstPrivate;

typedef struct
{
	GObjectClass obj_class;
} RBEncoderGstClass;

typedef struct
{
	GObject obj;
	RBEncoderGstPrivate *priv;
} RBEncoderGst;

RBEncoder*	rb_encoder_gst_new		(void);
GType rb_encoder_gst_get_type (void);


G_END_DECLS

#endif /* __RB_ENCODER_GST_H__ */
