/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2005 Renato Araujo Oliveira Filho - INdT <renato.filho@indt.org.br>
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

#include <string.h>
#define __USE_XOPEN
#include <time.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gtk/gtk.h>
#include <libsoup/soup.h>

#include "rb-podcast-settings.h"
#include "rb-podcast-manager.h"
#include "rb-podcast-entry-types.h"
#include "rb-podcast-search.h"
#include "rb-file-helpers.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rhythmdb-query-result-list.h"
#include "rb-podcast-parse.h"
#include "rb-dialog.h"
#include "rb-metadata.h"
#include "rb-util.h"
#include "rb-missing-plugins.h"
#include "rb-ext-db.h"

#define DOWNLOAD_BUFFER_SIZE		65536
#define DOWNLOAD_RETRY_DELAY		15

enum
{
	PROP_0,
	PROP_DB,
	PROP_UPDATING
};

enum
{
	START_DOWNLOAD,
	FINISH_DOWNLOAD,
	FEED_UPDATE_STATUS,

	LAST_SIGNAL
};

typedef struct
{
	RBPodcastManager *pd;

	RhythmDBEntry *entry;

	SoupMessage *request;
	GFile *destination;
	GInputStream *in_stream;
	GFileOutputStream *out_stream;

	guint64 download_size;
	guint progress;
	char *buffer;

	GCancellable *cancel;
	GTask *task;
} RBPodcastDownload;

typedef struct
{
	RBPodcastManager *pd;
	gboolean automatic;
	RBPodcastChannel *channel;
	GError *error;
} RBPodcastUpdate;

struct RBPodcastManagerPrivate
{
	RhythmDB *db;
	GList *download_list;
	RBPodcastDownload *active_download;
	RBExtDB *art_store;
	GCancellable *update_cancel;

	guint update_feeds_id;
	GList *updating;

	GArray *searches;
	GSettings *settings;
	GFile *timestamp_file;

	SoupSession *soup_session;
};

#define RB_PODCAST_MANAGER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PODCAST_MANAGER, RBPodcastManagerPrivate))


static guint rb_podcast_manager_signals[LAST_SIGNAL] = { 0 };

/* functions */
static void rb_podcast_manager_class_init 		(RBPodcastManagerClass *klass);
static void rb_podcast_manager_init 			(RBPodcastManager *dp);
static void rb_podcast_manager_constructed		(GObject *object);
static void rb_podcast_manager_dispose 			(GObject *object);
static void rb_podcast_manager_finalize 		(GObject *object);
static void rb_podcast_manager_set_property 		(GObject *object,
                                   			 guint prop_id,
		                                	 const GValue *value,
		                                	 GParamSpec *pspec);
static void rb_podcast_manager_get_property 		(GObject *object,
							 guint prop_id,
		                                	 GValue *value,
                		                	 GParamSpec *pspec);
static gboolean rb_podcast_manager_update_feeds_cb 	(gpointer data);
static void rb_podcast_manager_save_metadata		(RBPodcastManager *pd,
						  	 RhythmDBEntry *entry);
static void rb_podcast_manager_db_entry_added_cb 	(RBPodcastManager *pd,
							 RhythmDBEntry *entry);
static gboolean rb_podcast_manager_next_file 		(RBPodcastManager * pd);

static void podcast_settings_changed_cb			(GSettings *settings,
							 const char *key,
							 RBPodcastManager *mgr);

/* internal functions */
static gboolean retry_on_http_status			(SoupStatus status);
static gboolean retry_on_error                          (GError *error);
static void download_task				(GTask *task,
							 gpointer source_object,
							 gpointer task_data,
							 GCancellable *cancel);
static void download_info_free				(RBPodcastDownload *data);
static gboolean cancel_download				(RBPodcastDownload *pd);
static void rb_podcast_manager_start_update_timer 	(RBPodcastManager *pd);

static void podcast_album_art_request_cb		(RBExtDB *db,
							 RBExtDBKey *key,
							 guint64 last_time,
							 RBPodcastManager *pd);

G_DEFINE_TYPE (RBPodcastManager, rb_podcast_manager, G_TYPE_OBJECT)

static void
rb_podcast_manager_class_init (RBPodcastManagerClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_podcast_manager_constructed;
	object_class->dispose = rb_podcast_manager_dispose;
	object_class->finalize = rb_podcast_manager_finalize;

	object_class->set_property = rb_podcast_manager_set_property;
        object_class->get_property = rb_podcast_manager_get_property;

	g_object_class_install_property (object_class,
                                         PROP_DB,
                                         g_param_spec_object ("db",
							      "db",
							      "database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_UPDATING,
					 g_param_spec_boolean ("updating",
							       "updating",
							       "updating",
							       FALSE,
							       G_PARAM_READABLE));

	rb_podcast_manager_signals[START_DOWNLOAD] =
	       g_signal_new ("start_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				NULL,
				G_TYPE_NONE,
				1,
				RHYTHMDB_TYPE_ENTRY);

	rb_podcast_manager_signals[FINISH_DOWNLOAD] =
	       g_signal_new ("finish_download",
		       		G_OBJECT_CLASS_TYPE (object_class),
		 		G_SIGNAL_RUN_LAST,
				0,
				NULL, NULL,
				NULL,
				G_TYPE_NONE,
				2,
				RHYTHMDB_TYPE_ENTRY,
				G_TYPE_ERROR);

	rb_podcast_manager_signals[FEED_UPDATE_STATUS] =
		g_signal_new ("feed-update-status",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      3,
			      G_TYPE_STRING,
			      RB_TYPE_PODCAST_FEED_UPDATE_STATUS,
			      G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBPodcastManagerPrivate));
}

static void
rb_podcast_manager_init (RBPodcastManager *pd)
{
	pd->priv = RB_PODCAST_MANAGER_GET_PRIVATE (pd);

	pd->priv->update_feeds_id = 0;
	pd->priv->db = NULL;
}

static void
rb_podcast_manager_constructed (GObject *object)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);
	GFileOutputStream *st;
	char *ts_file_path;
	GError *error = NULL;

	RB_CHAIN_GOBJECT_METHOD (rb_podcast_manager_parent_class, constructed, object);

	/* add built in search types */
	pd->priv->searches = g_array_new (FALSE, FALSE, sizeof (GType));
	rb_podcast_manager_add_search (pd, rb_podcast_search_itunes_get_type ());

	pd->priv->settings = g_settings_new (PODCAST_SETTINGS_SCHEMA);
	g_signal_connect_object (pd->priv->settings,
				 "changed",
				 G_CALLBACK (podcast_settings_changed_cb),
				 pd, 0);

	ts_file_path = g_build_filename (rb_user_data_dir (), "podcast-timestamp", NULL);
	pd->priv->timestamp_file = g_file_new_for_path (ts_file_path);
	g_free (ts_file_path);

	/* create it if it doesn't exist */
	st = g_file_create (pd->priv->timestamp_file, G_FILE_CREATE_NONE, NULL, &error);
	if (st != NULL) {
		rb_debug ("podcast update file created");
		g_output_stream_close (G_OUTPUT_STREAM (st), NULL, NULL);
		g_object_unref (st);
	} else {
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			rb_debug ("unable to create podcast timestamp file");
			g_clear_object (&pd->priv->timestamp_file);
		} else {
			rb_debug ("podcast timestamp file already exists");
		}
	}

	pd->priv->art_store = rb_ext_db_new ("album-art");
	g_signal_connect (pd->priv->art_store, "request", G_CALLBACK (podcast_album_art_request_cb), pd);

	pd->priv->soup_session = soup_session_new ();
	soup_session_set_user_agent (pd->priv->soup_session, PACKAGE "/" VERSION);

	pd->priv->update_cancel = g_cancellable_new ();

	rb_podcast_manager_start_update_timer (pd);
}

static void
rb_podcast_manager_dispose (GObject *object)
{
	RBPodcastManager *pd;
	g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PODCAST_MANAGER (object));

	pd = RB_PODCAST_MANAGER (object);
	g_return_if_fail (pd->priv != NULL);

	g_clear_handle_id (&pd->priv->update_feeds_id, g_source_remove);

	g_clear_object (&pd->priv->db);
	g_clear_object (&pd->priv->settings);
	g_clear_object (&pd->priv->timestamp_file);
	g_clear_object (&pd->priv->art_store);

	G_OBJECT_CLASS (rb_podcast_manager_parent_class)->dispose (object);
}

static void
rb_podcast_manager_finalize (GObject *object)
{
	RBPodcastManager *pd;
	g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_PODCAST_MANAGER (object));

	pd = RB_PODCAST_MANAGER(object);

	g_return_if_fail (pd->priv != NULL);

	if (pd->priv->download_list) {
		g_list_foreach (pd->priv->download_list, (GFunc)g_free, NULL);
		g_list_free (pd->priv->download_list);
	}

	g_array_free (pd->priv->searches, TRUE);

	G_OBJECT_CLASS (rb_podcast_manager_parent_class)->finalize (object);
}

static void
rb_podcast_manager_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);

	switch (prop_id) {
	case PROP_DB:
		if (pd->priv->db) {
			g_signal_handlers_disconnect_by_func (pd->priv->db,
							      G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
							      pd);
			g_object_unref (pd->priv->db);
		}

		pd->priv->db = g_value_get_object (value);
		g_object_ref (pd->priv->db);

	        g_signal_connect_object (pd->priv->db,
	                                 "entry-added",
	                                 G_CALLBACK (rb_podcast_manager_db_entry_added_cb),
	                                 pd, G_CONNECT_SWAPPED);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}
}

static void
rb_podcast_manager_get_property (GObject *object,
					guint prop_id,
		                        GValue *value,
                		        GParamSpec *pspec)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, pd->priv->db);
		break;
	case PROP_UPDATING:
		g_value_set_boolean (value, (g_list_length (pd->priv->updating) > 0));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
	}

}

RBPodcastManager *
rb_podcast_manager_new (RhythmDB *db)
{
	RBPodcastManager *pd;

	pd = g_object_new (RB_TYPE_PODCAST_MANAGER, "db", db, NULL);
	return pd;
}

static const char *
get_download_location (RhythmDBEntry *entry)
{
	/* We haven't tried to download the entry yet */
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) == NULL)
		return NULL;
	return rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
}

static void
set_download_location (RhythmDB *db, RhythmDBEntry *entry, GValue *value)
{
	char *remote_location;

	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) == NULL) {
		/* If the download location was never set */
		GValue val = {0, };

		/* Save the remote location */
		remote_location = g_strdup (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		/* Set the new download location */
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LOCATION, value);
		/* Set MOUNTPOINT to the remote location */
		g_value_init (&val, G_TYPE_STRING);
		g_value_take_string (&val, remote_location);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
		g_value_unset (&val);
	} else {
		/* Just update the location */
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LOCATION, value);
	}
}

static void
unset_download_location (RhythmDB *db, RhythmDBEntry *entry)
{
	GValue val = {0, };
	const char *remote_location;

	remote_location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (remote_location == NULL)
		return;

	/* restore mountpoint as location */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, remote_location);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LOCATION, &val);

	/* unset mountpoint */
	g_value_reset (&val);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
	g_value_unset (&val);
}

static const char *
get_remote_location (RhythmDBEntry *entry)
{
	const char *location;
	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL)
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	return location;
}

static void
set_remote_location (RhythmDB *db, RhythmDBEntry *entry, const char *url)
{
	GValue val = {0, };
	const char *old;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, url);

	old = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (old == NULL) {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LOCATION, &val);
	} else {
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_MOUNTPOINT, &val);
	}
	g_value_unset (&val);
}

void
rb_podcast_manager_download_entry (RBPodcastManager *pd,
				   RhythmDBEntry *entry)
{
	gulong status;
	g_assert (rb_is_main_thread ());

	g_return_if_fail (RB_IS_PODCAST_MANAGER (pd));

	if (entry == NULL)
		return;
	
	if (rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN))
		return;

	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	if ((status < RHYTHMDB_PODCAST_STATUS_COMPLETE) ||
	    (status == RHYTHMDB_PODCAST_STATUS_WAITING)) {
		RBPodcastDownload *data;
		GValue val = { 0, };
		GTimeVal now;

		if (status < RHYTHMDB_PODCAST_STATUS_COMPLETE) {
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_WAITING);
			rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);
		}

		/* set last seen time so it shows up in the 'new downloads' subsource */
		g_value_init (&val, G_TYPE_ULONG);
		g_get_current_time (&now);
		g_value_set_ulong (&val, now.tv_sec);
		rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_LAST_SEEN, &val);
		g_value_unset (&val);
		rhythmdb_commit (pd->priv->db);

		rb_debug ("Adding podcast episode %s to download list", get_remote_location (entry));

		data = g_new0 (RBPodcastDownload, 1);
		data->pd = g_object_ref (pd);
		data->entry = rhythmdb_entry_ref (entry);

		pd->priv->download_list = g_list_append (pd->priv->download_list, data);
		rb_podcast_manager_next_file (pd);
	}
}

gboolean
rb_podcast_manager_entry_downloaded (RhythmDBEntry *entry)
{
	gulong status;
	const gchar *file_name;
	RhythmDBEntryType *type = rhythmdb_entry_get_entry_type (entry);

	g_assert (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST);

	status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
	file_name = get_download_location (entry);

	return (status != RHYTHMDB_PODCAST_STATUS_ERROR && file_name != NULL);
}

gboolean
rb_podcast_manager_entry_in_download_queue (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	RBPodcastDownload *info;
	GList *l;

	for (l = pd->priv->download_list; l != NULL; l = l->next) {
		info = l->data;
		if (info->entry == entry) {
			return TRUE;
		}
	}

	return FALSE;
}

static void
rb_podcast_manager_start_update_timer (RBPodcastManager *pd)
{
	guint64 last_time;
	guint64 interval_sec;
	guint64 now;
	GFileInfo *fi;
	RBPodcastInterval interval;

	g_return_if_fail (RB_IS_PODCAST_MANAGER (pd));

	g_clear_handle_id (&pd->priv->update_feeds_id, g_source_remove);

	if (pd->priv->timestamp_file == NULL) {
		rb_debug ("unable to record podcast update time, so periodic updates are disabled");
		return;
	}

	interval = g_settings_get_enum (pd->priv->settings,
					PODCAST_DOWNLOAD_INTERVAL);
	if (interval == PODCAST_INTERVAL_MANUAL) {
		rb_debug ("periodic podcast updates disabled");
		return;
	}

	/* get last update time */
	fi = g_file_query_info (pd->priv->timestamp_file,
				G_FILE_ATTRIBUTE_TIME_MODIFIED,
				G_FILE_QUERY_INFO_NONE,
				NULL,
				NULL);
	if (fi != NULL) {
		last_time = g_file_info_get_attribute_uint64 (fi, G_FILE_ATTRIBUTE_TIME_MODIFIED);
		g_object_unref (fi);
	} else {
		last_time = 0;
	}

	switch (interval) {
	case PODCAST_INTERVAL_HOURLY:
		interval_sec = 3600;
		break;
	case PODCAST_INTERVAL_DAILY:
		interval_sec = (3600 * 24);
		break;
	case PODCAST_INTERVAL_WEEKLY:
		interval_sec = (3600 * 24 * 7);
		break;
	default:
		g_assert_not_reached ();
		return;
	}

	/* wait until next update time */
	now = time (NULL);
	rb_debug ("last periodic update at %" G_GUINT64_FORMAT ", interval %" G_GUINT64_FORMAT ", time is now %" G_GUINT64_FORMAT,
		  last_time, interval_sec, now);

	if (last_time + interval_sec < now) {
		rb_debug ("periodic update should already have happened");
		pd->priv->update_feeds_id = g_idle_add ((GSourceFunc) rb_podcast_manager_update_feeds_cb, pd);
	} else {
		rb_debug ("next periodic update in %" G_GUINT64_FORMAT " seconds", (last_time + interval_sec) - now);
		pd->priv->update_feeds_id = g_timeout_add_seconds ((last_time + interval_sec) - now,
								   (GSourceFunc) rb_podcast_manager_update_feeds_cb,
								   pd);
	}
}

void
rb_podcast_manager_update_feeds (RBPodcastManager *pd)
{
	RhythmDBQueryResultList *list;
	RhythmDBEntry *entry;
	const char *uri;
	GList *l;

	list = rhythmdb_query_result_list_new ();
	rhythmdb_do_full_query (pd->priv->db,
				RHYTHMDB_QUERY_RESULTS (list),
                                RHYTHMDB_QUERY_PROP_EQUALS,
                                RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
                                RHYTHMDB_QUERY_END);

	l = rhythmdb_query_result_list_get_results (list);
	for (; l != NULL; l = l->next) {
		entry = l->data;

		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		if (rb_podcast_manager_feed_updating (pd, uri) == FALSE)
			rb_podcast_manager_subscribe_feed (pd, uri, TRUE);
	}

	g_object_unref (list);
}

static gboolean
rb_podcast_manager_update_feeds_cb (gpointer data)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (data);

	g_assert (rb_is_main_thread ());

	pd->priv->update_feeds_id = 0;

	if (g_file_set_attribute_uint64 (pd->priv->timestamp_file,
					 G_FILE_ATTRIBUTE_TIME_MODIFIED,
					 (guint64) time (NULL),
					 G_FILE_QUERY_INFO_NONE,
					 NULL,
					 NULL)) {
		rb_podcast_manager_update_feeds (pd);
		rb_podcast_manager_start_update_timer (pd);
	} else {
		rb_debug ("unable to update podcast timestamp");
	}
	return FALSE;
}

static void
podcast_update_free (RBPodcastUpdate *update)
{
	RBPodcastManager *pd = update->pd;

	g_assert (g_list_find (pd->priv->updating, update));
	pd->priv->updating = g_list_remove (pd->priv->updating, update);
	if (g_list_length (pd->priv->updating) == 0)
		g_object_notify (G_OBJECT (pd), "updating");

	g_object_unref (pd);

	g_clear_error (&update->error);
	rb_podcast_parse_channel_unref (update->channel);
	g_free (update);
}

gboolean
rb_podcast_manager_feed_updating (RBPodcastManager *pm, const char *url)
{
	GList *l;
	for (l = pm->priv->updating; l != NULL; l = l->next) {
		RBPodcastUpdate *update = l->data;
		if (g_str_equal (update->channel->url, url))
			return TRUE;
	}

	return FALSE;
}

static void
emit_feed_update_status (RBPodcastManager *mgr, RBPodcastChannel *channel, RBPodcastFeedUpdateStatus status, const char *error)
{
	g_signal_emit (mgr, rb_podcast_manager_signals[FEED_UPDATE_STATUS], 0, channel->url, status, error);
}

static void
feed_parse_cb (RBPodcastChannel *channel, GError *error, gpointer user_data)
{
	RBPodcastUpdate *update = user_data;
	RBPodcastManager *pd = update->pd;
	RBPodcastFeedUpdateStatus status;
	RhythmDBEntry *entry;
	GValue v = {0,};

	if (error == NULL) {
		if (channel->is_opml) {
			GList *l;

			rb_debug ("Loading OPML feeds from %s", channel->url);

			for (l = channel->posts; l != NULL; l = l->next) {
				RBPodcastItem *item = l->data;
				/* assume the feeds don't already exist */
				rb_podcast_manager_subscribe_feed (pd, item->url, FALSE);
			}

			emit_feed_update_status (pd, channel, RB_PODCAST_FEED_UPDATE_SUBSCRIBED, NULL);
		} else {
			rb_podcast_manager_add_parsed_feed (pd, channel);
		}
	} else if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
		rb_debug ("podcast update cancelled");

		emit_feed_update_status (pd, channel, RB_PODCAST_FEED_UPDATE_CANCELLED, NULL);
	} else {
		/* set the error in the feed entry, if one exists */
		entry = rhythmdb_entry_lookup_by_location (pd->priv->db, channel->url);
		if (entry != NULL && rhythmdb_entry_get_entry_type (entry) == RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, error->message);
			rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &v);
			g_value_unset (&v);

			rhythmdb_commit (pd->priv->db);
		}

		if (update->automatic)
			status = RB_PODCAST_FEED_UPDATE_ERROR_BG;
		else
			status = RB_PODCAST_FEED_UPDATE_ERROR;

		emit_feed_update_status (pd, channel, status, error->message);
	}

	podcast_update_free (update);
}

static void
start_feed_parse (RBPodcastManager *pd, RBPodcastUpdate *update)
{
	rb_podcast_parse_load_feed (update->channel, pd->priv->update_cancel, feed_parse_cb, update);
}

static void
confirm_bad_mime_type_response_cb (GtkDialog *dialog, int response, RBPodcastUpdate *update)
{
	if (response == GTK_RESPONSE_YES) {
		start_feed_parse (update->pd, update);
	} else {
		emit_feed_update_status (update->pd, update->channel, RB_PODCAST_FEED_UPDATE_CANCELLED, NULL);
		podcast_update_free (update);
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
mime_type_check_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	RBPodcastUpdate *update = user_data;
	GFileInfo *file_info;
	GtkWidget *dialog;
	char *content_type;
	GError *error = NULL;

	file_info = g_file_query_info_finish (G_FILE (source_object), res, &error);
	if (file_info == NULL) {
		g_object_unref (source_object);
		feed_parse_cb (update->channel, error, update);
		g_clear_error (&error);
		return;
	}

	content_type = g_file_info_get_attribute_as_string (file_info, G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE);
	if (content_type != NULL
	    && strstr (content_type, "html") == NULL
	    && strstr (content_type, "xml") == NULL
	    && strstr (content_type, "rss") == NULL
	    && strstr (content_type, "opml") == NULL) {
		dialog = gtk_message_dialog_new (NULL, 0,
						 GTK_MESSAGE_QUESTION,
						 GTK_BUTTONS_YES_NO,
						 _("The URL '%s' does not appear to be a podcast feed. "
						 "It may be the wrong URL, or the feed may be broken. "
						 "Would you like Rhythmbox to attempt to use it anyway?"),
						 update->channel->url);
		gtk_widget_show_all (dialog);
		g_signal_connect (dialog, "response", G_CALLBACK (confirm_bad_mime_type_response_cb), update);
		g_clear_error (&error);
	} else if (content_type != NULL && strstr (content_type, "opml") != NULL) {
		update->channel->is_opml = TRUE;
		start_feed_parse (update->pd, update);
	} else {
		start_feed_parse (update->pd, update);
	}

	g_free (content_type);
	g_object_unref (file_info);
	g_object_unref (source_object);
}

gboolean
rb_podcast_manager_subscribe_feed (RBPodcastManager *pd, const char *url, gboolean automatic)
{
	RBPodcastUpdate *update;
	RhythmDBEntry *entry;
	GFile *feed;
	char *feed_url;

	if (g_str_has_prefix (url, "feed://") || g_str_has_prefix (url, "itpc://")) {
		char *tmp;

		tmp = g_strdup_printf ("http://%s", url + strlen ("feed://"));
		feed = g_file_new_for_uri (tmp);
		g_free (tmp);
	} else {
		feed = g_file_new_for_uri (url);
	}

	feed_url = g_file_get_uri (feed);

	update = g_new0 (RBPodcastUpdate, 1);
	update->pd = g_object_ref (pd);
	update->automatic = automatic;
	update->channel = rb_podcast_parse_channel_new ();
	update->channel->url = g_strdup (feed_url);

	pd->priv->updating = g_list_prepend (pd->priv->updating, update);
	if (g_list_length (pd->priv->updating) == 1)
		g_object_notify (G_OBJECT (pd), "updating");

	emit_feed_update_status (pd, update->channel, RB_PODCAST_FEED_UPDATE_STARTED, NULL);

	entry = rhythmdb_entry_lookup_by_location (pd->priv->db, feed_url);
	if (entry) {
		if (rhythmdb_entry_get_entry_type (entry) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			/* added as something else, probably iradio */
			rb_error_dialog (NULL, _("URL already added"),
					 _("The URL \"%s\" has already been added as a radio station. "
					 "If this is a podcast feed, please remove the radio station."), url);
			g_object_unref (feed);
			g_free (feed_url);

			emit_feed_update_status (pd, update->channel, RB_PODCAST_FEED_UPDATE_CONFLICT, NULL);
			podcast_update_free (update);
			return FALSE;
		}

		start_feed_parse (pd, update);
	} else if (rb_uri_could_be_podcast (feed_url, NULL)) {
		rb_debug ("not checking mime type for %s", feed_url);
		start_feed_parse (pd, update);
	} else {
		g_file_query_info_async (g_object_ref (feed),
					 G_FILE_ATTRIBUTE_STANDARD_CONTENT_TYPE,
					 0,
					 G_PRIORITY_DEFAULT,
					 pd->priv->update_cancel,
					 mime_type_check_cb,
					 update);
	}

	g_object_unref (feed);
	g_free (feed_url);
	return TRUE;
}

RhythmDBEntry *
rb_podcast_manager_add_post (RhythmDB *db,
			     gboolean search_result,
			     RhythmDBEntry *entry,
			     const char *name,
			     const char *title,
			     const char *subtitle,
			     const char *channel_author,
			     const char *item_author,
			     const char *uri,
			     const char *description,
			     const char *guid,
			     const char *img,
			     gulong date,
			     gint64 duration,
			     gulong position,
			     guint64 filesize)
{
	RhythmDBEntryType *entry_type;
	GValue val = {0,};
	GTimeVal time;
	gboolean new_entry;

	if (!uri || !name || !title || !g_utf8_validate(uri, -1, NULL)) {
		return NULL;
	}

	if (search_result) {
		g_assert (entry == NULL);
		entry_type = RHYTHMDB_ENTRY_TYPE_PODCAST_SEARCH;
	} else {
		entry_type = RHYTHMDB_ENTRY_TYPE_PODCAST_POST;
	}

	if (entry == NULL) {
		entry = rhythmdb_entry_new (db, entry_type, uri);
		if (entry == NULL)
			return NULL;
		new_entry = TRUE;
	} else {
		new_entry = FALSE;

		/* change uri if it moved */
		if (g_str_equal (uri, get_remote_location (entry)) == FALSE) {
			rb_debug ("episode uri changed from %s to %s", get_remote_location (entry), uri);
			set_remote_location (db, entry, uri);
		}
	}

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, name);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, _("Podcast"));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_GENRE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, title);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	if (subtitle)
		g_value_set_string (&val, subtitle);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_SUBTITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	if (description)
		g_value_set_string (&val, description);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DESCRIPTION, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	if (item_author)
		g_value_set_string (&val, item_author);
	else if (channel_author)
		g_value_set_string (&val, channel_author);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	if (channel_author)
		g_value_set_string (&val, channel_author);
	else
		g_value_set_static_string (&val, "");
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ALBUM_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_get_current_time (&time);
	if (date == 0)
		date = time.tv_sec;
	g_value_set_ulong (&val, date);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_POST_TIME, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, position);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
	g_value_unset (&val);

	if (guid != NULL) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, guid);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PODCAST_GUID, &val);
		g_value_unset (&val);
	}

	if (img != NULL) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, img);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_IMAGE, &val);
		g_value_unset (&val);
	}

	if (new_entry) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_PAUSED);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, 0);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, time.tv_sec);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FIRST_SEEN, &val);
		g_value_unset (&val);

		if (duration > 0) {
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, duration);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &val);
			g_value_unset (&val);
		}

		g_value_init (&val, G_TYPE_DOUBLE);
		g_value_set_double (&val, 0.0);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_RATING, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_UINT64);
		g_value_set_uint64 (&val, filesize);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_FILE_SIZE, &val);
		g_value_unset (&val);
	}

	return entry;
}

typedef struct {
	RhythmDBEntry *entry;
	RBPodcastManager *mgr;
} MissingPluginRetryData;

static void
missing_plugins_retry_cb (gpointer inst, gboolean retry, MissingPluginRetryData *retry_data)
{
	if (retry == FALSE)
		return;

	rb_podcast_manager_save_metadata (retry_data->mgr, retry_data->entry);
}

static void
missing_plugins_retry_cleanup (MissingPluginRetryData *retry)
{
	g_object_unref (retry->mgr);
	rhythmdb_entry_unref (retry->entry);
	g_free (retry);
}


static void
rb_podcast_manager_save_metadata (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	RBMetaData *md = rb_metadata_new ();
	GError *error = NULL;
	GValue val = { 0, };
	const char *media_type;
	const char *uri;
	char **missing_plugins;
	char **plugin_descriptions;

	uri = get_download_location (entry);
	rb_debug ("loading podcast metadata from %s", uri);
        rb_metadata_load (md, uri, &error);

	if (rb_metadata_get_missing_plugins (md, &missing_plugins, &plugin_descriptions)) {
		GClosure *closure;
		gboolean processing;
		MissingPluginRetryData *data;

		rb_debug ("missing plugins during podcast metadata load for %s", uri);
		data = g_new0 (MissingPluginRetryData, 1);
		data->mgr = g_object_ref (pd);
		data->entry = rhythmdb_entry_ref (entry);

		closure = g_cclosure_new ((GCallback) missing_plugins_retry_cb,
					  data,
					  (GClosureNotify) missing_plugins_retry_cleanup);
		g_closure_set_marshal (closure, g_cclosure_marshal_VOID__BOOLEAN);

		processing = rb_missing_plugins_install ((const char **)missing_plugins, FALSE, closure);
		g_closure_sink (closure);

		if (processing) {
			/* when processing is complete, we'll retry */
			return;
		}
	}

	if (error != NULL) {
		gulong status;

		status = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_STATUS);
		if (status != RHYTHMDB_PODCAST_STATUS_COMPLETE) {
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_ERROR);
			rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);
		}

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, error->message);
		rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
		g_value_unset (&val);

		rhythmdb_commit (pd->priv->db);

		g_object_unref (md);
		g_error_free (error);

		return;
	}

	media_type = rb_metadata_get_media_type (md);
	if (media_type) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, media_type);
		rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_MEDIA_TYPE, &val);
		g_value_unset (&val);
	}

	if (rb_metadata_get (md,
			     RB_METADATA_FIELD_DURATION,
			     &val)) {
		rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_DURATION, &val);
		g_value_unset (&val);
	}

	if (rb_metadata_get (md,
			     RB_METADATA_FIELD_BITRATE,
			     &val)) {
		rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_BITRATE, &val);
		g_value_unset (&val);
	}

	rhythmdb_commit (pd->priv->db);

	g_object_unref (md);
}

static void
rb_podcast_manager_db_entry_added_cb (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	RhythmDBEntryType *type = rhythmdb_entry_get_entry_type (entry);

	if (type != RHYTHMDB_ENTRY_TYPE_PODCAST_POST)
		return;

        rb_podcast_manager_download_entry (pd, entry);
}



gboolean
rb_podcast_manager_remove_feed (RBPodcastManager *pd, const char *url, gboolean remove_files)
{
	RhythmDBQueryModel *query;
	GtkTreeModel *query_model;
	GtkTreeIter iter;
	RhythmDBEntry *entry;

	entry = rhythmdb_entry_lookup_by_location (pd->priv->db, url);
	if (entry == NULL) {
		rb_debug ("unable to find entry for podcast feed %s", url);
		return FALSE;
	}

	rb_debug ("removing podcast feed: %s remove_files: %d", url, remove_files);

	/* first remove the posts from the feed. include deleted posts (which will be hidden).
	 * these need to be deleted so they will properly be readded should the feed be readded.
	 */
	query = rhythmdb_query_model_new_empty (pd->priv->db);
	g_object_set (query, "show-hidden", TRUE, NULL);
	query_model = GTK_TREE_MODEL (query);
	rhythmdb_do_full_query (pd->priv->db,
				RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
				RHYTHMDB_QUERY_PROP_LIKE,
				RHYTHMDB_PROP_SUBTITLE, get_remote_location (entry),
				RHYTHMDB_QUERY_END);

	if (gtk_tree_model_get_iter_first (query_model, &iter)) {
		gboolean has_next;
		do {
			RhythmDBEntry *entry;

			gtk_tree_model_get (query_model, &iter, 0, &entry, -1);
			has_next = gtk_tree_model_iter_next (query_model, &iter);

			/* make sure we're not downloading it */
			rb_podcast_manager_cancel_download (pd, entry);
			if (remove_files) {
				rb_podcast_manager_delete_download (pd, entry);
			}

			rhythmdb_entry_delete (pd->priv->db, entry);
			rhythmdb_entry_unref (entry);

		} while (has_next);

		rhythmdb_commit (pd->priv->db);
	}

	g_object_unref (query_model);

	/* now delete the feed */
	rhythmdb_entry_delete (pd->priv->db, entry);
	rhythmdb_commit (pd->priv->db);
	return TRUE;
}

void
rb_podcast_manager_delete_download (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	const char *file_name;
	GFile *file;
	GError *error = NULL;
	RhythmDBEntryType *type = rhythmdb_entry_get_entry_type (entry);

	/* make sure it's a podcast post */
	g_assert (type == RHYTHMDB_ENTRY_TYPE_PODCAST_POST);

	file_name = get_download_location (entry);
	if (file_name == NULL) {
		/* episode has not been downloaded */
		rb_debug ("Episode %s not downloaded",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return;
	}

	rb_debug ("deleting downloaded episode %s", file_name);
	file = g_file_new_for_uri (file_name);
	g_file_delete (file, NULL, &error);

	if (error != NULL) {
		rb_debug ("Removing episode failed: %s", error->message);
		g_clear_error (&error);
	} else {
		GFile *feed_dir;
		/* try to remove the directory
		 * (will only work once it's empty)
		 */
		feed_dir = g_file_get_parent (file);
		g_file_delete (feed_dir, NULL, &error);
		if (error != NULL) {
			rb_debug ("couldn't remove podcast feed directory: %s",
				  error->message);
			g_clear_error (&error);
		}
		g_object_unref (feed_dir);
	}
	g_object_unref (file);
}

gboolean
rb_podcast_manager_cancel_download (RBPodcastManager *pd, RhythmDBEntry *entry)
{
	GList *lst;
	g_assert (rb_is_main_thread ());

	for (lst = pd->priv->download_list; lst != NULL; lst = lst->next) {
		RBPodcastDownload *data = (RBPodcastDownload *) lst->data;
		if (data->entry == entry) {
			return cancel_download (data);
		}
	}

	return FALSE;
}

static void
podcast_settings_changed_cb (GSettings *settings, const char *key, RBPodcastManager *mgr)
{
	if (g_strcmp0 (key, PODCAST_DOWNLOAD_INTERVAL) == 0) {
		rb_podcast_manager_start_update_timer (mgr);
	}
}

void
rb_podcast_manager_insert_feed_url (RBPodcastManager *pd, const char *url)
{
	RhythmDBEntry *entry;
	GValue val = { 0, };

	entry = rhythmdb_entry_lookup_by_location (pd->priv->db, url);
	if (entry) {
		rb_debug ("podcast feed entry for %s found", url);
		return;
	}
	rb_debug ("adding podcast feed %s with no entries", url);
	entry = rhythmdb_entry_new (pd->priv->db,
				    RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				    url);
	if (entry == NULL)
		return;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, url);
	rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_static_string (&val, _("Unknown"));
	rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, time(NULL));
	rhythmdb_entry_set (pd->priv->db, entry, RHYTHMDB_PROP_LAST_SEEN, &val);
	g_value_unset (&val);
}

static gint
existing_entry_sort (gconstpointer a, gconstpointer b)
{
	gulong ta, tb;

	/* sort by track numbers (ascending), or by date (descending) if not available yet */
	ta = rhythmdb_entry_get_ulong ((RhythmDBEntry *)a, RHYTHMDB_PROP_TRACK_NUMBER);
	tb = rhythmdb_entry_get_ulong ((RhythmDBEntry *)b, RHYTHMDB_PROP_TRACK_NUMBER);
	if (ta < tb)
		return -1;
	else if (ta > tb)
		return 1;
	else if (ta != 0)
		return 0;

	ta = rhythmdb_entry_get_ulong ((RhythmDBEntry *)a, RHYTHMDB_PROP_POST_TIME);
	tb = rhythmdb_entry_get_ulong ((RhythmDBEntry *)b, RHYTHMDB_PROP_POST_TIME);
	if (ta > tb)
		return -1;
	else if (ta < tb)
		return 1;
	else
		return 0;
}

void
rb_podcast_manager_add_parsed_feed (RBPodcastManager *pd, RBPodcastChannel *data)
{
	GValue val = { 0, };
	gulong last_post = 0;
	gulong new_last_post;
	gulong position;
	const char *title;
	GList *download_entries = NULL;
	gboolean new_feed;
	RhythmDB *db = pd->priv->db;
	RhythmDBQueryResultList *results = NULL;
	GList *existing_entries = NULL;
	enum {
		DOWNLOAD_NONE,
		DOWNLOAD_NEWEST,
		DOWNLOAD_NEW
	} download_mode;
	RhythmDBEntry *entry;
	GList *l;
	RBPodcastFeedUpdateStatus status = RB_PODCAST_FEED_UPDATE_UNCHANGED;

	new_feed = TRUE;

	/* processing podcast head */
	entry = rhythmdb_entry_lookup_by_location (db, (gchar *)data->url);
	if (entry) {
		if (rhythmdb_entry_get_entry_type (entry) != RHYTHMDB_ENTRY_TYPE_PODCAST_FEED) {
			emit_feed_update_status (pd, data, RB_PODCAST_FEED_UPDATE_CONFLICT, NULL);
			return;
		}

		rb_debug ("Podcast feed entry for %s found", data->url);
		last_post = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_POST_TIME);
		new_feed = FALSE;

		/* find all the existing entries in this feed, so we can cull those
		 * that haven't been downloaded and are no longer present in the feed.
		 */
		results = rhythmdb_query_result_list_new ();
		rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (results),
				 	RHYTHMDB_QUERY_PROP_EQUALS,
					  RHYTHMDB_PROP_TYPE,
					  RHYTHMDB_ENTRY_TYPE_PODCAST_POST,
					RHYTHMDB_QUERY_PROP_EQUALS,
					  RHYTHMDB_PROP_SUBTITLE,
					  data->url,
					RHYTHMDB_QUERY_END);
		existing_entries = g_list_copy (rhythmdb_query_result_list_get_results (results));
		existing_entries = g_list_sort (existing_entries, existing_entry_sort);
	} else {
		rb_debug ("Adding podcast feed: %s", data->url);
		entry = rhythmdb_entry_new (db,
					    RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				    	    (gchar *) data->url);
		if (entry == NULL) {
			emit_feed_update_status (pd, data, RB_PODCAST_FEED_UPDATE_CONFLICT, NULL);
			return;
		}

		status = RB_PODCAST_FEED_UPDATE_SUBSCRIBED;
	}

	/* if the feed does not contain a title, use the URL instead */
	g_value_init (&val, G_TYPE_STRING);
	if (data->title == NULL || strlen ((gchar *)data->title) == 0) {
		g_value_set_string (&val, (gchar *) data->url);
		title = data->url;
	} else {
		g_value_set_string (&val, (gchar *) data->title);
		title = data->title;
	}
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TITLE, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);
	if (data->author)
		g_value_set_string (&val, (gchar *) data->author);
	else
		g_value_set_static_string (&val, _("Unknown"));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_ARTIST, &val);
	g_value_unset (&val);

	if (data->description) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, (gchar *) data->description);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DESCRIPTION, &val);
		g_value_unset (&val);
	}

	if (data->lang) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, (gchar *) data->lang);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LANG, &val);
		g_value_unset (&val);
	}

	if (data->copyright) {
		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, (gchar *) data->copyright);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_COPYRIGHT, &val);
		g_value_unset (&val);
	}

	if (data->img) {
		RBExtDBKey *key;

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, (gchar *) data->img);
		rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_IMAGE, &val);
		g_value_unset (&val);

		key = rb_ext_db_key_create_storage ("subtitle", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

		rb_ext_db_store_uri (pd->priv->art_store,
				     key,
				     RB_EXT_DB_SOURCE_SEARCH,	/* sort of */
				     data->img);
	}

	/* clear any error that might have been set earlier */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, NULL);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
	g_value_unset (&val);

	if (g_settings_get_enum (pd->priv->settings, PODCAST_DOWNLOAD_INTERVAL) == PODCAST_INTERVAL_MANUAL) {
		/* if automatic updates are disabled, don't download anything */
		rb_debug ("not downloading any new episodes");
		download_mode = DOWNLOAD_NONE;
	} else if (new_feed) {
		/* don't download the entire backlog for new feeds */
		rb_debug ("downloading most recent episodes");
		download_mode = DOWNLOAD_NEWEST;
	} else {
		/* download all episodes since the last update for existing feeds */
		rb_debug ("downloading all new episodes");
		download_mode = DOWNLOAD_NEW;
	}

	/* insert episodes */
	new_last_post = last_post;
	position = 1;
	for (l = data->posts; l != NULL; l = g_list_next (l)) {
		RBPodcastItem *item = (RBPodcastItem *) l->data;
		RhythmDBEntry *post_entry;
		gboolean new_entry;

		new_entry = TRUE;
		if (existing_entries != NULL) {
			GList *l;
			const char *guid;

			/* look for an existing entry that matches this item */
			l = existing_entries;
			for (l = existing_entries; l != NULL; l = g_list_next (l)) {
				post_entry = l->data;
				guid = rhythmdb_entry_get_string (post_entry, RHYTHMDB_PROP_PODCAST_GUID);
				if (g_strcmp0 (guid, item->guid) == 0) {
					rb_debug ("guid %s matched", guid);
					break;
				}
				if (strcmp (get_remote_location (post_entry), item->url) == 0) {
					rb_debug ("url %s matched", item->url);
					break;
				}
			}

			if (l != NULL) {
				/* mark this entry as still being available */
				existing_entries = g_list_remove_link (existing_entries, l);
				g_list_free (l);
				new_entry = FALSE;
			} else {
				rb_debug ("episode url %s (guid %s) not matched", item->url, item->guid);
				post_entry = NULL;
			}
		} else {
			rb_debug ("no existing episodes to match");
			post_entry = NULL;
		}

		post_entry =
		    rb_podcast_manager_add_post (db,
			    FALSE,
			    post_entry,
			    title,
			    item->title,
			    data->url,
			    data->author,
			    item->author,
			    item->url,
			    item->description,
			    item->guid,
			    item->img,
			    item->pub_date > 0 ? item->pub_date : data->pub_date,
			    item->duration,
			    position++,
			    item->filesize);

		if (new_entry && (post_entry != NULL)) {
			if (status == RB_PODCAST_FEED_UPDATE_UNCHANGED) {
				status = RB_PODCAST_FEED_UPDATE_UPDATED;
			}

			if (item->pub_date >= new_last_post) {
				switch (download_mode) {
				case DOWNLOAD_NEWEST:
					if (item->pub_date > new_last_post) {
						g_list_free (download_entries);
						download_entries = NULL;
					}
					new_last_post = item->pub_date;
					break;
				case DOWNLOAD_NONE:
				case DOWNLOAD_NEW:
					break;
				}
				download_entries = g_list_prepend (download_entries, post_entry);
			}
		}

	}

	if (download_mode != DOWNLOAD_NONE) {
		GList *t;

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_WAITING);
		for (t = download_entries; t != NULL; t = g_list_next (t)) {
			rhythmdb_entry_set (db,
					    (RhythmDBEntry*) t->data,
					    RHYTHMDB_PROP_STATUS,
					    &val);
		}
		g_value_unset (&val);
	}
	g_list_free (download_entries);

	if (data->pub_date > new_last_post)
		new_last_post = data->pub_date;

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, new_last_post);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_POST_TIME, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, time(NULL));
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_SEEN, &val);
	g_value_unset (&val);

	for (l = existing_entries; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *entry = l->data;

		if (rb_podcast_manager_entry_downloaded (entry) == FALSE) {
			RBExtDBKey *key;
			const char *guid;

			rb_debug ("entry %s is no longer present in the feed and has not been downloaded",
				  get_remote_location (entry));

			/* delete episode image, if we've downloaded it */
			guid = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PODCAST_GUID);
			if (guid != NULL) {
				key = rb_ext_db_key_create_storage ("subtitle", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_SUBTITLE));
				rb_ext_db_key_add_field (key, "podcast-guid", guid);
				rb_ext_db_delete (pd->priv->art_store, key);
				rb_ext_db_key_free (key);
			}

			rhythmdb_entry_delete (db, entry);
		} else {
			/* assign track numbers to remaining entries in the same order */
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, position++);
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &val);
			g_value_unset (&val);
		}
	}
	g_clear_object (&results);

	rhythmdb_commit (db);

	emit_feed_update_status (pd, data, status, NULL);
}

void
rb_podcast_manager_shutdown (RBPodcastManager *pd)
{
	GList *lst, *l;

	g_assert (rb_is_main_thread ());

	g_cancellable_cancel (pd->priv->update_cancel);

	lst = g_list_reverse (g_list_copy (pd->priv->download_list));
	for (l = lst; l != NULL; l = l->next) {
		RBPodcastDownload *data = (RBPodcastDownload *) l->data;
		cancel_download (data);
	}
	g_list_free (lst);
}

char *
rb_podcast_manager_get_podcast_dir (RBPodcastManager *pd)
{
	char *conf_dir_uri = g_settings_get_string (pd->priv->settings, PODCAST_DOWNLOAD_DIR_KEY);

	/* if we don't have a download directory yet, use the music dir,
	 * or the home dir if we can't find that.
	 */
	if (conf_dir_uri == NULL || (strcmp (conf_dir_uri, "") == 0)) {
		const char *conf_dir_name;

		conf_dir_name = g_get_user_special_dir (G_USER_DIRECTORY_MUSIC);
		if (!conf_dir_name)
			conf_dir_name = g_get_home_dir ();

		conf_dir_uri = g_filename_to_uri (conf_dir_name, NULL, NULL);
		g_settings_set_string (pd->priv->settings, PODCAST_DOWNLOAD_DIR_KEY, conf_dir_uri);
	}

	return conf_dir_uri;
}

void
rb_podcast_manager_add_search (RBPodcastManager *pd, GType search_type)
{
	g_array_append_val (pd->priv->searches, search_type);
}

/**
 * rb_podcast_manager_get_searches:
 * @pd: the #RBPodcastManager
 *
 * Returns the list of podcast searches
 *
 * Return value: (element-type RB.PodcastSearch) (transfer container): list of search instances
 */
GList *
rb_podcast_manager_get_searches (RBPodcastManager *pd)
{
	GList *searches = NULL;
	int i;

	for (i = 0; i < pd->priv->searches->len; i++) {
		RBPodcastSearch *search;
		GType search_type;

		search_type = g_array_index (pd->priv->searches, GType, i);
		search = RB_PODCAST_SEARCH (g_object_new (search_type, NULL));
		searches = g_list_append (searches, search);
	}

	return searches;
}

static void
podcast_download_cb (GObject *source_object, GAsyncResult *res, gpointer data)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (source_object);
	RBPodcastDownload *download;
	GError *error = NULL;
	GTask *task = G_TASK (res);
	GValue val = {0,};
	gboolean notify_user = TRUE;

	download = g_task_get_task_data (task);
	rb_debug ("cleaning up download of %s",
		  get_remote_location (download->entry));

	pd->priv->download_list = g_list_remove (pd->priv->download_list, download);

	g_assert (pd->priv->active_download == download);
	pd->priv->active_download = NULL;

	g_task_propagate_boolean (task, &error);
	if (error) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) == FALSE) {
			rb_debug ("error downloading %s: %s",
				  get_remote_location (download->entry),
				  error->message);

			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_ERROR);
			rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);

			g_value_init (&val, G_TYPE_STRING);
			g_value_set_string (&val, error->message);
			rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
			g_value_unset (&val);
		} else {
			/* no need to notify for cancellations */
			notify_user = FALSE;

			rb_debug ("download of %s was cancelled", get_remote_location (download->entry));
			g_value_init (&val, G_TYPE_ULONG);
			g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_PAUSED);
			rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_STATUS, &val);
			g_value_unset (&val);
		}

		unset_download_location (pd->priv->db, download->entry);

		rhythmdb_commit (pd->priv->db);
	}

	if (notify_user) {
		g_signal_emit (pd, rb_podcast_manager_signals[FINISH_DOWNLOAD],
			       0, download->entry, error);
	}

	g_clear_error (&error);
	download_info_free (download);

	g_object_unref (task);
	rb_podcast_manager_next_file (pd);
}

static gboolean
rb_podcast_manager_next_file (RBPodcastManager *pd)
{
	RBPodcastDownload *download;
	GList *d;
	GTask *task;

	g_assert (rb_is_main_thread ());

	rb_debug ("looking for something to download");

	if (pd->priv->active_download != NULL) {
		rb_debug ("already downloading something");
		return FALSE;
	}

	d = g_list_first (pd->priv->download_list);
	if (d == NULL) {
		rb_debug ("download queue is empty");
		return FALSE;
	}

	download = (RBPodcastDownload *) d->data;
	g_assert (download != NULL);
	g_assert (download->entry != NULL);

	rb_debug ("processing %s", get_remote_location (download->entry));

	pd->priv->active_download = download;
	download->cancel = g_cancellable_new ();
	task = g_task_new (pd, download->cancel, podcast_download_cb, NULL);
	g_task_set_task_data (task, download, NULL);
	g_task_run_in_thread (task, download_task);

	return FALSE;
}

static void
download_info_free (RBPodcastDownload *download)
{
	g_clear_object (&download->cancel);
	g_clear_object (&download->destination);

	if (download->in_stream) {
		g_input_stream_close (download->in_stream, NULL, NULL);
		g_clear_object (&download->in_stream);
	}

	if (download->out_stream) {
		g_output_stream_close (G_OUTPUT_STREAM (download->out_stream), NULL, NULL);
		g_clear_object (&download->out_stream);
	}

	if (download->entry) {
		rhythmdb_entry_unref (download->entry);
	}

	g_clear_object (&download->request);
	g_free (download->buffer);
	g_free (download);
}


static void
download_progress (RBPodcastDownload *data, guint64 downloaded, guint64 total)
{
	guint local_progress = 0;

	if (downloaded > 0 && total > 0)
		local_progress = (100 * downloaded) / total;

	if (local_progress != data->progress) {
		GValue val = {0,};

		rb_debug ("%s: %" G_GUINT64_FORMAT "/ %" G_GUINT64_FORMAT,
			  rhythmdb_entry_get_string (data->entry, RHYTHMDB_PROP_LOCATION),
			  downloaded, total);

		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, local_progress);
		rhythmdb_entry_set (data->pd->priv->db, data->entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		rhythmdb_commit (data->pd->priv->db);

		data->progress = local_progress;
	}
}

static char *
get_local_download_uri (RBPodcastManager *pd, RBPodcastDownload *download)
{
	SoupMessageHeaders *request_hdrs;
	char *local_file_name = NULL;
	char *esc_local_file_name;
	char *local_file_uri;
	char *conf_dir_uri;
	char *feed_folder;
	const char *query_string;
	GHashTable *params;

	request_hdrs = soup_message_get_response_headers (download->request);
	if (soup_message_headers_get_content_disposition (request_hdrs, NULL, &params)) {
		const char *name = g_hash_table_lookup (params, "filename");
		if (name) {
			local_file_name = g_strdup (name);
			rb_debug ("got content disposition filename %s", local_file_name);
		}

		g_hash_table_destroy (params);
	}
	if (local_file_name == NULL) {
		GFile *source;
		GUri *remote_uri;

		remote_uri = soup_message_get_uri (download->request);
		rb_debug ("download uri path %s", g_uri_get_path (remote_uri));

		source = g_file_new_for_path (g_uri_get_path (remote_uri));
		local_file_name = g_file_get_basename (source);
		g_object_unref (source);
		rb_debug ("got local filename from uri: %s", local_file_name);
	}

	/* if the filename ends with the query string from the original URI, remove it */
	query_string = strchr (get_remote_location (download->entry), '?');
	if (query_string != NULL) {
		query_string--;
		if (g_str_has_suffix (local_file_name, query_string)) {
			local_file_name[strlen (local_file_name) - strlen (query_string)] = '\0';
			rb_debug ("removing query string \"%s\" -> local file name \"%s\"", query_string, local_file_name);
		}
	}

	esc_local_file_name = g_uri_escape_string (local_file_name,
						   G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
						   TRUE);
	feed_folder = g_uri_escape_string (rhythmdb_entry_get_string (download->entry, RHYTHMDB_PROP_ALBUM),
					   G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
					   TRUE);
	g_strdelimit (feed_folder, "/", '_');
	g_strdelimit (esc_local_file_name, "/", '_');

	/* construct local filename */
	conf_dir_uri = rb_podcast_manager_get_podcast_dir (pd);
	local_file_uri = g_build_filename (conf_dir_uri, feed_folder, esc_local_file_name, NULL);

	g_free (local_file_name);
	g_free (feed_folder);
	g_free (esc_local_file_name);

	return local_file_uri;
}

static void
finish_download (RBPodcastManager *pd, RBPodcastDownload *download, guint64 remote_size, guint64 downloaded)
{
	GValue val = {0,};

	rb_debug ("download of %s completed", get_remote_location (download->entry));

	g_value_init (&val, G_TYPE_UINT64);
	g_value_set_uint64 (&val, downloaded);
	rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_FILE_SIZE, &val);
	g_value_unset (&val);

	if (remote_size == 0 || downloaded >= remote_size) {
		g_value_init (&val, G_TYPE_ULONG);
		g_value_set_ulong (&val, RHYTHMDB_PODCAST_STATUS_COMPLETE);
		rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_STATUS, &val);
		g_value_unset (&val);

		g_value_init (&val, G_TYPE_STRING);
		g_value_set_string (&val, NULL);
		rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_PLAYBACK_ERROR, &val);
		g_value_unset (&val);
	}

	rb_podcast_manager_save_metadata (pd, download->entry);
}

static gboolean
retry_on_http_status (SoupStatus status)
{
	switch (status) {
	case SOUP_STATUS_REQUEST_TIMEOUT:
	case SOUP_STATUS_INTERNAL_SERVER_ERROR:
	case SOUP_STATUS_BAD_GATEWAY:
	case SOUP_STATUS_SERVICE_UNAVAILABLE:
	case SOUP_STATUS_GATEWAY_TIMEOUT:
		rb_debug ("retrying on http status %d", status);
		return TRUE;
	default:
		rb_debug ("not retrying on http status %d", status);
		return FALSE;
	}
}

static gboolean
retry_on_error (GError *error)
{
	if (error->domain == G_IO_ERROR) {
		switch (error->code) {
			case G_IO_ERROR_CLOSED:
			case G_IO_ERROR_CONNECTION_CLOSED:
			case G_IO_ERROR_TIMED_OUT:
			case G_IO_ERROR_NOT_CONNECTED:
				rb_debug ("retrying on io error %s (%d)", error->message, error->code);
				return TRUE;

			default:
				rb_debug ("not retrying on io error %s (%d)", error->message, error->code);
				return FALSE;
		}
	} else if (error->domain == G_RESOLVER_ERROR) {
		switch (error->code) {
		case G_RESOLVER_ERROR_TEMPORARY_FAILURE:
			rb_debug ("retrying on resolver error %s (%d)", error->message, error->code);
			return TRUE;
		default:
			rb_debug ("not retrying on resolver error %s (%d)", error->message, error->code);
			return FALSE;
		}
	} else {
		rb_debug ("not retrying on error %s (%d)", error->message, error->code);
		return FALSE;
	}
}

static char *
disambiguate_download_uri (RBPodcastManager *pd, RBPodcastDownload *download, const char *dl_uri)
{
	GFile *file;
	GFile *parent;
	char *filename;
	char *extension;
	char *tryname;
	char *tryuri;
	int suffix = 1;

	file = g_file_new_for_uri (dl_uri);
	filename = g_file_get_basename (file);
	g_assert (filename != NULL);

	tryname = strrchr (filename, '.');
	if (tryname != NULL) {
		extension = g_strdup (tryname);
		*tryname = '\0';
	} else {
		extension = g_strdup ("");
	}

	parent = g_file_get_parent (file);
	g_assert (parent != NULL);
	g_object_unref (file);

	tryuri = NULL;
	do {
		g_free (tryuri);

		/*
		 * If you somehow have 2^31 episodes of a single podcast that all have
		 * the same filename downloaded into a single directory, congratulations.
		 * This loop will run forever to celebrate your achievement.
		 */

		tryname = g_strdup_printf ("%s_%d%s", filename, suffix++, extension);
		file = g_file_get_child (parent, tryname);
		g_free (tryname);

		tryuri = g_file_get_uri (file);
		g_object_unref (file);
	} while (rhythmdb_entry_lookup_by_location (pd->priv->db, tryuri) != NULL);

	g_object_unref (parent);
	g_free (extension);
	g_free (filename);
	return tryuri;
}

static void
download_task (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancel)
{
	RBPodcastManager *pd = RB_PODCAST_MANAGER (source_object);
	RBPodcastDownload *download = task_data;
	SoupMessageHeaders *request_hdrs;
	SoupMessageHeaders *response_hdrs;
	SoupStatus status;
	GError *error = NULL;
	gssize remote_size;
	const char *local_file_uri;
	gssize n_read;
	guint64 downloaded;
	gboolean retry;
	gboolean eof;
	goffset start, end, total;
	gboolean range_request;
	int retries;
	GValue val = {0,};
	char *dl_uri;
	char *sane_dl_uri;

	/*
	 * first request to get the remote file size and determine the local file name.
	 * some podcast hosts don't handle HEAD requests properly, so instead do a GET
	 * for only the first byte.
	 */
	retries = 5;
	remote_size = 0;
	range_request = TRUE;
	while (retries-- > 0) {
		if (download->in_stream != NULL) {
			g_input_stream_close (download->in_stream, NULL, NULL);
			g_clear_object (&download->in_stream);
		}
		g_clear_object (&download->request);
		g_clear_error (&error);

		download->request = soup_message_new (SOUP_METHOD_GET, get_remote_location (download->entry));

		request_hdrs = soup_message_get_request_headers (download->request);
		soup_message_headers_set_range (request_hdrs, 0, 0);

		download->in_stream = soup_session_send (pd->priv->soup_session, download->request, download->cancel, &error);

		status = soup_message_get_status (download->request);
		if (error == NULL) {
			if (SOUP_STATUS_IS_SUCCESSFUL (status)) {
				response_hdrs = soup_message_get_response_headers (download->request);
				if (soup_message_headers_get_content_range (response_hdrs, &start, &end, &total)) {
					remote_size = total;
				} else {
					remote_size = soup_message_headers_get_content_length (response_hdrs);
				}
				rb_debug ("remote file size %" G_GSSIZE_FORMAT, remote_size);
				break;
			} else if (!retry_on_http_status (status)) {
				error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "%s", soup_message_get_reason_phrase (download->request));
				rb_debug ("giving up after http status %d (%s)", status, error->message);
				break;
			} else {
				rb_debug ("retrying after http status %d (%s)", status, soup_message_get_reason_phrase (download->request));
			}
		} else {
			if (retry_on_error (error)) {
				rb_debug ("retrying after error from http request: %s", error->message);
			} else {
				rb_debug ("giving up after error from http request: %s", error->message);
				break;
			}
		}

		g_usleep (DOWNLOAD_RETRY_DELAY * G_USEC_PER_SEC);
	}

	if (error != NULL) {
		g_task_return_error (task, error);
		return;
	}

	dl_uri = get_local_download_uri (pd, download);
	sane_dl_uri = rb_sanitize_uri_for_filesystem (dl_uri, NULL);
	g_free (dl_uri);

	rb_debug ("download URI: %s", sane_dl_uri);

	if (rb_uri_create_parent_dirs (sane_dl_uri, &error) == FALSE) {
		rb_debug ("error creating parent dirs: %s", error->message);
		g_task_return_error (task, error);
		g_free (sane_dl_uri);
		return;
	}

	/*
	 * if we've previously attempted to download this item, and now our
	 * idea of the local uri is different, delete the old file.
	 * otherwise, if there's already an entry with this local uri, find
	 * a new one.
	 */
	local_file_uri = get_download_location (download->entry);
	if (local_file_uri != NULL) {
		if (strcmp (local_file_uri, sane_dl_uri) != 0) {
			GFile *del;

			del = g_file_new_for_uri (local_file_uri);
			rb_debug ("local download uri has changed, removing old file at %s", local_file_uri);
			g_file_delete (del, download->cancel, NULL);
			g_object_unref (del);
		}
	} else if (rhythmdb_entry_lookup_by_location (pd->priv->db, sane_dl_uri) != NULL) {
		char *dl_uri;

		rb_debug ("existing entry found with this download location");
		dl_uri = disambiguate_download_uri (pd, download, sane_dl_uri);
		g_free (sane_dl_uri);
		sane_dl_uri = dl_uri;

		rb_debug ("disambiguated download location: %s", sane_dl_uri);
	}

	/* set the download location for the episode, set progress to 0% */
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, sane_dl_uri);
	set_download_location (pd->priv->db, download->entry, &val);
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_ULONG);
	g_value_set_ulong (&val, 0);
	rhythmdb_entry_set (pd->priv->db, download->entry, RHYTHMDB_PROP_STATUS, &val);
	g_value_unset (&val);

	rhythmdb_commit (pd->priv->db);

	/* check for an existing partial download */
	downloaded = 0;
	rb_debug ("checking for local copy at %s", sane_dl_uri);
	download->destination = g_file_new_for_uri (sane_dl_uri);
	g_free (sane_dl_uri);

	if (g_file_query_exists (download->destination, NULL)) {
		GFileInfo *dest_info;

		dest_info = g_file_query_info (download->destination,
					       G_FILE_ATTRIBUTE_STANDARD_SIZE,
					       G_FILE_QUERY_INFO_NONE,
					       NULL,
					       &error);
		if (error != NULL) {
			g_task_return_error (task, error);
			return;
		}

		downloaded = g_file_info_get_attribute_uint64 (dest_info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
		g_object_unref (dest_info);
		if (downloaded == remote_size) {
			rb_debug ("local file is the same size as the download (%" G_GUINT64_FORMAT ")",
				  downloaded);
			finish_download (pd, download, remote_size, downloaded);
			g_task_return_boolean (task, TRUE);
			return;
		} else if (downloaded > remote_size) {
			rb_debug ("replacing local file as it's larger than the download");
			downloaded = 0;
		} else {
			rb_debug ("local file size %" G_GUINT64_FORMAT, downloaded);
		}
	} else {
		rb_debug ("local copy not found");
	}

	while (retries-- > 0) {
		if (download->in_stream != NULL) {
			g_input_stream_close (download->in_stream, NULL, NULL);
			g_clear_object (&download->in_stream);
		}
		g_clear_object (&download->request);
		g_clear_error (&error);
		retry = FALSE;
		eof = FALSE;

		download->request = soup_message_new (SOUP_METHOD_GET, get_remote_location (download->entry));
		if (downloaded != 0) {
			request_hdrs = soup_message_get_request_headers (download->request);
			soup_message_headers_set_range (request_hdrs, downloaded, -1);
		}

		download->in_stream = soup_session_send (pd->priv->soup_session, download->request, download->cancel, &error);

		status = soup_message_get_status (download->request);
		if (status == SOUP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE) {
			rb_debug ("got requested range not satisfiable, disabling resume and retrying");
			range_request = FALSE;
			downloaded = 0;
			continue;
		}

		if (error == NULL && !SOUP_STATUS_IS_SUCCESSFUL (status)) {
			error = g_error_new (G_IO_ERROR, G_IO_ERROR_FAILED, "%s", soup_message_get_reason_phrase (download->request));
		}
		if (error != NULL) {
			if (retry_on_error (error)) {
				rb_debug ("retrying after error from http request: %s", error ? error->message : "something");
				g_usleep (DOWNLOAD_RETRY_DELAY * G_USEC_PER_SEC);
				continue;
			}

			rb_debug ("giving up after error from http request: %s", error ? error->message : "something");
			g_task_return_error (task, error);
			return;
		}

		/* check that the server actually honoured our range request */
		if (downloaded != 0) {
			response_hdrs = soup_message_get_response_headers (download->request);
			if (soup_message_headers_get_content_range (response_hdrs, &start, &end, &total)) {
				if (start != downloaded) {
					rb_debug ("range request mismatched, redownloading from start");
					downloaded = 0;
					continue;
				}
				remote_size = total;
				rb_debug ("resuming download at offset %" G_GOFFSET_FORMAT ", %" G_GOFFSET_FORMAT " bytes left", start, total);
			} else {
				range_request = FALSE;
				downloaded = 0;
				remote_size = soup_message_headers_get_content_length (response_hdrs);
				rb_debug ("server didn't honour range request, starting again, total %" G_GSSIZE_FORMAT, remote_size);
			}
		}

		/* open the local file */
		if (downloaded != 0) {
			download->out_stream = g_file_append_to (download->destination,
								 G_FILE_CREATE_NONE,
								 download->cancel,
								 &error);
		} else {
			download->out_stream = g_file_replace (download->destination,
							       NULL,
							       FALSE,
							       G_FILE_CREATE_NONE,
							       download->cancel,
							       &error);
		}

		if (error != NULL) {
			rb_debug ("error opening local file: %s", error->message);
			g_task_return_error (task, error);
			return;
		}

		/* loop, copying from input stream to output stream */
		download->buffer = g_new0 (char, DOWNLOAD_BUFFER_SIZE);
		while (TRUE) {
			char *p;
			n_read = g_input_stream_read (download->in_stream,
						      download->buffer, DOWNLOAD_BUFFER_SIZE,
						      download->cancel,
						      &error);
			if (n_read == 0) {
				eof = TRUE;
				break;
			} else if (n_read < 0) {
				if (retry_on_error (error)) {
					rb_debug ("retrying after error reading from input stream: %s", error->message);
					retry = TRUE;
				} else {
					rb_debug ("giving up after error reading from input stream: %s", error->message);
				}
				break;
			}

			p = download->buffer;
			if (g_output_stream_write_all (G_OUTPUT_STREAM (download->out_stream),
						       p,
						       n_read,
						       NULL,
						       download->cancel,
						       &error) == FALSE) {
				/* never retry after a write error */
				break;
			}
			p += n_read;
			downloaded += n_read;

			download_progress (download, downloaded, remote_size);
		}

		/* close everything - don't allow these operations to be cancelled */
		g_input_stream_close (download->in_stream, NULL, NULL);
		g_clear_object (&download->in_stream);

		g_output_stream_close (G_OUTPUT_STREAM (download->out_stream), NULL, &error);
		g_clear_object (&download->out_stream);

		if (eof) {
			if (remote_size == 0 || downloaded == remote_size) {
				rb_debug ("full file downloaded");
				break;
			}
			if (range_request == FALSE) {
				rb_debug ("range request not supported, restarting");
				downloaded = 0;
			} else
				rb_debug ("haven't got the whole file yet, retrying at %" G_GUINT64_FORMAT, downloaded);
		} else if (retry == FALSE) {
			rb_debug ("not retrying");
			break;
		}

		g_usleep (DOWNLOAD_RETRY_DELAY * G_USEC_PER_SEC);
	}

	if (error != NULL) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			rb_debug ("deleting cancelled download");
			g_file_delete (download->destination, NULL, NULL);
		}
		g_task_return_error (task, error);
	} else {
		finish_download (pd, download, remote_size, downloaded);
		g_task_return_boolean (task, TRUE);
	}

	rb_debug ("finished");
}


static gboolean
cancel_download (RBPodcastDownload *data)
{
	g_assert (rb_is_main_thread ());
	rb_debug ("cancelling download of %s", get_remote_location (data->entry));

	/* is this the active download? */
	if (data == data->pd->priv->active_download) {
		g_cancellable_cancel (data->cancel);

		/* download data will be cleaned up after the task returns */
		return TRUE;
	} else {
		/* destroy download data */
		data->pd->priv->download_list = g_list_remove (data->pd->priv->download_list, data);
		download_info_free (data);
		return FALSE;
	}
}

static void
podcast_album_art_request_cb (RBExtDB *db, RBExtDBKey *key, guint64 last_time, RBPodcastManager *pd)
{
	RhythmDBEntry *entry;
	const char *feed;
	const char *guid;
	const char *location;
	const char *image;
	const char *feed_image;

	/*
	 * we only need to answer requests for specific episodes here
	 * as feed images are stored when the feed is updated
	 */
	feed = rb_ext_db_key_get_field (key, "subtitle");
	guid = rb_ext_db_key_get_field (key, "podcast-guid");
	location = rb_ext_db_key_get_info (key, "location");
	if (feed != NULL && guid != NULL && location != NULL) {
		entry = rhythmdb_entry_lookup_by_location (pd->priv->db, feed);
		if (entry == NULL)
			return;
		feed_image = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_IMAGE);

		entry = rhythmdb_entry_lookup_by_location (pd->priv->db, location);
		if (entry == NULL)
			return;

		image = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_IMAGE);
		if (image != NULL && g_strcmp0 (image, feed_image) != 0) {
			RBExtDBKey *skey;

			skey = rb_ext_db_key_create_storage ("subtitle", feed);
			rb_ext_db_key_add_field (skey, "podcast-guid", guid);
			rb_ext_db_store_uri (db,
					     skey,
					     RB_EXT_DB_SOURCE_EMBEDDED, /* sort of */
					     image);
		}
	}
}

#define ENUM_ENTRY(name, desc) { name, "" #name "", desc }

GType
rb_podcast_feed_update_status_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_STARTED, "started"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_ERROR, "error"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_ERROR_BG, "error-background"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_CONFLICT, "conflict"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_CANCELLED, "cancelled"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_SUBSCRIBED, "subscribed"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_UNCHANGED, "unchanged"),
			ENUM_ENTRY (RB_PODCAST_FEED_UPDATE_UPDATED, "updated"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBPodcastFeedUpdateStatus", values);
	}

	return etype;
}
