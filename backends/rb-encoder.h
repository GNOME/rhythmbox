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
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_ENCODER_H__
#define __RB_ENCODER_H__

#include <glib-object.h>

#include <gst/pbutils/encoding-profile.h>

#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RB_TYPE_ENCODER            (rb_encoder_get_type ())
#define RB_ENCODER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ENCODER, RBEncoder))
#define RB_IS_ENCODER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_ENCODER))
#define RB_ENCODER_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_ENCODER, RBEncoderIface))

#define RB_TYPE_ENCODER_FACTORY        (rb_encoder_factory_get_type ())
#define RB_ENCODER_FACTORY(obj)        (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_ENCODER, RBEncoderFactory))
#define RB_ENCODER_FACTORY_CLASS(k)    (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_ENCODER_FACTORY, RBEncoderFactoryClass))
#define RB_IS_ENCODER_FACTORY(obj)     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_ENCODER))
#define RB_IS_ENCODER_FACTORY_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_ENCODER_FACTORY))
#define RB_ENCODER_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_ENCODER_FACTORY, RBEncoderFactoryClass))

typedef enum
{
	RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
	RB_ENCODER_ERROR_INTERNAL,
	RB_ENCODER_ERROR_FILE_ACCESS,
	RB_ENCODER_ERROR_OUT_OF_SPACE,
	RB_ENCODER_ERROR_DEST_READ_ONLY,
	RB_ENCODER_ERROR_DEST_EXISTS
} RBEncoderError;

#define RB_ENCODER_DEST_TEMPFILE	"x-rb-tmp://"

GType rb_encoder_error_get_type (void);
#define RB_TYPE_ENCODER_ERROR	(rb_encoder_error_get_type())
#define RB_ENCODER_ERROR rb_encoder_error_quark ()
GQuark rb_encoder_error_quark (void);

typedef struct _RBEncoder RBEncoder;
typedef struct _RBEncoderIface RBEncoderIface;
typedef struct _RBEncoderFactory RBEncoderFactory;
typedef struct _RBEncoderFactoryClass RBEncoderFactoryClass;

struct _RBEncoderIface
{
	GTypeInterface g_iface;

	/* vtable */
	void		(*encode)	(RBEncoder *encoder,
					 RhythmDBEntry *entry,
					 const char *dest,
					 gboolean overwrite,
					 GstEncodingProfile *profile);
	void		(*cancel)	(RBEncoder *encoder);
	gboolean	(*get_missing_plugins) (RBEncoder *encoder,
					 GstEncodingProfile *profile,
					 char ***details,
					 char ***descriptions);

	/* signals */
	void (*progress) (RBEncoder *encoder,  double fraction);
	void (*completed) (RBEncoder *encoder, guint64 dest_size, const char *mediatype, GError *error);
};

struct _RBEncoderFactoryClass
{
	GObjectClass obj_class;

	/* signals */
	void (*prepare_source) (RBEncoderFactory *factory, const char *uri, GObject *source);
	void (*prepare_sink) (RBEncoderFactory *factory, const char *uri, GObject *sink);
};

struct _RBEncoderFactory
{
	GObject obj;
};

GType 		rb_encoder_factory_get_type (void);
RBEncoderFactory *rb_encoder_factory_get (void);


RBEncoder*	rb_encoder_new		(void);
GType 		rb_encoder_get_type 	(void);

void		rb_encoder_encode	(RBEncoder *encoder,
					 RhythmDBEntry *entry,
					 const char *dest,
					 gboolean overwrite,
					 GstEncodingProfile *profile);
void		rb_encoder_cancel	(RBEncoder *encoder);

gboolean	rb_encoder_get_missing_plugins (RBEncoder *encoder,
					 GstEncodingProfile *profile,
					 char ***details,
					 char ***descriptions);

/* only to be used by subclasses */
void	_rb_encoder_emit_progress (RBEncoder *encoder, double fraction);
void	_rb_encoder_emit_completed (RBEncoder *encoder, const char *uri, guint64 dest_size, const char *mediatype, GError *error);

void	_rb_encoder_emit_prepare_source (RBEncoder *encoder, const char *uri, GObject *source);
void	_rb_encoder_emit_prepare_sink (RBEncoder *encoder, const char *uri, GObject *sink);

G_END_DECLS

#endif /* __RB_ENCODER_H__ */
