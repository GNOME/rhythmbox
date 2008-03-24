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

/**
 * SECTION:rb-encoder
 * @short_description: audio transcoder interface
 *
 * The RBEncoder interface provides transcoding between audio formats based on
 * MIME types.
 *
 * The encoder picks the output format from a list provided by the caller,
 * limited by the available codecs.  It operatees asynchronously and provides
 * status updates in the form of signals.
 *
 * A new encoder instance should be created for each file that is transcoded.
 */

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
	/**
	 * RBEncoder::progress:
	 * @encoder: the #RBEncoder instance
	 * @fraction: progress as a fraction (0..1)
	 *
	 * Emitted regularly during the encoding process to provide progress updates.
	 */
	signals[PROGRESS] =
		g_signal_new ("progress",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, progress),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__DOUBLE,
			      G_TYPE_NONE,
			      1, G_TYPE_DOUBLE);
	/**
	 * RBEncoder::completed:
	 * @encoder: the #RBEncoder instance
	 * 
	 * Emitted when the encoding process is complete.  The destination file
	 * will be closed and flushed to disk when this occurs.
	 */
	signals[COMPLETED] =
		g_signal_new ("completed",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, completed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBEncoder::error:
	 * @encoder: the #RBEncoder instance
	 * @error: a #GError describing the error
	 *
	 * Emitted when an error occurs during encoding.
	 */
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

/**
 * rb_encoder_encode:
 * @encoder: the #RBEncoder
 * @entry: the #RhythmDBEntry to transcode
 * @dest: destination file URI
 * @mime_types: a #GList of target MIME types in order of preference
 *
 * Initiates encoding.  A target MIME type will be selected from the list
 * given.  If the source format is in the list, that will be chosen regardless
 * of order.  Otherwise, the first type in the list that the encoder can produce
 * will be selected.
 *
 * Encoding takes places asynchronously.  If the return value is TRUE, the caller
 * should wait for a 'completed' or 'error' signal to indicate that it has finished.
 *
 * Return value: TRUE if encoding has started
 */
gboolean
rb_encoder_encode (RBEncoder *encoder,
		   RhythmDBEntry *entry,
		   const char *dest,
		   GList *mime_types)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	return iface->encode (encoder, entry, dest, mime_types);
}

/**
 * rb_encoder_cancel:
 * @encoder: a #RBEncoder
 *
 * Attempts to cancel any in progress encoding.  The encoder should
 * delete the destination file, if it created one.
 */
void
rb_encoder_cancel (RBEncoder *encoder)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	iface->cancel (encoder);
}

/**
 * rb_encoder_get_preferred_mimetype:
 * @encoder: a #RBEncoder
 * @mime_types: a #GList of MIME type strings in order of preference
 * @mime: returns the selected MIME type, if any
 * @extension: returns the file extension associated with the selected MIME type, if any
 *
 * Identifies the first MIME type in the list that the encoder can actually encode to.
 * The file extension (eg. '.mp3' for audio/mpeg) associated with the selected type is
 * also returned.
 *
 * Return value: TRUE if a format was identified
 */
gboolean
rb_encoder_get_preferred_mimetype (RBEncoder *encoder,
				   GList *mime_types,
				   char **mime,
				   char **extension)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	return iface->get_preferred_mimetype (encoder, mime_types, mime, extension);
}

/**
 * rb_encoder_new:
 *
 * Creates a new #RBEncoder instance.
 *
 * Return value: the new #RBEncoder
 */
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

