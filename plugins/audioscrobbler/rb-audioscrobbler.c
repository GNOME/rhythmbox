/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Alex Revo <xiphoidappendix@gmail.com>,
 *		       Ruben Vermeersch <ruben@Lambda1.be>
 *            (C) 2007 Christophe Fergeau <teuf@gnome.org>
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

#include "config.h"

#include <errno.h>

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <glib/gprintf.h>
#include <gtk/gtk.h>

#include <libsoup/soup.h>

#include "rb-audioscrobbler.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-source.h"
#include "rb-cut-and-paste-code.h"
#include "rb-util.h"
#include "rb-podcast-entry-types.h"
#include "rb-audioscrobbler-entry.h"

#define CLIENT_ID "rbx"
#define CLIENT_VERSION VERSION

#define MAX_QUEUE_SIZE 1000
#define MAX_SUBMIT_SIZE	50
#define INITIAL_HANDSHAKE_DELAY 60
#define MAX_HANDSHAKE_DELAY 120*60

#define SCROBBLER_VERSION "1.2.1"

#define USER_AGENT	"Rhythmbox/" VERSION

struct _RBAudioscrobblerPrivate
{
	RBAudioscrobblerService *service;

	RBShellPlayer *shell_player;

	/* Data for the prefs pane */
	guint submit_count;
	char *submit_time;
	guint queue_count;
	enum {
		STATUS_OK = 0,
		HANDSHAKING,
		REQUEST_FAILED,
		BADAUTH,
		BAD_TIMESTAMP,
		CLIENT_BANNED,
		GIVEN_UP
	} status;
	char *status_msg;

	/* Submission queue */
	GQueue *queue;
	/* Entries currently being submitted */
	GQueue *submission;

	guint failures;
	guint handshake_delay;
	/* Handshake has been done? */
	gboolean handshake;
	time_t handshake_next;

	/* Only write the queue to a file if it has been changed */
	gboolean queue_changed;

	/* Authentication cookie + authentication info */
	gchar *sessionid;
	gchar *username;
	gchar *session_key;
	gchar *submit_url;
	gchar *nowplaying_url;

	/* Currently playing song info, if NULL this means the currently
	 * playing song isn't eligible to be queued
	 */
	AudioscrobblerEntry *currently_playing;
	guint current_elapsed;
	gboolean now_playing_updated;

	guint timeout_id;

	/* HTTP requests session */
	SoupSession *soup_session;

	/* callback for songs that were played offline (eg on an iPod) */
	gulong offline_play_notify_id;
};

#define RB_AUDIOSCROBBLER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER, RBAudioscrobblerPrivate))


static gboolean	     rb_audioscrobbler_load_queue (RBAudioscrobbler *audioscrobbler);
static int	     rb_audioscrobbler_save_queue (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_print_queue (RBAudioscrobbler *audioscrobbler, gboolean submission);
static void	     rb_audioscrobbler_free_queue_entries (RBAudioscrobbler *audioscrobbler, GQueue **queue);

static void	     rb_audioscrobbler_get_property (GObject *object,
						    guint prop_id,
						    GValue *value,
						    GParamSpec *pspec);
static void	     rb_audioscrobbler_set_property (GObject *object,
						    guint prop_id,
						    const GValue *value,
						    GParamSpec *pspec);
static void	     rb_audioscrobbler_dispose (GObject *object);
static void	     rb_audioscrobbler_finalize (GObject *object);

static void	     rb_audioscrobbler_add_timeout (RBAudioscrobbler *audioscrobbler);
static gboolean	     rb_audioscrobbler_timeout_cb (RBAudioscrobbler *audioscrobbler);

static void          rb_audioscrobbler_parse_response (RBAudioscrobbler *audioscrobbler,
                                                       SoupMessage *msg,
						       const char *body,
						       gboolean handshake);

static void	     rb_audioscrobbler_do_handshake (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_submit_queue (RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_perform (RBAudioscrobbler *audioscrobbler,
                                                const char *url,
                                                const char *method,
                                                char *query,
                                                GAsyncReadyCallback response_handler);

static void	     rb_audioscrobbler_do_handshake_cb (SoupSession *session,
                                                        GAsyncResult *result,
                                                        RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_submit_queue_cb (SoupSession *session,
                                                        GAsyncResult *result,
                                                        RBAudioscrobbler *audioscrobbler);
static void	     rb_audioscrobbler_nowplaying_cb (SoupSession *session,
                                                      GAsyncResult *result,
                                                      RBAudioscrobbler *audioscrobbler);

static void	     rb_audioscrobbler_song_changed_cb (RBShellPlayer *player,
							RhythmDBEntry *entry,
							RBAudioscrobbler *audioscrobbler);
static void          rb_audioscrobbler_offline_play_notify_cb (RhythmDB *db,
							       RhythmDBEntry *rb_entry,
							       const gchar *property_name,
							       const GValue *metadata,
							       RBAudioscrobbler *audioscrobbler);

static void          rb_audioscrobbler_nowplaying (RBAudioscrobbler *audioscrobbler, AudioscrobblerEntry *entry);




enum
{
	PROP_0,
	PROP_SERVICE,
	PROP_SHELL_PLAYER,
	PROP_USERNAME,
	PROP_SESSION_KEY,
};

enum
{
	AUTHENTICATION_ERROR,
	STATISTICS_CHANGED,
	LAST_SIGNAL
};

static guint rb_audioscrobbler_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_DYNAMIC_TYPE (RBAudioscrobbler, rb_audioscrobbler, G_TYPE_OBJECT)


static void
rb_audioscrobbler_constructed (GObject *object)
{
	RBAudioscrobbler *audioscrobbler;
	RhythmDB *db;
	RhythmDBEntry *playing_entry;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_parent_class, constructed, object);

	audioscrobbler = RB_AUDIOSCROBBLER (object);

	rb_audioscrobbler_load_queue (audioscrobbler);
	rb_audioscrobbler_add_timeout (audioscrobbler);
	rb_audioscrobbler_statistics_changed (audioscrobbler);

	g_object_get (audioscrobbler->priv->shell_player, "db", &db, NULL);

	audioscrobbler->priv->offline_play_notify_id = 
		g_signal_connect_object (db, 
					 "entry-extra-metadata-notify::rb:offlinePlay",
					 (GCallback)rb_audioscrobbler_offline_play_notify_cb, 
					 audioscrobbler, 0);

	/* if an entry is currently being played then handle it */
	playing_entry = rb_shell_player_get_playing_entry (audioscrobbler->priv->shell_player);
	if (playing_entry != NULL) {
		rb_audioscrobbler_song_changed_cb (audioscrobbler->priv->shell_player,
		                                   playing_entry,
		                                   audioscrobbler);
		rhythmdb_entry_unref (playing_entry);
	}

	g_object_unref (db);
}

/* Class-related functions: */
static void
rb_audioscrobbler_class_init (RBAudioscrobblerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_audioscrobbler_constructed;
	object_class->dispose = rb_audioscrobbler_dispose;
	object_class->finalize = rb_audioscrobbler_finalize;

	object_class->set_property = rb_audioscrobbler_set_property;
	object_class->get_property = rb_audioscrobbler_get_property;

	g_object_class_install_property (object_class,
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Audioscrobbler service to scrobble to",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_USERNAME,
	                                 g_param_spec_string ("username",
	                                                      "Username",
	                                                      "Username of the user who is scrobbling data",
	                                                      NULL,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_SESSION_KEY,
	                                 g_param_spec_string ("session-key",
	                                                      "Session Key",
	                                                      "Session key used to authenticate the user",
	                                                      NULL,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBAudioscrobbler::authentication-error:
	 * @account: the #RBAudioscrobblerAccount
	 * @status: new status
	 *
	 * Emitted when an authentication error occurs.
	 */
	rb_audioscrobbler_signals[AUTHENTICATION_ERROR] =
		g_signal_new ("authentication-error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBAudioscrobblerClass, authentication_error),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBAudioscrobbler::statistics-changed:
	 * @status_msg: description of the status
	 * @queued_count: the number of tracks queued for submission
	 * @submit_count: the number of tracks already submitted this session
	 * @submit_time: the time at which the last submission was made
	 *
	 * Emitted when the scrobbling session's statistics change.
	 */
	rb_audioscrobbler_signals[STATISTICS_CHANGED] =
		g_signal_new ("statistics-changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBAudioscrobblerClass, statistics_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
		              4,
			      G_TYPE_STRING,
		              G_TYPE_UINT,
		              G_TYPE_UINT,
		              G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerPrivate));
}

static void
rb_audioscrobbler_class_finalize (RBAudioscrobblerClass *klass)
{
}

static void
rb_audioscrobbler_init (RBAudioscrobbler *audioscrobbler)
{
	rb_debug ("Initialising Audioscrobbler");
	rb_debug ("Plugin ID: %s, Version %s (Protocol %s)",
		  CLIENT_ID, CLIENT_VERSION, SCROBBLER_VERSION);

	audioscrobbler->priv = RB_AUDIOSCROBBLER_GET_PRIVATE (audioscrobbler);

	audioscrobbler->priv->queue = g_queue_new();
	audioscrobbler->priv->submission = g_queue_new();
	audioscrobbler->priv->sessionid = g_strdup ("");
	audioscrobbler->priv->username = NULL;
	audioscrobbler->priv->session_key = NULL;
	audioscrobbler->priv->submit_url = g_strdup ("");
	audioscrobbler->priv->nowplaying_url = g_strdup ("");
}

static void
rb_audioscrobbler_dispose (GObject *object)
{
	RBAudioscrobbler *audioscrobbler;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_AUDIOSCROBBLER (object));

	audioscrobbler = RB_AUDIOSCROBBLER (object);
	
	rb_debug ("disposing audioscrobbler");

	/* Save any remaining entries */
	rb_audioscrobbler_save_queue (audioscrobbler);

	if (audioscrobbler->priv->offline_play_notify_id != 0) {
		RhythmDB *db;

		g_object_get (G_OBJECT (audioscrobbler->priv->shell_player),
			      "db", &db, 
			      NULL);
		g_signal_handler_disconnect (db, audioscrobbler->priv->offline_play_notify_id);
		audioscrobbler->priv->offline_play_notify_id = 0;
		g_object_unref (db);
	}

	if (audioscrobbler->priv->timeout_id != 0) {
		g_source_remove (audioscrobbler->priv->timeout_id);
		audioscrobbler->priv->timeout_id = 0;
	}

	if (audioscrobbler->priv->soup_session != NULL) {
		soup_session_abort (audioscrobbler->priv->soup_session);
		g_object_unref (audioscrobbler->priv->soup_session);
		audioscrobbler->priv->soup_session = NULL;
	}

	if (audioscrobbler->priv->service != NULL) {
		g_object_unref (audioscrobbler->priv->service);
		audioscrobbler->priv->service = NULL;
	}

	if (audioscrobbler->priv->shell_player != NULL) {
		g_object_unref (audioscrobbler->priv->shell_player);
		audioscrobbler->priv->shell_player = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_finalize (GObject *object)
{
	RBAudioscrobbler *audioscrobbler;

	rb_debug ("Finalizing Audioscrobbler");

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_AUDIOSCROBBLER (object));

	audioscrobbler = RB_AUDIOSCROBBLER (object);

	g_free (audioscrobbler->priv->sessionid);
	g_free (audioscrobbler->priv->username);
	g_free (audioscrobbler->priv->session_key);
	g_free (audioscrobbler->priv->submit_url);
	g_free (audioscrobbler->priv->nowplaying_url);

	if (audioscrobbler->priv->currently_playing != NULL) {
		rb_audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
		audioscrobbler->priv->currently_playing = NULL;
	}

	rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->queue);
	rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->submission);

	G_OBJECT_CLASS (rb_audioscrobbler_parent_class)->finalize (object);
}

RBAudioscrobbler*
rb_audioscrobbler_new (RBAudioscrobblerService *service,
                       RBShellPlayer *shell_player,
                       const char *username,
                       const char *session_key)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER,
	                     "service", service,
			     "shell-player", shell_player,
	                     "username", username,
	                     "session_key", session_key,
			     NULL);
}

static void
rb_audioscrobbler_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER (object);

	switch (prop_id) {
	case PROP_SERVICE:
		audioscrobbler->priv->service = g_value_dup_object (value);
		break;
	case PROP_SHELL_PLAYER:
		audioscrobbler->priv->shell_player = g_value_get_object (value);
		g_object_ref (G_OBJECT (audioscrobbler->priv->shell_player));
		g_signal_connect_object (G_OBJECT (audioscrobbler->priv->shell_player),
					 "playing-song-changed",
					 G_CALLBACK (rb_audioscrobbler_song_changed_cb),
					 audioscrobbler, 0);
		break;
	case PROP_USERNAME:
		audioscrobbler->priv->username = g_value_dup_string (value);
		break;
	case PROP_SESSION_KEY:
		audioscrobbler->priv->session_key = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBAudioscrobbler *audioscrobbler = RB_AUDIOSCROBBLER (object);

	switch (prop_id) {
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, audioscrobbler->priv->shell_player);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* emits the statistics-changed signal */
void
rb_audioscrobbler_statistics_changed (RBAudioscrobbler *audioscrobbler)
{
	const char *status;
	char *status_msg;

	switch (audioscrobbler->priv->status) {
	case STATUS_OK:
		status = _("OK");
		break;
	case HANDSHAKING:
		status = _("Logging in");
		break;
	case REQUEST_FAILED:
		status = _("Request failed");
		break;
	case BADAUTH:
		status = _("Authentication error");
		break;
	case BAD_TIMESTAMP:
		status = _("Clock is not set correctly");
		break;
	case CLIENT_BANNED:
		status = _("This version of Rhythmbox has been banned.");
		break;
	case GIVEN_UP:
		status = _("Track submission failed too many times");
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	if (audioscrobbler->priv->status_msg && audioscrobbler->priv->status_msg[0] != '\0') {
		status_msg = g_strdup_printf ("%s: %s", status, audioscrobbler->priv->status_msg);
	} else {
		status_msg = g_strdup (status);
	}

	g_signal_emit_by_name (audioscrobbler, "statistics-changed",
	                       status_msg, audioscrobbler->priv->queue_count,
	                       audioscrobbler->priv->submit_count, audioscrobbler->priv->submit_time);

	g_free (status_msg);
}

/* Add the audioscrobbler thread timer */
static void
rb_audioscrobbler_add_timeout (RBAudioscrobbler *audioscrobbler)
{
	if (!audioscrobbler->priv->timeout_id) {
		rb_debug ("Adding Audioscrobbler timer (15 seconds)");
		audioscrobbler->priv->timeout_id = 
			g_timeout_add_seconds (15,
					       (GSourceFunc) rb_audioscrobbler_timeout_cb,
					       audioscrobbler);
	}
}

static gboolean
rb_audioscrobbler_is_queueable (RhythmDBEntry *entry)
{
	const char *title;
	const char *artist;
	gulong duration;
	RhythmDBEntryType *type;
	RhythmDBEntryCategory category;

	/* First, check if the entry is appropriate for sending to 
	 * audioscrobbler
	 */
	type = rhythmdb_entry_get_entry_type (entry);
	g_object_get (type, "category", &category, NULL);
	if (category != RHYTHMDB_ENTRY_NORMAL) {
		rb_debug ("entry %s is not queueable: category not NORMAL", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return FALSE;
	}
	if (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		rb_debug ("entry %s is not queueable: is a podcast post", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return FALSE;
	}
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR)) {
		rb_debug ("entry %s is not queueable: has playback error (%s)",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PLAYBACK_ERROR));
		return FALSE;
	}

	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

	/* The specification (v1.1) tells us to ignore entries that do not
	 * meet these conditions
	 */
	if (duration < 30) {
		rb_debug ("entry %s not queueable: shorter than 30 seconds", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return FALSE;
	}
	if (strcmp (artist, _("Unknown")) == 0) {
		rb_debug ("entry %s not queueable: artist is %s (unknown)", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), artist);
		return FALSE;
	}
	if (strcmp (title, _("Unknown")) == 0) {
		rb_debug ("entry %s not queueable: title is %s (unknown)", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), title);
		return FALSE;
	}

	rb_debug ("entry %s is queueable", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	return TRUE;
}


static void
rb_audioscrobbler_add_to_queue (RBAudioscrobbler *audioscrobbler,
				AudioscrobblerEntry *entry)
{
	if (g_queue_get_length (audioscrobbler->priv->queue) >= MAX_QUEUE_SIZE) {
		AudioscrobblerEntry *oldest;

		rb_debug ("queue limit reached.  dropping oldest entry.");
		oldest = g_queue_pop_head (audioscrobbler->priv->queue);
		rb_audioscrobbler_entry_free (oldest);
	} else {
		audioscrobbler->priv->queue_count++;
	}

	g_queue_push_tail (audioscrobbler->priv->queue, entry);
	audioscrobbler->priv->queue_changed = TRUE;
}

static void
maybe_add_current_song_to_queue (RBAudioscrobbler *audioscrobbler)
{
	gboolean got_elapsed;
	guint elapsed;
	AudioscrobblerEntry *cur_entry;

	cur_entry = audioscrobbler->priv->currently_playing;
	if (cur_entry == NULL) {
		return;
	}

	got_elapsed = rb_shell_player_get_playing_time (audioscrobbler->priv->shell_player,
							&elapsed,
							NULL);
	if (got_elapsed) {
		int elapsed_delta = elapsed - audioscrobbler->priv->current_elapsed;
		audioscrobbler->priv->current_elapsed = elapsed;
		
		if ((elapsed >= cur_entry->length / 2 || elapsed >= 240) && elapsed_delta < 20) {
			rb_debug ("Adding currently playing song to queue");
			rb_audioscrobbler_add_to_queue (audioscrobbler, cur_entry);
			audioscrobbler->priv->currently_playing = NULL;

			rb_audioscrobbler_statistics_changed (audioscrobbler);
		} else if (elapsed_delta > 20) {
			rb_debug ("Skipping detected; not submitting current song");
			/* not sure about this - what if I skip to somewhere towards
			 * the end, but then go back and listen to the whole song?
			 */
			rb_audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
			audioscrobbler->priv->currently_playing = NULL;
		}
	}
}

/* updates the queue and submits entries as required */
static gboolean
rb_audioscrobbler_timeout_cb (RBAudioscrobbler *audioscrobbler)
{
	maybe_add_current_song_to_queue (audioscrobbler);

	/* do handshake if we need to */
	rb_audioscrobbler_do_handshake (audioscrobbler);

	if ((audioscrobbler->priv->now_playing_updated == FALSE) &&
	    (audioscrobbler->priv->currently_playing != NULL) &&
	    audioscrobbler->priv->handshake) {
		rb_debug ("Sending now playing data");
		audioscrobbler->priv->now_playing_updated = TRUE;
		rb_audioscrobbler_nowplaying (audioscrobbler, audioscrobbler->priv->currently_playing);
	}

	/* if there's something in the queue, submit it if we can, save it otherwise */
	if (!g_queue_is_empty(audioscrobbler->priv->queue)) {
		if (audioscrobbler->priv->handshake) {
			rb_audioscrobbler_submit_queue (audioscrobbler);
		} else {
			rb_audioscrobbler_save_queue (audioscrobbler);
		}
	}
	return TRUE;
}

static void
rb_audioscrobbler_offline_play_notify_cb (RhythmDB *db,
					  RhythmDBEntry *rb_entry,
					  const gchar *property_name,
					  const GValue *metadata,
					  RBAudioscrobbler *audioscrobbler)
{
	g_return_if_fail (G_VALUE_HOLDS_ULONG (metadata));

	/* FIXME: do coherence checks on play_date value? */
	if (rb_audioscrobbler_is_queueable (rb_entry)) {
		AudioscrobblerEntry *as_entry;
		
		as_entry = rb_audioscrobbler_entry_create (rb_entry, audioscrobbler->priv->service);
		as_entry->play_time = g_value_get_ulong (metadata);
		rb_audioscrobbler_add_to_queue (audioscrobbler, as_entry);
	}
}

static void
rb_audioscrobbler_parse_response (RBAudioscrobbler *audioscrobbler, SoupMessage *msg, const char *body, gboolean handshake)
{
	rb_debug ("Parsing response, status=%d Reason: %s",
                  soup_message_get_status (msg),
		  soup_message_get_reason_phrase (msg));

	if ((soup_message_get_status (msg) == SOUP_STATUS_OK) && (body != NULL)) {
		gchar **breaks;

		breaks = g_strsplit (body, "\n", 0);

		g_free (audioscrobbler->priv->status_msg);
		audioscrobbler->priv->status = STATUS_OK;
		audioscrobbler->priv->status_msg = NULL;

		if (g_str_has_prefix (breaks[0], "OK")) {
			rb_debug ("OK");
			if (handshake) {
				if (g_strv_length (breaks) < 4) {
					g_warning ("Unexpectedly short successful last.fm handshake response:\n%s",
						   body);
					audioscrobbler->priv->status = REQUEST_FAILED;
				} else {
					g_free (audioscrobbler->priv->sessionid);
					g_free (audioscrobbler->priv->nowplaying_url);
					g_free (audioscrobbler->priv->submit_url);
					audioscrobbler->priv->sessionid = g_strdup (breaks[1]);
					audioscrobbler->priv->nowplaying_url = g_strdup (breaks[2]);
					audioscrobbler->priv->submit_url = g_strdup (breaks[3]);
				}
			}
		} else if (g_str_has_prefix (breaks[0], "BANNED")) {
			rb_debug ("Client banned");
			audioscrobbler->priv->status = CLIENT_BANNED;
		} else if (g_str_has_prefix (breaks[0], "BADAUTH")) {
			rb_debug ("Bad authorization");
			audioscrobbler->priv->status = BADAUTH;
			/* emit an authentication error signal.
			 * this is the only error which needs to be addressed from outside this class */
			g_signal_emit (audioscrobbler, rb_audioscrobbler_signals[AUTHENTICATION_ERROR], 0);
		} else if (g_str_has_prefix (breaks[0], "BADTIME")) {
			rb_debug ("Bad timestamp");
			audioscrobbler->priv->status = BAD_TIMESTAMP;
		} else if (g_str_has_prefix (breaks[0], "FAILED")) {
			rb_debug ("Server failure:\n \tMessage: %s", breaks[0]);
			audioscrobbler->priv->status = REQUEST_FAILED;
			/* this is probably going to be ugly, but there isn't much we can do */
			if (strlen (breaks[0]) > strlen ("FAILED ")) {
				audioscrobbler->priv->status_msg = g_strdup (breaks[0] + strlen ("FAILED "));
			}
		} else {
			g_warning ("Unexpected last.fm response:\n%s", body);
			audioscrobbler->priv->status = REQUEST_FAILED;
		}

		g_strfreev (breaks);
	} else {
		audioscrobbler->priv->status = REQUEST_FAILED;
		audioscrobbler->priv->status_msg = g_strdup (soup_message_get_reason_phrase (msg));
	}
}

static gboolean
idle_unref_cb (GObject *object)
{
	g_object_unref (object);
	return FALSE;
}

/*
 * NOTE: the caller *must* unref the audioscrobbler object in an idle
 * handler created in the callback.
 */
static void
rb_audioscrobbler_perform (RBAudioscrobbler *audioscrobbler,
                           const char *url,
                           const char *method,
                           char *query,
                           GAsyncReadyCallback response_handler)
{
	SoupMessage *msg;
	SoupMessageHeaders *hdrs;

	msg = soup_message_new_from_encoded_form (method, url, query);
	g_return_if_fail (msg != NULL);

	hdrs = soup_message_get_request_headers (msg);
	soup_message_headers_set_content_type (hdrs, "application/x-www-form-urlencoded", NULL);
	soup_message_headers_append (hdrs, "User-Agent", USER_AGENT);

	/* create soup session, if we haven't got one yet */
	if (!audioscrobbler->priv->soup_session) {
		audioscrobbler->priv->soup_session = soup_session_new ();
	}

	soup_session_send_and_read_async (audioscrobbler->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  response_handler,
					  g_object_ref (audioscrobbler));
}

static gboolean
rb_audioscrobbler_should_handshake (RBAudioscrobbler *audioscrobbler)
{
	/* Perform handshake if necessary. Only perform handshake if
	 *   - we have no current handshake; AND
	 *   - we have waited the appropriate amount of time between
	 *     handshakes; AND
	 *   - we have a username; AND
	 *   - we have a session key
	 */
	if (audioscrobbler->priv->handshake) {
		return FALSE;
	}

	if (time (NULL) < audioscrobbler->priv->handshake_next) {
		rb_debug ("Too soon; time=%ld, handshake_next=%ld",
			  (long)time (NULL),
			  (long)audioscrobbler->priv->handshake_next);
		return FALSE;
	}

	if ((audioscrobbler->priv->username == NULL) ||
	    (strcmp (audioscrobbler->priv->username, "") == 0)) {
		rb_debug ("No username set");
		return FALSE;
	}

	if ((audioscrobbler->priv->session_key == NULL) ||
	    (strcmp (audioscrobbler->priv->session_key, "") == 0)) {
		return FALSE;
	}

	return TRUE;
}

static void
rb_audioscrobbler_do_handshake (RBAudioscrobbler *audioscrobbler)
{
	gchar *username;
	gchar *auth;
	gchar *autharg;
	gchar *query;
	const char *scrobble_url = rb_audioscrobbler_service_get_scrobbler_url (audioscrobbler->priv->service);
	guint timestamp;

	if (!rb_audioscrobbler_should_handshake (audioscrobbler)) {
		return;
	}

	username = g_uri_escape_string (audioscrobbler->priv->username, NULL, FALSE);
	timestamp = time (NULL);

	autharg = g_strdup_printf ("%s%d",
		                   rb_audioscrobbler_service_get_api_secret (audioscrobbler->priv->service),
		                   timestamp);
	auth = g_compute_checksum_for_string (G_CHECKSUM_MD5, autharg, -1);

	query = g_strdup_printf ("hs=true&p=%s&c=%s&v=%s&u=%s&t=%d&a=%s&api_key=%s&sk=%s",
				 SCROBBLER_VERSION,
				 CLIENT_ID,
				 CLIENT_VERSION,
				 username,
				 timestamp,
				 auth,
				 rb_audioscrobbler_service_get_api_key (audioscrobbler->priv->service),
				 audioscrobbler->priv->session_key);

	g_free (auth);
	g_free (autharg);
	g_free (username);

	rb_debug ("Performing handshake with Audioscrobbler server: %s", query);

	audioscrobbler->priv->status = HANDSHAKING;
	rb_audioscrobbler_statistics_changed (audioscrobbler);

	rb_audioscrobbler_perform (audioscrobbler,
                                   scrobble_url,
                                   SOUP_METHOD_GET,
                                   query,
                                   (GAsyncReadyCallback) rb_audioscrobbler_do_handshake_cb);
}


static void
rb_audioscrobbler_do_handshake_cb (SoupSession *session,
                                   GAsyncResult *result,
                                   RBAudioscrobbler *audioscrobbler)
{
	SoupMessage *message;
	GBytes *bytes;
	const char *body;

	rb_debug ("Handshake response");

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		message = soup_session_get_async_result_message (session, result);
		rb_audioscrobbler_parse_response (audioscrobbler, message, body, TRUE);
		g_bytes_unref (bytes);
	}

	rb_audioscrobbler_statistics_changed (audioscrobbler);

	switch (audioscrobbler->priv->status) {
	case STATUS_OK:
		audioscrobbler->priv->handshake = TRUE;
		audioscrobbler->priv->handshake_delay = INITIAL_HANDSHAKE_DELAY;
		audioscrobbler->priv->failures = 0;
		break;
	default:
		rb_debug ("Handshake failed");
		++audioscrobbler->priv->failures;

		audioscrobbler->priv->handshake_next = time (NULL) + audioscrobbler->priv->handshake_delay;

		audioscrobbler->priv->handshake_delay *= 2;
		if (audioscrobbler->priv->handshake_delay > MAX_HANDSHAKE_DELAY) {
			audioscrobbler->priv->handshake_delay = MAX_HANDSHAKE_DELAY;
		}
		rb_debug ("handshake delay is now %d minutes", audioscrobbler->priv->handshake_delay/60);
		break;
	}

	g_idle_add ((GSourceFunc) idle_unref_cb, audioscrobbler);
}

static gchar *
rb_audioscrobbler_build_post_data (RBAudioscrobbler *audioscrobbler)
{
	g_return_val_if_fail (!g_queue_is_empty (audioscrobbler->priv->queue), NULL);

	gchar *post_data = g_strdup_printf ("s=%s", audioscrobbler->priv->sessionid);
	int i = 0;
	do {
		AudioscrobblerEntry *entry;
		AudioscrobblerEncodedEntry *encoded;
		gchar *new;

		/* remove first queue entry */
		entry = g_queue_pop_head (audioscrobbler->priv->queue);
		encoded = rb_audioscrobbler_entry_encode (entry);
		new = g_strdup_printf ("%s&a[%d]=%s&t[%d]=%s&b[%d]=%s&m[%d]=%s&l[%d]=%d&i[%d]=%s&o[%d]=%s&n[%d]=%s&r[%d]=",
				       post_data,
				       i, encoded->artist,
				       i, encoded->title,
				       i, encoded->album,
				       i, encoded->mbid,
				       i, encoded->length,
				       i, encoded->timestamp,
				       i, encoded->source,
				       i, encoded->track,
				       i);
		rb_audioscrobbler_encoded_entry_free (encoded);
		g_free (post_data);
		post_data = new;

		/* add to submission list */
		g_queue_push_tail (audioscrobbler->priv->submission, entry);
		i++;
	} while ((!g_queue_is_empty (audioscrobbler->priv->queue)) && (i < MAX_SUBMIT_SIZE));
	
	return post_data;
}

static void
rb_audioscrobbler_submit_queue (RBAudioscrobbler *audioscrobbler)
{
	if (audioscrobbler->priv->sessionid != NULL) {
		gchar *post_data;
	
		post_data = rb_audioscrobbler_build_post_data (audioscrobbler);

		rb_debug ("Submitting queue to Audioscrobbler");
		rb_audioscrobbler_print_queue (audioscrobbler, TRUE);

		rb_audioscrobbler_perform (audioscrobbler,
					   audioscrobbler->priv->submit_url,
					   SOUP_METHOD_POST,
					   post_data,
					   (GAsyncReadyCallback) rb_audioscrobbler_submit_queue_cb);
		 /* libsoup will free post_data when the request is finished */
	}
}

static void
rb_g_queue_concat (GQueue *q1, GQueue *q2)
{
	GList *elem;

	while (!g_queue_is_empty (q2)) {
		elem = g_queue_pop_head_link (q2);
		g_queue_push_tail_link (q1, elem);
	}
}

static void
rb_audioscrobbler_submit_queue_cb (SoupSession *session,
                                   GAsyncResult *result,
                                   RBAudioscrobbler *audioscrobbler)
{
	SoupMessage *message;
	GBytes *bytes;
	const char *body;

	rb_debug ("Submission response");

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		message = soup_session_get_async_result_message (session, result);
		rb_audioscrobbler_parse_response (audioscrobbler, message, body, FALSE);
		g_bytes_unref (bytes);
	}

	if (audioscrobbler->priv->status == STATUS_OK) {
		rb_debug ("Queue submitted successfully");
		rb_audioscrobbler_free_queue_entries (audioscrobbler, &audioscrobbler->priv->submission);
		audioscrobbler->priv->submission = g_queue_new ();
		rb_audioscrobbler_save_queue (audioscrobbler);

		audioscrobbler->priv->submit_count += audioscrobbler->priv->queue_count;
		audioscrobbler->priv->queue_count = 0;

		g_free (audioscrobbler->priv->submit_time);
		audioscrobbler->priv->submit_time = rb_utf_friendly_time (time (NULL));
	} else {
		++audioscrobbler->priv->failures;

		/* add failed submission entries back to queue */
		rb_g_queue_concat (audioscrobbler->priv->submission, 
				   audioscrobbler->priv->queue);
		g_assert (g_queue_is_empty (audioscrobbler->priv->queue));
		g_queue_free (audioscrobbler->priv->queue);
		audioscrobbler->priv->queue = audioscrobbler->priv->submission;
		audioscrobbler->priv->submission = g_queue_new ();;
		rb_audioscrobbler_save_queue (audioscrobbler);

		rb_audioscrobbler_print_queue (audioscrobbler, FALSE);

		if (audioscrobbler->priv->failures >= 3) {
			rb_debug ("Queue submission has failed %d times; caching tracks locally",
				  audioscrobbler->priv->failures);
			g_free (audioscrobbler->priv->status_msg);

			audioscrobbler->priv->handshake = FALSE;
			audioscrobbler->priv->status = GIVEN_UP;
			audioscrobbler->priv->status_msg = NULL;
		} else {
			rb_debug ("Queue submission failed %d times", audioscrobbler->priv->failures);
		}
	}

	rb_audioscrobbler_statistics_changed (audioscrobbler);
	g_idle_add ((GSourceFunc) idle_unref_cb, audioscrobbler);
}

static void
rb_audioscrobbler_song_changed_cb (RBShellPlayer *player,
				   RhythmDBEntry *entry,
				   RBAudioscrobbler *audioscrobbler)
{
	gboolean got_time;
	guint playing_time;

	if (audioscrobbler->priv->currently_playing != NULL) {
		rb_audioscrobbler_entry_free (audioscrobbler->priv->currently_playing);
		audioscrobbler->priv->currently_playing = NULL;
	}

	if (entry == NULL) {
		rb_debug ("called with no playing entry");
		return;
	}
	rb_debug ("new entry: %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

	got_time = rb_shell_player_get_playing_time (audioscrobbler->priv->shell_player,
						     &playing_time,
						     NULL);
	if (got_time) {
		audioscrobbler->priv->current_elapsed = (int) playing_time;
	} else {
		rb_debug ("didn't get playing time; assuming 0");
		audioscrobbler->priv->current_elapsed = 0;
	}

	if (rb_audioscrobbler_is_queueable (entry) && (got_time == FALSE || playing_time < 15)) {
		AudioscrobblerEntry *as_entry;

		/* even if it's the same song, it's being played again from
		 * the start so we can queue it again.
		 */
		as_entry = rb_audioscrobbler_entry_create (entry, audioscrobbler->priv->service);
		as_entry->play_time = time (NULL);
		audioscrobbler->priv->currently_playing = as_entry;
		audioscrobbler->priv->now_playing_updated = FALSE;
	}
}


/* Queue functions: */
static gboolean
rb_audioscrobbler_load_queue (RBAudioscrobbler *audioscrobbler)
{
	char *pathname;
	GFile *file;
	GError *error = NULL;
	char *data;
	char *start;
	char *end;
	gsize size;

	/* ensure we don't have a queue file saved without a username */
	pathname = g_build_filename (rb_user_data_dir (),
				     "audioscrobbler",
				     "submission-queues",
				     rb_audioscrobbler_service_get_name (audioscrobbler->priv->service),
				     NULL);
	if (g_file_test (pathname, G_FILE_TEST_IS_REGULAR)) {
		rb_debug ("deleting usernameless queue file %s", pathname);
		unlink (pathname);
	}
	g_free (pathname);

	/* we don't really care about errors enough to report them here */
	pathname = g_build_filename (rb_user_data_dir (),
	                             "audioscrobbler",
	                             "submission-queues",
	                             rb_audioscrobbler_service_get_name (audioscrobbler->priv->service),
	                             audioscrobbler->priv->username,
	                             NULL);
	file = g_file_new_for_path (pathname);
	rb_debug ("loading Audioscrobbler queue from \"%s\"", pathname);
	g_free (pathname);

	if (g_file_load_contents (file, NULL, &data, &size, NULL, &error) == FALSE) {
		rb_debug ("unable to load audioscrobbler queue: %s", error->message);
		g_error_free (error);
		return FALSE;
	}

	start = data;
	while (start < (data + size)) {
		AudioscrobblerEntry *entry;

		/* find the end of the line, to terminate the string */
		end = g_utf8_strchr (start, -1, '\n');
		if (end == NULL)
			break;
		*end = 0;

		entry = rb_audioscrobbler_entry_load_from_string (start);
		if (entry) {
			g_queue_push_tail (audioscrobbler->priv->queue,
					   entry);
			audioscrobbler->priv->queue_count++;
		}

		start = end + 1;
	}

	g_free (data);
	return TRUE;
}

static gboolean
rb_audioscrobbler_save_queue (RBAudioscrobbler *audioscrobbler)
{
	char *pathname;
	char *uri;
	GFile *file;
	GError *error = NULL;
	GList *l;
	GString *str;

	if (!audioscrobbler->priv->queue_changed) {
		return TRUE;
	}

	if (!audioscrobbler->priv->username) {
		rb_debug ("can't save queue without a username");
		return TRUE;
	}

	str = g_string_new ("");
	for (l = audioscrobbler->priv->queue->head; l != NULL; l = g_list_next (l)) {
		AudioscrobblerEntry *entry;

		entry = (AudioscrobblerEntry *) l->data;
		rb_audioscrobbler_entry_save_to_string (str, entry);
	}

	/* we don't really care about errors enough to report them here */
	pathname = g_build_filename (rb_user_data_dir (),
	                             "audioscrobbler",
	                             "submission-queues",
	                             rb_audioscrobbler_service_get_name (audioscrobbler->priv->service),
	                             audioscrobbler->priv->username,
	                             NULL);
	rb_debug ("Saving Audioscrobbler queue to \"%s\"", pathname);

	uri = g_filename_to_uri (pathname, NULL, NULL);
	rb_uri_create_parent_dirs (uri, &error);

	file = g_file_new_for_path (pathname);
	g_free (pathname);
	g_free (uri);

	error = NULL;
	g_file_replace_contents (file,
				 str->str, str->len,
				 NULL,
				 FALSE,
				 G_FILE_CREATE_NONE,
				 NULL,
				 NULL,
				 &error);
	g_string_free (str, TRUE);

	if (error == NULL) {
		audioscrobbler->priv->queue_changed = FALSE;
		return TRUE;
	} else {
		rb_debug ("error saving audioscrobbler queue: %s",
			  error->message);
		g_error_free (error);
		return FALSE;
	}
}

static void
rb_audioscrobbler_print_queue (RBAudioscrobbler *audioscrobbler, gboolean submission)
{
	GList *l;
	AudioscrobblerEntry *entry;
	int i = 0;

	if (submission) {
		l = audioscrobbler->priv->submission->head;
		rb_debug ("Audioscrobbler submission (%d entries): ", 
			  g_queue_get_length (audioscrobbler->priv->submission));

	} else {
		l = audioscrobbler->priv->queue->head;
		rb_debug ("Audioscrobbler queue (%d entries): ", 
			  g_queue_get_length (audioscrobbler->priv->queue));
	}

	for (; l != NULL; l = g_list_next (l)) {
		entry = (AudioscrobblerEntry *) l->data;
		rb_audioscrobbler_entry_debug (entry, ++i);
	}
}

static void
rb_audioscrobbler_free_queue_entries (RBAudioscrobbler *audioscrobbler, GQueue **queue)
{
	g_queue_foreach (*queue, (GFunc) rb_audioscrobbler_entry_free, NULL);
	g_queue_free (*queue);
	*queue = NULL;

	audioscrobbler->priv->queue_changed = TRUE;
}

static void
rb_audioscrobbler_nowplaying (RBAudioscrobbler *audioscrobbler, AudioscrobblerEntry *entry)
{
	AudioscrobblerEncodedEntry *encoded;
	gchar *post_data;

	if (audioscrobbler->priv->handshake) {
		encoded = rb_audioscrobbler_entry_encode (entry);

		post_data = g_strdup_printf ("s=%s&a=%s&t=%s&b=%s&l=%d&n=%s&m=%s",
					     audioscrobbler->priv->sessionid,
					     encoded->artist,
					     encoded->title,
					     encoded->album,
					     encoded->length,
					     encoded->track,
					     encoded->mbid);

		rb_audioscrobbler_perform (audioscrobbler,
					   audioscrobbler->priv->nowplaying_url,
					   SOUP_METHOD_POST,
					   post_data,
					   (GAsyncReadyCallback) rb_audioscrobbler_nowplaying_cb);

		rb_audioscrobbler_encoded_entry_free (encoded);
	}
}

static void
rb_audioscrobbler_nowplaying_cb (SoupSession *session,
                                 GAsyncResult *result,
                                 RBAudioscrobbler *audioscrobbler)
{
	SoupMessage *message;
	GBytes *bytes;
	const char *body;

	rb_debug ("Now playing response");

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		message = soup_session_get_async_result_message (session, result);
		rb_audioscrobbler_parse_response (audioscrobbler, message, body, FALSE);
		g_bytes_unref (bytes);
	}

	if (audioscrobbler->priv->status == STATUS_OK) {
		rb_debug("Submission success!");
	} else {
		rb_debug("Error submitting now playing information.");
	}

	g_idle_add ((GSourceFunc) idle_unref_cb, audioscrobbler);
}

void
_rb_audioscrobbler_register_type (GTypeModule *module)
{
	rb_audioscrobbler_register_type (module);
}
