/*  monkey-sound
 *
 *  arch-tag: Implementation of Ogg Vorbis metadata retrieval
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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
#include <libgnomevfs/gnome-vfs-mime-utils.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <stdlib.h>

#include <ogg/ogg.h>
#include <vorbis/codec.h>
#include <vorbis/vorbisfile.h>

#include "ogg-helper.h"

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"

#include "vorbis-stream-info-impl.h"

static ov_callbacks file_info_callbacks =
{
	ogg_helper_read,
	ogg_helper_seek,
	ogg_helper_close_dummy,
	ogg_helper_tell
};

static void vorbis_stream_info_impl_class_init (VorbisStreamInfoImplClass *klass);
static void vorbis_stream_info_impl_init (VorbisStreamInfoImpl *ma);
static void vorbis_stream_info_impl_finalize (GObject *object);
static void vorbis_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info);
static gboolean vorbis_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
					           MonkeyMediaStreamInfoField field,
					           int index,
					           GValue *value);
static gboolean vorbis_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
					           MonkeyMediaStreamInfoField field,
					           int index,
					           const GValue *value);
static int vorbis_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				                 MonkeyMediaStreamInfoField field);

struct VorbisStreamInfoImplPrivate
{
	vorbis_comment *comment;
	vorbis_info *info;
	OggVorbis_File vf;
	GnomeVFSHandle *handle;
};

static GObjectClass *parent_class = NULL;

GType
vorbis_stream_info_impl_get_type (void)
{
	static GType vorbis_stream_info_impl_type = 0;

	if (vorbis_stream_info_impl_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (VorbisStreamInfoImplClass),
			NULL,
			NULL,
			(GClassInitFunc) vorbis_stream_info_impl_class_init,
			NULL,
			NULL,
			sizeof (VorbisStreamInfoImpl),
			0,
			(GInstanceInitFunc) vorbis_stream_info_impl_init
		};

		vorbis_stream_info_impl_type = g_type_register_static (MONKEY_MEDIA_TYPE_STREAM_INFO,
								       "VorbisStreamInfoImpl",
								       &our_info, 0);
	}

	return vorbis_stream_info_impl_type;
}

static void
vorbis_stream_info_impl_class_init (VorbisStreamInfoImplClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MonkeyMediaStreamInfoClass *info_class = MONKEY_MEDIA_STREAM_INFO_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = vorbis_stream_info_impl_finalize;

	info_class->open_stream  = vorbis_stream_info_impl_open_stream;
	info_class->get_n_values = vorbis_stream_info_impl_get_n_values;
	info_class->get_value    = vorbis_stream_info_impl_get_value;
	info_class->set_value    = vorbis_stream_info_impl_set_value;
}

static void
vorbis_stream_info_impl_init (VorbisStreamInfoImpl *impl)
{
	impl->priv = g_new0 (VorbisStreamInfoImplPrivate, 1);
}

static void
vorbis_stream_info_impl_finalize (GObject *object)
{
	VorbisStreamInfoImpl *impl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_VORBIS_STREAM_INFO_IMPL (object));

	impl = VORBIS_STREAM_INFO_IMPL (object);

	g_return_if_fail (impl->priv != NULL);

	if (&impl->priv->vf != NULL)
		ov_clear (&impl->priv->vf);
	if (impl->priv->handle != NULL)
		ogg_helper_close (impl->priv->handle);

	g_free (impl->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
vorbis_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info)
{
	VorbisStreamInfoImpl *impl = VORBIS_STREAM_INFO_IMPL (info);
	GnomeVFSResult res;
	GError *error;
	char *uri;
	int rc;

	g_object_get (G_OBJECT (info),
		      "error", &error,
		      "location", &uri,
		      NULL);

	res = gnome_vfs_open (&impl->priv->handle, uri, GNOME_VFS_OPEN_READ);
	g_free (uri);
	if (res != GNOME_VFS_OK)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
			             MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			             _("Failed to open file for reading"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	/* Try the different SEEK types that might fail */
	if ((gnome_vfs_seek (impl->priv->handle, GNOME_VFS_SEEK_END, 0) != GNOME_VFS_OK) ||
	    (gnome_vfs_seek (impl->priv->handle, GNOME_VFS_SEEK_START, 0) != GNOME_VFS_OK))
	{
		gnome_vfs_close (impl->priv->handle);
		impl->priv->handle = NULL;
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
                                     MONKEY_MEDIA_STREAM_INFO_ERROR_SEEK_FAILED,
			             _("Failed to seek file"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	rc = ov_open_callbacks (impl->priv->handle, &impl->priv->vf, NULL, 0,
			        file_info_callbacks);
	if (rc < 0)
	{
		/* see ogg-helper.c for details */
		ogg_helper_close (impl->priv->handle);
		impl->priv->handle = NULL;
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
                                     MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			             _("Failed to open file as Ogg Vorbis"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	impl->priv->comment = ov_comment (&impl->priv->vf, -1);
	impl->priv->info = ov_info (&impl->priv->vf, -1);
}

static int
vorbis_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				      MonkeyMediaStreamInfoField field)
{
	VorbisStreamInfoImpl *impl;
	
	g_return_val_if_fail (IS_VORBIS_STREAM_INFO_IMPL (info), 0);

	impl = VORBIS_STREAM_INFO_IMPL (info);

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		return vorbis_comment_query_count (impl->priv->comment, "title");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		return vorbis_comment_query_count (impl->priv->comment, "artist");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		return vorbis_comment_query_count (impl->priv->comment, "album");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		return vorbis_comment_query_count (impl->priv->comment, "date");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		return vorbis_comment_query_count (impl->priv->comment, "genre");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		return vorbis_comment_query_count (impl->priv->comment, "comment") + 
		       vorbis_comment_query_count (impl->priv->comment, "");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		{
			char *tmp, **parts;
			gboolean ret = FALSE;

			tmp = vorbis_comment_query (impl->priv->comment, "tracknumber", 0);
			if (tmp == NULL)
				return 0;

			parts = g_strsplit (tmp, "/", -1);
			
			if (parts[0] != NULL)
				ret = TRUE;

			g_strfreev (parts);

			return ret;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			char *tmp, **parts;
			gboolean ret = FALSE;

			tmp = vorbis_comment_query (impl->priv->comment, "tracktotal", 0);
			if (tmp != NULL)
				return TRUE;

			tmp = vorbis_comment_query (impl->priv->comment, "tracknumber", 0);
			if (tmp == NULL)
				return FALSE;

			parts = g_strsplit (tmp, "/", -1);
			
			if (parts[0] != NULL && parts[1] != NULL)
				ret = TRUE;

			g_strfreev (parts);

			return ret;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
		return vorbis_comment_query_count (impl->priv->comment, "location");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
		return vorbis_comment_query_count (impl->priv->comment, "description");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
		return vorbis_comment_query_count (impl->priv->comment, "version");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
		return vorbis_comment_query_count (impl->priv->comment, "isrc");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
		return vorbis_comment_query_count (impl->priv->comment, "organization");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
		return vorbis_comment_query_count (impl->priv->comment, "copyright");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
		return vorbis_comment_query_count (impl->priv->comment, "contact");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
		return vorbis_comment_query_count (impl->priv->comment, "license");
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		return vorbis_comment_query_count (impl->priv->comment, "performer");

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:
		return 1;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		return 1;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		return 0;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
		return (ov_serialnumber (&impl->priv->vf, -1) > 0);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
		return (impl->priv->comment->vendor != NULL);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_album_gain", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_audiophile", 0);
			
			return (tmp != NULL);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_track_gain", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_radio", 0);
			
			return (tmp != NULL);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_album_peak", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_peak", 0);

			return (tmp != NULL);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_track_peak", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_peak", 0);

			return (tmp != NULL);
		}
		break;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		return 1;

	/* default */
	default:
		return 0;
	}
}

static gboolean
vorbis_stream_info_impl_get_strvalue_utf8 (VorbisStreamInfoImpl *impl,
					   int index, char *entry, GValue *value)
{
	gsize read, written;
	char *strval;

	strval = g_strdup (vorbis_comment_query (impl->priv->comment, entry, index));

	if (!g_utf8_validate (strval, -1, NULL))
	{
		char *tem;

		g_warning ("Invalid UTF-8 in %s field in vorbis file\n", entry);

		tem = g_locale_to_utf8 (strval, -1, &read, &written, NULL);
		g_free (strval);

		if (!tem)
			return FALSE;

		strval = tem;
	}

	g_value_init (value, G_TYPE_STRING);
	g_value_set_string_take_ownership (value, strval);

	return TRUE;
}

static gboolean
vorbis_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
			           MonkeyMediaStreamInfoField field,
				   int index,
				   GValue *value)
{
	VorbisStreamInfoImpl *impl;
	
	g_return_val_if_fail (IS_VORBIS_STREAM_INFO_IMPL (info), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	impl = VORBIS_STREAM_INFO_IMPL (info);

	if (vorbis_stream_info_impl_get_n_values (info, field) <= 0)
		return FALSE;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "title", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "artist", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "album", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "date", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "genre", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		{
			int count;
			
			g_value_init (value, G_TYPE_STRING);

			count = vorbis_comment_query_count (impl->priv->comment, "comment");
			if (index < count)
				g_value_set_string (value, vorbis_comment_query (impl->priv->comment, "comment", index));
			else
				g_value_set_string (value, vorbis_comment_query (impl->priv->comment, "", index - count));
			if (!g_utf8_validate (g_value_get_string (value), -1, NULL))
			{
				g_warning ("Invalid UTF-8 in comment field in vorbis file\n");
				g_value_unset (value);
				return FALSE;
			}
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		{
			char *tmp, **parts;
			int num = -1;

			g_value_init (value, G_TYPE_INT);

			tmp = vorbis_comment_query (impl->priv->comment, "tracknumber", 0);
			if (tmp == NULL)
			{
				g_value_set_int (value, -1);
				break;
			}
			
			parts = g_strsplit (tmp, "/", -1);
			
			if (parts[0] != NULL)
				num = atoi (parts[0]);

			g_value_set_int (value, num);

			g_strfreev (parts);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			char *tmp;
			int num = -1;

			g_value_init (value, G_TYPE_INT);

			tmp = vorbis_comment_query (impl->priv->comment, "tracktotal", 0);
			if (tmp != NULL) {
				g_value_set_int (value, atoi (tmp));
				break;
			}

			tmp = vorbis_comment_query (impl->priv->comment, "tracknumber", 0);
			if (tmp != NULL) {
				char **parts = g_strsplit (tmp, "/", -1);
				
				if (parts[0] != NULL && parts[1] != NULL) {
					num = atoi (parts[1]);
					g_value_set_int (value, num);
				} else 
					g_value_set_int (value, -1);
				
				g_strfreev (parts);
			}
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "location", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "description", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "version", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "isrc", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "organization", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "copyright", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "contact", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "license", value);
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		return vorbis_stream_info_impl_get_strvalue_utf8 (impl, index, "performer", value);

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
		{
			GnomeVFSFileInfo *i;
			GnomeVFSResult res;

			g_value_init (value, G_TYPE_LONG);

			i = gnome_vfs_file_info_new ();
			res = gnome_vfs_get_file_info_from_handle
				(impl->priv->handle, i,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
			if (res == GNOME_VFS_OK)
				g_value_set_long (value, i->size);
			else
				g_value_set_long (value, 0);

			gnome_vfs_file_info_unref (i);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, ov_time_total (&impl->priv->vf, -1));
		break;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, _("Ogg Vorbis"));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, (int) (impl->priv->info->bitrate_nominal / 1000));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
		g_value_init (value, MONKEY_MEDIA_TYPE_AUDIO_QUALITY);
		g_value_set_enum (value, monkey_media_audio_quality_from_bit_rate ((int) (impl->priv->info->bitrate_nominal / 1000)));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		/* FIXME */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, impl->priv->info->rate);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, impl->priv->info->channels);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, ov_serialnumber (&impl->priv->vf, -1));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, impl->priv->comment->vendor);
		if (!g_utf8_validate (g_value_get_string (value), -1, NULL))
		{
			g_warning ("Invalid UTF-8 in audio vendor field in vorbis file\n");
			g_value_unset (value);
			return FALSE;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_album_gain", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_audiophile", 0);
			
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, tmp != NULL ? atof (tmp) : 0.0);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_track_gain", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_radio", 0);
			
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, tmp != NULL ? atof (tmp) : 0.0);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_album_peak", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_peak", 0);
			
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, tmp != NULL ? atof (tmp) : 0.0);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
		{
			char *tmp;

			tmp = vorbis_comment_query (impl->priv->comment, "replaygain_track_peak", 0);
			if (tmp == NULL)
				tmp = vorbis_comment_query (impl->priv->comment, "rg_peak", 0);
			
			g_value_init (value, G_TYPE_DOUBLE);
			g_value_set_double (value, tmp != NULL ? atof (tmp) : 0.0);
		}
		break;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;

	/* default */
	default:
		g_value_init (value, G_TYPE_NONE);
		break;
	}
	
	return TRUE;
}

static gboolean
vorbis_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
				   MonkeyMediaStreamInfoField field,
				   int index,
				   const GValue *value)
{
	/* FIXME */
	return FALSE;
}
