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

#ifndef __RB_ENCODER_H__
#define __RB_ENCODER_H__

#include <glib-object.h>

#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_ENCODER            (rb_encoder_get_type ())
#define RB_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ENCODER, RBEncoder))
#define RB_IS_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_ENCODER))
#define RB_ENCODER_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_ENCODER, RBEncoderIface))


enum
{
	RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
	RB_ENCODER_ERROR_INTERNAL,
};

#define RB_ENCODER_ERROR rb_encoder_error_quark ()

GQuark rb_encoder_error_quark (void);

typedef struct _RBEncoder RBEncoder;

typedef struct
{
	GTypeInterface g_iface;

	/* vtable */
	gboolean	(*encode)	(RBEncoder *encoder,
					 RhythmDBEntry *entry,
					 const char *dest,
					 const char *mime_type);
	void		(*cancel)	(RBEncoder *encoder);

	/* signals */
	void (*progress) (RBEncoder *encoder,  double fraction);
	void (*completed) (RBEncoder *encoder);
	void (*error) (RBEncoder *encoder, GError *error);
} RBEncoderIface;

RBEncoder*	rb_encoder_new		(void);
GType rb_encoder_get_type (void);

gboolean	rb_encoder_encode	(RBEncoder *encoder,
					 RhythmDBEntry *entry,
					 const char *dest,
					 const char *mime_type);
void		rb_encoder_cancel	(RBEncoder *encoder);

/* obly to be used by subclasses */
void	_rb_encoder_emit_progress (RBEncoder *encoder, double fraction);
void	_rb_encoder_emit_completed (RBEncoder *encoder);
void	_rb_encoder_emit_error (RBEncoder *encoder, GError *error);

G_END_DECLS

#endif /* __RB_ENCODER_H__ */
