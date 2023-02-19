/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * rhythmbox
 * Copyright (C) Alexandre Rosenfeld 2010 <alexandre.rosenfeld@gmail.com>
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

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include "rhythmdb.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-dacp-player.h"
#include "rb-daap-record.h"
#include "rb-playlist-manager.h"
#include "rb-play-queue-source.h"

struct _RBDACPPlayerPrivate {
	RBShell *shell;
	RBShellPlayer *shell_player;
	RBSource *play_queue;
};

static void rb_dacp_player_get_property (GObject *object, guint prop_id,
                                         GValue *value, GParamSpec *pspec);
static void rb_dacp_player_set_property (GObject *object, guint prop_id,
                                         const GValue *value, GParamSpec *pspec);

static void playing_song_changed (RBShellPlayer *shell_player, RhythmDBEntry *entry, RBDACPPlayer *player);
static void elapsed_changed (RBShellPlayer *shell_player, guint elapsed, RBDACPPlayer *player);

static DmapAvRecord *rb_dacp_player_now_playing_record  (DmapControlPlayer *player);
gchar *rb_dacp_player_now_playing_artwork (DmapControlPlayer *player, guint width, guint height);
static void rb_dacp_player_play_pause          (DmapControlPlayer *player);
static void rb_dacp_player_pause               (DmapControlPlayer *player);
static void rb_dacp_player_next_item           (DmapControlPlayer *player);
static void rb_dacp_player_prev_item           (DmapControlPlayer *player);

static void rb_dacp_player_cue_clear           (DmapControlPlayer *player);
static void rb_dacp_player_cue_play            (DmapControlPlayer *player, GList *records, guint index);

enum {
	PROP_0,
	PROP_PLAYING_TIME,
	PROP_SHUFFLE_STATE,
	PROP_REPEAT_STATE,
	PROP_PLAY_STATE,
	PROP_VOLUME
};

enum {
	PLAYER_UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rb_dacp_player_iface_init (gpointer iface, gpointer data)
{
	DmapControlPlayerInterface *dacp_player = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dacp_player) == DMAP_TYPE_CONTROL_PLAYER);

	dacp_player->now_playing_record  = rb_dacp_player_now_playing_record;
	dacp_player->now_playing_artwork = rb_dacp_player_now_playing_artwork;
	dacp_player->play_pause          = rb_dacp_player_play_pause;
	dacp_player->pause               = rb_dacp_player_pause;
	dacp_player->next_item           = rb_dacp_player_next_item;
	dacp_player->prev_item           = rb_dacp_player_prev_item;

	dacp_player->cue_clear           = rb_dacp_player_cue_clear;
	dacp_player->cue_play            = rb_dacp_player_cue_play;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBDACPPlayer,
				rb_dacp_player,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_CONTROL_PLAYER,
							       rb_dacp_player_iface_init))

static void
rb_dacp_player_init (RBDACPPlayer *object)
{
	object->priv = RB_DACP_PLAYER_GET_PRIVATE (object);
}

static void
rb_dacp_player_finalize (GObject *object)
{
	RBDACPPlayer *player = RB_DACP_PLAYER (object);

	g_signal_handlers_disconnect_by_func (player->priv->shell_player, playing_song_changed, player);

	g_object_unref (player->priv->shell);
	g_object_unref (player->priv->shell_player);

	G_OBJECT_CLASS (rb_dacp_player_parent_class)->finalize (object);
}

static void
rb_dacp_player_class_init (RBDACPPlayerClass *klass)
{
	GObjectClass* object_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBDACPPlayerPrivate));

	object_class->set_property = rb_dacp_player_set_property;
	object_class->get_property = rb_dacp_player_get_property;
	object_class->finalize     = rb_dacp_player_finalize;

	g_object_class_override_property (object_class, PROP_PLAYING_TIME, "playing-time");
	g_object_class_override_property (object_class, PROP_SHUFFLE_STATE, "shuffle-state");
	g_object_class_override_property (object_class, PROP_REPEAT_STATE, "repeat-state");
	g_object_class_override_property (object_class, PROP_PLAY_STATE, "play-state");
	g_object_class_override_property (object_class, PROP_VOLUME, "volume");

	signals[PLAYER_UPDATED] =
		g_signal_new ("player_updated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBDACPPlayerClass, player_updated),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE, 0);

	object_class->finalize = rb_dacp_player_finalize;
}

static void
rb_dacp_player_class_finalize (RBDACPPlayerClass *klass)
{
}

static void
rb_dacp_player_get_property (GObject *object,
                             guint prop_id,
                             GValue *value,
                             GParamSpec *pspec)
{
	RBDACPPlayer *player = RB_DACP_PLAYER (object);

	gboolean repeat;
	gboolean shuffle;
	guint playing_time;
	gboolean playing;
	gdouble volume;
	RhythmDBEntry *entry;

	switch (prop_id) {
		case PROP_PLAYING_TIME:
			rb_shell_player_get_playing_time (player->priv->shell_player, &playing_time, NULL);
			g_value_set_ulong (value, playing_time * 1000);
			break;
		case PROP_SHUFFLE_STATE:
			rb_shell_player_get_playback_state (player->priv->shell_player, &shuffle, &repeat);
			g_value_set_boolean (value, shuffle);
			break;
		case PROP_REPEAT_STATE:
			rb_shell_player_get_playback_state (player->priv->shell_player, &shuffle, &repeat);
			g_value_set_enum (value, repeat ? DMAP_CONTROL_REPEAT_ALL : DMAP_CONTROL_REPEAT_NONE);
			break;
		case PROP_PLAY_STATE:
			entry = rb_shell_player_get_playing_entry (player->priv->shell_player);
			if (entry) {
				g_object_get (player->priv->shell_player, "playing", &playing, NULL);
				g_value_set_enum (value, playing ? DMAP_CONTROL_PLAY_PLAYING : DMAP_CONTROL_PLAY_PAUSED);
				rhythmdb_entry_unref (entry);
			} else {
				g_value_set_enum (value, DMAP_CONTROL_PLAY_STOPPED);
			}
			break;
		case PROP_VOLUME:
			rb_shell_player_get_volume (player->priv->shell_player, &volume, NULL);
			g_value_set_ulong (value, (gulong) ceil (volume * 100.0));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_dacp_player_set_property (GObject *object,
                             guint prop_id,
                             const GValue *value,
                             GParamSpec *pspec)
{
	RBDACPPlayer *player = RB_DACP_PLAYER (object);

	gboolean shuffle;
	gboolean repeat;
	gulong playing_time;
	gdouble volume;

	switch (prop_id) {
		case PROP_PLAYING_TIME:
			playing_time = g_value_get_ulong (value);
			rb_shell_player_set_playing_time (player->priv->shell_player, (gulong) ceil (playing_time / 1000), NULL);
			break;
		case PROP_SHUFFLE_STATE:
			rb_shell_player_get_playback_state (player->priv->shell_player, &shuffle, &repeat);
			rb_shell_player_set_playback_state (player->priv->shell_player, g_value_get_boolean (value), repeat);
			break;
		case PROP_REPEAT_STATE:
			rb_shell_player_get_playback_state (player->priv->shell_player, &shuffle, &repeat);
			rb_shell_player_set_playback_state (player->priv->shell_player, shuffle, g_value_get_enum (value) != DMAP_CONTROL_REPEAT_NONE);
			break;
		case PROP_VOLUME:
			volume = ((double) g_value_get_ulong (value))  / 100.0;
			rb_shell_player_set_volume (player->priv->shell_player, volume, NULL);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
playing_song_changed (RBShellPlayer *shell_player,
                      RhythmDBEntry *entry,
                      RBDACPPlayer *player)
{
	g_signal_emit (player, signals [PLAYER_UPDATED], 0);
}

static void
elapsed_changed (RBShellPlayer *shell_player,
                 guint elapsed,
                 RBDACPPlayer *player)
{
	g_signal_emit (player, signals [PLAYER_UPDATED], 0);
}

RBDACPPlayer *
rb_dacp_player_new (RBShell *shell)
{
	RBDACPPlayer *player;

	player = RB_DACP_PLAYER (g_object_new (RB_TYPE_DACP_PLAYER, NULL));

	player->priv->shell = g_object_ref (shell);
	g_object_get (shell,
		      "shell-player", &player->priv->shell_player,
		      "queue-source", &player->priv->play_queue,
		      NULL);

	g_signal_connect_object (player->priv->shell_player,
	                         "playing-song-changed",
	                         G_CALLBACK (playing_song_changed),
	                         player,
	                         0);
	g_signal_connect_object (player->priv->shell_player,
	                         "elapsed-changed",
	                         G_CALLBACK (elapsed_changed),
	                         player,
	                         0);

	return player;
}

static DmapAvRecord *
rb_dacp_player_now_playing_record (DmapControlPlayer *player)
{
	RhythmDBEntry *entry;
	DmapAvRecord *record;

	entry = rb_shell_player_get_playing_entry (RB_DACP_PLAYER (player)->priv->shell_player);
	if (entry == NULL) {
		return NULL;
	} else {
		record = DMAP_AV_RECORD (rb_daap_record_new (entry));
		rhythmdb_entry_unref (entry);
		return record;
	}
}

gchar *
rb_dacp_player_now_playing_artwork (DmapControlPlayer *player, guint width, guint height)
{
	return NULL;
}

static void
rb_dacp_player_play_pause (DmapControlPlayer *player)
{
	rb_shell_player_playpause (RB_DACP_PLAYER (player)->priv->shell_player, NULL);
}

static void
rb_dacp_player_pause (DmapControlPlayer *player)
{
	rb_shell_player_pause (RB_DACP_PLAYER (player)->priv->shell_player, NULL);
}

static void
rb_dacp_player_next_item (DmapControlPlayer *player)
{
	rb_shell_player_do_next (RB_DACP_PLAYER (player)->priv->shell_player, NULL);
}

static void
rb_dacp_player_prev_item (DmapControlPlayer *player)
{
	rb_shell_player_do_previous (RB_DACP_PLAYER (player)->priv->shell_player, NULL);
}

static void
rb_dacp_player_cue_clear (DmapControlPlayer *player)
{
	RBDACPPlayer *rbplayer;
	rbplayer = RB_DACP_PLAYER (player);
	rb_play_queue_source_clear_queue (RB_PLAY_QUEUE_SOURCE (rbplayer->priv->play_queue));
}

static void
rb_dacp_player_cue_play (DmapControlPlayer *player, GList *records, guint index)
{
	GList *record;
	gint current = 0;

	for (record = records; record; record = record->next) {
		gchar *location;
		RBDACPPlayer *rbplayer;

		g_object_get (record->data, "location", &location, NULL);
		rbplayer = RB_DACP_PLAYER (player);
		rb_static_playlist_source_add_location (RB_STATIC_PLAYLIST_SOURCE (rbplayer->priv->play_queue),
							location,
							-1);

		if (current == index) {
			RhythmDB *db;
			RhythmDBEntry *entry;
			RBPlayQueueSource *queue;
			g_object_get (RB_DACP_PLAYER (player)->priv->shell,
			              "db", &db,
			              "queue-source", &queue,
			              NULL);
			entry = rhythmdb_entry_lookup_by_location (db, location);
			if (entry)
				rb_shell_player_play_entry (RB_DACP_PLAYER (player)->priv->shell_player, entry, RB_SOURCE (queue));
			g_object_unref (db);
			g_object_unref (queue);
		}

		g_free (location);
		current++;
	}
}

void
_rb_dacp_player_register_type (GTypeModule *module)
{
	rb_dacp_player_register_type (module);
}
