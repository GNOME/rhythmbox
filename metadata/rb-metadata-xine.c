/*
 *  arch-tag: Implementation of xine-lib metadata loading
 *
 *  Copyright (C) 2003-2004 Bastien Nocera <hadess@hadess.net>
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

#include <libgnomevfs/gnome-vfs.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <xine.h>

#include "rb-string-helpers.h"

#include "rb-metadata.h"

G_DEFINE_TYPE(RBMetaData, rb_metadata, G_TYPE_OBJECT)

static void rb_metadata_finalize (GObject *object);

struct RBMetaDataPrivate
{
	xine_t *xine;
	xine_stream_t *stream;
};

static void
rb_metadata_class_init (RBMetaDataClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_metadata_finalize;
}

static void
rb_metadata_init (RBMetaData *md)
{
	char *path;

	md->priv = g_new0 (RBMetaDataPrivate, 1);
	md->priv->xine = xine_new ();
	path = g_build_filename (g_get_home_dir(), ".gnome2",
				 "xine-config", NULL);
	xine_config_load (md->priv->xine, path);

	xine_init (md->priv->xine);
	md->priv->stream = xine_stream_new (md->priv->xine, NULL, NULL);
}

RBMetaData *
rb_metadata_new (void)
{
	return RB_METADATA (g_object_new (RB_TYPE_METADATA, NULL));
}

static void
rb_metadata_finalize (GObject *object)
{
	RBMetaData *md;

	md = RB_METADATA (object);

	xine_close (md->priv->stream);
	xine_dispose (md->priv->stream);
	xine_exit (md->priv->xine);

	g_free (md->priv);

	G_OBJECT_CLASS (rb_metadata_parent_class)->finalize (object);
}

/* FIXME implement error collapsing like in Totem */
static gboolean
xine_error (RBMetaData *md, GError **error)
{
	int err;

	err = xine_get_error (md->priv->stream);
	if (err == XINE_ERROR_NONE)
		return TRUE;

	/* FIXME actualise to the current metadata errors */
#if 0
	switch (err)
	{
	case XINE_ERROR_NO_INPUT_PLUGIN:
	case XINE_ERROR_NO_DEMUX_PLUGIN:
		*error = g_error_new (RB_METADATA_ERROR, RB_METADATA_ERROR_OPEN_FAILED, _("There is no plugin to handle this song"));
		break;
	case XINE_ERROR_DEMUX_FAILED:
		*error = g_error_new (RB_METADATA_ERROR, RB_METADATA_ERROR_OPEN_FAILED, _("This song is broken and can not be played further"));
		break;
	case XINE_ERROR_MALFORMED_MRL:
		*error = g_error_new (RB_METADATA_ERROR, RB_METADATA_ERROR_OPEN_FAILED, _("This location is not a valid one"));
		break;
	case XINE_ERROR_INPUT_FAILED:
		*error = g_error_new (RB_METADATA_ERROR, RB_METADATA_ERROR_OPEN_FAILED, _("This song could not be opened"));                break;
	default:
		*error = g_error_new (RB_METADATA_ERROR, RB_METADATA_ERROR_OPEN_FAILED, _("Generic Error"));
		break;
	}
#endif
	return FALSE;
}

void
rb_metadata_load (RBMetaData *md, const char *uri, GError **error)
{
	int err;

	xine_close (md->priv->stream);
	err = xine_open (md->priv->stream, uri);

	if (error == 0)
	{
		if (xine_error (md, error) == FALSE)
		{
			xine_close (md->priv->stream);
			g_object_set (G_OBJECT (md), "error", error, NULL);
			return;
		}
	}

	if (xine_get_stream_info (md->priv->stream,
				XINE_STREAM_INFO_HAS_AUDIO) == FALSE)
	{
#if 0
		error = g_error_new (RB_METADATA_ERROR,
				     RB_METADATA_ERROR_OPEN_FAILED,
				     _("File is not an audio file"));
		g_object_set (G_OBJECT (md), "error", error, NULL);
#endif
		return;
	}

	if (xine_get_stream_info (md->priv->stream,
				XINE_STREAM_INFO_AUDIO_HANDLED) == FALSE)
	{
#if 0
		error = g_error_new (RB_METADATA_ERROR,
				     RB_METADATA_ERROR_OPEN_FAILED,
				     _("Song is not handled"));
		g_object_set (G_OBJECT (md), "error", error, NULL);
#endif
		return;
	}
}

gboolean
rb_metadata_get (RBMetaData *md,
			        RBMetaDataField field,
				GValue *value)
{
	const char *tmp = NULL;

	switch (field)
	{
	/* tags */
	case RB_METADATA_FIELD_TITLE:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_TITLE);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_ARTIST:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_ARTIST);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_ALBUM:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_ALBUM);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_DATE:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_YEAR);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_GENRE:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_GENRE);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_COMMENT:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_COMMENT);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_TRACK_NUMBER:
#if 0
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, xine_get_stream_info (md->priv->stream,
					XINE_STREAM_INFO_TRACK_NUMBER));
#endif
		break;
	case RB_METADATA_FIELD_MAX_TRACK_NUMBER:
#if 0
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, xine_get_stream_info (md->priv->stream,
					XINE_STREAM_INFO_MAX_TRACK_NUMBER));
#endif
		break;
	case RB_METADATA_FIELD_DESCRIPTION:
	case RB_METADATA_FIELD_VERSION:
	case RB_METADATA_FIELD_ISRC:
	case RB_METADATA_FIELD_ORGANIZATION:
	case RB_METADATA_FIELD_COPYRIGHT:
	case RB_METADATA_FIELD_CONTACT:
	case RB_METADATA_FIELD_LICENSE:
	case RB_METADATA_FIELD_PERFORMER:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
		break;
	case RB_METADATA_FIELD_DURATION:
		{
			int length = 0;
			int pos_stream, pos_time;

			xine_get_pos_length (md->priv->stream,
					&pos_stream, &pos_time, &length);

			g_value_init (value, G_TYPE_LONG);
			g_value_set_long (value, length / 1000);
		}
		break;
	case RB_METADATA_FIELD_CODEC:
		g_value_init (value, G_TYPE_STRING);
		tmp = xine_get_meta_info (md->priv->stream,
				XINE_META_INFO_AUDIOCODEC);
		g_value_set_string (value, tmp);
		break;
	case RB_METADATA_FIELD_BITRATE:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, xine_get_stream_info (md->priv->stream,
					XINE_STREAM_INFO_AUDIO_BITRATE) / 1000);
		break;
	case RB_METADATA_FIELD_ALBUM_GAIN:
	case RB_METADATA_FIELD_TRACK_GAIN:
	case RB_METADATA_FIELD_ALBUM_PEAK:
	case RB_METADATA_FIELD_TRACK_PEAK:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, 0.0);
		break;

	/* default */
	default:
		g_warning ("Invalid field!");
		g_value_init (value, G_TYPE_NONE);
		break;
	}

	return TRUE;
}

gboolean
rb_metadata_set (RBMetaData *md,
				RBMetaDataField field,
				const GValue *value)
{
	return FALSE;
}

gboolean
rb_metadata_can_save (RBMetaData *md, const char *mimetype)
{
	return FALSE;
}

const char *
rb_metadata_get_mime (RBMetaData *md)
{
	return "application/x-fixme";
}

void
rb_metadata_save (RBMetaData *md, GError **error)
{
	return;
}

