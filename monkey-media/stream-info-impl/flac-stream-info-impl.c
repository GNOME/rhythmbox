/*  monkey-sound
 *
 *  arch-tag: Implementation of FLAC metadata loading
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
#include <string.h>
#include <stdlib.h>

#include <id3tag.h>

#include "id3-vfs/id3-vfs.h"

#include <FLAC/metadata.h>
#include <FLAC/stream_decoder.h>

#include "monkey-media-stream-info.h"
#include "monkey-media-private.h"

#include "flac-stream-info-impl.h"

static void FLAC_stream_info_impl_class_init (FLACStreamInfoImplClass *klass);
static void FLAC_stream_info_impl_init (FLACStreamInfoImpl *ma);
static void FLAC_stream_info_impl_finalize (GObject *object);
static void FLAC_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info);
static gboolean FLAC_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        GValue *value);
static gboolean FLAC_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
					        MonkeyMediaStreamInfoField field,
					        int index,
					        const GValue *value);
static char *FLAC_stream_info_impl_id3_tag_get_utf8 (struct id3_tag *tag,
						    const char *field_name);
static char* FLAC_stream_info_impl_vc_tag_get_utf8 (MonkeyMediaStreamInfo *info,
						   const char *field_name);
static int FLAC_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				              MonkeyMediaStreamInfoField field);

/* FLAC stream decoder callbacks */

static FLAC__StreamDecoderReadStatus
FLAC_read_callback (const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], unsigned *bytes, void *client_data);

static FLAC__StreamDecoderWriteStatus
FLAC_write_callback (const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame,
		     const FLAC__int32 *const buffer[], void *client_data);

static void
FLAC_metadata_callback (const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data);

static void
FLAC_error_callback (const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data);

struct FLACStreamInfoImplPrivate
{
	FLAC__StreamMetadata *streaminfo;
	GnomeVFSFileSize file_size;

	/* ID3 tag information, if present */
	struct id3_tag *tag;
	struct id3_vfs_file *file;

	/* Vorbis comments, if present */
	GHashTable *vorbis_comments;
};

struct FLACDecoderCallbackContext {
	FLACStreamInfoImpl *stream_info_impl;
	GnomeVFSHandle *file;
}; 


static GObjectClass *parent_class = NULL;

GType
FLAC_stream_info_impl_get_type (void)
{
	static GType FLAC_stream_info_impl_type = 0;

	if (FLAC_stream_info_impl_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (FLACStreamInfoImplClass),
			NULL,
			NULL,
			(GClassInitFunc) FLAC_stream_info_impl_class_init,
			NULL,
			NULL,
			sizeof (FLACStreamInfoImpl),
			0,
			(GInstanceInitFunc) FLAC_stream_info_impl_init
		};

		FLAC_stream_info_impl_type = g_type_register_static (MONKEY_MEDIA_TYPE_STREAM_INFO,
								       "FLACStreamInfoImpl",
								       &our_info, 0);
	}

	return FLAC_stream_info_impl_type;
}

static void
FLAC_stream_info_impl_class_init (FLACStreamInfoImplClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	MonkeyMediaStreamInfoClass *info_class = MONKEY_MEDIA_STREAM_INFO_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = FLAC_stream_info_impl_finalize;

	info_class->open_stream  = FLAC_stream_info_impl_open_stream;
	info_class->get_n_values = FLAC_stream_info_impl_get_n_values;
	info_class->get_value    = FLAC_stream_info_impl_get_value;
	info_class->set_value    = FLAC_stream_info_impl_set_value;
}

static void
FLAC_stream_info_impl_init (FLACStreamInfoImpl *impl)
{
	impl->priv = g_new0 (FLACStreamInfoImplPrivate, 1);
}

static void
FLAC_stream_info_impl_finalize (GObject *object)
{
	FLACStreamInfoImpl *impl;

	g_return_if_fail (object != NULL);
	g_return_if_fail (IS_FLAC_STREAM_INFO_IMPL (object));

	impl = FLAC_STREAM_INFO_IMPL (object);

	g_return_if_fail (impl->priv != NULL);

	if (impl->priv->file != NULL)
		id3_vfs_close (impl->priv->file);

	if (impl->priv->streaminfo)
		FLAC__metadata_object_delete (impl->priv->streaminfo);

	if (impl->priv->vorbis_comments)
	{
		/* because we constructed the table with g_hash_table_new_full,
		 * all of the keys and values will be freed on destroy */
		g_hash_table_destroy (impl->priv->vorbis_comments);
	}

	g_free (impl->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
FLAC_stream_info_impl_open_stream (MonkeyMediaStreamInfo *info)
{
	FLACStreamInfoImpl *impl = FLAC_STREAM_INFO_IMPL (info);
	char *uri;
	GError *error;
	GnomeVFSHandle *file;

	g_object_get (G_OBJECT (info),
		      "error", &error,
		      "location", &uri,
		      NULL);

	impl->priv->file = id3_vfs_open (uri, ID3_FILE_MODE_READONLY);
	if (impl->priv->file == NULL)
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
			             MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			             _("Failed to open file for reading"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		g_free (uri);
		return;
	}

	impl->priv->tag = id3_vfs_tag (impl->priv->file);

	/* read FLAC STREAMINFO block and any VORBISCOMMENT blocks */

	if (gnome_vfs_open (&file, uri, GNOME_VFS_OPEN_READ) == GNOME_VFS_OK)
	{
		FLAC__StreamDecoder *flac_decoder;
		struct FLACDecoderCallbackContext flac_decoder_callback_context = { impl, file };
		GnomeVFSFileInfo *fi;

		/* get file size, useful for several calculations later on */

		fi = gnome_vfs_file_info_new ();

		if (gnome_vfs_get_file_info_from_handle (file, fi, GNOME_VFS_FILE_INFO_FOLLOW_LINKS) == GNOME_VFS_OK)
			impl->priv->file_size = fi->size;
		else
			impl->priv->file_size = 1;  /* not zero to prevent divide-by-zero errors */

		gnome_vfs_file_info_unref (fi);

		/* Set up and run FLAC stream decoder to decode metadata */

		impl->priv->vorbis_comments = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, g_free);
		impl->priv->streaminfo = NULL;
		flac_decoder = FLAC__stream_decoder_new ();

		FLAC__stream_decoder_set_read_callback (flac_decoder, FLAC_read_callback);
		FLAC__stream_decoder_set_write_callback (flac_decoder, FLAC_write_callback);
		FLAC__stream_decoder_set_metadata_callback (flac_decoder, FLAC_metadata_callback);
		FLAC__stream_decoder_set_error_callback (flac_decoder, FLAC_error_callback);
		FLAC__stream_decoder_set_client_data (flac_decoder, &flac_decoder_callback_context);

		/* by default, only the STREAMINFO block is parsed and passed to
		 * the metadata callback.  Here we instruct the decoder to also
		 * pass us the VORBISCOMMENT block if there is one. */
		FLAC__stream_decoder_set_metadata_respond (flac_decoder, FLAC__METADATA_TYPE_VORBIS_COMMENT);

		FLAC__stream_decoder_init (flac_decoder);

		/* this runs the decoding process, calling the callbacks as appropriate */
		if ((FLAC__stream_decoder_process_until_end_of_metadata (flac_decoder) == 0)
		    || (impl->priv->streaminfo == NULL))
		{
			error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
					     MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
					     _("Error decoding FLAC file"));
			g_object_set (G_OBJECT (info), "error", error, NULL);

			FLAC__stream_decoder_delete (flac_decoder);
			gnome_vfs_close (file);
			return;
		}

		FLAC__stream_decoder_finish (flac_decoder);
		FLAC__stream_decoder_delete (flac_decoder);

		gnome_vfs_close (file);
	}
	else
	{
		error = g_error_new (MONKEY_MEDIA_STREAM_INFO_ERROR,
			             MONKEY_MEDIA_STREAM_INFO_ERROR_OPEN_FAILED,
			             _("Failed to open file for reading"));
		g_object_set (G_OBJECT (info), "error", error, NULL);
		g_free (uri);
		return;
	}
	g_free (uri);
}

/*
 * FLAC Stream Decoder callbacks
 */

static FLAC__StreamDecoderReadStatus
FLAC_read_callback (const FLAC__StreamDecoder *decoder, FLAC__byte buffer[], unsigned *bytes, void *client_data)
{
	struct FLACDecoderCallbackContext *context = (struct FLACDecoderCallbackContext*) client_data;
	GnomeVFSFileSize read;
	GnomeVFSResult result;

	result = gnome_vfs_read (context->file, buffer, *bytes, &read);

	if (result == GNOME_VFS_OK)
	{
		*bytes = read;
		return FLAC__STREAM_DECODER_READ_STATUS_CONTINUE;
	}
	else if (result == GNOME_VFS_ERROR_EOF)
	{
		return FLAC__STREAM_DECODER_READ_STATUS_END_OF_STREAM;
	}
	else
	{
		return FLAC__STREAM_DECODER_READ_STATUS_ABORT;
	}
}

static FLAC__StreamDecoderWriteStatus
FLAC_write_callback (const FLAC__StreamDecoder *decoder, const FLAC__Frame *frame, 
		     const FLAC__int32 *const buffer[], void *client_data)
{
	/* This callback should never be called, because we request that
	 * FLAC only decodes metadata, never actual sound data. */
	return FLAC__STREAM_DECODER_WRITE_STATUS_ABORT;
}

static void
FLAC_metadata_callback (const FLAC__StreamDecoder *decoder, const FLAC__StreamMetadata *metadata, void *client_data)
{
	struct FLACDecoderCallbackContext *context = (struct FLACDecoderCallbackContext*) client_data;

	if (metadata->type == FLAC__METADATA_TYPE_STREAMINFO)
	{
		context->stream_info_impl->priv->streaminfo = FLAC__metadata_object_clone (metadata);
	}
	else if (metadata->type == FLAC__METADATA_TYPE_VORBIS_COMMENT)
	{
		GHashTable *vorbis_comments = context->stream_info_impl->priv->vorbis_comments;
		const FLAC__StreamMetadata_VorbisComment *vc_block = &metadata->data.vorbis_comment;
		int c;

		for (c = 0; c < vc_block->num_comments; c++)
		{
			FLAC__StreamMetadata_VorbisComment_Entry entry = vc_block->comments[c];
			char *null_terminated_comment = malloc (entry.length + 1);
                        gchar* upcase_fieldname;
			gchar** parts;

			memcpy (null_terminated_comment, entry.entry, entry.length);
			null_terminated_comment[entry.length] = '\0';
			parts = g_strsplit (null_terminated_comment, "=", 2);

			if (parts[0] == NULL || parts[1] == NULL)
				goto free_continue;

			/* Make sure the fieldname is uppercase */
			upcase_fieldname = g_utf8_strup (parts[0], -1);
			if (upcase_fieldname == NULL)
				goto free_continue;

			g_free (parts[0]);
			parts[0] = upcase_fieldname;

			/* if an earlier comment had this same key name, it will be replaced.
			 * a possible improvement is to make the values of the hash table
			 * lists of strings instead of single strings. */
			g_hash_table_insert (vorbis_comments, g_strdup (parts[0]), g_strdup (parts[1]));

		free_continue:
			g_strfreev (parts);
			free (null_terminated_comment);
		}
	}
}

static void
FLAC_error_callback (const FLAC__StreamDecoder *decoder, FLAC__StreamDecoderErrorStatus status, void *client_data)
{
}

/*
 * End FLAC Stream Decoder callbacks
 */

static int
FLAC_stream_info_impl_get_n_values (MonkeyMediaStreamInfo *info,
				   MonkeyMediaStreamInfoField field)
{
	FLACStreamInfoImpl *impl;
	FLAC__StreamMetadata_StreamInfo stream_info;
	char *tmp;
	gboolean ret = FALSE;
	
	g_return_val_if_fail (IS_FLAC_STREAM_INFO_IMPL (info), 0);

	impl = FLAC_STREAM_INFO_IMPL (info);

	stream_info = impl->priv->streaminfo->data.stream_info;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "TITLE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TITLE);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ARTIST");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ARTIST);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ALBUM");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ALBUM);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "DATE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_YEAR);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "GENRE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_GENRE);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_COMMENT);
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "TRACKNUMBER");
		if (tmp != NULL)
		{
			g_free (tmp);
			return TRUE;
		}
		else
		{
			char **parts;
			
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
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
			
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
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
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "LOCATION");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "DESCRIPTION");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "VERSION");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ISRC");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ORGANIZATION");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "COPYRIGHT");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "CONTACT");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "LICENSE");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "PERFORMER");
		ret = (tmp != NULL);
		g_free (tmp);
		return ret;

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
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		return 1;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SERIAL_NUMBER:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VENDOR:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_GAIN:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_ALBUM_PEAK:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRACK_PEAK:
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
FLAC_stream_info_impl_get_value (MonkeyMediaStreamInfo *info,
			        MonkeyMediaStreamInfoField field,
				int index,
				GValue *value)
{
	FLACStreamInfoImpl *impl;
	FLAC__StreamMetadata_StreamInfo *stream_info;
	char *tmp;
	
	g_return_val_if_fail (IS_FLAC_STREAM_INFO_IMPL (info), FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	impl = FLAC_STREAM_INFO_IMPL (info);

	stream_info = &impl->priv->streaminfo->data.stream_info;

	if (FLAC_stream_info_impl_get_n_values (info, field) <= 0)
		return FALSE;

	switch (field)
	{
	/* tags */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "TITLE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TITLE);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ARTIST");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ARTIST);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ALBUM");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_ALBUM);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DATE:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "DATE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_YEAR);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "GENRE");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_GENRE);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "COMMENT");
		if (tmp == NULL)
			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_COMMENT);
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
		g_value_init (value, G_TYPE_INT);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "TRACKNUMBER");
		if (tmp != NULL)
		{
			g_value_set_int (value, atoi(tmp));
			g_free (tmp);
		}
		else
		{
			char **parts;
			int num = -1;
			

			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
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

			tmp = FLAC_stream_info_impl_id3_tag_get_utf8 (impl->priv->tag, ID3_FRAME_TRACK);
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
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "LOCATION");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DESCRIPTION:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "DESCRIPTION");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_VERSION:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "VERSION");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ISRC:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ISRC");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_ORGANIZATION:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "ORGANIZATION");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_COPYRIGHT:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "COPYRIGHT");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_CONTACT:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "CONTACT");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_LICENSE:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "LICENSE");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_PERFORMER:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "PERFORMER");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;

	/* generic bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, impl->priv->file_size);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, stream_info->total_samples / stream_info->sample_rate);
		break;

	/* audio bits */
	case MONKEY_MEDIA_STREAM_INFO_FIELD_HAS_AUDIO:
		g_value_init (value, G_TYPE_BOOLEAN);
		g_value_set_boolean (value, TRUE);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO:
		{
			tmp = g_strdup_printf (_("FLAC"));
			g_value_init (value, G_TYPE_STRING);
			g_value_set_string (value, tmp);
			g_free (tmp);
		}
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE:
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_AVERAGE_BIT_RATE:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, impl->priv->file_size * 8 / 1024.0f /  /* kilobits */
					((float)stream_info->total_samples / stream_info->sample_rate)); /* seconds */
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_QUALITY:
		g_value_init (value, MONKEY_MEDIA_TYPE_AUDIO_QUALITY);
		g_value_set_enum (value, MONKEY_MEDIA_AUDIO_QUALITY_LOSSLESS);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_TRM_ID:
		g_value_init (value, G_TYPE_STRING);
		tmp = FLAC_stream_info_impl_vc_tag_get_utf8 (info, "MUSICBRAINZ_TRMID");
		g_value_set_string (value, tmp);
		g_free (tmp);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_VARIABLE_BIT_RATE:
		g_value_init (value, G_TYPE_BOOLEAN);
		/* FLAC is VBR by nature */
		g_value_set_boolean (value, true);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_SAMPLE_RATE:
		g_value_init (value, G_TYPE_LONG);
		g_value_set_long (value, stream_info->sample_rate);
		break;
	case MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CHANNELS:
		g_value_init (value, G_TYPE_INT);
		g_value_set_int (value, stream_info->channels);
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
		/* FIXME i believe flac does support this */
		/* [JH] I don't think it does */
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
FLAC_stream_info_impl_set_value (MonkeyMediaStreamInfo *info,
				MonkeyMediaStreamInfoField field,
				int index,
				const GValue *value)
{
	/* FIXME */
	return FALSE;
}

static char *
FLAC_stream_info_impl_id3_tag_get_utf8 (struct id3_tag *tag, const char *field_name)
{
	unsigned int nstrings, j;
	const struct id3_frame *frame;
	const union id3_field *field;
	const id3_ucs4_t *ucs4;
	id3_utf8_t *utf8 = NULL;

	frame = id3_tag_findframe (tag, field_name, 0);
	if (frame == 0)
		return NULL;

	field = &frame->fields[1];
	nstrings = id3_field_getnstrings (field);
	for (j = 0; j < nstrings; j++)
	{
		ucs4 = id3_field_getstrings (field, j);

		if (strcmp (field_name, ID3_FRAME_GENRE) == 0)
			ucs4 = id3_genre_name (ucs4);

		utf8 = id3_ucs4_utf8duplicate (ucs4);
	}

	return utf8;
}

static char*
FLAC_stream_info_impl_vc_tag_get_utf8 (MonkeyMediaStreamInfo *info, const char *field_name)
{
	FLACStreamInfoImpl *impl;
	char *utf8;

	g_return_val_if_fail (IS_FLAC_STREAM_INFO_IMPL (info), NULL);
	g_return_val_if_fail (field_name != NULL, NULL);

	impl = FLAC_STREAM_INFO_IMPL (info);

	utf8 = g_hash_table_lookup (impl->priv->vorbis_comments, field_name);
	return g_strdup (utf8);
}

