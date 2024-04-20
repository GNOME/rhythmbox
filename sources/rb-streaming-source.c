/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.wh9.net>
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

/**
 * SECTION:rbstreamingsource
 * @short_description: Base class for streaming sources such as internet radio
 *
 * This class provides handling of buffering signals and streaming song metadata
 * common to different types of sources that play continuous streaming media.
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-streaming-source.h"

#include "rhythmdb-query-model.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-metadata.h"

#define STREAMING_METADATA_NOTIFY_TIMEOUT	350

static void rb_streaming_source_class_init (RBStreamingSourceClass *klass);
static void rb_streaming_source_init (RBStreamingSource *source);
static void rb_streaming_source_constructed (GObject *object);
static void rb_streaming_source_dispose (GObject *object);

/* source methods */
static RBSourceEOFType impl_handle_eos (RBSource *asource);

static void playing_entry_changed_cb (RBShellPlayer *player,
				      RhythmDBEntry *entry,
				      RBStreamingSource *source);
static GValue * streaming_title_request_cb (RhythmDB *db,
					    RhythmDBEntry *entry,
					    RBStreamingSource *source);
static GValue * streaming_artist_request_cb (RhythmDB *db,
					     RhythmDBEntry *entry,
					     RBStreamingSource *source);
static GValue * streaming_album_request_cb (RhythmDB *db,
					    RhythmDBEntry *entry,
					    RBStreamingSource *source);
static void extra_metadata_gather_cb (RhythmDB *db,
				      RhythmDBEntry *entry,
				      RBStringValueMap *data,
				      RBStreamingSource *source);

struct _RBStreamingSourcePrivate
{
	RhythmDB *db;

	gboolean initialized;
	gboolean is_playing;

	RBShellPlayer *player;
	RhythmDBEntry *playing_stream;
	char *streaming_title;
	char *streaming_artist;
	char *streaming_album;

	gint emit_notify_id;
	gint buffering_id;
	guint buffering;

	gboolean dispose_has_run;
};

G_DEFINE_TYPE (RBStreamingSource, rb_streaming_source, RB_TYPE_SOURCE)

static void
rb_streaming_source_class_init (RBStreamingSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_streaming_source_dispose;
	object_class->constructed = rb_streaming_source_constructed;

	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->handle_eos = impl_handle_eos;
	source_class->try_playlist = (RBSourceFeatureFunc) rb_true_function;

	g_type_class_add_private (klass, sizeof (RBStreamingSourcePrivate));
}

static void
rb_streaming_source_init (RBStreamingSource *source)
{
	source->priv = (G_TYPE_INSTANCE_GET_PRIVATE ((source),
						     RB_TYPE_STREAMING_SOURCE,
						     RBStreamingSourcePrivate));
}

static void
rb_streaming_source_dispose (GObject *object)
{
	RBStreamingSource *source;

	source = RB_STREAMING_SOURCE (object);

	if (source->priv->player) {
		g_object_unref (source->priv->player);
		source->priv->player = NULL;
	}

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_streaming_source_parent_class)->dispose (object);
}

static void
rb_streaming_source_constructed (GObject *object)
{
	RBStreamingSource *source;
	RBShell *shell;

	RB_CHAIN_GOBJECT_METHOD (rb_streaming_source_parent_class, constructed, object);
	source = RB_STREAMING_SOURCE (object);

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell),
		      "db", &source->priv->db,
		      "shell-player", &source->priv->player,
		      NULL);
	g_object_unref (shell);

	g_signal_connect_object (G_OBJECT (source->priv->db),
				 "entry-extra-metadata-request::" RHYTHMDB_PROP_STREAM_SONG_TITLE,
				 G_CALLBACK (streaming_title_request_cb),
				 source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->db),
				 "entry-extra-metadata-request::" RHYTHMDB_PROP_STREAM_SONG_ARTIST,
				 G_CALLBACK (streaming_artist_request_cb),
				 source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->db),
				 "entry-extra-metadata-request::" RHYTHMDB_PROP_STREAM_SONG_ALBUM,
				 G_CALLBACK (streaming_album_request_cb),
				 source, 0);

	g_signal_connect_object (G_OBJECT (source->priv->db),
				 "entry-extra-metadata-gather",
				 G_CALLBACK (extra_metadata_gather_cb),
				 source, 0);

/*	g_signal_connect_object (source->priv->player, "playing-source-changed",
				 G_CALLBACK (playing_source_changed_cb),
				 source, 0); */
	g_signal_connect_object (source->priv->player, "playing-song-changed",
				 G_CALLBACK (playing_entry_changed_cb),
				 source, 0);
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_RETRY;
}

/**
 * rb_streaming_source_get_progress:
 * @source: a #RBStreamingSource
 * @text: (out callee-allocates) (transfer full): returns buffering status text
 * @progress: (out callee-allocates): returns buffering progress fraction
 *
 * Provides status text and progress fraction suitable for use in
 * a streaming source's @rb_source_get_status method.
 */
void
rb_streaming_source_get_progress (RBStreamingSource *source, char **text, float *progress)
{
	if (source->priv->buffering == -1) {
		*progress = 0.0;
		g_free (*text);
		*text = g_strdup (_("Connecting"));
	} else if (source->priv->buffering > 0) {
		*progress = ((float)source->priv->buffering)/100;
		g_free (*text);
		*text = g_strdup (_("Buffering"));
	}
}

static void
buffering_cb (GObject *backend, gpointer whatever, guint progress, RBStreamingSource *source)
{
	if (progress == 0)
		progress = 1;
	else if (progress == 100)
		progress = 0;

	source->priv->buffering = progress;
	rb_source_notify_playback_status_changed (RB_SOURCE (source));
}

static gboolean
check_entry_type (RBStreamingSource *source, RhythmDBEntry *entry)
{
	RhythmDBEntryType *entry_type;
	gboolean matches = FALSE;

	g_object_get (source, "entry-type", &entry_type, NULL);
	if (entry != NULL && rhythmdb_entry_get_entry_type (entry) == entry_type)
		matches = TRUE;
	g_object_unref (entry_type);

	return matches;
}

static GValue *
streaming_title_request_cb (RhythmDB *db,
			    RhythmDBEntry *entry,
			    RBStreamingSource *source)
{
	GValue *value;
	if (check_entry_type (source, entry) == FALSE ||
	    entry != rb_shell_player_get_playing_entry (source->priv->player) ||
	    source->priv->streaming_title == NULL)
		return NULL;

	rb_debug ("returning streaming title \"%s\" to extra metadata request", source->priv->streaming_title);
	value = g_new0 (GValue, 1);
	g_value_init (value, G_TYPE_STRING);
	g_value_set_string (value, source->priv->streaming_title);
	return value;
}

static GValue *
streaming_artist_request_cb (RhythmDB *db,
			     RhythmDBEntry *entry,
			     RBStreamingSource *source)
{
	GValue *value;

	if (check_entry_type (source, entry) == FALSE ||
	    entry != rb_shell_player_get_playing_entry (source->priv->player) ||
	    source->priv->streaming_artist == NULL)
		return NULL;

	rb_debug ("returning streaming artist \"%s\" to extra metadata request", source->priv->streaming_artist);
	value = g_new0 (GValue, 1);
	g_value_init (value, G_TYPE_STRING);
	g_value_set_string (value, source->priv->streaming_artist);
	return value;
}

static GValue *
streaming_album_request_cb (RhythmDB *db,
			    RhythmDBEntry *entry,
			    RBStreamingSource *source)
{
	GValue *value;

	if (check_entry_type (source, entry) == FALSE ||
	    entry != rb_shell_player_get_playing_entry (source->priv->player) ||
	    source->priv->streaming_album == NULL)
		return NULL;

	rb_debug ("returning streaming album \"%s\" to extra metadata request", source->priv->streaming_album);
	value = g_new0 (GValue, 1);
	g_value_init (value, G_TYPE_STRING);
	g_value_set_string (value, source->priv->streaming_album);
	return value;
}
static void
extra_metadata_gather_cb (RhythmDB *db,
			  RhythmDBEntry *entry,
			  RBStringValueMap *data,
			  RBStreamingSource *source)
{
	/* our extra metadata only applies to the playing entry */
	if (entry != rb_shell_player_get_playing_entry (source->priv->player) ||
	    check_entry_type (source, entry) == FALSE)
		return;

	if (source->priv->streaming_title != NULL) {
		GValue value = {0,};

		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, source->priv->streaming_title);
		rb_string_value_map_set (data, RHYTHMDB_PROP_STREAM_SONG_TITLE, &value);
		g_value_unset (&value);
	}

	if (source->priv->streaming_artist != NULL) {
		GValue value = {0,};

		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, source->priv->streaming_artist);
		rb_string_value_map_set (data, RHYTHMDB_PROP_STREAM_SONG_ARTIST, &value);
		g_value_unset (&value);
	}

	if (source->priv->streaming_album != NULL) {
		GValue value = {0,};

		g_value_init (&value, G_TYPE_STRING);
		g_value_set_string (&value, source->priv->streaming_album);
		rb_string_value_map_set (data, RHYTHMDB_PROP_STREAM_SONG_ALBUM, &value);
		g_value_unset (&value);
	}
}

static gboolean
emit_notification_cb (RBStreamingSource *source)
{
	RBShell *shell;

	source->priv->emit_notify_id = 0;

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	rb_shell_do_notify (shell, FALSE, NULL);
	g_object_unref (shell);

	return FALSE;
}

static void
set_streaming_metadata (RBStreamingSource *source,
			char **field,
			const char *metadata_field,
			const char *value)
{
	GValue v = {0,};

	/* don't do anything if the value isn't changing */
	if (*field != NULL && strcmp (*field, value) == 0) {
		return;
	}

	g_free (*field);
	*field = g_strdup (value);

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, value);
	rhythmdb_emit_entry_extra_metadata_notify (source->priv->db,
						   source->priv->playing_stream,
						   metadata_field,
						   &v);
	g_value_unset (&v);

	if (source->priv->emit_notify_id != 0)
		g_source_remove (source->priv->emit_notify_id);

	source->priv->emit_notify_id = g_timeout_add (STREAMING_METADATA_NOTIFY_TIMEOUT,
						      (GSourceFunc) emit_notification_cb,
						      source);
}

/**
 * rb_streaming_source_set_streaming_title:
 * @source: a #RBStreamingSource
 * @title: the new streaming song title
 *
 * Updates the streaming song title.  Call this when an updated
 * streaming song title is received from the stream.
 */
void
rb_streaming_source_set_streaming_title (RBStreamingSource *source,
					 const char *title)
{
	rb_debug ("streaming title: \"%s\"", title);
	set_streaming_metadata (source,
				&source->priv->streaming_title,
				RHYTHMDB_PROP_STREAM_SONG_TITLE,
				title);
}

/**
 * rb_streaming_source_set_streaming_artist:
 * @source: a #RBStreamingSource
 * @artist: the new streaming song artist name
 *
 * Updates the streaming song artist name.  Call this when an updated
 * streaming song artist name is received from the stream.
 */
void
rb_streaming_source_set_streaming_artist (RBStreamingSource *source,
					  const char *artist)
{
	rb_debug ("streaming artist: \"%s\"", artist);
	set_streaming_metadata (source,
				&source->priv->streaming_artist,
				RHYTHMDB_PROP_STREAM_SONG_ARTIST,
				artist);
}

/**
 * rb_streaming_source_set_streaming_album:
 * @source: a #RBStreamingSource
 * @album: the new streaming song album name
 *
 * Updates the streaming song album name.  Call this when an updated
 * streaming song album name is received from the stream.
 */
void
rb_streaming_source_set_streaming_album (RBStreamingSource *source,
					 const char *album)
{
	rb_debug ("streaming album: \"%s\"", album);
	set_streaming_metadata (source,
				&source->priv->streaming_album,
				RHYTHMDB_PROP_STREAM_SONG_ALBUM,
				album);
}


static void
playing_entry_changed_cb (RBShellPlayer *player,
			  RhythmDBEntry *entry,
			  RBStreamingSource *source)
{
	GObject *backend;

	if (source->priv->playing_stream == entry)
		return;

	g_free (source->priv->streaming_title);
	g_free (source->priv->streaming_artist);
	g_free (source->priv->streaming_album);
	source->priv->streaming_title = NULL;
	source->priv->streaming_artist = NULL;
	source->priv->streaming_album = NULL;

	if (source->priv->playing_stream != NULL) {
		rb_source_update_play_statistics (RB_SOURCE (source),
						  source->priv->db,
						  source->priv->playing_stream);

		rhythmdb_entry_unref (source->priv->playing_stream);
		source->priv->playing_stream = NULL;
	}

	g_object_get (source->priv->player, "player", &backend, NULL);

	if (check_entry_type (source, entry) == FALSE) {
		source->priv->buffering = 0;
		if (source->priv->buffering_id) {
			g_signal_handler_disconnect (backend,
						     source->priv->buffering_id);
			source->priv->buffering_id = 0;

			rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));
		}
	} else {
		rb_debug ("playing new stream; resetting buffering");
		if (source->priv->buffering_id == 0) {
			source->priv->buffering_id =
				g_signal_connect_object (backend, "buffering",
							 G_CALLBACK (buffering_cb),
							 source, 0);
		}

		source->priv->buffering = -1;

		source->priv->playing_stream = rhythmdb_entry_ref (entry);
		rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));
	}

	g_object_unref (backend);
}
