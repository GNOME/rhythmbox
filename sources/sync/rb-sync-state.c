/*
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "rb-sync-state.h"
#include "rb-util.h"
#include "rb-debug.h"

#include "rhythmdb-query-model.h"
#include "rb-podcast-manager.h"
#include "rb-podcast-entry-types.h"
#include "rb-playlist-manager.h"
#include "rb-shell.h"

struct _RBSyncStatePrivate
{
	/* we don't own a reference on these */
	RBMediaPlayerSource *source;
	RBSyncSettings *sync_settings;
};

enum {
	PROP_0,
	PROP_SOURCE,
	PROP_SYNC_SETTINGS
};

enum {
	UPDATED,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBSyncState, rb_sync_state, G_TYPE_OBJECT)

static gboolean
entry_is_undownloaded_podcast (RhythmDBEntry *entry)
{
	if (rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_POST) {
		return (!rb_podcast_manager_entry_downloaded (entry));
	}

	return FALSE;
}

static guint64
_sum_entry_size (GHashTable *entries)
{
	GHashTableIter iter;
	gpointer key, value;
	guint64 sum = 0;

	g_hash_table_iter_init (&iter, entries);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		RhythmDBEntry *entry = (RhythmDBEntry *)value;
		sum += rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
	}

	return sum;
}

static void
_g_hash_table_transfer_all (GHashTable *target, GHashTable *source)
{
	GHashTableIter iter;
	gpointer key, value;

	g_hash_table_iter_init (&iter, source);
	while (g_hash_table_iter_next (&iter, &key, &value)) {
		g_hash_table_insert (target, key, value);
		g_hash_table_iter_steal (&iter);
	}
}

char *
rb_sync_state_make_track_uuid  (RhythmDBEntry *entry)
{
	/* This function is for hashing the two databases for syncing. */
	GString *str = g_string_new ("");
	char *result;

	/*
	 * possible improvements here:
	 * - use musicbrainz track ID if known (maybe not a great idea?)
	 * - maybe don't include genre, since there's no canonical genre for anything
	 */

	g_string_printf (str, "%s%s%s%s%lu%lu",
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE),
			 rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_TRACK_NUMBER),
			 rhythmdb_entry_get_ulong  (entry, RHYTHMDB_PROP_DISC_NUMBER));

	/* not sure why we md5 this.  how does it help? */
	result = g_compute_checksum_for_string (G_CHECKSUM_MD5, str->str, str->len);

	g_string_free (str, TRUE);

	return result;
}

static void
free_sync_lists (RBSyncState *state)
{
	rb_list_destroy_free (state->sync_to_add, (GDestroyNotify) rhythmdb_entry_unref);
	rb_list_destroy_free (state->sync_to_remove, (GDestroyNotify) rhythmdb_entry_unref);
	state->sync_to_add = NULL;
	state->sync_to_remove = NULL;
}

typedef struct {
	GHashTable *target;
	GList *result;
	guint64 bytes;
	guint64 duration;
} BuildSyncListData;

static void
build_sync_list_cb (char *uuid, RhythmDBEntry *entry, BuildSyncListData *data)
{
	guint64 bytes;
	gulong duration;

	if (g_hash_table_lookup (data->target, uuid) != NULL) {
		/* already present */
		return;
	}

	bytes = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
	duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
	rb_debug ("adding %s (%" G_GINT64_FORMAT " bytes); id %s to sync list",
		  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
		  bytes,
		  uuid);
	data->bytes += bytes;
	data->duration += duration;
	data->result = g_list_prepend (data->result, rhythmdb_entry_ref (entry));
}


static gboolean
hash_table_insert_from_tree_model_cb (GtkTreeModel *query_model,
				      GtkTreePath  *path,
				      GtkTreeIter  *iter,
				      GHashTable   *target)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), iter);
	if (!entry_is_undownloaded_podcast (entry)) {
		g_hash_table_insert (target,
				     rb_sync_state_make_track_uuid (entry),
				     rhythmdb_entry_ref (entry));
	}

	return FALSE;
}

static void
itinerary_insert_all_of_type (RhythmDB *db,
			      RhythmDBEntryType *entry_type,
			      GHashTable *target)
{
	GtkTreeModel *query_model;

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_END);

	gtk_tree_model_foreach (query_model,
				(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
				target);
}

static void
itinerary_insert_some_playlists (RBSyncState *state,
				 GHashTable *target)
{
	GList *list_iter;
	GList *playlists;
	RBPlaylistManager *playlist_manager;
	RBShell *shell;

	g_object_get (state->priv->source, "shell", &shell, NULL);
	g_object_get (shell, "playlist-manager", &playlist_manager, NULL);
	playlists = rb_playlist_manager_get_playlists (playlist_manager);
	g_object_unref (playlist_manager);
	g_object_unref (shell);

	for (list_iter = playlists; list_iter; list_iter = list_iter->next) {
		gchar *name;

		g_object_get (list_iter->data, "name", &name, NULL);

		/* See if we should sync it */
		if (rb_sync_settings_sync_group (state->priv->sync_settings, SYNC_CATEGORY_MUSIC, name)) {
			GtkTreeModel *query_model;

			rb_debug ("adding entries from playlist %s to itinerary", name);
			g_object_get (RB_SOURCE (list_iter->data), "base-query-model", &query_model, NULL);
			gtk_tree_model_foreach (query_model,
						(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
						target);
			g_object_unref (query_model);
		} else {
			rb_debug ("not adding playlist %s to itinerary", name);
		}

		g_free (name);
	}

	g_list_free (playlists);
}

static void
itinerary_insert_some_podcasts (RBSyncState *state,
				RhythmDB *db,
				GHashTable *target)
{
	GList *podcasts;
	GList *i;

	podcasts = rb_sync_settings_get_enabled_groups (state->priv->sync_settings, SYNC_CATEGORY_PODCAST);
	for (i = podcasts; i != NULL; i = i->next) {
		GtkTreeModel *query_model;
		rb_debug ("adding entries from podcast %s to itinerary", (char *)i->data);
		query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
		rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					RHYTHMDB_QUERY_PROP_EQUALS,
					RHYTHMDB_PROP_SUBTITLE, i->data,
					RHYTHMDB_QUERY_END);

		/* TODO: exclude undownloaded episodes, sort by post date, set limit, optionally exclude things with play count > 0
		 * RHYTHMDB_QUERY_PROP_NOT_EQUAL, RHYTHMDB_PROP_MOUNTPOINT, NULL,	(will this work?)
		 * RHYTHMDB_QUERY_PROP_NOT_EQUAL, RHYTHMDB_PROP_STATUS, RHYTHMDB_PODCAST_STATUS_ERROR,
		 *
		 * RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_PLAYCOUNT, 0
		 */

		gtk_tree_model_foreach (query_model,
					(GtkTreeModelForeachFunc) hash_table_insert_from_tree_model_cb,
					target);
		g_object_unref (query_model);
	}
}

static GHashTable *
build_sync_itinerary (RBSyncState *state)
{
	RBShell *shell;
	RhythmDB *db;
	GHashTable *itinerary;

	rb_debug ("building itinerary hash");

	g_object_get (state->priv->source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	itinerary = g_hash_table_new_full (g_str_hash,
					   g_str_equal,
					   g_free,
					   (GDestroyNotify)rhythmdb_entry_unref);

	if (rb_sync_settings_sync_category (state->priv->sync_settings, SYNC_CATEGORY_MUSIC) ||
	    rb_sync_settings_sync_group (state->priv->sync_settings, SYNC_CATEGORY_MUSIC, SYNC_GROUP_ALL_MUSIC)) {
		rb_debug ("adding all music to the itinerary");
		itinerary_insert_all_of_type (db, RHYTHMDB_ENTRY_TYPE_SONG, itinerary);
	} else if (rb_sync_settings_has_enabled_groups (state->priv->sync_settings, SYNC_CATEGORY_MUSIC)) {
		rb_debug ("adding selected playlists to the itinerary");
		itinerary_insert_some_playlists (state, itinerary);
	}

	state->sync_music_size = _sum_entry_size (itinerary);

	if (rb_sync_settings_sync_category (state->priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		rb_debug ("adding all podcasts to the itinerary");
		/* TODO: when we get #episodes/not-if-played settings, use
		 * equivalent of insert_some_podcasts, iterating through all feeds
		 * (use a query for all entries of type PODCAST_FEED to find them)
		 */
		itinerary_insert_all_of_type (db, RHYTHMDB_ENTRY_TYPE_PODCAST_POST, itinerary);
	} else if (rb_sync_settings_has_enabled_groups (state->priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		rb_debug ("adding selected podcasts to the itinerary");
		itinerary_insert_some_podcasts (state, db, itinerary);
	}

	state->sync_podcast_size = _sum_entry_size (itinerary) - state->sync_music_size;

	g_object_unref (shell);
	g_object_unref (db);

	rb_debug ("finished building itinerary hash; has %d entries", g_hash_table_size (itinerary));
	return itinerary;
}

static GHashTable *
build_device_state (RBSyncState *state)
{
	GHashTable *device;
	GHashTable *entries;

	rb_debug ("building device contents hash");

	device = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify)rhythmdb_entry_unref);

	rb_debug ("getting music entries from device");
	entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) rhythmdb_entry_unref);

	rb_media_player_source_get_entries (state->priv->source, SYNC_CATEGORY_MUSIC, entries);
	/*klass->impl_get_entries (source, SYNC_CATEGORY_MUSIC, entries);*/
	state->total_music_size = _sum_entry_size (entries);
	if (rb_sync_settings_sync_category (state->priv->sync_settings, SYNC_CATEGORY_MUSIC) ||
	    rb_sync_settings_has_enabled_groups (state->priv->sync_settings, SYNC_CATEGORY_MUSIC)) {
		_g_hash_table_transfer_all (device, entries);
	}
	g_hash_table_destroy (entries);
	rb_debug ("done getting music entries from device");

	rb_debug ("getting podcast entries from device");
	entries = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, (GDestroyNotify) rhythmdb_entry_unref);
	rb_media_player_source_get_entries (state->priv->source, SYNC_CATEGORY_PODCAST, entries);
	/*klass->impl_get_entries (source, SYNC_CATEGORY_PODCAST, entries);*/
	state->total_podcast_size = _sum_entry_size (entries);
	if (rb_sync_settings_sync_category (state->priv->sync_settings, SYNC_CATEGORY_PODCAST) ||
	    rb_sync_settings_has_enabled_groups (state->priv->sync_settings, SYNC_CATEGORY_PODCAST)) {
		_g_hash_table_transfer_all (device, entries);
	}
	g_hash_table_destroy (entries);
	rb_debug ("done getting podcast entries from device");

	rb_debug ("done building device contents hash; has %d entries", g_hash_table_size (device));
	return device;
}

void
rb_sync_state_update (RBSyncState *state)
{
	GHashTable *device;
	GHashTable *itinerary;
	BuildSyncListData data;
	GList *list_iter;
	gint64 add_size = 0;
	gint64 remove_size = 0;

	/* clear existing state */
	free_sync_lists (state);

	/* figure out what we want on the device and what's already there */
	itinerary = build_sync_itinerary (state);
	device = build_device_state (state);

	/* figure out what to add to the device */
	rb_debug ("building list of files to transfer to device");
	data.target = device;
	data.result = NULL;
	g_hash_table_foreach (itinerary, (GHFunc)build_sync_list_cb, &data);
	state->sync_to_add = data.result;
	state->sync_add_size = data.bytes;
	state->sync_add_count = g_list_length (state->sync_to_add);
	rb_debug ("decided to transfer %d files (%" G_GINT64_FORMAT" bytes) to the device",
		  state->sync_add_count,
		  state->sync_add_size);

	/* and what to remove */
	rb_debug ("building list of files to remove from device");
	data.target = itinerary;
	data.result = NULL;
	g_hash_table_foreach (device, (GHFunc)build_sync_list_cb, &data);
	state->sync_to_remove = data.result;
	state->sync_remove_size = data.bytes;
	state->sync_remove_count = g_list_length (state->sync_to_remove);
	rb_debug ("decided to remove %d files (%" G_GINT64_FORMAT" bytes) from the device",
		  state->sync_remove_count,
		  state->sync_remove_size);

	state->sync_keep_count = g_hash_table_size (device) - g_list_length (state->sync_to_remove);
	rb_debug ("keeping %d files on the device", state->sync_keep_count);

	g_hash_table_destroy (device);
	g_hash_table_destroy (itinerary);

	/* calculate space requirements */
	for (list_iter = state->sync_to_add; list_iter; list_iter = list_iter->next) {
		add_size += rhythmdb_entry_get_uint64 (list_iter->data, RHYTHMDB_PROP_FILE_SIZE);
	}

	for (list_iter = state->sync_to_remove; list_iter; list_iter = list_iter->next) {
		remove_size += rhythmdb_entry_get_uint64 (list_iter->data, RHYTHMDB_PROP_FILE_SIZE);
	}

	state->sync_space_needed = rb_media_player_source_get_capacity (state->priv->source) -
				   rb_media_player_source_get_free_space (state->priv->source);
	rb_debug ("current space used: %" G_GINT64_FORMAT " bytes; adding %" G_GINT64_FORMAT ", removing %" G_GINT64_FORMAT,
		  state->sync_space_needed,
		  add_size,
		  remove_size);
	state->sync_space_needed = state->sync_space_needed + add_size - remove_size;
	rb_debug ("space used after sync: %" G_GINT64_FORMAT " bytes", state->sync_space_needed);

	g_signal_emit (state, signals[UPDATED], 0);
}

static void
sync_settings_updated (RBSyncSettings *settings, RBSyncState *state)
{
	rb_debug ("sync settings updated, updating state");
	rb_sync_state_update (state);
}


RBSyncState *
rb_sync_state_new (RBMediaPlayerSource *source, RBSyncSettings *settings)
{
	GObject *state;
	state = g_object_new (RB_TYPE_SYNC_STATE,
			      "source", source,
			      "sync-settings", settings,
			      NULL);
	return RB_SYNC_STATE (state);
}


static void
rb_sync_state_init (RBSyncState *state)
{
	state->priv = G_TYPE_INSTANCE_GET_PRIVATE (state, RB_TYPE_SYNC_STATE, RBSyncStatePrivate);
}

static void
impl_constructed (GObject *object)
{
	RBSyncState *state = RB_SYNC_STATE (object);

	rb_sync_state_update (state);

	g_signal_connect_object (state->priv->sync_settings,
				 "updated",
				 G_CALLBACK (sync_settings_updated),
				 state, 0);

	RB_CHAIN_GOBJECT_METHOD(rb_sync_state_parent_class, constructed, object);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSyncState *state = RB_SYNC_STATE (object);
	switch (prop_id) {
	case PROP_SOURCE:
		state->priv->source = g_value_get_object (value);
		break;
	case PROP_SYNC_SETTINGS:
		state->priv->sync_settings = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSyncState *state = RB_SYNC_STATE (object);
	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, state->priv->source);
		break;
	case PROP_SYNC_SETTINGS:
		g_value_set_object (value, state->priv->sync_settings);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_finalize (GObject *object)
{
	RBSyncState *state = RB_SYNC_STATE (object);

	free_sync_lists (state);

	G_OBJECT_CLASS (rb_sync_state_parent_class)->finalize (object);
}

static void
rb_sync_state_class_init (RBSyncStateClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;
	object_class->constructed = impl_constructed;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "source",
							      "RBMediaPlayerSource instance",
							      RB_TYPE_MEDIA_PLAYER_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SYNC_SETTINGS,
					 g_param_spec_object ("sync-settings",
							      "sync-settings",
							      "RBSyncSettings instance",
							      RB_TYPE_SYNC_SETTINGS,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[UPDATED] = g_signal_new ("updated",
					 RB_TYPE_SYNC_STATE,
					 G_SIGNAL_RUN_LAST,
					 G_STRUCT_OFFSET (RBSyncStateClass, updated),
					 NULL, NULL,
					 NULL,
					 G_TYPE_NONE,
					 0);

	g_type_class_add_private (object_class, sizeof (RBSyncStatePrivate));
}
