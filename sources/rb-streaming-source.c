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

/*
 * base class for streaming sources (internet radio, last.fm streams)
 * provides handling of:
 *  - buffering signals (done)
 *  - streaming song metadata (done)
 *  - possibly updating play count etc.?
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-streaming-source.h"

#include "rhythmdb-query-model.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-metadata.h"

#define STREAMING_METADATA_NOTIFY_TIMEOUT	350

static void rb_streaming_source_class_init (RBStreamingSourceClass *klass);
static void rb_streaming_source_init (RBStreamingSource *source);
static GObject *rb_streaming_source_constructor (GType type, guint n_construct_properties,
						 GObjectConstructParam *construct_properties);
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
				      GHashTable *data,
				      RBStreamingSource *source);

struct RBStreamingSourcePrivate
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
	object_class->constructor = rb_streaming_source_constructor;

	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_true_function;

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

static GObject *
rb_streaming_source_constructor (GType type,
			      guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBStreamingSource *source;
	RBStreamingSourceClass *klass;
	RBShell *shell;

	klass = RB_STREAMING_SOURCE_CLASS (g_type_class_peek (RB_TYPE_STREAMING_SOURCE));

	source = RB_STREAMING_SOURCE (G_OBJECT_CLASS (rb_streaming_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

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

	return G_OBJECT (source);
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_RETRY;
}

void
rb_streaming_source_get_progress (RBStreamingSource *source, char **text, float *progress)
{
	*progress = 0.0;
	if (source->priv->buffering == -1) {
		*text = g_strdup (_("Connecting"));
	} else if (source->priv->buffering > 0) {
		*progress = ((float)source->priv->buffering)/100;
		*text = g_strdup (_("Buffering"));
	}
}

static void
buffering_cb (GObject *backend, guint progress, RBStreamingSource *source)
{
	if (progress == 0)
		return;

	if (progress == 100)
		progress = 0;

	GDK_THREADS_ENTER ();
	source->priv->buffering = progress;
	rb_source_notify_status_changed (RB_SOURCE (source));
	GDK_THREADS_LEAVE ();
}

static gboolean
check_entry_type (RBStreamingSource *source, RhythmDBEntry *entry)
{
	RhythmDBEntryType entry_type;
	gboolean matches = FALSE;

	g_object_get (source, "entry-type", &entry_type, NULL);
	if (entry != NULL && rhythmdb_entry_get_entry_type (entry) == entry_type)
		matches = TRUE;
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

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
			  GHashTable *data,
			  RBStreamingSource *source)
{
	/* our extra metadata only applies to the playing entry */
	if (entry != rb_shell_player_get_playing_entry (source->priv->player) ||
	    check_entry_type (source, entry) == FALSE)
		return;

	if (source->priv->streaming_title != NULL) {
		GValue *value;

		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, source->priv->streaming_title);
		g_hash_table_insert (data, g_strdup (RHYTHMDB_PROP_STREAM_SONG_TITLE), value);
	}

	if (source->priv->streaming_artist != NULL) {
		GValue *value;

		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, source->priv->streaming_artist);
		g_hash_table_insert (data, g_strdup (RHYTHMDB_PROP_STREAM_SONG_ARTIST), value);
	}

	if (source->priv->streaming_album != NULL) {
		GValue *value;

		value = g_new0 (GValue, 1);
		g_value_init (value, G_TYPE_STRING);
		g_value_set_string (value, source->priv->streaming_album);
		g_hash_table_insert (data, g_strdup (RHYTHMDB_PROP_STREAM_SONG_ALBUM), value);
	}
}

static gboolean
emit_notification_cb (RBStreamingSource *source)
{
	RBShell *shell;

	source->priv->emit_notify_id = 0;

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	rb_shell_notify_playing_entry (shell,
				       source->priv->playing_stream,
				       FALSE);
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

			rb_source_notify_status_changed (RB_SOURCE (source));
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
		rb_source_notify_status_changed (RB_SOURCE (source));
	}

	g_object_unref (backend);
}

