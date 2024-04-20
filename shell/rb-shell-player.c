/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002, 2003 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2002,2003 Colin Walters <walters@debian.org>
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
 * SECTION:rbshellplayer
 * @short_description: playback state management
 *
 * The shell player (or player shell, depending on who you're talking to)
 * manages the #RBPlayer instance, tracks the current playing #RhythmDBEntry,
 * and manages the various #RBPlayOrder instances.  It provides simple operations
 * such as next, previous, play/pause, and seek.
 *
 * When playing internet radio streams, it first attempts to read the stream URL
 * as a playlist.  If this succeeds, the URLs from the playlist are stored in a
 * list and tried in turn in case of errors.  If the playlist parsing fails, the
 * stream URL is played directly.
 *
 * The mapping from the separate shuffle and repeat settings to an #RBPlayOrder
 * instance occurs in here.  The play order logic can also support a number of
 * additional play orders not accessible via the shuffle and repeat buttons.
 *
 * If the player backend supports multiple streams, the shell player crossfades
 * between streams by watching the elapsed time of the current stream and simulating
 * an end-of-stream event when it gets within the crossfade duration of the actual
 * end.
 */

#include "config.h"

#include <unistd.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-application.h"
#include "rb-property-view.h"
#include "rb-shell-player.h"
#include "rb-builder-helpers.h"
#include "rb-file-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-player.h"
#include "rb-header.h"
#include "totem-pl-parser.h"
#include "rb-metadata.h"
#include "rb-library-source.h"
#include "rb-util.h"
#include "rb-play-order.h"
#include "rb-playlist-source.h"
#include "rb-play-queue-source.h"
#include "rhythmdb.h"
#include "rb-podcast-manager.h"
#include "rb-missing-plugins.h"
#include "rb-ext-db.h"

/* Play Orders */
#include "rb-play-order-linear.h"
#include "rb-play-order-linear-loop.h"
#include "rb-play-order-shuffle.h"
#include "rb-play-order-random-equal-weights.h"
#include "rb-play-order-random-by-age.h"
#include "rb-play-order-random-by-rating.h"
#include "rb-play-order-random-by-age-and-rating.h"
#include "rb-play-order-queue.h"

static const char* const state_to_play_order[2][2] =
	{{"linear",	"linear-loop"},
	 {"shuffle",	"random-by-age-and-rating"}};

static void rb_shell_player_class_init (RBShellPlayerClass *klass);
static void rb_shell_player_init (RBShellPlayer *shell_player);
static void rb_shell_player_constructed (GObject *object);
static void rb_shell_player_dispose (GObject *object);
static void rb_shell_player_finalize (GObject *object);
static void rb_shell_player_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_shell_player_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
							 RBSource *source,
							 gboolean sync_entry_view);
static void rb_shell_player_sync_with_source (RBShellPlayer *player);
static void rb_shell_player_sync_with_selected_source (RBShellPlayer *player);
static void rb_shell_player_entry_changed_cb (RhythmDB *db,
					      RhythmDBEntry *entry,
					      GPtrArray *changes,
					      RBShellPlayer *player);

static void rb_shell_player_entry_activated_cb (RBEntryView *view,
						RhythmDBEntry *entry,
						RBShellPlayer *player);
static void rb_shell_player_property_row_activated_cb (RBPropertyView *view,
						       const char *name,
						       RBShellPlayer *player);
static void rb_shell_player_sync_volume (RBShellPlayer *player, gboolean notify, gboolean set_volume);
static void tick_cb (RBPlayer *player, RhythmDBEntry *entry, gint64 elapsed, gint64 duration, gpointer data);
static void error_cb (RBPlayer *player, RhythmDBEntry *entry, const GError *err, gpointer data);
static void missing_plugins_cb (RBPlayer *player, RhythmDBEntry *entry, const char **details, const char **descriptions, RBShellPlayer *sp);
static void playing_stream_cb (RBPlayer *player, RhythmDBEntry *entry, RBShellPlayer *shell_player);
static void player_image_cb (RBPlayer *player, RhythmDBEntry *entry, GdkPixbuf *image, RBShellPlayer *shell_player);
static void rb_shell_player_error (RBShellPlayer *player, gboolean async, const GError *err);
static void rb_shell_player_error_idle (RBShellPlayer *player, gboolean async, const GError *err);

static void rb_shell_player_play_order_update_cb (RBPlayOrder *porder,
						  gboolean has_next,
						  gboolean has_previous,
						  RBShellPlayer *player);

static void rb_shell_player_sync_play_order (RBShellPlayer *player);
static void rb_shell_player_sync_control_state (RBShellPlayer *player);
static void rb_shell_player_sync_buttons (RBShellPlayer *player);

static void player_settings_changed_cb (GSettings *settings,
					const char *key,
					RBShellPlayer *player);
static void rb_shell_player_extra_metadata_cb (RhythmDB *db,
					       RhythmDBEntry *entry,
					       const char *field,
					       GValue *metadata,
					       RBShellPlayer *player);

static gboolean rb_shell_player_open_location (RBShellPlayer *player,
					       RhythmDBEntry *entry,
					       RBPlayerPlayType play_type,
					       GError **error);
static gboolean rb_shell_player_do_next_internal (RBShellPlayer *player,
						  gboolean from_eos,
						  gboolean allow_stop,
						  GError **error);
static void rb_shell_player_slider_dragging_cb (GObject *header,
						GParamSpec *pspec,
						RBShellPlayer *player);
static void rb_shell_player_volume_changed_cb (RBPlayer *player,
					       float volume,
					       RBShellPlayer *shell_player);



typedef struct {
	/* Value of the state/play-order setting */
	char *name;
	/* Contents of the play order dropdown; should be gettext()ed before use. */
	char *description;
	/* the play order's gtype id */
	GType order_type;
	/* TRUE if the play order should appear in the dropdown */
	gboolean is_in_dropdown;
} RBPlayOrderDescription;

static void _play_order_description_free (RBPlayOrderDescription *order);

static RBPlayOrder* rb_play_order_new (RBShellPlayer *player, const char* porder_name);

/* number of nanoseconds before the end of a track to start prerolling the next */
#define PREROLL_TIME		RB_PLAYER_SECOND

struct RBShellPlayerPrivate
{
	RhythmDB *db;

	gboolean syncing_state;
	gboolean queue_only;

	RBSource *selected_source;
	RBSource *source;
	RBPlayQueueSource *queue_source;
	RBSource *current_playing_source;

	GHashTable *play_orders; /* char* -> RBPlayOrderDescription* map */

	gboolean did_retry;
	GTimeVal last_retry;

	gboolean handling_error;

	RBPlayer *mmplayer;

	guint elapsed;
	gint64 track_transition_time;
	RhythmDBEntry *playing_entry;
	gboolean playing_entry_eos;

	RBPlayOrder *play_order;
	RBPlayOrder *queue_play_order;

	GQueue *playlist_urls;
	GCancellable *parser_cancellable;

	RBHeader *header_widget;

	GSettings *settings;
	GSettings *ui_settings;

	gboolean has_prev;
	gboolean has_next;
	gboolean mute;
	float volume;

	guint do_next_idle_id;
	GMutex error_idle_mutex;
	guint error_idle_id;
};

#define RB_SHELL_PLAYER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SHELL_PLAYER, RBShellPlayerPrivate))

enum
{
	PROP_0,
	PROP_SOURCE,
	PROP_DB,
	PROP_UI_MANAGER,
	PROP_ACTION_GROUP,
	PROP_PLAY_ORDER,
	PROP_PLAYING,
	PROP_VOLUME,
	PROP_HEADER,
	PROP_QUEUE_SOURCE,
	PROP_QUEUE_ONLY,
	PROP_PLAYING_FROM_QUEUE,
	PROP_PLAYER,
	PROP_MUTE,
	PROP_HAS_NEXT,
	PROP_HAS_PREV
};

enum
{
	WINDOW_TITLE_CHANGED,
	ELAPSED_CHANGED,
	PLAYING_SOURCE_CHANGED,
	PLAYING_CHANGED,
	PLAYING_SONG_CHANGED,
	PLAYING_URI_CHANGED,
	PLAYING_SONG_PROPERTY_CHANGED,
	ELAPSED_NANO_CHANGED,
	LAST_SIGNAL
};


static guint rb_shell_player_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBShellPlayer, rb_shell_player, G_TYPE_OBJECT)

static void
volume_pre_unmount_cb (GVolumeMonitor *monitor,
		       GMount *mount,
		       RBShellPlayer *player)
{
	const char *entry_mount_point;
	GFile *mount_root;
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (player);
	if (entry == NULL) {
		return;
	}

	entry_mount_point = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (entry_mount_point == NULL) {
		return;
	}

	mount_root = g_mount_get_root (mount);
	if (mount_root != NULL) {
		char *mount_point;
		
		mount_point = g_file_get_uri (mount_root);
		if (mount_point && entry_mount_point &&
		    strcmp (entry_mount_point, mount_point) == 0) {
			rb_shell_player_stop (player);
		}

		g_free (mount_point);
		g_object_unref (mount_root);
	}

	rhythmdb_entry_unref (entry);
}

static void
reemit_playing_signal (RBShellPlayer *player,
		       GParamSpec *pspec,
		       gpointer data)
{
	g_signal_emit (player, rb_shell_player_signals[PLAYING_CHANGED], 0,
		       rb_player_playing (player->priv->mmplayer));
}

static void
rb_shell_player_open_playlist_url (RBShellPlayer *player,
				   const char *location,
				   RhythmDBEntry *entry,
				   RBPlayerPlayType play_type)
{
	GError *error = NULL;

	rb_debug ("playing stream url %s", location);
	rb_player_open (player->priv->mmplayer,
			location,
			rhythmdb_entry_ref (entry),
			(GDestroyNotify) rhythmdb_entry_unref,
			&error);
	if (error == NULL)
		rb_player_play (player->priv->mmplayer, play_type, player->priv->track_transition_time, &error);

	if (error) {
		rb_shell_player_error_idle (player, TRUE, error);
		g_error_free (error);
	}
}

static void
rb_shell_player_handle_eos_unlocked (RBShellPlayer *player, RhythmDBEntry *entry, gboolean allow_stop)
{
	RBSource *source;
	gboolean update_stats;
	gboolean dragging;

	source = player->priv->current_playing_source;

	/* nothing to do */
	if (source == NULL) {
		return;
	}

	if (player->priv->playing_entry_eos) {
		rb_debug ("playing entry has already EOS'd");
		return;
	}

	if (entry != NULL) {
		if (player->priv->playing_entry != entry) {
			rb_debug ("EOS'd entry is not the current playing entry; ignoring");
			return;
		}

		rhythmdb_entry_ref (entry);
	}

	/* defer EOS handling while the position slider is being dragged */
	g_object_get (player->priv->header_widget, "slider-dragging", &dragging, NULL);
	if (dragging) {
		rb_debug ("slider is dragging, will handle EOS (if applicable) on release");
		player->priv->playing_entry_eos = TRUE;
		if (entry != NULL)
			rhythmdb_entry_unref (entry);
		return;
	}

	update_stats = FALSE;
	switch (rb_source_handle_eos (source)) {
	case RB_SOURCE_EOF_ERROR:
		if (allow_stop) {
			rb_error_dialog (NULL, _("Stream error"),
					 _("Unexpected end of stream!"));
			rb_shell_player_stop (player);
			player->priv->playing_entry_eos = TRUE;
			update_stats = TRUE;
		}
		break;
	case RB_SOURCE_EOF_STOP:
		if (allow_stop) {
			rb_shell_player_stop (player);
			player->priv->playing_entry_eos = TRUE;
			update_stats = TRUE;
		}
		break;
	case RB_SOURCE_EOF_RETRY: {
		GTimeVal current;
		gint diff;

		g_get_current_time (&current);
		diff = current.tv_sec - player->priv->last_retry.tv_sec;
		player->priv->last_retry = current;

		if (rb_source_try_playlist (source) &&
		    !g_queue_is_empty (player->priv->playlist_urls)) {
			char *location = g_queue_pop_head (player->priv->playlist_urls);
			rb_debug ("trying next radio stream url: %s", location);

			/* we're handling an unexpected EOS here, so crossfading isn't
			 * really possible anyway -> specify FALSE.
			 */
			rb_shell_player_open_playlist_url (player, location, entry, FALSE);
			g_free (location);
			break;
		}

		if (allow_stop) {
			if (diff < 4) {
				rb_debug ("Last retry was less than 4 seconds ago...aborting retry playback");
				rb_shell_player_stop (player);
			} else {
				rb_shell_player_play_entry (player, entry, NULL);
			}
			player->priv->playing_entry_eos = TRUE;
			update_stats = TRUE;
		}
	}
		break;
	case RB_SOURCE_EOF_NEXT:
		{
			GError *error = NULL;

			player->priv->playing_entry_eos = TRUE;
			update_stats = TRUE;
			if (!rb_shell_player_do_next_internal (player, TRUE, allow_stop, &error)) {
				if (error->domain != RB_SHELL_PLAYER_ERROR ||
				    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST) {
					g_warning ("Unhandled error: %s", error->message);
				} else if (allow_stop == FALSE) {
					/* handle the real EOS when it happens */
					player->priv->playing_entry_eos = FALSE;
					update_stats = FALSE;
				}
			}
		}
		break;
	}

	if (update_stats &&
	    rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR) == NULL) {
		rb_debug ("updating play statistics");
		rb_source_update_play_statistics (source,
						  player->priv->db,
						  entry);
	}

	if (entry != NULL)
		rhythmdb_entry_unref (entry);
}

static void
rb_shell_player_slider_dragging_cb (GObject *header, GParamSpec *pspec, RBShellPlayer *player)
{
	gboolean drag;

	g_object_get (player->priv->header_widget, "slider-dragging", &drag, NULL);
	rb_debug ("slider dragging? %d", drag);

	/* if an EOS occurred while dragging, process it now */
	if (drag == FALSE && player->priv->playing_entry_eos) {
		rb_debug ("processing EOS delayed due to slider dragging");
		player->priv->playing_entry_eos = FALSE;
		rb_shell_player_handle_eos_unlocked (player, rb_shell_player_get_playing_entry (player), FALSE);
	}
}

static void
rb_shell_player_handle_eos (RBPlayer *player,
			    RhythmDBEntry *entry,
			    gboolean early,
			    RBShellPlayer *shell_player)
{
	const char *location;
	if (entry == NULL) {
		/* special case: this is called with entry == NULL to simulate an EOS
		 * from the current playing entry.
		 */
		entry = shell_player->priv->playing_entry;
		if (entry == NULL) {
			rb_debug ("called to simulate EOS for playing entry, but nothing is playing");
			return;
		}
	}

	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	if (entry != shell_player->priv->playing_entry) {
		rb_debug ("got unexpected eos for %s", location);
	} else {
		rb_debug ("handling eos for %s", location);
		/* don't allow playback to be stopped on early EOS notifications */
		rb_shell_player_handle_eos_unlocked (shell_player, entry, (early == FALSE));
	}
}


static void
rb_shell_player_handle_redirect (RBPlayer *player,
				 RhythmDBEntry *entry,
				 const gchar *uri,
				 RBShellPlayer *shell_player)
{
	GValue val = { 0 };

	rb_debug ("redirect to %s", uri);

	/* Stop existing stream */
	rb_player_close (shell_player->priv->mmplayer, NULL, NULL);

	/* Update entry */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, uri);
	rhythmdb_entry_set (shell_player->priv->db, entry, RHYTHMDB_PROP_LOCATION, &val);
	g_value_unset (&val);
	rhythmdb_commit (shell_player->priv->db);

	/* Play new URI */
	rb_shell_player_open_location (shell_player, entry, RB_PLAYER_PLAY_REPLACE, NULL);
}


GQuark
rb_shell_player_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_shell_player_error");

	return quark;
}

/**
 * rb_shell_player_set_selected_source:
 * @player: the #RBShellPlayer
 * @source: the #RBSource to select
 *
 * Updates the player to reflect a new source being selected.
 */
void
rb_shell_player_set_selected_source (RBShellPlayer *player,
				     RBSource *source)
{
	g_return_if_fail (RB_IS_SHELL_PLAYER (player));
	g_return_if_fail (source == NULL || RB_IS_SOURCE (source));

	g_object_set (player, "source", source, NULL);
}

/**
 * rb_shell_player_get_playing_source:
 * @player: the #RBShellPlayer
 *
 * Retrieves the current playing source.  That is, the source from
 * which the current song was drawn.  This differs from 
 * #rb_shell_player_get_active_source when the current song came
 * from the play queue.
 *
 * Return value: (transfer none): the current playing #RBSource
 */
RBSource *
rb_shell_player_get_playing_source (RBShellPlayer *player)
{
	return player->priv->current_playing_source;
}

/**
 * rb_shell_player_get_active_source:
 * @player: the #RBShellPlayer
 *
 * Retrieves the active source.  This is the source that the user
 * selected for playback.
 *
 * Return value: (transfer none): the active #RBSource
 */
RBSource *
rb_shell_player_get_active_source (RBShellPlayer *player)
{
	return player->priv->source;
}

/**
 * rb_shell_player_get_playing_entry:
 * @player: the #RBShellPlayer
 *
 * Retrieves the currently playing #RhythmDBEntry, or NULL if
 * nothing is playing.  The caller must unref the entry
 * (using #rhythmdb_entry_unref) when it is no longer needed.
 *
 * Return value: (transfer full) (allow-none): the currently playing #RhythmDBEntry, or NULL
 */
RhythmDBEntry *
rb_shell_player_get_playing_entry (RBShellPlayer *player)
{
	RBPlayOrder *porder;
	RhythmDBEntry *entry;

	if (player->priv->current_playing_source == NULL) {
		return NULL;
	}

	g_object_get (player->priv->current_playing_source, "play-order", &porder, NULL);
	if (porder == NULL)
		porder = g_object_ref (player->priv->play_order);

	entry = rb_play_order_get_playing_entry (porder);
	g_object_unref (porder);

	return entry;
}

typedef struct {
	RBShellPlayer *player;
	char *location;
	RhythmDBEntry *entry;
	RBPlayerPlayType play_type;
	GCancellable *cancellable;
} OpenLocationThreadData;

static void
playlist_entry_cb (TotemPlParser *playlist,
		   const char *uri,
		   GHashTable *metadata,
		   OpenLocationThreadData *data)
{
	if (g_cancellable_is_cancelled (data->cancellable)) {
		rb_debug ("playlist parser cancelled");
	} else {
		rb_debug ("adding stream url %s (%p)", uri, playlist);
		g_queue_push_tail (data->player->priv->playlist_urls, g_strdup (uri));
	}
}

static gpointer
open_location_thread (OpenLocationThreadData *data)
{
	TotemPlParser *playlist;
	TotemPlParserResult playlist_result;

	playlist = totem_pl_parser_new ();

	g_signal_connect_data (playlist, "entry-parsed",
			       G_CALLBACK (playlist_entry_cb),
			       data, NULL, 0);

	totem_pl_parser_add_ignored_mimetype (playlist, "x-directory/normal");
	totem_pl_parser_add_ignored_mimetype (playlist, "inode/directory");

	playlist_result = totem_pl_parser_parse (playlist, data->location, FALSE);
	g_object_unref (playlist);

	if (g_cancellable_is_cancelled (data->cancellable)) {
		playlist_result = TOTEM_PL_PARSER_RESULT_CANCELLED;
	}

	switch (playlist_result) {
	case TOTEM_PL_PARSER_RESULT_SUCCESS:
		if (g_queue_is_empty (data->player->priv->playlist_urls)) {
			GError *error = g_error_new (RB_SHELL_PLAYER_ERROR,
						     RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
						     _("Playlist was empty"));
			rb_shell_player_error_idle (data->player, TRUE, error);
			g_error_free (error);
		} else {
			char *location;

			location = g_queue_pop_head (data->player->priv->playlist_urls);
			rb_debug ("playing first stream url %s", location);
			rb_shell_player_open_playlist_url (data->player, location, data->entry, data->play_type);
			g_free (location);
		}
		break;

	case TOTEM_PL_PARSER_RESULT_CANCELLED:
		rb_debug ("playlist parser was cancelled");
		break;

	default:
		/* if we can't parse it as a playlist, just try playing it */
		rb_debug ("playlist parser failed, playing %s directly", data->location);
		rb_shell_player_open_playlist_url (data->player, data->location, data->entry, data->play_type);
		break;
	}

	g_object_unref (data->cancellable);
	g_free (data);
	return NULL;
}

static gboolean
rb_shell_player_open_location (RBShellPlayer *player,
			       RhythmDBEntry *entry,
			       RBPlayerPlayType play_type,
			       GError **error)
{
	char *location;
	gboolean ret = TRUE;

	/* dispose of any existing playlist urls */
	if (player->priv->playlist_urls) {
		g_queue_foreach (player->priv->playlist_urls,
				 (GFunc) g_free,
				 NULL);
		g_queue_free (player->priv->playlist_urls);
		player->priv->playlist_urls = NULL;
	}
	if (rb_source_try_playlist (player->priv->source)) {
		player->priv->playlist_urls = g_queue_new ();
	}

	location = rhythmdb_entry_get_playback_uri (entry);
	if (location == NULL) {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_PLAYABLE,
			     _("This item is not playable"));
		return FALSE;
	}

	if (rb_source_try_playlist (player->priv->source)) {
		OpenLocationThreadData *data;

		data = g_new0 (OpenLocationThreadData, 1);
		data->player = player;
		data->play_type = play_type;
		data->entry = entry;

		/* add http:// as a prefix, if it doesn't have a URI scheme */
		if (strstr (location, "://"))
			data->location = g_strdup (location);
		else
			data->location = g_strconcat ("http://", location, NULL);

		if (player->priv->parser_cancellable == NULL) {
			player->priv->parser_cancellable = g_cancellable_new ();
		}
		data->cancellable = g_object_ref (player->priv->parser_cancellable);

		g_thread_new ("open-location", (GThreadFunc)open_location_thread, data);
	} else {
		if (player->priv->parser_cancellable != NULL) {
			g_object_unref (player->priv->parser_cancellable);
			player->priv->parser_cancellable = NULL;
		}

		rhythmdb_entry_ref (entry);
		ret = ret && rb_player_open (player->priv->mmplayer, location, entry, (GDestroyNotify) rhythmdb_entry_unref, error);

		ret = ret && rb_player_play (player->priv->mmplayer, play_type, player->priv->track_transition_time, error);
	}

	g_free (location);
	return ret;
}

/**
 * rb_shell_player_play:
 * @player: a #RBShellPlayer
 * @error: error return
 *
 * Starts playback, if it is not already playing.
 *
 * Return value: whether playback is now occurring (TRUE when successfully started
 * or already playing).
 **/
gboolean
rb_shell_player_play (RBShellPlayer *player,
		      GError **error)
{
	RBEntryView *songs;

	if (player->priv->current_playing_source == NULL) {
		rb_debug ("current playing source is NULL");
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
			     "Current playing source is NULL");
		return FALSE;
	}

	if (rb_player_playing (player->priv->mmplayer))
		return TRUE;

	if (player->priv->parser_cancellable != NULL) {
		rb_debug ("currently parsing a playlist");
		return TRUE;
	}

	/* we're obviously not playing anything, so crossfading is irrelevant */
	if (!rb_player_play (player->priv->mmplayer, RB_PLAYER_PLAY_REPLACE, 0.0f, error)) {
		rb_debug ("player doesn't want to");
		return FALSE;
	}

	songs = rb_source_get_entry_view (player->priv->current_playing_source);
	if (songs)
		rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PLAYING);

	return TRUE;
}

static void
rb_shell_player_set_entry_playback_error (RBShellPlayer *player,
					  RhythmDBEntry *entry,
					  char *message)
{
	GValue value = { 0, };

	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, message);
	rhythmdb_entry_set (player->priv->db,
			    entry,
			    RHYTHMDB_PROP_PLAYBACK_ERROR,
			    &value);
	g_value_unset (&value);
	rhythmdb_commit (player->priv->db);
}

static gboolean
rb_shell_player_set_playing_entry (RBShellPlayer *player,
				   RhythmDBEntry *entry,
				   gboolean out_of_order,
				   gboolean wait_for_eos,
				   GError **error)
{
	GError *tmp_error = NULL;
	GValue val = {0,};
	RBPlayerPlayType play_type;

	g_return_val_if_fail (player->priv->current_playing_source != NULL, TRUE);
	g_return_val_if_fail (entry != NULL, TRUE);

	play_type = wait_for_eos ? RB_PLAYER_PLAY_AFTER_EOS : RB_PLAYER_PLAY_REPLACE;

	if (out_of_order) {
		RBPlayOrder *porder;

		g_object_get (player->priv->current_playing_source, "play-order", &porder, NULL);
		if (porder == NULL)
			porder = g_object_ref (player->priv->play_order);
		rb_play_order_set_playing_entry (porder, entry);
		g_object_unref (porder);
	}

	if (player->priv->playing_entry != NULL &&
	    player->priv->track_transition_time > 0) {
		const char *previous_album;
		const char *album;

		previous_album = rhythmdb_entry_get_string (player->priv->playing_entry, RHYTHMDB_PROP_ALBUM);
		album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
		/* only crossfade if we're not going from the end of one song on an
		 * album to the start of another.  "Unknown" doesn't count as an album.
		 */
		if (wait_for_eos == FALSE ||
		    strcmp (album, _("Unknown")) == 0 ||
		    strcmp (album, previous_album) != 0) {
			play_type = RB_PLAYER_PLAY_CROSSFADE;
		}
	}

	if (rb_shell_player_open_location (player, entry, play_type, &tmp_error) == FALSE) {
		goto lose;
	}

	rb_debug ("Success!");
	/* clear error on successful playback */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, NULL);
	rhythmdb_entry_set (player->priv->db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
	rhythmdb_commit (player->priv->db);
	g_value_unset (&val);

	return TRUE;
 lose:
	/* Ignore errors, shutdown the player */
	rb_player_close (player->priv->mmplayer, NULL /* XXX specify uri? */, NULL);

	if (tmp_error == NULL) {
		tmp_error = g_error_new (RB_SHELL_PLAYER_ERROR,
					 RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
					 "Problem occurred without error being set. "
					 "This is a bug in Rhythmbox or GStreamer.");
	}
	/* Mark this song as failed */
	rb_shell_player_set_entry_playback_error (player, entry, tmp_error->message);
	g_propagate_error (error, tmp_error);

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");

	return FALSE;
}

static void
player_settings_changed_cb (GSettings *settings, const char *key, RBShellPlayer *player)
{
	if (g_strcmp0 (key, "play-order") == 0) {
		rb_debug ("play order setting changed");
		player->priv->syncing_state = TRUE;
		rb_shell_player_sync_play_order (player);
		rb_shell_player_sync_buttons (player);
		rb_shell_player_sync_control_state (player);
		g_object_notify (G_OBJECT (player), "play-order");
		player->priv->syncing_state = FALSE;
	} else if (g_strcmp0 (key, "transition-time") == 0) {
		double newtime;
		rb_debug ("track transition time changed");
		newtime = g_settings_get_double (player->priv->settings, "transition-time");
		player->priv->track_transition_time = newtime * RB_PLAYER_SECOND;
	}
}

/**
 * rb_shell_player_get_playback_state:
 * @player: the #RBShellPlayer
 * @shuffle: (out): returns the current shuffle setting
 * @repeat: (out): returns the current repeat setting
 *
 * Retrieves the current state of the shuffle and repeat settings.
 *
 * Return value: %TRUE if successful.
 */
gboolean
rb_shell_player_get_playback_state (RBShellPlayer *player,
				    gboolean *shuffle,
				    gboolean *repeat)
{
	int i, j;
	char *play_order;

	play_order = g_settings_get_string (player->priv->settings, "play-order");
	for (i = 0; i < G_N_ELEMENTS(state_to_play_order); i++)
		for (j = 0; j < G_N_ELEMENTS(state_to_play_order[0]); j++)
			if (!strcmp (play_order, state_to_play_order[i][j]))
				goto found;

	g_free (play_order);
	return FALSE;

found:
	if (shuffle != NULL) {
		*shuffle = i > 0;
	}
	if (repeat != NULL) {
		*repeat = j > 0;
	}
	g_free (play_order);
	return TRUE;
}

/**
 * rb_shell_player_set_playback_state:
 * @player: the #RBShellPlayer
 * @shuffle: whether to enable the shuffle setting
 * @repeat: whether to enable the repeat setting
 *
 * Sets the state of the shuffle and repeat settings.
 */
void
rb_shell_player_set_playback_state (RBShellPlayer *player,
				    gboolean shuffle,
				    gboolean repeat)
{
	const char *neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	g_settings_set_string (player->priv->settings, "play-order", neworder);
}

static void
rb_shell_player_sync_play_order (RBShellPlayer *player)
{
	char *new_play_order;
	RhythmDBEntry *playing_entry = NULL;
	RBSource *source;

	new_play_order = g_settings_get_string (player->priv->settings, "play-order");
	if (player->priv->play_order != NULL) {
		playing_entry = rb_play_order_get_playing_entry (player->priv->play_order);
		g_signal_handlers_disconnect_by_func (player->priv->play_order,
						      G_CALLBACK (rb_shell_player_play_order_update_cb),
						      player);
		g_object_unref (player->priv->play_order);
	}

	player->priv->play_order = rb_play_order_new (player, new_play_order);

	g_signal_connect_object (player->priv->play_order,
				 "have_next_previous_changed",
				 G_CALLBACK (rb_shell_player_play_order_update_cb),
				 player, 0);
	rb_shell_player_play_order_update_cb (player->priv->play_order,
					      FALSE, FALSE,
					      player);

	source = player->priv->current_playing_source;
	if (source == NULL) {
		source = player->priv->selected_source;
	}
	rb_play_order_playing_source_changed (player->priv->play_order, source);

	if (playing_entry != NULL) {
		rb_play_order_set_playing_entry (player->priv->play_order, playing_entry);
		rhythmdb_entry_unref (playing_entry);
	}

	g_free (new_play_order);
}

static void
rb_shell_player_play_order_update_cb (RBPlayOrder *porder,
				      gboolean _has_next,
				      gboolean _has_previous,
				      RBShellPlayer *player)
{
	/* we cannot depend on the values of has_next, has_previous or porder
	 * since this can be called for the main porder, queue porder, etc
	 */
	gboolean has_next = FALSE;
	gboolean has_prev = FALSE;
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (player);
	if (entry != NULL) {
		has_next = TRUE;
		has_prev = TRUE;
		rhythmdb_entry_unref (entry);
	} else {
		if (player->priv->current_playing_source &&
		    (rb_source_handle_eos (player->priv->current_playing_source) == RB_SOURCE_EOF_NEXT)) {
			RBPlayOrder *porder;
			g_object_get (player->priv->current_playing_source, "play-order", &porder, NULL);
			if (porder == NULL)
				porder = g_object_ref (player->priv->play_order);
			has_next = rb_play_order_has_next (porder);
			g_object_unref (porder);
		}
		if (player->priv->queue_play_order) {
			has_next |= rb_play_order_has_next (player->priv->queue_play_order);
		}
		has_prev = (player->priv->current_playing_source != NULL);
	}

	if (has_prev != player->priv->has_prev) {
		player->priv->has_prev = has_prev;
		g_object_notify (G_OBJECT (player), "has-prev");
	}
	if (has_next != player->priv->has_next) {
		player->priv->has_next = has_next;
		g_object_notify (G_OBJECT (player), "has-next");
	}
}

static void
swap_playing_source (RBShellPlayer *player,
		     RBSource *new_source)
{
	if (player->priv->current_playing_source != NULL) {
		RBEntryView *old_songs = rb_source_get_entry_view (player->priv->current_playing_source);
		if (old_songs)
			rb_entry_view_set_state (old_songs, RB_ENTRY_VIEW_NOT_PLAYING);
	}
	if (new_source != NULL) {
		RBEntryView *new_songs = rb_source_get_entry_view (new_source);

		if (new_songs) {
			rb_entry_view_set_state (new_songs, RB_ENTRY_VIEW_PLAYING);
			rb_shell_player_set_playing_source (player, new_source);
		}
	}
}

/**
 * rb_shell_player_do_previous:
 * @player: the #RBShellPlayer
 * @error: returns any error information
 *
 * If the current song has been playing for more than 3 seconds,
 * restarts it, otherwise, goes back to the previous song.
 * Fails if there is no current song, or if inside the first
 * 3 seconds of the first song in the play order.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_do_previous (RBShellPlayer *player,
			     GError **error)
{
	RhythmDBEntry *entry = NULL;
	RBSource *new_source;

	if (player->priv->current_playing_source == NULL) {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_PLAYING,
			     _("Not currently playing"));
		return FALSE;
	}

	/* If we're in the first 3 seconds go to the previous song,
	 * else restart the current one.
	 */
	if (player->priv->current_playing_source != NULL
	    && rb_source_can_pause (player->priv->current_playing_source)
	    && rb_player_get_time (player->priv->mmplayer) > (G_GINT64_CONSTANT (3) * RB_PLAYER_SECOND)) {
		rb_debug ("after 3 second previous, restarting song");
		rb_player_set_time (player->priv->mmplayer, 0);
		rb_shell_player_sync_with_source (player);
		return TRUE;
	}

	rb_debug ("going to previous");

	/* hrm, does this actually do anything at all? */
	if (player->priv->queue_play_order) {
		entry = rb_play_order_get_previous (player->priv->queue_play_order);
		if (entry != NULL) {
			new_source = RB_SOURCE (player->priv->queue_source);
			rb_play_order_go_previous (player->priv->queue_play_order);
		}
	}

	if (entry == NULL) {
		RBPlayOrder *porder;

		new_source = player->priv->source;
		g_object_get (new_source, "play-order", &porder, NULL);
		if (porder == NULL)
			porder = g_object_ref (player->priv->play_order);

		entry = rb_play_order_get_previous (porder);
		if (entry)
			rb_play_order_go_previous (porder);
		g_object_unref (porder);
	}

	if (entry != NULL) {
		rb_debug ("previous song found, doing previous");
		if (new_source != player->priv->current_playing_source)
			swap_playing_source (player, new_source);

		if (!rb_shell_player_set_playing_entry (player, entry, FALSE, FALSE, error)) {
			rhythmdb_entry_unref (entry);
			return FALSE;
		}

		rhythmdb_entry_unref (entry);
	} else {
		rb_debug ("no previous song found, signalling error");
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
			     _("No previous song"));
		rb_shell_player_stop (player);
		return FALSE;
	}

	return TRUE;
}

static gboolean
rb_shell_player_do_next_internal (RBShellPlayer *player, gboolean from_eos, gboolean allow_stop, GError **error)
{
	RBSource *new_source = NULL;
	RhythmDBEntry *entry = NULL;
	gboolean rv = TRUE;

	if (player->priv->source == NULL)
		return TRUE;


	/* try the current playing source's play order, if it has one */
	if (player->priv->current_playing_source != NULL) {
		RBPlayOrder *porder;
		g_object_get (player->priv->current_playing_source, "play-order", &porder, NULL);
		if (porder != NULL) {
			entry = rb_play_order_get_next (porder);
			if (entry != NULL) {
				rb_play_order_go_next (porder);
				new_source = player->priv->current_playing_source;
			}
			g_object_unref (porder);
		}
	}

	/* if that's different to the playing source that the user selected
	 * (ie we're playing from the queue), try that too
	 */
	if (entry == NULL) {
		RBPlayOrder *porder;
		g_object_get (player->priv->source, "play-order", &porder, NULL);
		if (porder == NULL)
			porder = g_object_ref (player->priv->play_order);

		/*
		 * If we interrupted this source to play from something else,
		 * we should go back to whatever it wanted to play before.
		 */
		if (player->priv->source != player->priv->current_playing_source)
			entry = rb_play_order_get_playing_entry (porder);

		/* if that didn't help, advance the play order */
		if (entry == NULL) {
			entry = rb_play_order_get_next (porder);
			if (entry != NULL) {
				rb_debug ("got new entry %s from play order",
					  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
				rb_play_order_go_next (porder);
			}
		}

		if (entry != NULL)
			new_source = player->priv->source;
		
		g_object_unref (porder);
	}

	/* if the new entry isn't from the play queue anyway, let the play queue
	 * override the regular play order.
	 */
	if (player->priv->queue_play_order &&
	    new_source != RB_SOURCE (player->priv->queue_source)) {
		RhythmDBEntry *queue_entry;

		queue_entry = rb_play_order_get_next (player->priv->queue_play_order);
		rb_play_order_go_next (player->priv->queue_play_order);
		if (queue_entry != NULL) {
			rb_debug ("got new entry %s from queue play order",
				  rhythmdb_entry_get_string (queue_entry, RHYTHMDB_PROP_LOCATION));
			if (entry != NULL) {
				rhythmdb_entry_unref (entry);
			}
			entry = queue_entry;
			new_source = RB_SOURCE (player->priv->queue_source);
		} else {
			rb_debug ("didn't get a new entry from queue play order");
		}
	}

	/* play the new entry */
	if (entry != NULL) {
		/* if the entry view containing the playing entry changed, update it */
		if (new_source != player->priv->current_playing_source)
			swap_playing_source (player, new_source);

		if (!rb_shell_player_set_playing_entry (player, entry, FALSE, from_eos, error))
			rv = FALSE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST,
			     _("No next song"));
		rv = FALSE;

		if (allow_stop) {
			rb_debug ("No next entry, stopping playback");

			/* hmm, need to set playing entry on the playing source's
			 * play order if it has one?
			 */

			rb_shell_player_stop (player);
			rb_play_order_set_playing_entry (player->priv->play_order, NULL);
		}
	}

	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}

	return rv;
}

/**
 * rb_shell_player_do_next:
 * @player: the #RBShellPlayer
 * @error: returns error information
 *
 * Skips to the next song.  Consults the play queue and handles
 * transitions between the play queue and the active source.
 * Fails if there is no entry to play after the current one.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_do_next (RBShellPlayer *player,
			 GError **error)
{
	return rb_shell_player_do_next_internal (player, FALSE, TRUE, error);
}

/**
 * rb_shell_player_play_entry:
 * @player: the #RBShellPlayer
 * @entry: the #RhythmDBEntry to play
 * @source: the new #RBSource to set as playing (or NULL to use the
 *   selected source)
 *
 * Plays a specified entry.
 */
void
rb_shell_player_play_entry (RBShellPlayer *player,
			    RhythmDBEntry *entry,
			    RBSource *source)
{
	GError *error = NULL;

	if (source == NULL)
		source = player->priv->selected_source;
	rb_shell_player_set_playing_source (player, source);

	if (!rb_shell_player_set_playing_entry (player, entry, TRUE, FALSE, &error)) {
		rb_shell_player_error (player, FALSE, error);
		g_clear_error (&error);
	}
}

/**
 * rb_shell_player_playpause:
 * @player: the #RBShellPlayer
 * @error: returns error information
 *
 * Toggles between playing and paused state.  If there is no playing
 * entry, chooses an entry from (in order of preference) the play queue,
 * the selection in the current source, or the play order.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_playpause (RBShellPlayer *player,
			   GError **error)
{
	gboolean ret;
	RBEntryView *songs;

	rb_debug ("doing playpause");

	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), TRUE);

	ret = TRUE;

	if (rb_player_playing (player->priv->mmplayer)) {
		if (player->priv->source == NULL) {
			rb_debug ("playing source is already NULL");
		} else if (rb_source_can_pause (player->priv->current_playing_source)) {
			rb_debug ("pausing mm player");
			if (player->priv->parser_cancellable != NULL) {
				g_object_unref (player->priv->parser_cancellable);
				player->priv->parser_cancellable = NULL;
			}
			rb_player_pause (player->priv->mmplayer);
			songs = rb_source_get_entry_view (player->priv->current_playing_source);
			if (songs)
				rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PAUSED);

			/* might need a signal for when the player has actually paused here? */
			g_object_notify (G_OBJECT (player), "playing");
			/* mostly for that */
		} else {
			rb_debug ("stopping playback");
			rb_shell_player_stop (player);
		}
	} else {
		RhythmDBEntry *entry;
		RBSource *new_source;
		gboolean out_of_order = FALSE;

		if (player->priv->source == NULL) {
			/* no current stream, pull one in from the currently
			 * selected source */
			rb_debug ("no playing source, using selected source");
			rb_shell_player_set_playing_source (player, player->priv->selected_source);
		}
		new_source = player->priv->current_playing_source;

		entry = rb_shell_player_get_playing_entry (player);
		if (entry == NULL) {
			/* queue takes precedence over selection */
			if (player->priv->queue_play_order) {
				entry = rb_play_order_get_next (player->priv->queue_play_order);
				if (entry != NULL) {
					new_source = RB_SOURCE (player->priv->queue_source);
					rb_play_order_go_next (player->priv->queue_play_order);
				}
			}

			/* selection takes precedence over first item in play order */
			if (entry == NULL) {
				GList *selection = NULL;

				songs = rb_source_get_entry_view (player->priv->source);
				if (songs)
					selection = rb_entry_view_get_selected_entries (songs);

				if (selection != NULL) {
					rb_debug ("choosing first selected entry");
					entry = (RhythmDBEntry*) selection->data;
					if (entry)
						out_of_order = TRUE;

					g_list_free (selection);
				}
			}

			/* play order is last */
			if (entry == NULL) {
				RBPlayOrder *porder;

				rb_debug ("getting entry from play order");
				g_object_get (player->priv->source, "play-order", &porder, NULL);
				if (porder == NULL)
					porder = g_object_ref (player->priv->play_order);

				entry = rb_play_order_get_next (porder);
				if (entry != NULL)
					rb_play_order_go_next (porder);
				g_object_unref (porder);
			}

			if (entry != NULL) {
				/* if the entry view containing the playing entry changed, update it */
				if (new_source != player->priv->current_playing_source)
					swap_playing_source (player, new_source);

				if (!rb_shell_player_set_playing_entry (player, entry, out_of_order, FALSE, error))
					ret = FALSE;
			}
		} else {
			if (!rb_shell_player_play (player, error)) {
				rb_shell_player_stop (player);
				ret = FALSE;
			}
		}

		if (entry != NULL) {
			rhythmdb_entry_unref (entry);
		}
	}

	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);

	return ret;
}

static void
rb_shell_player_sync_control_state (RBShellPlayer *player)
{
	gboolean shuffle, repeat;
	GAction *action;
	rb_debug ("syncing control state");

	if (!rb_shell_player_get_playback_state (player, &shuffle,
						 &repeat))
		return;


	action = g_action_map_lookup_action (G_ACTION_MAP (g_application_get_default ()),
					     "play-shuffle");
	g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (shuffle));

	action = g_action_map_lookup_action (G_ACTION_MAP (g_application_get_default ()),
					     "play-repeat");
	g_simple_action_set_state (G_SIMPLE_ACTION (action), g_variant_new_boolean (repeat));
}

static void
sync_volume_cb (GSettings *settings, RBShellPlayer *player)
{
	g_settings_set_double (player->priv->settings, "volume", player->priv->volume);
}

static void
rb_shell_player_sync_volume (RBShellPlayer *player,
			     gboolean notify,
			     gboolean set_volume)
{
	RhythmDBEntry *entry;

	if (player->priv->volume <= 0.0){
		player->priv->volume = 0.0;
	} else if (player->priv->volume >= 1.0){
		player->priv->volume = 1.0;
	}

	if (set_volume) {
		rb_player_set_volume (player->priv->mmplayer,
				      player->priv->mute ? 0.0 : player->priv->volume);
	}

	if (player->priv->syncing_state == FALSE) {
		rb_settings_delayed_sync (player->priv->settings,
					  (RBDelayedSyncFunc) sync_volume_cb,
					  g_object_ref (player),
					  g_object_unref);
	}

	entry = rb_shell_player_get_playing_entry (player);
	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}

	if (notify)
		g_object_notify (G_OBJECT (player), "volume");
}

/**
 * rb_shell_player_set_volume:
 * @player: the #RBShellPlayer
 * @volume: the volume level (between 0 and 1)
 * @error: returns the error information
 *
 * Sets the playback volume level.
 *
 * Return value: %TRUE on success
 */
gboolean
rb_shell_player_set_volume (RBShellPlayer *player,
			    gdouble volume,
			    GError **error)
{
	player->priv->volume = volume;
	rb_shell_player_sync_volume (player, TRUE, TRUE);
	return TRUE;
}

/**
 * rb_shell_player_set_volume_relative:
 * @player: the #RBShellPlayer
 * @delta: difference to apply to the volume level (between -1 and 1)
 * @error: returns error information
 *
 * Adds the specified value to the current volume level.
 *
 * Return value: %TRUE on success
 */
gboolean
rb_shell_player_set_volume_relative (RBShellPlayer *player,
				     gdouble delta,
				     GError **error)
{
	/* rb_shell_player_sync_volume does clipping */
	player->priv->volume += delta;
	rb_shell_player_sync_volume (player, TRUE, TRUE);
	return TRUE;
}

/**
 * rb_shell_player_get_volume:
 * @player: the #RBShellPlayer
 * @volume: (out): returns the volume level
 * @error: returns error information
 *
 * Returns the current volume level
 *
 * Return value: the current volume level.
 */
gboolean
rb_shell_player_get_volume (RBShellPlayer *player,
			    gdouble *volume,
			    GError **error)
{
	*volume = player->priv->volume;
	return TRUE;
}

static void
rb_shell_player_volume_changed_cb (RBPlayer *player,
				   float volume,
				   RBShellPlayer *shell_player)
{
	shell_player->priv->volume = volume;
	rb_shell_player_sync_volume (shell_player, TRUE, FALSE);
}

/**
 * rb_shell_player_set_mute:
 * @player: the #RBShellPlayer
 * @mute: %TRUE to mute playback
 * @error: returns error information
 *
 * Updates the mute setting on the player.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_set_mute (RBShellPlayer *player,
			  gboolean mute,
			  GError **error)
{
	player->priv->mute = mute;
	rb_shell_player_sync_volume (player, FALSE, TRUE);
	return TRUE;
}

/**
 * rb_shell_player_get_mute:
 * @player: the #RBShellPlayer
 * @mute: (out): returns the current mute setting
 * @error: returns error information
 *
 * Returns %TRUE if currently muted
 *
 * Return value: %TRUE if currently muted
 */
gboolean
rb_shell_player_get_mute (RBShellPlayer *player,
			  gboolean *mute,
			  GError **error)
{
	*mute = player->priv->mute;
	return TRUE;
}

static void
rb_shell_player_entry_activated_cb (RBEntryView *view,
				    RhythmDBEntry *entry,
				    RBShellPlayer *player)
{
	gboolean was_from_queue = FALSE;
	RhythmDBEntry *prev_entry = NULL;
	GError *error = NULL;
	gboolean source_set = FALSE;
	char *playback_uri;

	g_return_if_fail (entry != NULL);

	rb_debug  ("got entry %p activated", entry);

	/* don't play hidden entries */
	if (rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN))
		return;

	/* skip entries with no playback uri */
	playback_uri = rhythmdb_entry_get_playback_uri (entry);
	if (playback_uri == NULL)
		return;

	g_free (playback_uri);

	/* figure out where the previous entry came from */
	if ((player->priv->queue_source != NULL) &&
	    (player->priv->current_playing_source == RB_SOURCE (player->priv->queue_source))) {
		prev_entry = rb_shell_player_get_playing_entry (player);
		was_from_queue = TRUE;
	}

	if (player->priv->queue_source) {
		RBEntryView *queue_sidebar;

		g_object_get (player->priv->queue_source, "sidebar", &queue_sidebar, NULL);

		if (view == queue_sidebar || view == rb_source_get_entry_view (RB_SOURCE (player->priv->queue_source))) {

			/* fall back to the current selected source once the queue is empty */
			if (view == queue_sidebar && player->priv->source == NULL) {
				/* XXX only do this if the selected source doesn't have its own play order? */
				rb_play_order_playing_source_changed (player->priv->play_order,
								      player->priv->selected_source);
				player->priv->source = player->priv->selected_source;
			}

			rb_shell_player_set_playing_source (player, RB_SOURCE (player->priv->queue_source));

			was_from_queue = FALSE;
			source_set = TRUE;
		} else {
			if (player->priv->queue_only) {
				rb_source_add_to_queue (player->priv->selected_source,
							RB_SOURCE (player->priv->queue_source));
				rb_shell_player_set_playing_source (player, RB_SOURCE (player->priv->queue_source));
				source_set = TRUE;
			}
		}

		g_object_unref (queue_sidebar);
	}

	/* bail out if queue only */
	if (player->priv->queue_only) {
		return;
	}

	if (!source_set) {
		rb_shell_player_set_playing_source (player, player->priv->selected_source);
		source_set = TRUE;
	}

	if (!rb_shell_player_set_playing_entry (player, entry, TRUE, FALSE, &error)) {
		rb_shell_player_error (player, FALSE, error);
		g_clear_error (&error);
	}

	/* if we were previously playing from the queue, clear its playing entry,
	 * so we'll start again from the start.
	 */
	if (was_from_queue && prev_entry != NULL) {
		rb_play_order_set_playing_entry (player->priv->queue_play_order, NULL);
	}

	if (prev_entry != NULL) {
		rhythmdb_entry_unref (prev_entry);
	}
}

static void
rb_shell_player_property_row_activated_cb (RBPropertyView *view,
					   const char *name,
					   RBShellPlayer *player)
{
	RBPlayOrder *porder;
	RhythmDBEntry *entry = NULL;
	GError *error = NULL;

	rb_debug ("got property activated");

	rb_shell_player_set_playing_source (player, player->priv->selected_source);

	/* RHYTHMDBFIXME - do we need to wait here until the query is finished?
	 * in theory, yes, but in practice the query is started when the row is
	 * selected (on the first click when doubleclicking, or when using the
	 * keyboard to select then activate) and is pretty much always done by
	 * the time we get in here.
	 */

	g_object_get (player->priv->selected_source, "play-order", &porder, NULL);
	if (porder == NULL)
		porder = g_object_ref (player->priv->play_order);

	entry = rb_play_order_get_next (porder);
	if (entry != NULL) {
		rb_play_order_go_next (porder);

		if (!rb_shell_player_set_playing_entry (player, entry, TRUE, FALSE, &error)) {
			rb_shell_player_error (player, FALSE, error);
			g_clear_error (&error);
		}

		rhythmdb_entry_unref (entry);
	}

	g_object_unref (porder);
}

static void
rb_shell_player_entry_changed_cb (RhythmDB *db,
				  RhythmDBEntry *entry,
				  GPtrArray *changes,
				  RBShellPlayer *player)
{
	gboolean synced = FALSE;
	const char *location;
	RhythmDBEntry *playing_entry;
	int i;

	playing_entry = rb_shell_player_get_playing_entry (player);

	/* We try to update only if the changed entry is currently playing */
	if (entry != playing_entry) {
		if (playing_entry != NULL) {
			rhythmdb_entry_unref (playing_entry);
		}
		return;
	}

	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	for (i = 0; i < changes->len; i++) {
		RhythmDBEntryChange *change = g_ptr_array_index (changes, i);

		/* update UI if the artist, title or album has changed */
		switch (change->prop) {
		case RHYTHMDB_PROP_TITLE:
		case RHYTHMDB_PROP_ARTIST:
		case RHYTHMDB_PROP_ALBUM:
			if (!synced) {
				rb_shell_player_sync_with_source (player);
				synced = TRUE;
			}
			break;
		default:
			break;
		}

		/* emit dbus signals for changes with easily marshallable types */
		switch (rhythmdb_get_property_type (db, change->prop)) {
		case G_TYPE_STRING:
		case G_TYPE_BOOLEAN:
		case G_TYPE_ULONG:
		case G_TYPE_UINT64:
		case G_TYPE_DOUBLE:
			g_signal_emit (G_OBJECT (player),
				       rb_shell_player_signals[PLAYING_SONG_PROPERTY_CHANGED], 0,
				       location,
				       rhythmdb_nice_elt_name_from_propid (db, change->prop),
				       &change->old,
				       &change->new);
			break;
		default:
			break;
		}
	}

	if (playing_entry != NULL) {
		rhythmdb_entry_unref (playing_entry);
	}
}

static void
rb_shell_player_extra_metadata_cb (RhythmDB *db,
				   RhythmDBEntry *entry,
				   const char *field,
				   GValue *metadata,
				   RBShellPlayer *player)
{

	RhythmDBEntry *playing_entry;

	playing_entry = rb_shell_player_get_playing_entry (player);
	if (entry != playing_entry) {
		if (playing_entry != NULL) {
			rhythmdb_entry_unref (playing_entry);
		}
		return;
	}

	rb_shell_player_sync_with_source (player);

	/* emit dbus signals for changes with easily marshallable types */
	switch (G_VALUE_TYPE (metadata)) {
	case G_TYPE_STRING:
		/* make sure it's valid utf8, otherwise dbus barfs */
		if (g_utf8_validate (g_value_get_string (metadata), -1, NULL) == FALSE) {
			rb_debug ("not emitting extra metadata field %s as value is not valid utf8", field);
			return;
		}
	case G_TYPE_BOOLEAN:
	case G_TYPE_ULONG:
	case G_TYPE_UINT64:
	case G_TYPE_DOUBLE:
		g_signal_emit (G_OBJECT (player),
			       rb_shell_player_signals[PLAYING_SONG_PROPERTY_CHANGED], 0,
			       rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			       field,
			       metadata,		/* slightly silly */
			       metadata);
		break;
	default:
		break;
	}
}


static void
rb_shell_player_sync_with_source (RBShellPlayer *player)
{
	const char *entry_title = NULL;
	const char *artist = NULL;
	const char *stream_name = NULL;
	char *streaming_title = NULL;
	char *streaming_artist = NULL;
	RhythmDBEntry *entry;
	char *title = NULL;
	gint64 elapsed;

	entry = rb_shell_player_get_playing_entry (player);
	rb_debug ("playing source: %p, active entry: %p", player->priv->current_playing_source, entry);

	if (entry != NULL) {
		GValue *value;

		entry_title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
		artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);

		value = rhythmdb_entry_request_extra_metadata (player->priv->db,
							       entry,
							       RHYTHMDB_PROP_STREAM_SONG_TITLE);
		if (value != NULL) {
			streaming_title = g_value_dup_string (value);
			g_value_unset (value);
			g_free (value);

			rb_debug ("got streaming title \"%s\"", streaming_title);
			/* use entry title for stream name */
			stream_name = entry_title;
			entry_title = streaming_title;
		}

		value = rhythmdb_entry_request_extra_metadata (player->priv->db,
							       entry,
							       RHYTHMDB_PROP_STREAM_SONG_ARTIST);
		if (value != NULL) {
			streaming_artist = g_value_dup_string (value);
			g_value_unset (value);
			g_free (value);

			rb_debug ("got streaming artist \"%s\"", streaming_artist);
			/* override artist from entry */
			artist = streaming_artist;
		}

		rhythmdb_entry_unref (entry);
	}

	if ((artist && artist[0] != '\0') || entry_title || stream_name) {

		GString *title_str = g_string_sized_new (100);
		if (artist && artist[0] != '\0') {
			g_string_append (title_str, artist);
			g_string_append (title_str, " - ");
		}
		if (entry_title != NULL)
			g_string_append (title_str, entry_title);

		if (stream_name != NULL)
			g_string_append_printf (title_str, " (%s)", stream_name);

		title = g_string_free (title_str, FALSE);
	}

	elapsed = rb_player_get_time (player->priv->mmplayer);
	if (elapsed < 0)
		elapsed = 0;
	player->priv->elapsed = elapsed / RB_PLAYER_SECOND;

	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[WINDOW_TITLE_CHANGED], 0,
		       title);
	g_free (title);

	g_signal_emit (G_OBJECT (player), rb_shell_player_signals[ELAPSED_CHANGED], 0,
		       player->priv->elapsed);

	g_free (streaming_artist);
	g_free (streaming_title);
}

static void
rb_shell_player_sync_buttons (RBShellPlayer *player)
{
	GActionMap *map;
	GAction *action;
	RBSource *source;
	RBEntryView *view;
	int entry_view_state;
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (player);
	if (entry != NULL) {
		source = player->priv->current_playing_source;
		entry_view_state = rb_player_playing (player->priv->mmplayer) ?
			RB_ENTRY_VIEW_PLAYING : RB_ENTRY_VIEW_PAUSED;
	} else {
		source = player->priv->selected_source;
		entry_view_state = RB_ENTRY_VIEW_NOT_PLAYING;
	}

	source = (entry == NULL) ? player->priv->selected_source : player->priv->current_playing_source;

	rb_debug ("syncing with source %p", source);

	map = G_ACTION_MAP (g_application_get_default ());
	action = g_action_map_lookup_action (map, "play");
	g_simple_action_set_enabled (G_SIMPLE_ACTION (action), entry != NULL || source != NULL);

	if (source != NULL) {
		view = rb_source_get_entry_view (source);
		if (view)
			rb_entry_view_set_state (view, entry_view_state);
	}

	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}
}

/**
 * rb_shell_player_set_playing_source:
 * @player: the #RBShellPlayer
 * @source: the new playing #RBSource
 *
 * Replaces the current playing source.
 */
void
rb_shell_player_set_playing_source (RBShellPlayer *player,
				    RBSource *source)
{
	rb_shell_player_set_playing_source_internal (player, source, TRUE);
}

static void
actually_set_playing_source (RBShellPlayer *player,
			     RBSource *source,
			     gboolean sync_entry_view)
{
	RBPlayOrder *porder;

	player->priv->source = source;
	player->priv->current_playing_source = source;

	if (source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->source);
		if (sync_entry_view && songs) {
			rb_entry_view_set_state (songs, RB_ENTRY_VIEW_PLAYING);
		}
	}

	if (source != RB_SOURCE (player->priv->queue_source)) {
		if (source == NULL)
			source = player->priv->selected_source;

		if (source != NULL) {
			g_object_get (source, "play-order", &porder, NULL);
			if (porder == NULL)
				porder = g_object_ref (player->priv->play_order);

			rb_play_order_playing_source_changed (porder, source);
			g_object_unref (porder);
		}
	}

	rb_shell_player_play_order_update_cb (player->priv->play_order,
					      FALSE, FALSE,
					      player);
}

static void
rb_shell_player_set_playing_source_internal (RBShellPlayer *player,
					     RBSource *source,
					     gboolean sync_entry_view)

{
	gboolean emit_source_changed = TRUE;
	gboolean emit_playing_from_queue_changed = FALSE;

	if (player->priv->source == source &&
	    player->priv->current_playing_source == source &&
	    source != NULL)
		return;

	rb_debug ("setting playing source to %p", source);

	if (RB_SOURCE (player->priv->queue_source) == source) {

		if (player->priv->current_playing_source != source)
			emit_playing_from_queue_changed = TRUE;

		if (player->priv->source == NULL) {
			actually_set_playing_source (player, source, sync_entry_view);
		} else {
			emit_source_changed = FALSE;
			player->priv->current_playing_source = source;
		}

	} else {
		if (player->priv->current_playing_source != source) {
			if (player->priv->current_playing_source == RB_SOURCE (player->priv->queue_source))
				emit_playing_from_queue_changed = TRUE;

			/* stop the old source */
			if (player->priv->current_playing_source != NULL) {
				if (sync_entry_view) {
					RBEntryView *songs = rb_source_get_entry_view (player->priv->current_playing_source);
					rb_debug ("source is already playing, stopping it");

					/* clear the playing entry if we're switching between non-queue sources */
					if (player->priv->current_playing_source != RB_SOURCE (player->priv->queue_source))
						rb_play_order_set_playing_entry (player->priv->play_order, NULL);

					if (songs)
						rb_entry_view_set_state (songs, RB_ENTRY_VIEW_NOT_PLAYING);
				}
			}
		}
		actually_set_playing_source (player, source, sync_entry_view);
	}

	rb_shell_player_sync_with_source (player);
	/*g_object_notify (G_OBJECT (player), "playing");*/
	if (player->priv->selected_source)
		rb_shell_player_sync_buttons (player);

	if (emit_source_changed) {
		g_signal_emit (G_OBJECT (player), rb_shell_player_signals[PLAYING_SOURCE_CHANGED],
			       0, player->priv->source);
	}
	if (emit_playing_from_queue_changed) {
		g_object_notify (G_OBJECT (player), "playing-from-queue");
	}
}

/**
 * rb_shell_player_stop:
 * @player: a #RBShellPlayer.
 *
 * Completely stops playback, freeing resources and unloading the file.
 *
 * In general rb_shell_player_pause() should be used instead, as it stops the
 * audio, but does not completely free resources.
 **/
void
rb_shell_player_stop (RBShellPlayer *player)
{
	GError *error = NULL;
	rb_debug ("stopping");

	g_return_if_fail (RB_IS_SHELL_PLAYER (player));

	if (error == NULL)
		rb_player_close (player->priv->mmplayer, NULL, &error);
	if (error) {
		rb_error_dialog (NULL,
				 _("Couldn't stop playback"),
				 "%s", error->message);
		g_error_free (error);
	}

	if (player->priv->parser_cancellable != NULL) {
		rb_debug ("cancelling playlist parser");
		g_cancellable_cancel (player->priv->parser_cancellable);
		g_object_unref (player->priv->parser_cancellable);
		player->priv->parser_cancellable = NULL;
	}

	if (player->priv->playing_entry != NULL) {
		rhythmdb_entry_unref (player->priv->playing_entry);
		player->priv->playing_entry = NULL;
	}

	rb_shell_player_set_playing_source (player, NULL);
	rb_shell_player_sync_with_source (player);
	g_signal_emit (G_OBJECT (player),
		       rb_shell_player_signals[PLAYING_SONG_CHANGED], 0,
		       NULL);
	g_signal_emit (G_OBJECT (player),
		       rb_shell_player_signals[PLAYING_URI_CHANGED], 0,
		       NULL);
	g_object_notify (G_OBJECT (player), "playing");
	rb_shell_player_sync_buttons (player);
}

/**
 * rb_shell_player_pause:
 * @player: a #RBShellPlayer
 * @error: error return
 *
 * Pauses playback if possible, completely stopping if not.
 *
 * Return value: whether playback is not occurring (TRUE when successfully
 * paused/stopped or playback was not occurring).
 **/

gboolean
rb_shell_player_pause (RBShellPlayer *player,
		       GError **error)
{
	if (rb_player_playing (player->priv->mmplayer))
		return rb_shell_player_playpause (player, error);
	else
		return TRUE;
}

/**
 * rb_shell_player_get_playing:
 * @player: a #RBShellPlayer
 * @playing: (out): playback state return
 * @error: error return
 *
 * Reports whether playback is occuring by setting #playing.
 *
 * Return value: %TRUE if successful
 **/
gboolean
rb_shell_player_get_playing (RBShellPlayer *player,
			     gboolean *playing,
			     GError **error)
{
	if (playing != NULL)
		*playing = rb_player_playing (player->priv->mmplayer);

	return TRUE;
}

/**
 * rb_shell_player_get_playing_time_string:
 * @player: the #RBShellPlayer
 * 
 * Constructs a string showing the current playback position,
 * taking the time display settings into account.
 *
 * Return value: allocated playing time string
 */
char *
rb_shell_player_get_playing_time_string (RBShellPlayer *player)
{
	gboolean elapsed;
	elapsed = g_settings_get_boolean (player->priv->ui_settings, "time-display");
	return rb_make_elapsed_time_string (player->priv->elapsed,
					    rb_shell_player_get_playing_song_duration (player),
					    elapsed);
}

/**
 * rb_shell_player_get_playing_time:
 * @player: the #RBShellPlayer
 * @time: (out): returns the current playback position
 * @error: returns error information
 *
 * Retrieves the current playback position.  Fails if
 * the player currently cannot provide the playback
 * position.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_get_playing_time (RBShellPlayer *player,
				  guint *time,
				  GError **error)
{
	gint64 ptime;

	ptime = rb_player_get_time (player->priv->mmplayer);
	if (ptime >= 0) {
		if (time != NULL) {
			*time = (guint)(ptime / RB_PLAYER_SECOND);
		}
		return TRUE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_POSITION_NOT_AVAILABLE,
			     _("Playback position not available"));
		return FALSE;
	}
}

/**
 * rb_shell_player_set_playing_time:
 * @player: the #RBShellPlayer
 * @time: the target playback position (in seconds)
 * @error: returns error information
 *
 * Attempts to set the playback position.  Fails if the
 * current song is not seekable.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_set_playing_time (RBShellPlayer *player,
				  guint time,
				  GError **error)
{
	if (rb_player_seekable (player->priv->mmplayer)) {
		if (player->priv->playing_entry_eos) {
			rb_debug ("forgetting that playing entry had EOS'd due to seek");
			player->priv->playing_entry_eos = FALSE;
		}
		rb_player_set_time (player->priv->mmplayer, ((gint64) time) * RB_PLAYER_SECOND);
		return TRUE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_SEEKABLE,
			     _("Current song is not seekable"));
		return FALSE;
	}
}

/**
 * rb_shell_player_seek:
 * @player: the #RBShellPlayer
 * @offset: relative seek target (in seconds)
 * @error: returns error information
 *
 * Seeks forwards or backwards in the current playing
 * song. Fails if the current song is not seekable.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_seek (RBShellPlayer *player,
		      gint32 offset,
		      GError **error)
{
	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), FALSE);

	if (rb_player_seekable (player->priv->mmplayer)) {
		gint64 target_time = rb_player_get_time (player->priv->mmplayer) +
			(((gint64)offset) * RB_PLAYER_SECOND);
		if (target_time < 0)
			target_time = 0;
		rb_player_set_time (player->priv->mmplayer, target_time);
		return TRUE;
	} else {
		g_set_error (error,
			     RB_SHELL_PLAYER_ERROR,
			     RB_SHELL_PLAYER_ERROR_NOT_SEEKABLE,
			     _("Current song is not seekable"));
		return FALSE;
	}
}

/**
 * rb_shell_player_get_playing_song_duration:
 * @player: the #RBShellPlayer
 *
 * Retrieves the duration of the current playing song.
 *
 * Return value: duration, or -1 if not playing
 */
long
rb_shell_player_get_playing_song_duration (RBShellPlayer *player)
{
	RhythmDBEntry *current_entry;
	long val;

	g_return_val_if_fail (RB_IS_SHELL_PLAYER (player), -1);

	current_entry = rb_shell_player_get_playing_entry (player);

	if (current_entry == NULL) {
		rb_debug ("Did not get playing entry : return -1 as length");
		return -1;
	}

	val = rhythmdb_entry_get_ulong (current_entry, RHYTHMDB_PROP_DURATION);

	rhythmdb_entry_unref (current_entry);

	return val;
}

static void
rb_shell_player_sync_with_selected_source (RBShellPlayer *player)
{
	rb_debug ("syncing with selected source: %p", player->priv->selected_source);
	if (player->priv->source == NULL)
	{
		rb_debug ("no playing source, new source is %p", player->priv->selected_source);
		rb_shell_player_sync_with_source (player);
	}
}

static gboolean
do_next_idle (RBShellPlayer *player)
{
	/* use the EOS callback, so that EOF_SOURCE_ conditions are handled properly */
	rb_shell_player_handle_eos (NULL, NULL, FALSE, player);
	player->priv->do_next_idle_id = 0;

	return FALSE;
}

static gboolean
do_next_not_found_idle (RBShellPlayer *player)
{
	RhythmDBEntry *entry;
	entry = rb_shell_player_get_playing_entry (player);

	do_next_idle (player);

	if (entry != NULL) {
		rhythmdb_entry_update_availability (entry, RHYTHMDB_ENTRY_AVAIL_NOT_FOUND);
		rhythmdb_commit (player->priv->db);
		rhythmdb_entry_unref (entry);
	}

	return FALSE;
}

typedef struct {
	RBShellPlayer *player;
	gboolean async;
	GError *error;
} ErrorIdleData;

static void
free_error_idle_data (ErrorIdleData *data)
{
	g_error_free (data->error);
	g_free (data);
}

static gboolean
error_idle_cb (ErrorIdleData *data)
{
	rb_shell_player_error (data->player, data->async, data->error);
	g_mutex_lock (&data->player->priv->error_idle_mutex);
	data->player->priv->error_idle_id = 0;
	g_mutex_unlock (&data->player->priv->error_idle_mutex);
	return FALSE;
}

static void
rb_shell_player_error_idle (RBShellPlayer *player, gboolean async, const GError *error)
{
	ErrorIdleData *eid;

	eid = g_new0 (ErrorIdleData, 1);
	eid->player = player;
	eid->async = async;
	eid->error = g_error_copy (error);

	g_mutex_lock (&player->priv->error_idle_mutex);
	if (player->priv->error_idle_id != 0)
		g_source_remove (player->priv->error_idle_id);

	player->priv->error_idle_id = g_idle_add_full (G_PRIORITY_DEFAULT,
						       (GSourceFunc) error_idle_cb,
						       eid,
						       (GDestroyNotify) free_error_idle_data);
	g_mutex_unlock (&player->priv->error_idle_mutex);
}

static void
rb_shell_player_error (RBShellPlayer *player,
		       gboolean async,
		       const GError *err)
{
	RhythmDBEntry *entry;
	gboolean do_next;

	g_return_if_fail (player->priv->handling_error == FALSE);

	player->priv->handling_error = TRUE;

	entry = rb_shell_player_get_playing_entry (player);

	rb_debug ("playback error while playing: %s", err->message);
	/* For synchronous errors the entry playback error has already been set */
	if (entry && async)
		rb_shell_player_set_entry_playback_error (player, entry, err->message);

	if (entry == NULL) {
		do_next = TRUE;
	} else if (err->domain == RB_PLAYER_ERROR && err->code == RB_PLAYER_ERROR_NOT_FOUND) {
		/* process not found errors after we've started the next track */
		if (player->priv->do_next_idle_id != 0) {
			g_source_remove (player->priv->do_next_idle_id);
		}
		player->priv->do_next_idle_id = g_idle_add ((GSourceFunc)do_next_not_found_idle, player);
		do_next = FALSE;
	} else if (err->domain == RB_PLAYER_ERROR && err->code == RB_PLAYER_ERROR_NO_AUDIO) {

		/* stream has completely ended */
		rb_shell_player_stop (player);
		do_next = FALSE;
	} else if ((player->priv->current_playing_source != NULL) &&
		   (rb_source_handle_eos (player->priv->current_playing_source) == RB_SOURCE_EOF_RETRY)) {
		/* receiving an error means a broken stream or non-audio stream, so abort
		 * unless we've got more URLs to try */
		if (g_queue_is_empty (player->priv->playlist_urls)) {
			rb_error_dialog (NULL,
					 _("Couldn't start playback"),
					 "%s", (err) ? err->message : "(null)");
			rb_shell_player_stop (player);
			do_next = FALSE;
		} else {
			rb_debug ("haven't yet exhausted the URLs from the playlist");
			do_next = TRUE;
		}
	} else {
		do_next = TRUE;
	}

	if (do_next && player->priv->do_next_idle_id == 0) {
		player->priv->do_next_idle_id = g_idle_add ((GSourceFunc)do_next_idle, player);
	}

	player->priv->handling_error = FALSE;

	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}
}

static void
playing_stream_cb (RBPlayer *mmplayer,
		   RhythmDBEntry *entry,
		   RBShellPlayer *player)
{
	gboolean entry_changed;

	g_return_if_fail (entry != NULL);

	entry_changed = (player->priv->playing_entry != entry);

	/* update playing entry */
	if (player->priv->playing_entry)
		rhythmdb_entry_unref (player->priv->playing_entry);
	player->priv->playing_entry = rhythmdb_entry_ref (entry);
	player->priv->playing_entry_eos = FALSE;

	if (entry_changed) {
		const char *location;

		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		rb_debug ("new playing stream: %s", location);
		g_signal_emit (G_OBJECT (player),
			       rb_shell_player_signals[PLAYING_SONG_CHANGED], 0,
			       entry);
		g_signal_emit (G_OBJECT (player),
			       rb_shell_player_signals[PLAYING_URI_CHANGED], 0,
			       location);
	}

	/* resync UI */
	rb_shell_player_sync_with_source (player);
	rb_shell_player_sync_buttons (player);
	g_object_notify (G_OBJECT (player), "playing");
}

static void
error_cb (RBPlayer *mmplayer,
	  RhythmDBEntry *entry,
	  const GError *err,
	  gpointer data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (data);

	if (player->priv->handling_error)
		return;

	if (player->priv->source == NULL) {
		rb_debug ("ignoring error (no source): %s", err->message);
		return;
	}

	if (entry != player->priv->playing_entry) {
		rb_debug ("got error for unexpected entry %p (expected %p)", entry, player->priv->playing_entry);
	} else {
		rb_shell_player_error (player, TRUE, err);
		rb_debug ("exiting error hander");
	}
}

static void
tick_cb (RBPlayer *mmplayer,
	 RhythmDBEntry *entry,
	 gint64 elapsed,
	 gint64 duration,
	 gpointer data)
{
 	RBShellPlayer *player = RB_SHELL_PLAYER (data);
	gint64 remaining_check = 0;
	gboolean duration_from_player = TRUE;
	const char *uri;
	long elapsed_sec;

	if (player->priv->playing_entry != entry) {
		rb_debug ("got tick for unexpected entry %p (expected %p)", entry, player->priv->playing_entry);
		return;
	}

	/* if we aren't getting a duration value from the player, use the
	 * value from the entry, if any.
	 */
	if (duration < 1) {
		duration = ((gint64)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION)) * RB_PLAYER_SECOND;
		duration_from_player = FALSE;
	}

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	rb_debug ("tick: [%s, %" G_GINT64_FORMAT ":%" G_GINT64_FORMAT "(%d)]",
		  uri,
		  elapsed,
		  duration,
		  duration_from_player);

	if (elapsed < 0) {
		elapsed_sec = 0;
	} else {
		elapsed_sec = elapsed / RB_PLAYER_SECOND;
	}

	if (player->priv->elapsed != elapsed_sec) {
		player->priv->elapsed = elapsed_sec;
		g_signal_emit (G_OBJECT (player), rb_shell_player_signals[ELAPSED_CHANGED],
			       0, player->priv->elapsed);
	}
	g_signal_emit (player, rb_shell_player_signals[ELAPSED_NANO_CHANGED], 0, elapsed);

	if (duration_from_player) {
		/* XXX update duration in various things? */
	}

	/* check if we should start a crossfade */
	if (rb_player_multiple_open (mmplayer)) {
		if (player->priv->track_transition_time < PREROLL_TIME) {
			remaining_check = PREROLL_TIME;
		} else {
			remaining_check = player->priv->track_transition_time;
		}
	}

	/*
	 * just pretending we got an EOS will do exactly what we want
	 * here.  if we don't want to crossfade, we'll just leave the stream
	 * prerolled until the current stream really ends.
	 */
	if (remaining_check > 0 &&
	    duration > 0 &&
	    elapsed > 0 &&
	    ((duration - elapsed) <= remaining_check)) {
		rb_debug ("%" G_GINT64_FORMAT " ns remaining in stream %s; need %" G_GINT64_FORMAT " for transition",
			  duration - elapsed,
			  uri,
			  remaining_check);
		rb_shell_player_handle_eos_unlocked (player, entry, FALSE);
	}
}

typedef struct {
	RhythmDBEntry *entry;
	RBShellPlayer *player;
} MissingPluginRetryData;

static void
missing_plugins_retry_cb (gpointer inst,
			  gboolean retry,
			  MissingPluginRetryData *retry_data)
{
	GError *error = NULL;
	if (retry == FALSE) {
		/* next?  or stop playback? */
		rb_debug ("not retrying playback; stopping player");
		rb_shell_player_stop (retry_data->player);
		return;
	}

	rb_debug ("retrying playback");
	rb_shell_player_set_playing_entry (retry_data->player,
					   retry_data->entry,
					   FALSE, FALSE,
					   &error);
	if (error != NULL) {
		rb_shell_player_error (retry_data->player, FALSE, error);
		g_clear_error (&error);
	}
}

static void
missing_plugins_retry_cleanup (MissingPluginRetryData *retry)
{
	retry->player->priv->handling_error = FALSE;

	g_object_unref (retry->player);
	rhythmdb_entry_unref (retry->entry);
	g_free (retry);
}


static void
missing_plugins_cb (RBPlayer *player,
		    RhythmDBEntry *entry,
		    const char **details,
		    const char **descriptions,
		    RBShellPlayer *sp)
{
	gboolean processing;
	GClosure *retry;
	MissingPluginRetryData *retry_data;

	retry_data = g_new0 (MissingPluginRetryData, 1);
	retry_data->player = g_object_ref (sp);
	retry_data->entry = rhythmdb_entry_ref (entry);

	retry = g_cclosure_new ((GCallback) missing_plugins_retry_cb,
				retry_data,
				(GClosureNotify) missing_plugins_retry_cleanup);
	g_closure_set_marshal (retry, g_cclosure_marshal_VOID__BOOLEAN);
	processing = rb_missing_plugins_install (details, FALSE, retry);
	if (processing) {
		/* don't handle any further errors */
		sp->priv->handling_error = TRUE;

		/* probably specify the URI here.. */
		rb_debug ("stopping player while processing missing plugins");
		rb_player_close (retry_data->player->priv->mmplayer, NULL, NULL);
	} else {
		rb_debug ("not processing missing plugins; simulating EOS");
		rb_shell_player_handle_eos (NULL, NULL, FALSE, retry_data->player);
	}

	g_closure_sink (retry);
}

static void
player_image_cb (RBPlayer *player,
		 RhythmDBEntry *entry,
		 GdkPixbuf *image,
		 RBShellPlayer *shell_player)
{
	RBExtDB *store;
	RBExtDBKey *key;
	const char *artist;
	GValue v = G_VALUE_INIT;

	if (image == NULL)
		return;

	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (artist == NULL || artist[0] == '\0' || strcmp (artist, _("Unknown")) == 0) {
		artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
		if (artist == NULL || artist[0] == '\0' || strcmp (artist, _("Unknown")) == 0) {
			return;
		}
	}

	store = rb_ext_db_new ("album-art");

	key = rb_ext_db_key_create_storage ("album", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	rb_ext_db_key_add_field (key, "artist", artist);

	g_value_init (&v, GDK_TYPE_PIXBUF);
	g_value_set_object (&v, image);
	rb_ext_db_store (store, key, RB_EXT_DB_SOURCE_EMBEDDED, &v);
	g_value_unset (&v);

	g_object_unref (store);
	rb_ext_db_key_free (key);
}

/**
 * rb_shell_player_get_playing_path:
 * @player: the #RBShellPlayer
 * @path: (out callee-allocates) (transfer full): returns the URI of the current playing entry
 * @error: returns error information
 *
 * Retrieves the URI of the current playing entry.  The
 * caller must not free the returned string.
 *
 * Return value: %TRUE if successful
 */
gboolean
rb_shell_player_get_playing_path (RBShellPlayer *player,
				  const gchar **path,
				  GError **error)
{
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (player);
	if (entry != NULL) {
		*path = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	} else {
		*path = NULL;
	}

	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}

	return TRUE;
}

static void
play_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	GError *error = NULL;

	rb_debug ("play!");
	if (rb_shell_player_playpause (player, &error) == FALSE) {
		rb_error_dialog (NULL,
				 _("Couldn't start playback"),
				 "%s", (error) ? error->message : "(null)");
	}
	g_clear_error (&error);
}

static void
play_previous_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	GError *error = NULL;

	if (!rb_shell_player_do_previous (player, &error)) {
		if (error->domain != RB_SHELL_PLAYER_ERROR ||
		    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST) {
			g_warning ("cmd_previous: Unhandled error: %s", error->message);
		} else if (error->code == RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST) {
			rb_shell_player_stop (player);
		}
	}
}

static void
play_next_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	GError *error = NULL;

	if (!rb_shell_player_do_next (player, &error)) {
		if (error->domain != RB_SHELL_PLAYER_ERROR ||
		    error->code != RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST) {
			g_warning ("cmd_next: Unhandled error: %s", error->message);
		} else if (error->code == RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST) {
			rb_shell_player_stop (player);
		}
	}
}

static void
play_repeat_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	const char *neworder;
	gboolean shuffle = FALSE;
	gboolean repeat = FALSE;
	rb_debug ("repeat changed");

	if (player->priv->syncing_state)
		return;

	rb_shell_player_get_playback_state (player, &shuffle, &repeat);

	repeat = !repeat;
	neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	g_settings_set_string (player->priv->settings, "play-order", neworder);
}

static void
play_shuffle_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	const char *neworder;
	gboolean shuffle = FALSE;
	gboolean repeat = FALSE;

	if (player->priv->syncing_state)
		return;

	rb_debug ("shuffle changed");

	rb_shell_player_get_playback_state (player, &shuffle, &repeat);

	shuffle = !shuffle;
	neworder = state_to_play_order[shuffle ? 1 : 0][repeat ? 1 : 0];
	g_settings_set_string (player->priv->settings, "play-order", neworder);
}

static void
play_volume_up_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	rb_shell_player_set_volume_relative (player, 0.1, NULL);
}

static void
play_volume_down_action_cb (GSimpleAction *action, GVariant *parameter, gpointer user_data)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (user_data);
	rb_shell_player_set_volume_relative (player, -0.1, NULL);
}


static void
_play_order_description_free (RBPlayOrderDescription *order)
{
	g_free (order->name);
	g_free (order->description);
	g_free (order);
}

/**
 * rb_play_order_new:
 * @porder_name: Play order type name
 * @player: #RBShellPlayer instance to attach to
 *
 * Creates a new #RBPlayOrder of the specified type.
 *
 * Returns: #RBPlayOrder instance
 **/

#define DEFAULT_PLAY_ORDER "linear"

static RBPlayOrder *
rb_play_order_new (RBShellPlayer *player, const char* porder_name)
{
	RBPlayOrderDescription *order;

	g_return_val_if_fail (porder_name != NULL, NULL);
	g_return_val_if_fail (player != NULL, NULL);

	order = g_hash_table_lookup (player->priv->play_orders, porder_name);

	if (order == NULL) {
		g_warning ("Unknown value \"%s\" in GSettings key \"play-order"
				"\". Using %s play order.", porder_name, DEFAULT_PLAY_ORDER);
		order = g_hash_table_lookup (player->priv->play_orders, DEFAULT_PLAY_ORDER);
	}

	return RB_PLAY_ORDER (g_object_new (order->order_type, "player", player, NULL));
}

/**
 * rb_shell_player_add_play_order:
 * @player: the #RBShellPlayer
 * @name: name of the new play order
 * @description: description of the new play order
 * @order_type: the #GType of the play order class
 * @hidden: if %TRUE, don't display the play order in the UI
 *
 * Adds a new play order to the set of available play orders.
 */
void
rb_shell_player_add_play_order (RBShellPlayer *player, const char *name,
				const char *description, GType order_type, gboolean hidden)
{
	RBPlayOrderDescription *order;

	g_return_if_fail (g_type_is_a (order_type, RB_TYPE_PLAY_ORDER));

	order = g_new0(RBPlayOrderDescription, 1);
	order->name = g_strdup (name);
	order->description = g_strdup (description);
	order->order_type = order_type;
	order->is_in_dropdown = !hidden;

	g_hash_table_insert (player->priv->play_orders, order->name, order);
}

/**
 * rb_shell_player_remove_play_order:
 * @player: the #RBShellPlayer
 * @name: name of the play order to remove
 *
 * Removes a play order previously added with #rb_shell_player_add_play_order
 * from the set of available play orders.
 */
void
rb_shell_player_remove_play_order (RBShellPlayer *player, const char *name)
{
	g_hash_table_remove (player->priv->play_orders, name);
}

static void
rb_shell_player_constructed (GObject *object)
{
	RBApplication *app;
	RBShellPlayer *player;
	GAction *action;

	GActionEntry actions[] = {
		{ "play", play_action_cb },
		{ "play-previous", play_previous_action_cb },
		{ "play-next", play_next_action_cb },
		{ "play-repeat", play_repeat_action_cb, "b", "false" },
		{ "play-shuffle", play_shuffle_action_cb, "b", "false" },
		{ "volume-up", play_volume_up_action_cb },
		{ "volume-down", play_volume_down_action_cb }
	};
	const char *play_accels[] = {
		"<Ctrl>p",
		NULL
	};
	const char *play_repeat_accels[] = {
		"<Ctrl>r",
		NULL
	};
	const char *play_shuffle_accels[] = {
		"<Ctrl>u",
		NULL
	};

	RB_CHAIN_GOBJECT_METHOD (rb_shell_player_parent_class, constructed, object);

	player = RB_SHELL_PLAYER (object);

	app = RB_APPLICATION (g_application_get_default ());
	g_action_map_add_action_entries (G_ACTION_MAP (app),
					 actions,
					 G_N_ELEMENTS (actions),
					 player);

	/* these only take effect if the focused widget doesn't handle the event */
	rb_application_add_accelerator (app, "<Ctrl>Left", "app.play-previous", NULL);
	rb_application_add_accelerator (app, "<Ctrl>Right", "app.play-next", NULL);
	rb_application_add_accelerator (app, "<Ctrl>Up", "app.volume-up", NULL);
	rb_application_add_accelerator (app, "<Ctrl>Down", "app.volume-down", NULL);

	/* these take effect regardless of widget key handling */
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.play", play_accels);
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.play-repeat(true)", play_repeat_accels);
	gtk_application_set_accels_for_action (GTK_APPLICATION (app), "app.play-shuffle(true)", play_shuffle_accels);

	player_settings_changed_cb (player->priv->settings, "transition-time", player);
	player_settings_changed_cb (player->priv->settings, "play-order", player);

	action = g_action_map_lookup_action (G_ACTION_MAP (app), "play-previous");
	g_object_bind_property (player, "has-prev", action, "enabled", G_BINDING_DEFAULT);
	action = g_action_map_lookup_action (G_ACTION_MAP (app), "play-next");
	g_object_bind_property (player, "has-next", action, "enabled", G_BINDING_DEFAULT);

	player->priv->syncing_state = TRUE;
	rb_shell_player_set_playing_source (player, NULL);
	rb_shell_player_sync_play_order (player);
	rb_shell_player_sync_control_state (player);
	rb_shell_player_sync_volume (player, FALSE, TRUE);
	player->priv->syncing_state = FALSE;
}

static void
rb_shell_player_set_source_internal (RBShellPlayer *player,
				     RBSource      *source)
{
	if (player->priv->selected_source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
		GList *property_views = rb_source_get_property_views (player->priv->selected_source);
		GList *l;

		if (songs != NULL) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (songs),
							      G_CALLBACK (rb_shell_player_entry_activated_cb),
							      player);
		}

		for (l = property_views; l != NULL; l = g_list_next (l)) {
			g_signal_handlers_disconnect_by_func (G_OBJECT (l->data),
							      G_CALLBACK (rb_shell_player_property_row_activated_cb),
							      player);
		}

		g_list_free (property_views);
	}

	player->priv->selected_source = source;

	rb_debug ("selected source %p", player->priv->selected_source);

	rb_shell_player_sync_with_selected_source (player);
	rb_shell_player_sync_buttons (player);

	if (player->priv->selected_source != NULL) {
		RBEntryView *songs = rb_source_get_entry_view (player->priv->selected_source);
		GList *property_views = rb_source_get_property_views (player->priv->selected_source);
		GList *l;

		if (songs)
			g_signal_connect_object (G_OBJECT (songs),
						 "entry-activated",
						 G_CALLBACK (rb_shell_player_entry_activated_cb),
						 player, 0);
		for (l = property_views; l != NULL; l = g_list_next (l)) {
			g_signal_connect_object (G_OBJECT (l->data),
						 "property-activated",
						 G_CALLBACK (rb_shell_player_property_row_activated_cb),
						 player, 0);
		}

		g_list_free (property_views);
	}

	/* If we're not playing, change the play order's view of the current source;
	 * if the selected source is the queue, however, set it to NULL so it'll stop
	 * once the queue is empty.
	 */
	if (player->priv->current_playing_source == NULL) {
		RBPlayOrder *porder = NULL;
		RBSource *source = player->priv->selected_source;
		if (source == RB_SOURCE (player->priv->queue_source)) {
			source = NULL;
		} else if (source != NULL) {
			g_object_get (source, "play-order", &porder, NULL);
		}

		if (porder == NULL)
			porder = g_object_ref (player->priv->play_order);

		rb_play_order_playing_source_changed (porder, source);
		g_object_unref (porder);
	}
}
static void
rb_shell_player_set_db_internal (RBShellPlayer *player,
				 RhythmDB      *db)
{
	if (player->priv->db != NULL) {
		g_signal_handlers_disconnect_by_func (player->priv->db,
						      G_CALLBACK (rb_shell_player_entry_changed_cb),
						      player);
		g_signal_handlers_disconnect_by_func (player->priv->db,
						      G_CALLBACK (rb_shell_player_extra_metadata_cb),
						      player);
	}

	player->priv->db = db;

	if (player->priv->db != NULL) {
		/* Listen for changed entries to update metadata display */
		g_signal_connect_object (G_OBJECT (player->priv->db),
					 "entry_changed",
					 G_CALLBACK (rb_shell_player_entry_changed_cb),
					 player, 0);
		g_signal_connect_object (G_OBJECT (player->priv->db),
					 "entry_extra_metadata_notify",
					 G_CALLBACK (rb_shell_player_extra_metadata_cb),
					 player, 0);
	}
}

static void
rb_shell_player_set_queue_source_internal (RBShellPlayer     *player,
					   RBPlayQueueSource *source)
{
	if (player->priv->queue_source != NULL) {
		RBEntryView *sidebar;

		g_object_get (player->priv->queue_source, "sidebar", &sidebar, NULL);
		g_signal_handlers_disconnect_by_func (sidebar,
						      G_CALLBACK (rb_shell_player_entry_activated_cb),
						      player);
		g_object_unref (sidebar);

		if (player->priv->queue_play_order != NULL) {
			g_signal_handlers_disconnect_by_func (player->priv->queue_play_order,
							      G_CALLBACK (rb_shell_player_play_order_update_cb),
							      player);
			g_object_unref (player->priv->queue_play_order);
		}

	}

	player->priv->queue_source = source;

	if (player->priv->queue_source != NULL) {
		RBEntryView *sidebar;

		g_object_get (player->priv->queue_source, "play-order", &player->priv->queue_play_order, NULL);

		g_signal_connect_object (G_OBJECT (player->priv->queue_play_order),
					 "have_next_previous_changed",
					 G_CALLBACK (rb_shell_player_play_order_update_cb),
					 player, 0);
		rb_shell_player_play_order_update_cb (player->priv->play_order,
						      FALSE, FALSE,
						      player);
		rb_play_order_playing_source_changed (player->priv->queue_play_order,
						      RB_SOURCE (player->priv->queue_source));

		g_object_get (player->priv->queue_source, "sidebar", &sidebar, NULL);
		g_signal_connect_object (G_OBJECT (sidebar),
					 "entry-activated",
					 G_CALLBACK (rb_shell_player_entry_activated_cb),
					 player, 0);
		g_object_unref (sidebar);
	}
}

static void
rb_shell_player_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		rb_shell_player_set_source_internal (player, g_value_get_object (value));
		break;
	case PROP_DB:
		rb_shell_player_set_db_internal (player, g_value_get_object (value));
		break;
	case PROP_PLAY_ORDER:
		g_settings_set_string (player->priv->settings,
				       "play-order",
				       g_value_get_string (value));
		break;
	case PROP_VOLUME:
		player->priv->volume = g_value_get_float (value);
		rb_shell_player_sync_volume (player, FALSE, TRUE);
		break;
	case PROP_HEADER:
		player->priv->header_widget = g_value_get_object (value);
		g_signal_connect_object (player->priv->header_widget,
					 "notify::slider-dragging",
					 G_CALLBACK (rb_shell_player_slider_dragging_cb),
					 player, 0);
		break;
	case PROP_QUEUE_SOURCE:
		rb_shell_player_set_queue_source_internal (player, g_value_get_object (value));
		break;
	case PROP_QUEUE_ONLY:
		player->priv->queue_only = g_value_get_boolean (value);
		break;
	case PROP_MUTE:
		player->priv->mute = g_value_get_boolean (value);
		rb_shell_player_sync_volume (player, FALSE, TRUE);
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_shell_player_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBShellPlayer *player = RB_SHELL_PLAYER (object);

	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, player->priv->selected_source);
		break;
	case PROP_DB:
		g_value_set_object (value, player->priv->db);
		break;
	case PROP_PLAY_ORDER:
	{
		char *play_order = g_settings_get_string (player->priv->settings,
							  "play-order");
		if (play_order == NULL)
			play_order = g_strdup ("linear");
		g_value_take_string (value, play_order);
		break;
	}
	case PROP_PLAYING:
		if (player->priv->mmplayer != NULL)
			g_value_set_boolean (value, rb_player_playing (player->priv->mmplayer));
		else
			g_value_set_boolean (value, FALSE);
		break;
	case PROP_VOLUME:
		g_value_set_float (value, player->priv->volume);
		break;
	case PROP_HEADER:
		g_value_set_object (value, player->priv->header_widget);
		break;
	case PROP_QUEUE_SOURCE:
		g_value_set_object (value, player->priv->queue_source);
		break;
	case PROP_QUEUE_ONLY:
		g_value_set_boolean (value, player->priv->queue_only);
		break;
	case PROP_PLAYING_FROM_QUEUE:
		g_value_set_boolean (value, player->priv->current_playing_source == RB_SOURCE (player->priv->queue_source));
		break;
	case PROP_PLAYER:
		g_value_set_object (value, player->priv->mmplayer);
		break;
	case PROP_MUTE:
		g_value_set_boolean (value, player->priv->mute);
		break;
	case PROP_HAS_NEXT:
		g_value_set_boolean (value, player->priv->has_next);
		break;
	case PROP_HAS_PREV:
		g_value_set_boolean (value, player->priv->has_prev);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}
static void
rb_shell_player_init (RBShellPlayer *player)
{
	GError *error = NULL;

	player->priv = RB_SHELL_PLAYER_GET_PRIVATE (player);

	g_mutex_init (&player->priv->error_idle_mutex);

	player->priv->settings = g_settings_new ("org.gnome.rhythmbox.player");
	player->priv->ui_settings = g_settings_new ("org.gnome.rhythmbox");
	g_signal_connect_object (player->priv->settings,
				 "changed",
				 G_CALLBACK (player_settings_changed_cb),
				 player, 0);

	player->priv->play_orders = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)_play_order_description_free);
	
	rb_shell_player_add_play_order (player, "linear", N_("Linear"),
					RB_TYPE_LINEAR_PLAY_ORDER, FALSE);
	rb_shell_player_add_play_order (player, "linear-loop", N_("Linear looping"),
					RB_TYPE_LINEAR_PLAY_ORDER_LOOP, FALSE);
	rb_shell_player_add_play_order (player, "shuffle", N_("Shuffle"),
					RB_TYPE_SHUFFLE_PLAY_ORDER, FALSE);
	rb_shell_player_add_play_order (player, "random-equal-weights", N_("Random with equal weights"),
					RB_TYPE_RANDOM_PLAY_ORDER_EQUAL_WEIGHTS, FALSE);
	rb_shell_player_add_play_order (player, "random-by-age", N_("Random by time since last play"),
					RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE, FALSE);
	rb_shell_player_add_play_order (player, "random-by-rating", N_("Random by rating"),
					RB_TYPE_RANDOM_PLAY_ORDER_BY_RATING, FALSE);
	rb_shell_player_add_play_order (player, "random-by-age-and-rating", N_("Random by time since last play and rating"),
					RB_TYPE_RANDOM_PLAY_ORDER_BY_AGE_AND_RATING, FALSE);
	rb_shell_player_add_play_order (player, "queue", N_("Linear, removing entries once played"),
					RB_TYPE_QUEUE_PLAY_ORDER, TRUE);

	player->priv->mmplayer = rb_player_new (g_settings_get_boolean (player->priv->settings, "use-xfade-backend"),
					        &error);
	if (error != NULL) {
		GtkWidget *dialog;
		dialog = gtk_message_dialog_new (NULL, GTK_DIALOG_MODAL,
						 GTK_MESSAGE_ERROR,
						 GTK_BUTTONS_CLOSE,
						 _("Failed to create the player: %s"),
						 error->message);
		gtk_dialog_run (GTK_DIALOG (dialog));
		exit (1);
	}

	g_signal_connect_object (player->priv->mmplayer,
				 "eos",
				 G_CALLBACK (rb_shell_player_handle_eos),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "redirect",
				 G_CALLBACK (rb_shell_player_handle_redirect),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "tick",
				 G_CALLBACK (tick_cb),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "error",
				 G_CALLBACK (error_cb),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "playing-stream",
				 G_CALLBACK (playing_stream_cb),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "missing-plugins",
				 G_CALLBACK (missing_plugins_cb),
				 player, 0);
	g_signal_connect_object (player->priv->mmplayer,
				 "volume-changed",
				 G_CALLBACK (rb_shell_player_volume_changed_cb),
				 player, 0);

	g_signal_connect_object (player->priv->mmplayer,
				 "image",
				 G_CALLBACK (player_image_cb),
				 player, 0);

	{
		GVolumeMonitor *monitor = g_volume_monitor_get ();
		g_signal_connect (G_OBJECT (monitor),
				  "mount-pre-unmount",
				  G_CALLBACK (volume_pre_unmount_cb),
				  player);
		g_object_unref (monitor);	/* hmm */
	}

	player->priv->volume = g_settings_get_double (player->priv->settings, "volume");

	g_signal_connect (player, "notify::playing",
			  G_CALLBACK (reemit_playing_signal), NULL);
}

static void
rb_shell_player_dispose (GObject *object)
{
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PLAYER (object));

	player = RB_SHELL_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	if (player->priv->ui_settings != NULL) {
		g_object_unref (player->priv->ui_settings);
		player->priv->ui_settings = NULL;
	}

	if (player->priv->settings != NULL) {
		/* hm, is this really the place to do this? */
		g_settings_set_double (player->priv->settings,
				       "volume",
				       player->priv->volume);

		g_object_unref (player->priv->settings);
		player->priv->settings = NULL;
	}

	if (player->priv->mmplayer != NULL) {
		g_object_unref (player->priv->mmplayer);
		player->priv->mmplayer = NULL;
	}

	if (player->priv->play_order != NULL) {
		g_object_unref (player->priv->play_order);
		player->priv->play_order = NULL;
	}

	if (player->priv->queue_play_order != NULL) {
		g_object_unref (player->priv->queue_play_order);
		player->priv->queue_play_order = NULL;
	}

	if (player->priv->do_next_idle_id != 0) {
		g_source_remove (player->priv->do_next_idle_id);
		player->priv->do_next_idle_id = 0;
	}
	if (player->priv->error_idle_id != 0) {
		g_source_remove (player->priv->error_idle_id);
		player->priv->error_idle_id = 0;
	}

	G_OBJECT_CLASS (rb_shell_player_parent_class)->dispose (object);
}

static void
rb_shell_player_finalize (GObject *object)
{
	RBShellPlayer *player;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SHELL_PLAYER (object));

	player = RB_SHELL_PLAYER (object);

	g_return_if_fail (player->priv != NULL);

	g_hash_table_destroy (player->priv->play_orders);
	
	G_OBJECT_CLASS (rb_shell_player_parent_class)->finalize (object);
}

static void
rb_shell_player_class_init (RBShellPlayerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_shell_player_dispose;
	object_class->finalize = rb_shell_player_finalize;
	object_class->constructed = rb_shell_player_constructed;

	object_class->set_property = rb_shell_player_set_property;
	object_class->get_property = rb_shell_player_get_property;

	/**
	 * RBShellPlayer:source:
	 *
	 * The current source that is selected for playback.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "RBSource",
							      "RBSource object",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE));
	/**
	 * RBShellPlayer:db:
	 *
	 * The #RhythmDB
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBShellPlayer:queue-source:
	 *
	 * The play queue source
	 */
	g_object_class_install_property (object_class,
					 PROP_QUEUE_SOURCE,
					 g_param_spec_object ("queue-source",
							      "RBPlayQueueSource",
							      "RBPlayQueueSource object",
							      RB_TYPE_PLAYLIST_SOURCE,
							      G_PARAM_READWRITE));

	/**
	 * RBShellPlayer:queue-only:
	 *
	 * If %TRUE, activating an entry should only add it to the play queue.
	 */
	g_object_class_install_property (object_class,
					 PROP_QUEUE_ONLY,
					 g_param_spec_boolean ("queue-only",
							       "Queue only",
							       "Activation only adds to queue",
							       FALSE,
							       G_PARAM_READWRITE));

	/**
	 * RBShellPlayer:playing-from-queue:
	 *
	 * If %TRUE, the current playing entry came from the play queue.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYING_FROM_QUEUE,
					 g_param_spec_boolean ("playing-from-queue",
							       "Playing from queue",
							       "Whether playing from the play queue or not",
							       FALSE,
							       G_PARAM_READABLE));

	/**
	 * RBShellPlayer:player:
	 *
	 * The player backend object (an object implementing the #RBPlayer interface).
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYER,
					 g_param_spec_object ("player",
							      "RBPlayer",
							      "RBPlayer object",
							      G_TYPE_OBJECT,
							      G_PARAM_READABLE));

	/**
	 * RBShellPlayer:play-order:
	 *
	 * The current play order object.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAY_ORDER,
					 g_param_spec_string ("play-order",
							      "play-order",
							      "What play order to use",
							      "linear",
							      G_PARAM_READABLE));
	/**
	 * RBShellPlayer:playing:
	 *
	 * Whether Rhythmbox is currently playing something
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYING,
					 g_param_spec_boolean ("playing",
							       "playing",
							      "Whether Rhythmbox is currently playing",
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * RBShellPlayer:volume:
	 *
	 * The current playback volume (between 0.0 and 1.0)
	 */
	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_float ("volume",
							     "volume",
							     "Current playback volume",
							     0.0f, 1.0f, 1.0f,
							     G_PARAM_READWRITE));

	/**
	 * RBShellPlayer:header:
	 *
	 * The #RBHeader object
	 */
	g_object_class_install_property (object_class,
					 PROP_HEADER,
					 g_param_spec_object ("header",
							      "RBHeader",
							      "RBHeader object",
							      RB_TYPE_HEADER,
							      G_PARAM_READWRITE));
	/**
	 * RBShellPlayer:mute:
	 *
	 * Whether playback is currently muted.
	 */
	g_object_class_install_property (object_class,
					 PROP_MUTE,
					 g_param_spec_boolean ("mute",
							       "mute",
							       "Whether playback is muted",
							       FALSE,
							       G_PARAM_READWRITE));
	/**
	 * RBShellPlayer:has-next:
	 *
	 * Whether there is a track to play after the current track.
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_NEXT,
					 g_param_spec_boolean ("has-next",
							       "has-next",
							       "Whether there is a next track",
							       FALSE,
							       G_PARAM_READABLE));
	/**
	 * RBShellPlayer:has-prev:
	 *
	 * Whether there was a previous track before the current track.
	 */
	g_object_class_install_property (object_class,
					 PROP_HAS_PREV,
					 g_param_spec_boolean ("has-prev",
							       "has-prev",
							       "Whether there is a previous track",
							       FALSE,
							       G_PARAM_READABLE));

	/**
	 * RBShellPlayer::window-title-changed:
	 * @player: the #RBShellPlayer
	 * @title: the new window title
	 *
	 * Emitted when the main window title text should be changed
	 */
	rb_shell_player_signals[WINDOW_TITLE_CHANGED] =
		g_signal_new ("window_title_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, window_title_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBShellPlayer::elapsed-changed:
	 * @player: the #RBShellPlayer
	 * @elapsed: the new playback position in seconds
	 *
	 * Emitted when the playback position changes.
	 */
	rb_shell_player_signals[ELAPSED_CHANGED] =
		g_signal_new ("elapsed_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, elapsed_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_UINT);

	/**
	 * RBShellPlayer::playing-source-changed:
	 * @player: the #RBShellPlayer
	 * @source: the #RBSource that is now playing
	 *
	 * Emitted when a new #RBSource instance starts playing
	 */
	rb_shell_player_signals[PLAYING_SOURCE_CHANGED] =
		g_signal_new ("playing-source-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_source_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_SOURCE);

	/**
	 * RBShellPlayer::playing-changed:
	 * @player: the #RBShellPlayer
	 * @playing: flag indicating playback state
	 *
	 * Emitted when playback either stops or starts.
	 */
	rb_shell_player_signals[PLAYING_CHANGED] =
		g_signal_new ("playing-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);

	/**
	 * RBShellPlayer::playing-song-changed:
	 * @player: the #RBShellPlayer
	 * @entry: the new playing #RhythmDBEntry
	 *
	 * Emitted when the playing database entry changes
	 */
	rb_shell_player_signals[PLAYING_SONG_CHANGED] =
		g_signal_new ("playing-song-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_song_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      RHYTHMDB_TYPE_ENTRY);

	/**
	 * RBShellPlayer::playing-uri-changed:
	 * @player: the #RBShellPlayer
	 * @uri: the URI of the new playing entry
	 *
	 * Emitted when the playing database entry changes, providing the
	 * URI of the entry.
	 */
	rb_shell_player_signals[PLAYING_URI_CHANGED] =
		g_signal_new ("playing-uri-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_uri_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);

	/**
	 * RBShellPlayer::playing-song-property-changed:
	 * @player: the #RBShellPlayer
	 * @uri: the URI of the playing entry
	 * @property: the name of the property that changed
	 * @old: the previous value for the property
	 * @newvalue: the new value of the property
	 *
	 * Emitted when a property of the playing database entry changes.
	 */
	rb_shell_player_signals[PLAYING_SONG_PROPERTY_CHANGED] =
		g_signal_new ("playing-song-property-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, playing_song_property_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      4,
			      G_TYPE_STRING, G_TYPE_STRING,
			      G_TYPE_VALUE, G_TYPE_VALUE);

	/**
	 * RBShellPlayer::elapsed-nano-changed:
	 * @player: the #RBShellPlayer
	 * @elapsed: the new playback position in nanoseconds
	 *
	 * Emitted when the playback position changes.  Only use this (as opposed to
	 * elapsed-changed) when you require subsecond precision.  This signal will be
	 * emitted multiple times per second.
	 */
	rb_shell_player_signals[ELAPSED_NANO_CHANGED] =
		g_signal_new ("elapsed-nano-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBShellPlayerClass, elapsed_nano_changed),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT64);

	g_type_class_add_private (klass, sizeof (RBShellPlayerPrivate));
}

/**
 * rb_shell_player_new:
 * @db: the #RhythmDB
 *
 * Creates the #RBShellPlayer
 * 
 * Return value: the #RBShellPlayer instance
 */
RBShellPlayer *
rb_shell_player_new (RhythmDB *db)
{
	return g_object_new (RB_TYPE_SHELL_PLAYER,
			     "db", db,
			     NULL);
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_shell_player_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SHELL_PLAYER_ERROR_PLAYLIST_PARSE_ERROR, "playlist-parse-failed"),
			ENUM_ENTRY (RB_SHELL_PLAYER_ERROR_END_OF_PLAYLIST, "end-of-playlist"),
			ENUM_ENTRY (RB_SHELL_PLAYER_ERROR_NOT_PLAYING, "not-playing"),
			ENUM_ENTRY (RB_SHELL_PLAYER_ERROR_NOT_SEEKABLE, "not-seekable"),
			ENUM_ENTRY (RB_SHELL_PLAYER_ERROR_POSITION_NOT_AVAILABLE, "position-not-available"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBShellPlayerError", values);
	}

	return etype;
}

