/*
 *  arch-tag: Interface to metadata reading/writing
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#ifndef __RB_METADATA_H
#define __RB_METADATA_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_METADATA_NUM_FIELDS 23

/* This should correspond to the stuff in gsttag.h */
typedef enum
{
	RB_METADATA_FIELD_TITLE,                   /* string */
	RB_METADATA_FIELD_ARTIST,                  /* string */
	RB_METADATA_FIELD_ALBUM,                   /* string */
	RB_METADATA_FIELD_DATE,                    /* string */
	RB_METADATA_FIELD_GENRE,                   /* string */
	RB_METADATA_FIELD_COMMENT,                 /* string */
	RB_METADATA_FIELD_TRACK_NUMBER,            /* int */
	RB_METADATA_FIELD_MAX_TRACK_NUMBER,        /* int */
	RB_METADATA_FIELD_DISC_NUMBER,             /* int */
	RB_METADATA_FIELD_MAX_DISC_NUMBER,         /* int */
	RB_METADATA_FIELD_DESCRIPTION,             /* string */
	RB_METADATA_FIELD_VERSION,                 /* string */
	RB_METADATA_FIELD_ISRC,                    /* string */
	RB_METADATA_FIELD_ORGANIZATION,            /* string */
	RB_METADATA_FIELD_COPYRIGHT,               /* string */
	RB_METADATA_FIELD_CONTACT,                 /* string */
	RB_METADATA_FIELD_LICENSE,                 /* string */
	RB_METADATA_FIELD_PERFORMER,               /* string */
	RB_METADATA_FIELD_DURATION,                /* long */
	RB_METADATA_FIELD_CODEC,		   /* string */
	RB_METADATA_FIELD_BITRATE,                 /* int */
	RB_METADATA_FIELD_TRACK_GAIN,		   /* double */
	RB_METADATA_FIELD_TRACK_PEAK,		   /* double */
	RB_METADATA_FIELD_ALBUM_GAIN,		   /* double */
	RB_METADATA_FIELD_ALBUM_PEAK,		   /* double */
} RBMetaDataField;

typedef enum
{
	RB_METADATA_ERROR_GNOMEVFS,
	RB_METADATA_ERROR_MISSING_PLUGIN,
	RB_METADATA_ERROR_UNRECOGNIZED,
	RB_METADATA_ERROR_UNSUPPORTED,
	RB_METADATA_ERROR_GENERAL,
	RB_METADATA_ERROR_INTERNAL,
} RBMetaDataError;

#define RB_METADATA_ERROR rb_metadata_error_quark ()

GQuark rb_metadata_error_quark (void);

#define RB_TYPE_METADATA         (rb_metadata_get_type ())
#define RB_METADATA(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_METADATA, RBMetaData))
#define RB_METADATA_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_METADATA, RBMetaDataClass))
#define RB_IS_METADATA(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_METADATA))
#define RB_IS_METADATA_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_METADATA))
#define RB_METADATA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_METADATA, RBMetaDataClass))

typedef struct RBMetaDataPrivate RBMetaDataPrivate;

typedef struct
{
	GObject parent;

	RBMetaDataPrivate *priv;
} RBMetaData;

typedef struct
{
	GObjectClass parent_class;
} RBMetaDataClass;

GType		rb_metadata_get_type	(void);

RBMetaData *	rb_metadata_new		(void);

GType		rb_metadata_get_field_type (RBMetaData *md, RBMetaDataField field);

gboolean	rb_metadata_can_save	(RBMetaData *md, const char *mimetype);

void		rb_metadata_load	(RBMetaData *md,
					 const char *uri,
					 GError **error);

void		rb_metadata_save	(RBMetaData *md,
					 GError **error);

const char *	rb_metadata_get_mime	(RBMetaData *md);

gboolean	rb_metadata_get		(RBMetaData *md, RBMetaDataField field,
					 GValue *val);

gboolean	rb_metadata_set		(RBMetaData *md, RBMetaDataField field,
					 const GValue *val);

G_END_DECLS

#endif /* __RB_METADATA_H */
