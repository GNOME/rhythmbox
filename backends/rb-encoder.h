/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Interface to audio encoder backend
 *
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grants permission for non-GPL compatible
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
	RB_ENCODER_ERROR_FILE_ACCESS,
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
					 GList *mime_types);
	void		(*cancel)	(RBEncoder *encoder);
	gboolean	(*get_preferred_mimetype)	(RBEncoder *encoder,
							 GList *mime_types,
							 char **mime,
							 char **extension);

	/* signals */
	void (*progress) (RBEncoder *encoder,  double fraction);
	void (*completed) (RBEncoder *encoder, guint64 dest_size);
	void (*error) (RBEncoder *encoder, GError *error);
} RBEncoderIface;

RBEncoder*	rb_encoder_new		(void);
GType rb_encoder_get_type (void);

gboolean	rb_encoder_encode	(RBEncoder *encoder,
					 RhythmDBEntry *entry,
					 const char *dest,
					 GList *mime_types);
void		rb_encoder_cancel	(RBEncoder *encoder);

gboolean	rb_encoder_get_preferred_mimetype (RBEncoder *encoder,
						   GList *mime_types,
						   char **mime,
						   char **extension);

/* only to be used by subclasses */
void	_rb_encoder_emit_progress (RBEncoder *encoder, double fraction);
void	_rb_encoder_emit_completed (RBEncoder *encoder, guint64 dest_size);
void	_rb_encoder_emit_error (RBEncoder *encoder, GError *error);

G_END_DECLS

#endif /* __RB_ENCODER_H__ */
