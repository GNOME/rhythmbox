/*  monkey-sound
 *
 *  arch-tag: Implementation of MP3 metadata loading
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
#include <string.h>
#include <stdlib.h>

#include <id3tag.h>

#include "rb-string-helpers.h"

#include "id3-vfs/id3-vfs.h"

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"

#include "mp3-stream-info-impl.h"

static void MP3_stream_info_impl_class_init (MP3StreamInfoImplClass *klass);
static void MP3_stream_info_impl_init (MP3StreamInfoImpl *ma);
static void MP3_stream_info_impl_finalize (GObject *object);
static void MP3_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info);
static gboolean MP3_stream_info_impl_get_bitrate_info (MP3StreamInfoImpl *impl);
static gboolean MP3_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        GValue *value);
static gboolean MP3_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        const GValue *value);
static char *MP3_stream_info_impl_id3_tag_get_utf8 (struct id3_tag *tag,
						    const char *field_name);
static int MP3_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				              MonkeyMediaStreamInfoField field);

struct MP3BitrateInfo
{
	int bitrate;
	int samplerate;
	int time;
	int channels;
	int version;
	int vbr;
};

struct MP3StreamInfoImplPrivate
{
	struct id3_tag *tag;
	struct id3_vfs_file *file;
	struct MP3BitrateInfo *info_num;
};

static GObjectClass *parent_class = NULL;

GType
MP3_stream_info_impl_get_type (void)
{
	static GType MP3_stream_info_impl_type = 0;

	if (MP3_stream_info_impl_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (MP3StreamInfoImplClass),
			NULL,
			NULL,
			(GClassInitFunc) MP3_stream_info_impl_class_init,
			NULL,
			NULL,
			sizeof (MP3StreamInfoImpl),
			0,
			(GInstanceInitFunc) MP3_stream_info_impl_init
		};

		MP3_stream_info_impl_type = g_type_register_static (MONKEY_MEDIA_TYPE_STREAM_INFO,
								       "MP3StreamInfoImpl",
								       &our_info, 0);
	}

	return MP3_stream_info_impl_type;
}

static void
MP3_stream_info_impl_class_init (MP3StreamInfoImplClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MonkeyMediaStreamInfoClass *info_class = MONKEY_MEDIA_STREAM_INFO_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = MP3_stream_info_impl_finalize;

	info_class->open_stream  = MP3_stream_info_impl_open_stream;
	info_class->get_n_values = MP3_stream_info_impl_get_n_values;
	info_class->get_value    = MP3_stream_info_impl_get_value;
	info_class->set_value    = MP3_stream_info_impl_set_value;
}

static void
MP3_stream_info_impl_init (MP3StreamInfoImpl *impl)
{
	impl->priv = g_new0 (MP3StreamInfoImplPrivate, 1);
}

static void
MP3_stream_info_impl_finalize (GObject *object)
{
	MP3StreamInfoImpl *impl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_MP3_STREAM_INFO_IMPL (object));

	impl = MP3_STREAM_INFO_IMPL (object);

	g_return_if_fail (impl->priv != NULL);

	if (impl->priv->file != NULL)
		id3_vfs_close (impl->priv->file);

	g_free (impl->priv->info_num);
	g_free (impl->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
MP3_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info)
{
	MP3StreamInfoImpl *impl = MP3_STREAM_INFO_IMPL (info);
	char *uri;
	GError *error;

	g_object_get (G_OBJECT (info),
		      "error", &error,
		      "location", &uri,
		      NULL);

	impl->priv->file = id3_vfs_open (uri, ID3_FILE_MODE_READONLY);
	g_free (uri);
	if (impl->priv->file == NULL)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
			             MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			             _("Failed to open file for reading"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}

	impl->priv->tag = id3_vfs_tag (impl->priv->file);

	if (MP3_stream_info_impl_get_bitrate_info (impl) == FALSE)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
				     MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
				     _("Failed to gather information about the file"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		return;
	}
}

static void
MP3_stream_info_impl_get_length_from_tag (MP3StreamInfoImpl *impl)
{
	struct id3_frame const *frame;

	/* The following is based on information from the
	 * ID3 tag version 2.4.0 Native Frames informal standard.
	 */
	frame = id3_tag_findframe(impl->priv->tag, "TLEN", 0);

	if (frame == NULL)
		return;

	{
		union id3_field const *field;
		unsigned int nstrings;
		id3_latin1_t *latin1;

		field = id3_frame_field (frame, 1);
		nstrings = id3_field_getnstrings (field);
		if (nstrings <= 0)
			return;

		latin1 = id3_ucs4_latin1duplicate
			(id3_field_getstrings(field, 0));

		if (latin1 == NULL)
			return;

		/* "The 'Length' frame contains the length of the
		 * audio file in milliseconds, represented as a
		 * numeric string."
		 */
		if (atol(latin1) > 0)
			/* monkey-media needs a duration in seconds */
			impl->priv->info_num->time = atol(latin1)/1000;
		g_free (latin1);
	}
}

static gboolean
MP3_stream_info_impl_get_bitrate_info (MP3StreamInfoImpl *impl)
{
	if (impl->priv->info_num == NULL)
	{
		struct MP3BitrateInfo *info;

		info = g_new0 (struct MP3BitrateInfo, 1);
		if (id3_vfs_bitrate (impl->priv->file,
				&info->bitrate,
				&info->samplerate,
				&info->time,
				&info->version,
				&info->vbr,
				&info->channels) == 0)
		{
			impl->priv->info_num = info;
			return FALSE;
		}

		impl->priv->info_num = info;

		MP3_stream_info_impl_get_length_from_tag (impl);
	}

	return TRUE;
}

static int
MP3_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				   MonkeyMediaStreamInfoField field)
{
	MP3StreamInfoImpl *impl;
	char *tmp;
	gboolean ret = FALSE;
	
	g_return_val_if_fail (IS_MP3_STREAM_INFO_IMPL (info), 0);

	impl = MP3_STREAM_INFO_IMPL (info);

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TITLE);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ARTIST);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ALBUM);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_YEAR);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_GENRE);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_COMMENT);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		{
			char **parts;
			
			tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
			if (tmp == NULL)
			{
				g_free (tmp);
				return 0;
			}
			
			parts = g_strsplit (tmp, "/", -1);

			if (parts[0] != NULL)
				ret = TRUE;

			g_strfreev (parts);
			g_free (tmp);

			return ret;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			char **parts;
			
			tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
			if (tmp == NULL)
			{
				g_free (tmp);
				return 0;
			}

			parts = g_strsplit (tmp, "/", -1);

			if (parts[0] != NULL && parts[1] != NULL)
				ret = TRUE;

			g_strfreev (parts);
			g_free (tmp);

			return ret;
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		return 0;

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
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		return 0;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		return 1;

	/* default */
	default:
		return 0;
	}
}

static gboolean
MP3_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
			        MonkeyMediaStreamInfoField field,
				int index,
				GValue *value)
{
	MP3StreamInfoImpl *impl;
	char *tmp;
	
	g_return_val_if_fail (IS_MP3_STREAM_INFO_IMPL (info), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	impl = MP3_STREAM_INFO_IMPL (info);
	
	if (MP3_stream_info_impl_get_n_values (info, field) <= 0)
		return FALSE;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TITLE);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ARTIST);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ALBUM);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_YEAR);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_GENRE);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		g_value_init (value, G_TYPE_STRING);
		tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_COMMENT);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		{
			char **parts;
			int num = -1;
			
			g_value_init (value, G_TYPE_INT);

			tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
			if (tmp == NULL)
			{
				g_free (tmp);
				g_value_set_int (value, -1);
				break;
			}

			parts = g_strsplit (tmp, "/", -1);

			if (parts[0] != NULL)
				num = atoi (parts[0]);

			g_value_set_int (value, num);

			g_strfreev (parts);
			g_free (tmp);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
		{
			char **parts;
			int num = -1;
			
			g_value_init (value, G_TYPE_INT);

			tmp = MP3_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
			if (tmp == NULL)
			{
				g_free (tmp);
				g_value_set_int (value, -1);
				break;
			}

			parts = g_strsplit (tmp, "/", -1);

			if (parts[0] != NULL && parts[1] != NULL)
				num = atoi (parts[1]);

			g_value_set_int (value, num);

			g_strfreev (parts);
			g_free (tmp);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LOCATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
		break;

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
		{
			GnomeVFSFileInfo *i;
			GnomeVFSResult res;
			char *uri;

			g_object_get (G_OBJECT (info), "location", &uri, NULL);

			g_value_init (value, G_TYPE_LONG);
			
			i = gnome_vfs_file_info_new ();
			res = gnome_vfs_get_file_info (uri, i,
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

		if (impl->priv->info_num->vbr == 0)
		{
			GnomeVFSFileSize size;
			GValue val = { 0, };

			MP3_stream_info_impl_get_value (info,
							MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
							0,
							&val);
			size = g_value_get_long (&val);
			g_value_unset (&val);

			if (impl->priv->info_num->bitrate > 0)
				g_value_set_long (value, ((double) size / 1000.0f) / ((double) impl->priv->info_num->bitrate / 8.0f / 1000.0f));
			else
				g_value_set_long (value, 0);
		} else {
			g_value_set_long (value, impl->priv->info_num->time);
		}
		break;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
		g_value_init (value, G_TYPE_STRING);
		if (impl->priv->info_num->version != 3)
		{
			tmp = g_strdup_printf (_("MPEG %d Layer III"), impl->priv->info_num->version);
		} else {
			tmp = g_strdup (_("MPEG 2.5 Layer III"));
		}
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, impl->priv->info_num->bitrate / 1000);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
		g_value_init (value, MONKEY_MEDIA_TYPE_AUDIO_QUALITY);
		g_value_set_enum (value, monkey_media_audio_quality_from_bit_rate (impl->priv->info_num->bitrate / 1000));
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		/* FIXME */
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, NULL);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, impl->priv->info_num->vbr);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, impl->priv->info_num->samplerate);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, impl->priv->info_num->channels);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, 0);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, "");
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
		g_value_init (value, G_TYPE_DOUBLE);
		g_value_set_double (value, 0.0);
		break;

	/* video bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_VIDEO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, FALSE);
		break;

	/* default */
	default:
		g_warning ("Invalid field!");
		g_value_init (value, G_TYPE_NONE);
		break;
	}

	return TRUE;
}

static gboolean
MP3_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
				MonkeyMediaStreamInfoField field,
				int index,
				const GValue *value)
{
	/* FIXME */
	return FALSE;
}

static char *
MP3_stream_info_impl_id3_tag_get_utf8 (struct id3_tag *tag, const char *field_name)
{
	unsigned int nstrings, j; 
	const struct id3_frame *frame;
	const union id3_field *field;
	const id3_ucs4_t *ucs4 = NULL;
	id3_utf8_t *utf8 = NULL;

	frame = id3_tag_findframe (tag, field_name, 0);
	if (frame == 0)
		return NULL;

	field = id3_frame_field(frame, 1);
	nstrings = id3_field_getnstrings (field);
	for (j = 0; j < nstrings; j++)
	{
		id3_utf8_t *tmp = NULL;

		ucs4 = id3_field_getstrings (field, j);

		if (strcmp (field_name, ID3_FRAME_GENRE) == 0)
			ucs4 = id3_genre_name (ucs4);

		tmp = id3_ucs4_utf8duplicate (ucs4);
		if (strcmp (tmp, "") != 0)
			utf8 = tmp;
		else
			g_free (tmp);
	}

	if (utf8 && !g_utf8_validate (utf8, -1, NULL)) {
		g_warning ("Invalid UTF-8 in %s field in mp3 file\n", field_name);
		/* Should rb_unicodify really be used to convert ucs4 data to utf8? */
		utf8 = rb_unicodify ((char *) ucs4);
	}

	return utf8;
}
