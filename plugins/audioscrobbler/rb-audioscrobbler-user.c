/*
 * rb-audioscrobbler-user.c
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include "config.h"

#include <string.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "rb-audioscrobbler-user.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

#define USER_PROFILE_IMAGE_SIZE 126
#define LIST_ITEM_IMAGE_SIZE 34

#define USER_INFO_LIFETIME 86400             /* 24 hours */
#define RECENT_TRACKS_LIFETIME 3600          /* 1 hour */
#define TOP_TRACKS_LIFETIME 86400            /* 24 hours */
#define LOVED_TRACKS_LIFETIME 86400          /* 24 hours */
#define TOP_ARTISTS_LIFETIME 86400           /* 24 hours */

static RBAudioscrobblerUserData *
rb_audioscrobbler_user_data_new (void)
{
	RBAudioscrobblerUserData *data = g_slice_new0 (RBAudioscrobblerUserData);

	data->refcount = 1;
	return data;
}

static RBAudioscrobblerUserData *
rb_audioscrobbler_user_data_ref (RBAudioscrobblerUserData *data)
{
	data->refcount++;
	return data;
}

static void
rb_audioscrobbler_user_data_free (RBAudioscrobblerUserData *data)
{
	if (data->image != NULL) {
		g_object_unref (data->image);
	}
	g_free (data->url);

	switch (data->type) {
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO:
		g_free (data->user_info.username);
		g_free (data->user_info.playcount);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK:
		g_free (data->track.title);
		g_free (data->track.artist);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST:
		g_free (data->artist.name);
		break;
	}

	g_slice_free (RBAudioscrobblerUserData, data);
}

static void
rb_audioscrobbler_user_data_unref (RBAudioscrobblerUserData *data) {
	if (--data->refcount == 0) {
		rb_audioscrobbler_user_data_free (data);
	}
}

GType
rb_audioscrobbler_user_data_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBAudioscrobblerUserData",
		                                     (GBoxedCopyFunc)rb_audioscrobbler_user_data_ref,
		                                     (GBoxedFreeFunc)rb_audioscrobbler_user_data_unref);
	}

	return type;
}

/* unrefs each element and frees the queue */
static void
free_data_queue (gpointer data_queue)
{
	g_queue_free_full (data_queue,
	                   (GDestroyNotify)rb_audioscrobbler_user_data_unref);
}

struct _RBAudioscrobblerUserPrivate {
	RBAudioscrobblerService *service;
	char *username;
	char *session_key;

	SoupSession *soup_session;

	RBAudioscrobblerUserData *user_info;
	GPtrArray *recent_tracks;
	GPtrArray *top_tracks;
	GPtrArray *loved_tracks;
	GPtrArray *top_artists;

	/* for image downloads */
	GHashTable *file_to_data_queue_map;
	GHashTable *file_to_cancellable_map;
};

#define RB_AUDIOSCROBBLER_USER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_USER, RBAudioscrobblerUserPrivate))

static void rb_audioscrobbler_user_constructed (GObject *object);
static void rb_audioscrobbler_user_dispose (GObject* object);
static void rb_audioscrobbler_user_finalize (GObject *object);
static void rb_audioscrobbler_user_get_property (GObject *object,
                                                 guint prop_id,
                                                 GValue *value,
                                                 GParamSpec *pspec);
static void rb_audioscrobbler_user_set_property (GObject *object,
                                                 guint prop_id,
                                                 const GValue *value,
                                                 GParamSpec *pspec);

static void load_from_cache (RBAudioscrobblerUser *user);

static char * calculate_cached_response_path (RBAudioscrobblerUser *user,
                                              const char *request_name);
static gboolean is_cached_response_expired (RBAudioscrobblerUser *user,
                                            const char *request_name,
                                            long lifetime);
static void save_response_to_cache (RBAudioscrobblerUser *user,
                                    const char *request_name,
                                    const char *data);

static GPtrArray * parse_track_array (RBAudioscrobblerUser *user, JsonArray *track_array);
static GPtrArray * parse_artist_array (RBAudioscrobblerUser *user, JsonArray *track_array);

static void load_cached_user_info (RBAudioscrobblerUser *user);
static void request_user_info (RBAudioscrobblerUser *user);
static void user_info_response_cb (SoupSession *session,
                                   GAsyncResult *result,
				   RBAudioscrobblerUser *user);
static RBAudioscrobblerUserData * parse_user_info (RBAudioscrobblerUser *user,
                                                   const char *data);

static void load_cached_recent_tracks (RBAudioscrobblerUser *user);
static void request_recent_tracks (RBAudioscrobblerUser *user, int limit);
static void recent_tracks_response_cb (SoupSession *session,
                                       GAsyncResult *result,
                                       RBAudioscrobblerUser *user);
static GPtrArray * parse_recent_tracks (RBAudioscrobblerUser *user,
                                        const char *data);

static void load_cached_top_tracks (RBAudioscrobblerUser *user);
static void request_top_tracks (RBAudioscrobblerUser *user, int limit);
static void top_tracks_response_cb (SoupSession *session,
                                    GAsyncResult *result,
				    RBAudioscrobblerUser *user);
static GPtrArray * parse_top_tracks (RBAudioscrobblerUser *user,
                                     const char *data);

static void load_cached_loved_tracks (RBAudioscrobblerUser *user);
static void request_loved_tracks (RBAudioscrobblerUser *user, int limit);
static void loved_tracks_response_cb (SoupSession *session,
                                      GAsyncResult *result,
				      RBAudioscrobblerUser *user);
static GPtrArray * parse_loved_tracks (RBAudioscrobblerUser *user,
                                       const char *data);

static void load_cached_top_artists (RBAudioscrobblerUser *user);
static void request_top_artists (RBAudioscrobblerUser *user, int limit);
static void top_artists_response_cb (SoupSession *session,
                                     GAsyncResult *result,
				     RBAudioscrobblerUser *user);
static GPtrArray * parse_top_artists (RBAudioscrobblerUser *user,
                                      const char *data);

static char * calculate_cached_image_path (RBAudioscrobblerUser *user,
                                           RBAudioscrobblerUserData *data);
static void download_image (RBAudioscrobblerUser *user,
                            const char *image_url,
                            RBAudioscrobblerUserData *data);
static void image_download_cb (GObject *source_object,
                               GAsyncResult *res,
                               gpointer user_data);
static void love_track_response_cb (SoupSession *session,
                                    GAsyncResult *result,
                                    RBAudioscrobblerUser *user);
static void ban_track_response_cb (SoupSession *session,
                                   GAsyncResult *result,
                                   RBAudioscrobblerUser *user);

enum {
	PROP_0,
	PROP_SERVICE
};

enum {
	USER_INFO_UPDATED,
	RECENT_TRACKS_UPDATED,
	TOP_TRACKS_UPDATED,
	LOVED_TRACKS_UPDATED,
	TOP_ARTISTS_UPDATED,
	LAST_SIGNAL
};

static guint rb_audioscrobbler_user_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_DYNAMIC_TYPE (RBAudioscrobblerUser, rb_audioscrobbler_user, G_TYPE_OBJECT)

RBAudioscrobblerUser *
rb_audioscrobbler_user_new (RBAudioscrobblerService *service)
{
	return g_object_new (RB_TYPE_AUDIOSCROBBLER_USER,
	                     "service", service,
	                     NULL);
}

static void
rb_audioscrobbler_user_class_init (RBAudioscrobblerUserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = rb_audioscrobbler_user_constructed;
	object_class->dispose = rb_audioscrobbler_user_dispose;
	object_class->finalize = rb_audioscrobbler_user_finalize;
	object_class->get_property = rb_audioscrobbler_user_get_property;
	object_class->set_property = rb_audioscrobbler_user_set_property;

	g_object_class_install_property (object_class,
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Audioscrobbler service that this should use for requests",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
	                                                      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));


	rb_audioscrobbler_user_signals[USER_INFO_UPDATED] =
		g_signal_new ("user-info-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              RB_TYPE_AUDIOSCROBBLER_USER_DATA);

	rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED] =
		g_signal_new ("recent-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED] =
		g_signal_new ("top-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED] =
		g_signal_new ("loved-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED] =
		g_signal_new ("top-artists-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              NULL,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerUserPrivate));
}

static void
rb_audioscrobbler_user_class_finalize (RBAudioscrobblerUserClass *klass)
{
}

static void
rb_audioscrobbler_user_init (RBAudioscrobblerUser *user)
{
	user->priv = RB_AUDIOSCROBBLER_USER_GET_PRIVATE (user);

	user->priv->soup_session = soup_session_new ();
	user->priv->file_to_data_queue_map = g_hash_table_new_full (g_file_hash,
	                                                            (GEqualFunc) g_file_equal,
	                                                            g_object_unref,
	                                                            free_data_queue);
	user->priv->file_to_cancellable_map = g_hash_table_new_full (g_file_hash,
	                                                             (GEqualFunc) g_file_equal,
	                                                             NULL,
	                                                             g_object_unref);
}

static void
rb_audioscrobbler_user_constructed (GObject *object)
{
}

static void
rb_audioscrobbler_user_dispose (GObject* object)
{
	RBAudioscrobblerUser *user = RB_AUDIOSCROBBLER_USER (object);

	if (user->priv->service != NULL) {
		g_object_unref (user->priv->service);
		user->priv->service = NULL;
	}

	if (user->priv->soup_session != NULL) {
		soup_session_abort (user->priv->soup_session);
		g_object_unref (user->priv->soup_session);
		user->priv->soup_session = NULL;
	}

	if (user->priv->user_info != NULL) {
		rb_audioscrobbler_user_data_unref (user->priv->user_info);
		user->priv->user_info = NULL;
	}

	if (user->priv->recent_tracks != NULL) {
		g_ptr_array_unref (user->priv->recent_tracks);
		user->priv->recent_tracks = NULL;
	}

	if (user->priv->top_tracks != NULL) {
		g_ptr_array_unref (user->priv->top_tracks);
		user->priv->top_tracks = NULL;
	}

	if (user->priv->loved_tracks != NULL) {
		g_ptr_array_unref (user->priv->loved_tracks);
		user->priv->loved_tracks = NULL;
	}

	if (user->priv->top_artists != NULL) {
		g_ptr_array_unref (user->priv->top_artists);
		user->priv->top_artists = NULL;
	}

	/* free this map first because file_to_data_queue_map owns the file reference */
	if (user->priv->file_to_cancellable_map != NULL) {
		GList *key;

		for (key = g_hash_table_get_keys (user->priv->file_to_cancellable_map);
		     key != NULL;
		     key = g_list_next (key)) {
			GCancellable *cancellable = g_hash_table_lookup (user->priv->file_to_cancellable_map, key->data);
			g_cancellable_cancel (cancellable);
		}
		g_list_free (key);

		g_hash_table_unref (user->priv->file_to_cancellable_map);
		user->priv->file_to_cancellable_map = NULL;
	}

	if (user->priv->file_to_data_queue_map != NULL) {
		g_hash_table_unref (user->priv->file_to_data_queue_map);
		user->priv->file_to_data_queue_map = NULL;
	}
}

static void
rb_audioscrobbler_user_finalize (GObject *object)
{
	RBAudioscrobblerUser *user = RB_AUDIOSCROBBLER_USER (object);

	g_free (user->priv->username);
	g_free (user->priv->session_key);
}

static void
rb_audioscrobbler_user_get_property (GObject *object,
                                     guint prop_id,
                                     GValue *value,
                                     GParamSpec *pspec)
{
	switch (prop_id) {
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_user_set_property (GObject *object,
                                     guint prop_id,
                                     const GValue *value,
                                     GParamSpec *pspec)
{
	RBAudioscrobblerUser *user = RB_AUDIOSCROBBLER_USER (object);
	switch (prop_id) {
	case PROP_SERVICE:
		user->priv->service = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_audioscrobbler_user_set_authentication_details (RBAudioscrobblerUser *user,
                                                   const char *username,
                                                   const char *session_key)
{
	g_free (user->priv->username);
	user->priv->username = g_strdup (username);

	g_free (user->priv->session_key);
	user->priv->session_key = g_strdup (session_key);

	/* cancel pending requests */
	soup_session_abort (user->priv->soup_session);

	/* load new user from cache (or set to NULL) */
	load_from_cache (user);
}

void
rb_audioscrobbler_user_update (RBAudioscrobblerUser *user)
{
	if (user->priv->username != NULL) {
		/* update if cached data is no longer valid */
		if (is_cached_response_expired (user, "user_info", USER_INFO_LIFETIME)) {
			rb_debug ("cached user info response is expired, updating");
			request_user_info (user);
		} else {
			rb_debug ("cached user info response is still valid, not updating");
		}

		if (is_cached_response_expired (user, "recent_tracks", RECENT_TRACKS_LIFETIME)) {
			rb_debug ("cached recent tracks response is expired, updating");
			request_recent_tracks (user, 15);
		} else {
			rb_debug ("cached recent tracks response is still valid, not updating");
		}

		if (is_cached_response_expired (user, "top_tracks", TOP_TRACKS_LIFETIME)) {
			rb_debug ("cached top tracks response is expired, updating");
			request_top_tracks (user, 15);
		} else {
			rb_debug ("cached top tracks response is still valid, not updating");
		}

		if (is_cached_response_expired (user, "loved_tracks", LOVED_TRACKS_LIFETIME)) {
			rb_debug ("cached loved tracks response is expired, updating");
			request_loved_tracks (user, 15);
		} else {
			rb_debug ("cached loved tracks response is still valid, not updating");
		}

		if (is_cached_response_expired (user, "top_artists", TOP_ARTISTS_LIFETIME)) {
			rb_debug ("cached top artists response is expired, updating");
			request_top_artists (user, 15);
		} else {
			rb_debug ("cached top artists is still valid, not updating");
		}
	}
}

void
rb_audioscrobbler_user_force_update (RBAudioscrobblerUser *user)
{
	if (user->priv->username != NULL) {
		rb_debug ("forcing update of user data");
		request_user_info (user);
		request_recent_tracks (user, 15);
		request_top_tracks (user, 15);
		request_loved_tracks (user, 15);
		request_top_artists (user, 15);
	}
}

static void
load_from_cache (RBAudioscrobblerUser *user)
{
	/* delete old data */
	if (user->priv->user_info != NULL) {
		rb_audioscrobbler_user_data_unref (user->priv->user_info);
		user->priv->user_info = NULL;
	}

	if (user->priv->recent_tracks != NULL) {
		g_ptr_array_unref (user->priv->recent_tracks);
		user->priv->recent_tracks = NULL;
	}

	if (user->priv->top_tracks != NULL) {
		g_ptr_array_unref (user->priv->top_tracks);
		user->priv->top_tracks = NULL;
	}

	if (user->priv->loved_tracks != NULL) {
		g_ptr_array_unref (user->priv->loved_tracks);
		user->priv->loved_tracks = NULL;
	}

	if (user->priv->top_artists != NULL) {
		g_ptr_array_unref (user->priv->top_artists);
		user->priv->top_artists = NULL;
	}

	/* if a username is set then attempt to load cached data */
	if (user->priv->username != NULL) {
		load_cached_user_info (user);
		load_cached_recent_tracks (user);
		load_cached_top_tracks (user);
		load_cached_loved_tracks (user);
		load_cached_top_artists (user);
	}
}

static char *
calculate_cached_response_path (RBAudioscrobblerUser *user, const char *request_name)
{
	const char *rb_cache_dir;
	rb_cache_dir = rb_user_cache_dir ();

	return g_build_filename (rb_cache_dir,
	                         "audioscrobbler",
	                         rb_audioscrobbler_service_get_name (user->priv->service),
	                         "ws-responses",
	                         user->priv->username,
	                         request_name,
	                         NULL);
}

static gboolean
is_cached_response_expired (RBAudioscrobblerUser *user,
                            const char *request_name,
                            long lifetime)
{
	char *response_path;
	GFile *file;
	GFileInfo *info;

	response_path = calculate_cached_response_path (user, request_name);
	file = g_file_new_for_path (response_path);
	info = g_file_query_info (file,
	                          G_FILE_ATTRIBUTE_TIME_MODIFIED,
	                          G_FILE_QUERY_INFO_NONE,
	                          NULL,
	                          NULL);
	g_free (response_path);
	g_object_unref (file);

	if (info == NULL) {
		return TRUE;
	} else {
		GTimeVal now;
		GTimeVal modified;

		g_get_current_time (&now);
		g_file_info_get_modification_time (info, &modified);

		g_object_unref (info);

		return now.tv_sec - modified.tv_sec > lifetime;
	}
}

static void
save_response_to_cache (RBAudioscrobblerUser *user, const char *request_name, const char *data)
{
	char *filename;
	char *file_uri;
	GError *error;

	filename = calculate_cached_response_path (user, request_name);
	file_uri = g_filename_to_uri (filename, NULL, NULL);

	error = NULL;
	if (rb_uri_create_parent_dirs (file_uri, &error)) {
		g_file_set_contents (filename, data, -1, &error);
	}

	if (error == NULL) {
		rb_debug ("saved %s to cache", request_name);
	} else {
		rb_debug ("error saving %s to cache: %s", request_name, error->message);
		g_error_free (error);
	}

	g_free (filename);
	g_free (file_uri);
}

static GPtrArray *
parse_track_array (RBAudioscrobblerUser *user, JsonArray *track_array)
{
	GPtrArray *tracks;
	int i;

	tracks = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_unref);

	for (i = 0; i < json_array_get_length (track_array); i++) {
		JsonObject *track_object;
		JsonObject *artist_object;
		RBAudioscrobblerUserData *track;
		char *image_path;

		track_object = json_array_get_object_element (track_array, i);

		track = rb_audioscrobbler_user_data_new ();
		track->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK;
		track->track.title = g_strdup (json_object_get_string_member (track_object, "name"));

		/* sometimes the artist object has a "name" member,
		 * and other times it has a "#text" member.
		 */
		artist_object = json_object_get_object_member (track_object, "artist");
		if (json_object_has_member (artist_object, "name")) {
			track->track.artist = g_strdup (json_object_get_string_member (artist_object, "name"));
		} else {
			track->track.artist = g_strdup (json_object_get_string_member (artist_object, "#text"));
		}

		track->url = g_strdup (json_object_get_string_member (track_object, "url"));

		image_path = calculate_cached_image_path (user, track);
		track->image = gdk_pixbuf_new_from_file_at_size (image_path,
		                                                 LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE,
		                                                 NULL);
		if (track->image == NULL && json_object_has_member (track_object, "image") == TRUE) {
			JsonArray *image_array;
			JsonObject *image_object;

			image_array = json_object_get_array_member (track_object, "image");
			image_object = json_array_get_object_element (image_array, 0);
			download_image (user, json_object_get_string_member (image_object, "#text"), track);
		}

		g_ptr_array_add (tracks, track);

		g_free (image_path);
	}

	return tracks;
}

static GPtrArray *
parse_artist_array (RBAudioscrobblerUser *user, JsonArray *artist_array)
{
	GPtrArray *artists;
	int i;

	artists = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_unref);

	for (i = 0; i < json_array_get_length (artist_array); i++) {
		JsonObject *artist_object;
		RBAudioscrobblerUserData *artist;
		char *image_path;

		artist_object = json_array_get_object_element (artist_array, i);

		artist = rb_audioscrobbler_user_data_new ();
		artist->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST;
		artist->artist.name = g_strdup (json_object_get_string_member (artist_object, "name"));
		artist->url = g_strdup (json_object_get_string_member (artist_object, "url"));

		image_path = calculate_cached_image_path (user, artist);
		artist->image = gdk_pixbuf_new_from_file_at_size (image_path,
		                                                  LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE,
		                                                  NULL);
		if (artist->image == NULL && json_object_has_member (artist_object, "image") == TRUE) {
			JsonArray *image_array;
			JsonObject *image_object;

			image_array = json_object_get_array_member (artist_object, "image");
			image_object = json_array_get_object_element (image_array, 0);
			download_image (user, json_object_get_string_member (image_object, "#text"), artist);
		}

		g_ptr_array_add (artists, artist);

		g_free (image_path);
	}

	return artists;
}

/* user info */
static void
load_cached_user_info (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = calculate_cached_response_path (user, "user_info");

	/* delete old data */
	if (user->priv->user_info != NULL) {
		rb_audioscrobbler_user_data_unref (user->priv->user_info);
		user->priv->user_info = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached user_info");
		user->priv->user_info = parse_user_info (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[USER_INFO_UPDATED],
	               0, user->priv->user_info);

	g_free (filename);
	g_free (data);
}

static void
request_user_info (RBAudioscrobblerUser *user)
{
	const char *api_key;
	const char *api_url;
	char *query;
	SoupMessage *msg;

	rb_debug ("requesting user info");

	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);

	query = soup_form_encode ("method", "user.getInfo",
				  "user", user->priv->username,
				  "api_key", api_key,
				  "format", "json",
				  NULL);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) user_info_response_cb,
					  user);
}

static void
user_info_response_cb (SoupSession *session,
                       GAsyncResult *result,
                       RBAudioscrobblerUser *user)
{
	GBytes *bytes;
	const char *body;
	RBAudioscrobblerUserData *user_info;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		user_info = parse_user_info (user, body);

		if (user_info != NULL) {
			rb_debug ("user info request was successful");

			if (user->priv->user_info != NULL) {
				rb_audioscrobbler_user_data_unref (user->priv->user_info);
			}
			user->priv->user_info = user_info;

			save_response_to_cache (user, "user_info", body);

			g_signal_emit (user, rb_audioscrobbler_user_signals[USER_INFO_UPDATED],
			               0, user->priv->user_info);
		} else {
			rb_debug ("invalid response from user info request");
		}

		g_bytes_unref (bytes);
	} else {
		rb_debug ("error sending user info request");
	}
}

static RBAudioscrobblerUserData *
parse_user_info (RBAudioscrobblerUser *user, const char *data)
{
	RBAudioscrobblerUserData *user_info;
	JsonParser *parser;

	user_info = NULL;

	parser = json_parser_new ();
	if (data != NULL && json_parser_load_from_data (parser, data, -1, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		if (json_object_has_member (root_object, "user")) {
			JsonObject *user_object;
			user_object = json_object_get_object_member (root_object, "user");
			char *image_path;

			user_info = rb_audioscrobbler_user_data_new ();
			user_info->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO;
			user_info->user_info.username = g_strdup (json_object_get_string_member (user_object, "name"));
			user_info->user_info.playcount = g_strdup (json_object_get_string_member (user_object, "playcount"));
			user_info->url = g_strdup (json_object_get_string_member (user_object, "url"));

			image_path = calculate_cached_image_path (user, user_info);
			user_info->image = gdk_pixbuf_new_from_file_at_size (image_path,
					                                     USER_PROFILE_IMAGE_SIZE, -1, NULL);
			if (user_info->image == NULL && json_object_has_member (user_object, "image") == TRUE) {
				JsonArray *image_array;
				JsonObject *image_object;

				image_array = json_object_get_array_member (user_object, "image");
				image_object = json_array_get_object_element (image_array, 2);
				download_image (user, json_object_get_string_member (image_object, "#text"), user_info);
			}

			g_free (image_path);
		} else {
			rb_debug ("error parsing user info response: no user object exists");
		}
	} else {
		rb_debug ("error parsing user info response: empty or invalid response");
	}

	g_object_unref (parser);

	return user_info;
}

/* recent tracks */
static void
load_cached_recent_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = calculate_cached_response_path (user, "recent_tracks");

	/* delete old data */
	if (user->priv->recent_tracks != NULL) {
		g_ptr_array_unref (user->priv->recent_tracks);
		user->priv->recent_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached recent tracks");
		user->priv->recent_tracks = parse_recent_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED],
	               0, user->priv->recent_tracks);

	g_free (filename);
	g_free (data);
}

static void
request_recent_tracks (RBAudioscrobblerUser *user, int limit)
{
	const char *api_key;
	const char *api_url;
	char *limit_str;
	char *query;
	SoupMessage *msg;

	rb_debug ("requesting recent tracks");

	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);

	limit_str = g_strdup_printf ("%d", limit);

	query = soup_form_encode ("method", "user.getRecentTracks",
				  "user", user->priv->username,
				  "api_key", api_key,
				  "limit", limit_str,
				  "format", "json",
				  NULL);

	g_free (limit_str);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) recent_tracks_response_cb,
					  user);
}

static void
recent_tracks_response_cb (SoupSession *session,
                           GAsyncResult *result,
                           RBAudioscrobblerUser *user)
{
	GBytes *bytes;
	const char *body;
	GPtrArray *recent_tracks;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		recent_tracks = parse_recent_tracks (user, body);

		if (recent_tracks != NULL) {
			rb_debug ("recent tracks request was successful");

			if (user->priv->recent_tracks != NULL) {
				g_ptr_array_unref (user->priv->recent_tracks);
			}
			user->priv->recent_tracks = recent_tracks;

			save_response_to_cache (user, "recent_tracks", body);

			g_signal_emit (user, rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED],
			               0, user->priv->recent_tracks);
		} else {
			rb_debug ("invalid response from recent tracks request");
		}

		g_bytes_unref (bytes);
	} else {
		rb_debug ("error sending recent tracks request");
	}
}

static GPtrArray *
parse_recent_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *recent_tracks;
	JsonParser *parser;

	recent_tracks = NULL;

	parser = json_parser_new ();
	if (data != NULL && json_parser_load_from_data (parser, data, -1, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		if (json_object_has_member (root_object, "recenttracks")) {
			JsonObject *recent_tracks_object;
			recent_tracks_object = json_object_get_object_member (root_object, "recenttracks");

			if (json_object_has_member (recent_tracks_object, "track") == TRUE) {
				JsonArray *track_array;

				track_array = json_object_get_array_member (recent_tracks_object, "track");
				recent_tracks = parse_track_array (user, track_array);
			}
		} else {
			rb_debug ("error parsing recent tracks response: no recenttracks object exists");
		}
	} else {
		rb_debug ("error parsing recent tracks response: empty or invalid response");
	}

	g_object_unref (parser);

	return recent_tracks;
}

/* top tracks */
static void
load_cached_top_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = calculate_cached_response_path (user, "top_tracks");

	/* delete old data */
	if (user->priv->top_tracks != NULL) {
		g_ptr_array_unref (user->priv->top_tracks);
		user->priv->top_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached top tracks");
		user->priv->top_tracks = parse_top_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED],
	               0, user->priv->top_tracks);

	g_free (filename);
	g_free (data);
}

static void
request_top_tracks (RBAudioscrobblerUser *user, int limit)
{
	const char *api_key;
	const char *api_url;
	char *limit_str;
	char *query;
	SoupMessage *msg;

	rb_debug ("requesting top tracks");

	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);
	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);

	limit_str = g_strdup_printf ("%d", limit);

	query = soup_form_encode ("method", "library.getTracks",
				  "user", user->priv->username,
				  "api_key", api_key,
				  "limit", limit_str,
				  "format", "json",
				  NULL);
	g_free (limit_str);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) top_tracks_response_cb,
					  user);
}

static void
top_tracks_response_cb (SoupSession *session,
                        GAsyncResult *result,
			RBAudioscrobblerUser *user)
{
	GBytes *bytes;
	const char *body;
	GPtrArray *top_tracks;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		top_tracks = parse_top_tracks (user, body);

		if (top_tracks != NULL) {
			rb_debug ("top tracks request was successful");

			if (user->priv->top_tracks != NULL) {
				g_ptr_array_unref (user->priv->top_tracks);
			}
			user->priv->top_tracks = top_tracks;

			save_response_to_cache (user, "top_tracks", body);

			g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED],
				       0, user->priv->top_tracks);
		} else {
			rb_debug ("invalid response from top tracks request");
		}

		g_bytes_unref (bytes);
	} else {
		rb_debug ("error sending top tracks request");
	}
}

static GPtrArray *
parse_top_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *top_tracks;
	JsonParser *parser;

	top_tracks = NULL;

	parser = json_parser_new ();
	if (data != NULL && json_parser_load_from_data (parser, data, -1, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		if (json_object_has_member (root_object, "toptracks")) {
			JsonObject *top_tracks_object;
			top_tracks_object = json_object_get_object_member (root_object, "toptracks");

			if (json_object_has_member (top_tracks_object, "track") == TRUE) {
				JsonArray *track_array;

				track_array = json_object_get_array_member (top_tracks_object, "track");
				top_tracks = parse_track_array (user, track_array);
			}
		} else {
			rb_debug ("error parsing top tracks response: no toptracks object exists");
		}
	} else {
		rb_debug ("error parsing top tracks response: empty or invalid response");
	}

	g_object_unref (parser);

	return top_tracks;
}

/* loved tracks */
static void
load_cached_loved_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = calculate_cached_response_path (user, "loved_tracks");

	/* delete old data */
	if (user->priv->loved_tracks != NULL) {
		g_ptr_array_unref (user->priv->loved_tracks);
		user->priv->loved_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached loved tracks");
		user->priv->loved_tracks = parse_loved_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED],
	               0, user->priv->loved_tracks);

	g_free (filename);
	g_free (data);
}

static void
request_loved_tracks (RBAudioscrobblerUser *user, int limit)
{
	const char *api_key;
	const char *api_url;
	char *limit_str;
	char *query;
	SoupMessage *msg;

	rb_debug ("requesting loved tracks");

	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);

	limit_str = g_strdup_printf ("%d", limit);

	query = soup_form_encode ("method", "user.getLovedTracks",
				  "user", user->priv->username,
				  "api_key", api_key,
				  "limit", limit_str,
				  "format", "json",
				  NULL);

	g_free (limit_str);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) loved_tracks_response_cb,
					  user);
}

static void
loved_tracks_response_cb (SoupSession *session,
                          GAsyncResult *result,
                          RBAudioscrobblerUser *user)
{
	GBytes *bytes;
	const char *body;
	GPtrArray *loved_tracks;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		loved_tracks = parse_loved_tracks (user, body);

		if (loved_tracks != NULL) {
			rb_debug ("loved tracks request was successful");

			if (user->priv->loved_tracks != NULL) {
				g_ptr_array_unref (user->priv->loved_tracks);
			}
			user->priv->loved_tracks = loved_tracks;

			save_response_to_cache (user, "loved_tracks", body);

			g_signal_emit (user, rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED],
				       0, user->priv->loved_tracks);
		} else {
			rb_debug ("invalid response from loved tracks request");
		}

		g_bytes_unref (bytes);
	} else {
		rb_debug ("error sending loved tracks request");
	}
}

static GPtrArray *
parse_loved_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *loved_tracks;
	JsonParser *parser;

	loved_tracks = NULL;

	parser = json_parser_new ();
	if (data != NULL && json_parser_load_from_data (parser, data, -1, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		if (json_object_has_member (root_object, "lovedtracks")) {
			JsonObject *loved_tracks_object;
			loved_tracks_object = json_object_get_object_member (root_object, "lovedtracks");

			if (json_object_has_member (loved_tracks_object, "track") == TRUE) {
				JsonArray *track_array;

				track_array = json_object_get_array_member (loved_tracks_object, "track");
				loved_tracks = parse_track_array (user, track_array);
			}
		} else {
			rb_debug ("error parsing loved tracks response: no lovedtracks object exists");
		}
	} else {
		rb_debug ("error parsing loved tracks response: empty or invalid response");
	}

	g_object_unref (parser);

	return loved_tracks;
}

/* top artists */
static void
load_cached_top_artists (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = calculate_cached_response_path (user, "top_artists");

	/* delete old data */
	if (user->priv->top_artists != NULL) {
		g_ptr_array_unref (user->priv->top_artists);
		user->priv->top_artists = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached top artists");
		user->priv->top_artists = parse_top_artists (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED],
	               0, user->priv->top_artists);

	g_free (filename);
	g_free (data);
}

static void
request_top_artists (RBAudioscrobblerUser *user, int limit)
{
	const char *api_key;
	const char *api_url;
	char *limit_str;
	char *query;
	SoupMessage *msg;

	rb_debug ("requesting top artists");

	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);
	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);

	limit_str = g_strdup_printf ("%d", limit);
	query = soup_form_encode ("method", "library.getArtists",
				  "user", user->priv->username,
				  "api_key", api_key,
				  "limit", limit_str,
				  "format", "json",
				  NULL);
	g_free (limit_str);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_GET, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) top_artists_response_cb,
					  user);
}

static void
top_artists_response_cb (SoupSession *session,
                         GAsyncResult *result,
			 RBAudioscrobblerUser *user)
{
	GBytes *bytes;
	const char *body;
	GPtrArray *top_artists;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, NULL);
		top_artists = parse_top_artists (user, body);

		if (top_artists != NULL) {
			rb_debug ("top artists request was successful");

			if (user->priv->top_artists != NULL) {
				g_ptr_array_unref (user->priv->top_artists);
			}
			user->priv->top_artists = top_artists;

			save_response_to_cache (user, "top_artists", body);

			g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED],
				       0, user->priv->top_artists);
		} else {
			rb_debug ("invalid response from top artists request");
		}
	} else {
		rb_debug ("error sending top artists request");
	}
}

static GPtrArray *
parse_top_artists (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *top_artists;
	JsonParser *parser;

	top_artists = NULL;

	parser = json_parser_new ();
	if (data != NULL && json_parser_load_from_data (parser, data, -1, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		if (json_object_has_member (root_object, "topartists")) {
			JsonObject *top_artists_object;
			top_artists_object = json_object_get_object_member (root_object, "topartists");

			if (json_object_has_member (top_artists_object, "artist") == TRUE) {
				JsonArray *artist_array;

				artist_array = json_object_get_array_member (top_artists_object, "artist");
				top_artists = parse_artist_array (user, artist_array);
			}
		} else {
			rb_debug ("error parsing top artists response: no topartists object exists");
		}
	} else {
		rb_debug ("error parsing top artists response: empty or invalid response");
	}

	g_object_unref (parser);

	return top_artists;
}

static char *
calculate_cached_image_path (RBAudioscrobblerUser *user, RBAudioscrobblerUserData *data)
{
	const char *rb_cache_dir;
	char *cache_dir;
	char *image_path = NULL;

	rb_cache_dir = rb_user_cache_dir ();
	cache_dir = g_build_filename (rb_cache_dir,
	                              "audioscrobbler",
	                              rb_audioscrobbler_service_get_name (user->priv->service),
	                              "images",
	                              NULL);

	if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO) {
		image_path = g_build_filename (cache_dir,
		                               "users",
		                               data->user_info.username,
		                               NULL);

	} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
		char *filename = g_strdup_printf ("%s - %s",
		                                  data->track.artist,
		                                  data->track.title);
		image_path = g_build_filename (cache_dir,
		                               "tracks",
		                               filename,
		                               NULL);
		g_free (filename);

	} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
		image_path = g_build_filename (cache_dir,
		                               "artists",
		                               data->artist.name,
		                               NULL);
	}

	g_free (cache_dir);
	return image_path;
}

static void
download_image (RBAudioscrobblerUser *user, const char *image_url, RBAudioscrobblerUserData *data)
{
	GFile *src_file;
	GQueue *data_queue;

	/* check image_url is not null or empty */
	if (image_url == NULL || image_url[0] == '\0') {
		return;
	}

	src_file = g_file_new_for_uri (image_url);
	data_queue = g_hash_table_lookup (user->priv->file_to_data_queue_map, src_file);

	/* only start a download if the file is not already being downloaded */
	if (data_queue == NULL) {
		char *dest_filename;
		char *dest_file_uri;
		GError *error;

		/* ensure the dest dir exists */
		dest_filename = calculate_cached_image_path (user, data);
		dest_file_uri = g_filename_to_uri (dest_filename, NULL, NULL);
		error = NULL;
		rb_uri_create_parent_dirs (dest_file_uri, &error);

		if (error == NULL) {
			GCancellable *cancellable;
			GFile *dest_file;

			/* add new queue containing data to map */
			data_queue = g_queue_new ();
			g_queue_push_tail (data_queue, rb_audioscrobbler_user_data_ref (data));
			g_hash_table_insert (user->priv->file_to_data_queue_map,
			                     src_file,
			                     data_queue);

			/* create a cancellable for this download */
			cancellable = g_cancellable_new ();
			g_hash_table_insert (user->priv->file_to_cancellable_map, src_file, cancellable);

			/* download the file */
			rb_debug ("downloading image %s to %s", image_url, dest_filename);
			dest_file = g_file_new_for_path (dest_filename);
			g_file_copy_async (src_file,
				           dest_file,
				           G_FILE_COPY_OVERWRITE,
				           G_PRIORITY_DEFAULT,
				           cancellable,
				           NULL,
				           NULL,
				           image_download_cb,
				           user);

			g_object_unref (dest_file);
		} else {
			rb_debug ("not downloading image: error creating dest dir");
			g_error_free (error);
			g_object_unref (src_file);
		}

		g_free (dest_filename);
		g_free (dest_file_uri);
	} else {
		/* the file is already being downloaded. add this data to the queue for
		 * the file, so that data will be updated when the download completes */
		rb_debug ("image %s is already being downloaded. adding data to queue", image_url);
		g_queue_push_tail (data_queue, rb_audioscrobbler_user_data_ref (data));
	}
}

static void
copy_image_for_data (RBAudioscrobblerUser *user, const char *src_file_path, RBAudioscrobblerUserData *dest_data)
{
	GFile *src_file = g_file_new_for_path (src_file_path);
	char *dest_file_path = calculate_cached_image_path (user, dest_data);
	GFile *dest_file = g_file_new_for_path (dest_file_path);

	if (g_file_equal (src_file, dest_file) == FALSE) {
		rb_debug ("copying cache image %s to %s",
		          src_file_path,
		          dest_file_path);

		g_file_copy_async (src_file,
		                   dest_file,
		                   G_FILE_COPY_OVERWRITE,
		                   G_PRIORITY_DEFAULT,
		                   NULL,
		                   NULL,
		                   NULL,
		                   NULL,
		                   NULL);
	}

	g_object_unref (src_file);
	g_free (dest_file_path);
	g_object_unref (dest_file);
}

static void
image_download_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	RBAudioscrobblerUser *user = RB_AUDIOSCROBBLER_USER (user_data);
	GFile *src_file = G_FILE (source_object);
	GQueue *data_queue;

	/* free the cancellable */
	g_hash_table_remove (user->priv->file_to_cancellable_map, src_file);

	data_queue = g_hash_table_lookup (user->priv->file_to_data_queue_map, src_file);

	if (g_file_copy_finish (src_file, res, NULL)) {
		char *dest_file_path;
		GList *data_i;

		/* the image was downloaded for the first item in the queue,
		 * so the first item must be used to get the path */
		dest_file_path = calculate_cached_image_path (user, g_queue_peek_head (data_queue));

		/* iterate through each data item in the queue,
		 * and if necessary update the image and emit appropriate signal */
		for (data_i = g_queue_peek_head_link(data_queue); data_i != NULL; data_i = g_list_next (data_i)) {
			RBAudioscrobblerUserData *data = data_i->data;

			/* if nobody else has a reference to the data then
			 * there is no need to update the image */
			if (data->refcount <= 1) {
				continue;
			}

			if (data->image != NULL) {
				g_object_unref (data->image);
			}

			/* load image at correct size for the data type */
			if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO) {
				data->image = gdk_pixbuf_new_from_file_at_size (dest_file_path, USER_PROFILE_IMAGE_SIZE, -1, NULL);
			} else {
				data->image = gdk_pixbuf_new_from_file_at_size (dest_file_path, LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
			}

			/* copy the image to the correct location for this data item, for next time */
			copy_image_for_data (user, dest_file_path, data);

			/* emit appropriate signal - quite ugly, surely this could be done in a nicer way */
			if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO) {
				g_signal_emit (user, rb_audioscrobbler_user_signals[USER_INFO_UPDATED],
				               0, data);
			} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK) {
				int i;
				if (user->priv->recent_tracks != NULL) {
					for (i = 0; i < user->priv->recent_tracks->len; i++) {
						if (g_ptr_array_index (user->priv->recent_tracks, i) == data) {
							g_signal_emit (user, rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED],
							               0, user->priv->recent_tracks);
						}
					}
				}
				if (user->priv->top_tracks != NULL) {
					for (i = 0; i < user->priv->top_tracks->len; i++) {
						if (g_ptr_array_index (user->priv->top_tracks, i) == data) {
							g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED],
							               0, user->priv->top_tracks);
						}
					}
				}
				if (user->priv->loved_tracks != NULL) {
					for (i = 0; i < user->priv->loved_tracks->len; i++) {
						if (g_ptr_array_index (user->priv->loved_tracks, i) == data) {
							g_signal_emit (user, rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED],
							               0, user->priv->loved_tracks);
						}
					}
				}
			} else if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST) {
				int i;
				if (user->priv->top_artists != NULL) {
					for (i = 0; i < user->priv->top_artists->len; i++) {
						if (g_ptr_array_index (user->priv->top_artists, i) == data) {
							g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED],
							               0, user->priv->top_artists);
						}
					}
				}
			}
		}
		g_free (dest_file_path);
	} else {
		rb_debug ("error downloading image. possibly due to cancellation");
	}

	/* cleanup the file and data */
	g_hash_table_remove (user->priv->file_to_data_queue_map, src_file);
}

void
rb_audioscrobbler_user_love_track (RBAudioscrobblerUser *user,
                                   const char *title,
                                   const char *artist)
{
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	SoupMessage *msg;

	rb_debug ("loving track %s - %s", artist, title);

	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (user->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);

	sig_arg = g_strdup_printf ("api_key%sartist%smethodtrack.lovesk%strack%s%s",
	                           api_key,
	                           artist,
	                           user->priv->session_key,
	                           title,
				   api_sec);

	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "track.love",
				  "track", title,
				  "artist", artist,
				  "api_key", api_key,
				  "api_sig", sig,
				  "sk", user->priv->session_key,
				  NULL);

	g_free (sig_arg);
	g_free (sig);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_POST, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) love_track_response_cb,
					  user);
}

static void
love_track_response_cb (SoupSession *session,
                        GAsyncResult *result,
                        RBAudioscrobblerUser *user)
{
	/* Don't know if there's anything to do here,
	 * might want a debug message indicating success or failure?
	 */
}

void
rb_audioscrobbler_user_ban_track (RBAudioscrobblerUser *user,
                                  const char *title,
                                  const char *artist)
{
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	SoupMessage *msg;

	rb_debug ("banning track %s - %s", artist, title);

	api_key = rb_audioscrobbler_service_get_api_key (user->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (user->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (user->priv->service);

	sig_arg = g_strdup_printf ("api_key%sartist%smethodtrack.ban%strack%s%s",
	                           api_key,
	                           artist,
	                           user->priv->session_key,
	                           title,
				   api_sec);

	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "track.ban",
				  "track", title,
				  "artist", artist,
				  "api_key", api_key,
				  "api_sig", sig,
				  "sk", user->priv->session_key,
				  NULL);

	g_free (sig_arg);
	g_free (sig);

	msg = soup_message_new_from_encoded_form (SOUP_METHOD_POST, api_url, query);
	g_return_if_fail (msg != NULL);

	soup_session_send_and_read_async (user->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) ban_track_response_cb,
					  user);
}

static void
ban_track_response_cb (SoupSession *session,
                       GAsyncResult *result,
                       RBAudioscrobblerUser *user)
{
	/* Don't know if there's anything to do here,
	 * might want a debug message indicating success or failure?
	 */
}

void
_rb_audioscrobbler_user_register_type (GTypeModule *module)
{
	rb_audioscrobbler_user_register_type (module);
}
