/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
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

#include <config.h>

#include "rb-encoder.h"
#include "rb-encoder-gst.h"

/**
 * SECTION:rbencoder
 * @short_description: audio transcoder interface
 *
 * The RBEncoder interface provides transcoding between audio formats based on
 * encoding profiles.  The encoder implementation operates asynchronously and
 * provides status updates in the form of signals emitted on the main thread.
 * A new encoder instance should be created for each file that is transcoded
 * or copied.
 */

static void rb_encoder_factory_class_init (RBEncoderFactoryClass *klass);
static void rb_encoder_factory_init       (RBEncoderFactory *encoder);

/* Signals */
enum {
	PROGRESS,
	COMPLETED,
	PREPARE_SOURCE,		/* this is on RBEncoderFactory */
	PREPARE_SINK,		/* this is on RBEncoderFactory */
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static RBEncoderFactory *the_encoder_factory = NULL;
static gsize encoder_factory_init = 0;

G_DEFINE_TYPE(RBEncoderFactory, rb_encoder_factory, G_TYPE_OBJECT)

static void
rb_encoder_factory_init (RBEncoderFactory *factory)
{
}

static void
rb_encoder_factory_class_init (RBEncoderFactoryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	/**
	 * RBEncoderFactory::prepare-source:
	 * @factory: the #RBEncoderFactory instance
	 * @uri: the URI for the source
	 * @source: the source object (a GstElement in fact)
	 *
	 * Emitted when creating a source to read the specified URI.
	 * Plugins can use this when just creating a GStreamer element from the URI
	 * isn't enough.  Typically this happens when there's no way to pass device
	 * information through the URI format.
	 */
	signals[PREPARE_SOURCE] =
		g_signal_new ("prepare-source",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderFactoryClass, prepare_source),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_OBJECT);
	/**
	 * RBEncoderFactory::prepare-sink:
	 * @factory: the #RBEncoderFactory instance
	 * @uri: the URI for the sink
	 * @sink: the sink object (a GstElement in fact)
	 *
	 * Emitted when creating a sink to write to the specified URI.
	 * Plugins can use this when just creating a GStreamer element from the URI
	 * isn't enough.  Typically this happens when there's no way to pass device
	 * information through the URI format.
	 */
	signals[PREPARE_SINK] =
		g_signal_new ("prepare-sink",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderFactoryClass, prepare_sink),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, G_TYPE_OBJECT);
}

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
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_DOUBLE);
	/**
	 * RBEncoder::completed:
	 * @encoder: the #RBEncoder instance
	 * @dest_uri: destination URI
	 * @dest_size: size of the output file
	 * @mediatype: output media type
	 * @error: encoding error, or NULL if successful
	 * 
	 * Emitted when the encoding process is complete, or when a fatal error
	 * has occurred.  The destination file, if one exists,  will be closed
	 * and flushed to disk before this signal is emitted.
	 */
	signals[COMPLETED] =
		g_signal_new ("completed",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEncoderIface, completed),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      4, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_POINTER);
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
 * rb_encoder_factory_get:
 *
 * Returns the #RBEncoderFactory instance.
 *
 * Return value: (transfer none): the #RBEncoderFactory
 */
RBEncoderFactory *
rb_encoder_factory_get (void)
{
	if (g_once_init_enter (&encoder_factory_init)) {
		the_encoder_factory = g_object_new (RB_TYPE_ENCODER_FACTORY, NULL);
		g_once_init_leave (&encoder_factory_init, 1);
	}

	return the_encoder_factory;
}

/**
 * rb_encoder_encode:
 * @encoder: the #RBEncoder
 * @entry: the #RhythmDBEntry to transcode
 * @dest: destination file URI
 * @overwrite: if %TRUE, overwrite @dest if it already exists
 * @profile: encoding profile to use, or NULL to just copy
 *
 * Initiates encoding, transcoding to the specified profile if specified.
 *
 * Encoding and error reporting takes place asynchronously.  The caller should wait
 * for the 'completed' signal which indicates it has either completed or failed.
 */
void
rb_encoder_encode (RBEncoder *encoder,
		   RhythmDBEntry *entry,
		   const char *dest,
		   gboolean overwrite,
		   GstEncodingProfile *profile)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	iface->encode (encoder, entry, dest, overwrite, profile);
}

/**
 * rb_encoder_cancel:
 * @encoder: a #RBEncoder
 *
 * Attempts to cancel any in progress encoding.  The encoder should
 * delete the destination file, if it created one, and emit the
 * 'completed' signal.
 */
void
rb_encoder_cancel (RBEncoder *encoder)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);

	iface->cancel (encoder);
}

/**
 * rb_encoder_get_missing_plugins:
 * @encoder: a #RBEncoder
 * @profile: an encoding profile
 * @details: (out callee-allocates): returns plugin installer detail strings
 * @descriptions: (out callee-allocates): returns plugin descriptions
 *
 * Retrieves the plugin installer detail strings and descriptions
 * for any missing plugins required to use the specified encoding profile.
 *
 * Return value: %TRUE if some detail strings are returned, %FALSE otherwise
 */
gboolean
rb_encoder_get_missing_plugins (RBEncoder *encoder,
				GstEncodingProfile *profile,
				char ***details,
				char ***descriptions)
{
	RBEncoderIface *iface = RB_ENCODER_GET_IFACE (encoder);
	return iface->get_missing_plugins (encoder, profile, details, descriptions);
}

/**
 * rb_encoder_new:
 *
 * Creates a new #RBEncoder instance.
 *
 * Return value: (transfer full): the new #RBEncoder
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
_rb_encoder_emit_completed (RBEncoder *encoder, const char *dest_uri, guint64 dest_size, const char *mediatype, GError *error)
{
	g_signal_emit (encoder, signals[COMPLETED], 0, dest_uri, dest_size, mediatype, error);
}

void
_rb_encoder_emit_prepare_source (RBEncoder *encoder, const char *uri, GObject *source)
{
	g_signal_emit (rb_encoder_factory_get (), signals[PREPARE_SOURCE], 0, uri, source);
}

void
_rb_encoder_emit_prepare_sink (RBEncoder *encoder, const char *uri, GObject *sink)
{
	g_signal_emit (rb_encoder_factory_get (), signals[PREPARE_SINK], 0, uri, sink);
}

GQuark
rb_encoder_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_encoder_error");

	return quark;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_encoder_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_ENCODER_ERROR_FORMAT_UNSUPPORTED, "format-unsupported"),
			ENUM_ENTRY (RB_ENCODER_ERROR_INTERNAL, "internal-error"),
			ENUM_ENTRY (RB_ENCODER_ERROR_FILE_ACCESS, "file-access-error"),
			ENUM_ENTRY (RB_ENCODER_ERROR_OUT_OF_SPACE, "out-of-space"),
			ENUM_ENTRY (RB_ENCODER_ERROR_DEST_READ_ONLY, "destination-read-only"),
			ENUM_ENTRY (RB_ENCODER_ERROR_DEST_EXISTS, "destination-exists"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBEncoderError", values);
	}

	return etype;
}
