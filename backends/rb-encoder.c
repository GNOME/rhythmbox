/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <jrl@ids.org.au>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <config.h>

#include "rb-encoder.h"
#include "rb-encoder-gst.h"

/* Signals */
enum {
	PROGRESS,
	COMPLETED,
	ERROR,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rb_encoder_interface_init (RBEncoderIface *iface)
{
	signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, progress),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__DOUBLE,
			      G_TYPE_NONE,
			      1, G_TYPE_DOUBLE);
	signals[COMPLETED] =
		g_signal_new ("completed",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, completed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	signals[ERROR] =
		g_signal_new ("error",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, error),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);

}

GType
rb_encoder_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RBEncoderIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc)rb_encoder_interface_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RBEncoder", &our_info, 0);
	}

	return our_type;
}

gboolean
rb_encoder_encode (RBEncoder *encoder,
		   RhythmDBEntry *entry,
		   const char *dest,
		   const char *mime_type)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	return iface->encode (encoder, entry, dest, mime_type);
}

void
rb_encoder_cancel (RBEncoder *encoder)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	iface->cancel (encoder);
}

RBEncoder*
rb_encoder_new (void)
{
	return rb_encoder_gst_new ();
}

void
_rb_encoder_emit_progress (RBEncoder *encoder, double fraction)
{
	g_signal_emit (encoder, signals[PROGRESS], 0, fraction);
}

void
_rb_encoder_emit_completed (RBEncoder *encoder)
{
	g_signal_emit (encoder, signals[COMPLETED], 0);
}

void
_rb_encoder_emit_error (RBEncoder *encoder, GError *error)
{
	g_signal_emit (encoder, signals[ERROR], 0, error);
}

GQuark
rb_encoder_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_encoder_error");

	return quark;
}
