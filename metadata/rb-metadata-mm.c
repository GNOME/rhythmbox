/*
 *  arch-tag: Implementation of metadata reading using monkey-media
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

#include <config.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnome/gnome-i18n.h>

#include "rb-metadata.h"
#include "rb-debug.h"
#include "monkey-media-stream-info.h"

static void rb_metadata_class_init (RBMetaDataClass *klass);
static void rb_metadata_init (RBMetaData *md);
static void rb_metadata_finalize (GObject *object);

struct RBMetaDataPrivate
{
	char *uri;
	char *type;
  
	GHashTable *metadata;
};

static GObjectClass *parent_class = NULL;

GType
rb_metadata_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		static const GTypeInfo our_info =
		{
			sizeof (RBMetaDataClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_metadata_class_init,
			NULL,
			NULL,
			sizeof (RBMetaData),
			0,
			(GInstanceInitFunc) rb_metadata_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "RBMetaData",
					       &our_info, 0);
	}

	return type;
}

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_metadata_finalize;
}

static void
rb_metadata_init (RBMetaData *md)
{
	md->priv = g_new0 (RBMetaDataPrivate, 1);
	
	md->priv->uri = NULL;
	md->priv->type = NULL;
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);

	g_hash_table_destroy (md->priv->metadata);

	g_free (md->priv->type);
	g_free (md->priv->uri);

	g_free (md->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

static void
free_gvalue (GValue *val)
{
	g_value_unset (val);
	g_free (val);
}

static void
load_val (RBMetaData *md,
	  MonkeyMediaStreamInfo *info,
	  MonkeyMediaStreamInfoField mmfield,
	  RBMetaDataField mdfield)
{
	GValue *val = g_new0 (GValue, 1);

	if (monkey_media_stream_info_get_value (info,
				                mmfield,
					        0, val))	  
		g_hash_table_insert (md->priv->metadata,
				     GINT_TO_POINTER (mdfield),
				     val);
	else
		g_free (val);
}

void
rb_metadata_load (RBMetaData *md,
		  const char *uri,
		  GError **error)
{
	MonkeyMediaStreamInfo *info = NULL;

	g_free (md->priv->uri);
	md->priv->uri = NULL;

	g_free (md->priv->type);
	md->priv->type = NULL;
	
	if (uri == NULL)
		return;
		
	md->priv->uri = g_strdup (uri);
	md->priv->type = gnome_vfs_get_mime_type (md->priv->uri);

	if (md->priv->metadata)
		g_hash_table_destroy (md->priv->metadata);
	md->priv->metadata = g_hash_table_new_full (g_direct_hash, g_direct_equal,
						    NULL, (GDestroyNotify) free_gvalue);

	info = monkey_media_stream_info_new (uri, error);
	
	if (info == NULL)
		return;

	load_val (md, info, MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
		  RB_METADATA_FIELD_TRACK_NUMBER);
	
	load_val (md, info, MONKEY_MEDIA_STREAM_INFO_FIELD_DISC_NUMBER,
		  RB_METADATA_FIELD_DISC_NUMBER);

	/* duration */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
		  RB_METADATA_FIELD_DURATION);

	/* quality */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE,
		  RB_METADATA_FIELD_BITRATE);

	/* title */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
		  RB_METADATA_FIELD_TITLE);

	/* genre */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
		  RB_METADATA_FIELD_GENRE);

	/* artist */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
		  RB_METADATA_FIELD_ARTIST);

	/* album */
	load_val (md, info,
		  MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
		  RB_METADATA_FIELD_ALBUM);

	g_object_unref (G_OBJECT (info));

}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
	return FALSE;
}

void
rb_metadata_save (RBMetaData *md, GError **error)
{
	g_set_error (error,
		     RB_METADATA_ERROR,
		     RB_METADATA_ERROR_UNSUPPORTED,
		     _("Operation not supported"));
}

gboolean
rb_metadata_get (RBMetaData *md, RBMetaDataField field,
		 GValue *ret)
{
	GValue *val;
	if ((val = g_hash_table_lookup (md->priv->metadata,
					GINT_TO_POINTER (field)))) {
		g_value_init (ret, G_VALUE_TYPE (val));
		g_value_copy (val, ret);
		return TRUE;
	}
	return FALSE;
}

gboolean
rb_metadata_set (RBMetaData *md, RBMetaDataField field,
		 const GValue *val)
{
	return FALSE;
}

const char *
rb_metadata_get_mime (RBMetaData *md)
{
	return (md->priv->type);
}
