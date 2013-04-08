/*
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

#ifndef __RB_METADATA_H
#define __RB_METADATA_H

#include <glib-object.h>

G_BEGIN_DECLS

typedef enum
{
	RB_METADATA_FIELD_TITLE,                   /* string */
	RB_METADATA_FIELD_ARTIST,                  /* string */
	RB_METADATA_FIELD_ALBUM,                   /* string */
	RB_METADATA_FIELD_DATE,                    /* ulong */
	RB_METADATA_FIELD_GENRE,                   /* string */
	RB_METADATA_FIELD_COMMENT,                 /* string */
	RB_METADATA_FIELD_TRACK_NUMBER,            /* ulong */
	RB_METADATA_FIELD_MAX_TRACK_NUMBER,        /* ulong */
	RB_METADATA_FIELD_DISC_NUMBER,             /* ulong */
	RB_METADATA_FIELD_MAX_DISC_NUMBER,         /* ulong */
	RB_METADATA_FIELD_DESCRIPTION,             /* string */
	RB_METADATA_FIELD_VERSION,                 /* string */
	RB_METADATA_FIELD_ISRC,                    /* string */
	RB_METADATA_FIELD_ORGANIZATION,            /* string */
	RB_METADATA_FIELD_COPYRIGHT,               /* string */
	RB_METADATA_FIELD_CONTACT,                 /* string */
	RB_METADATA_FIELD_LICENSE,                 /* string */
	RB_METADATA_FIELD_PERFORMER,               /* string */
	RB_METADATA_FIELD_DURATION,                /* ulong */
	RB_METADATA_FIELD_CODEC,		   /* string */
	RB_METADATA_FIELD_BITRATE,                 /* ulong */
	RB_METADATA_FIELD_TRACK_GAIN,		   /* double */
	RB_METADATA_FIELD_TRACK_PEAK,		   /* double */
	RB_METADATA_FIELD_ALBUM_GAIN,		   /* double */
	RB_METADATA_FIELD_ALBUM_PEAK,		   /* double */
	RB_METADATA_FIELD_LANGUAGE_CODE,	   /* string */
	RB_METADATA_FIELD_BPM,			   /* double */
	RB_METADATA_FIELD_MUSICBRAINZ_TRACKID,     /* string */
	RB_METADATA_FIELD_MUSICBRAINZ_ARTISTID,    /* string */
	RB_METADATA_FIELD_MUSICBRAINZ_ALBUMID,     /* string */
	RB_METADATA_FIELD_MUSICBRAINZ_ALBUMARTISTID,   /* string */
	RB_METADATA_FIELD_ARTIST_SORTNAME,         /* string */
	RB_METADATA_FIELD_ALBUM_SORTNAME,          /* string */
	RB_METADATA_FIELD_ALBUM_ARTIST,            /* string */
	RB_METADATA_FIELD_ALBUM_ARTIST_SORTNAME,   /* string */
	RB_METADATA_FIELD_COMPOSER,            /* string */
	RB_METADATA_FIELD_COMPOSER_SORTNAME,   /* string */

	RB_METADATA_FIELD_LAST			   /* nothing */
} RBMetaDataField;

#define RB_TYPE_METADATA_FIELD (rb_metadata_field_get_type ())

typedef enum
{
	RB_METADATA_ERROR_IO,
	RB_METADATA_ERROR_MISSING_PLUGIN,
	RB_METADATA_ERROR_UNRECOGNIZED,
	RB_METADATA_ERROR_UNSUPPORTED,
	RB_METADATA_ERROR_GENERAL,
	RB_METADATA_ERROR_INTERNAL,
	RB_METADATA_ERROR_EMPTY_FILE
} RBMetaDataError;

#define RB_METADATA_ERROR rb_metadata_error_quark ()
#define RB_TYPE_METADATA_ERROR (rb_metadata_error_get_type ())

GQuark rb_metadata_error_quark (void);

#define RB_TYPE_METADATA         (rb_metadata_get_type ())
#define RB_METADATA(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_METADATA, RBMetaData))
#define RB_METADATA_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_METADATA, RBMetaDataClass))
#define RB_IS_METADATA(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_METADATA))
#define RB_IS_METADATA_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_METADATA))
#define RB_METADATA_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_METADATA, RBMetaDataClass))

typedef struct _RBMetaData RBMetaData;
typedef struct _RBMetaDataClass RBMetaDataClass;

typedef struct RBMetaDataPrivate RBMetaDataPrivate;

struct _RBMetaData
{
	GObject parent;

	RBMetaDataPrivate *priv;
};

struct _RBMetaDataClass
{
	GObjectClass parent_class;
};

GType		rb_metadata_get_type	(void);

GType		rb_metadata_field_get_type (void);
GType		rb_metadata_error_get_type (void);

GType		rb_metadata_get_field_type (RBMetaDataField field);

const char *	rb_metadata_get_field_name (RBMetaDataField field);

RBMetaData *	rb_metadata_new		(void);

gboolean	rb_metadata_can_save	(RBMetaData *md, const char *media_type);
char **		rb_metadata_get_saveable_types (RBMetaData *md);

void		rb_metadata_reset	(RBMetaData *md);

void		rb_metadata_load	(RBMetaData *md,
					 const char *uri,
					 GError **error);

void		rb_metadata_save	(RBMetaData *md,
					 const char *uri,
					 GError **error);

const char *	rb_metadata_get_media_type	(RBMetaData *md);

gboolean	rb_metadata_has_missing_plugins (RBMetaData *md);

gboolean	rb_metadata_get_missing_plugins (RBMetaData *md,
					 char ***missing_plugins,
					 char ***plugin_descriptions);
gboolean	rb_metadata_has_audio	(RBMetaData *md);
gboolean	rb_metadata_has_video	(RBMetaData *md);
gboolean	rb_metadata_has_other_data (RBMetaData *md);

gboolean	rb_metadata_get		(RBMetaData *md, RBMetaDataField field,
					 GValue *val);

gboolean	rb_metadata_set		(RBMetaData *md, RBMetaDataField field,
					 const GValue *val);

G_END_DECLS

#endif /* __RB_METADATA_H */
