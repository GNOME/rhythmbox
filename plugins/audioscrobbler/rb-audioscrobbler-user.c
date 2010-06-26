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

#include <libsoup/soup.h>
#include <libsoup/soup-gnome.h>
#include <json-glib/json-glib.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

#include "rb-audioscrobbler-user.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

#define USER_PROFILE_IMAGE_SIZE 126
#define LIST_ITEM_IMAGE_SIZE 34

static RBAudioscrobblerUserData *
rb_audioscrobbler_user_data_copy (RBAudioscrobblerUserData *data)
{
	RBAudioscrobblerUserData *d = g_slice_new0 (RBAudioscrobblerUserData);

	d->type = data->type;
	if (data->image != NULL) {
		d->image = g_object_ref (data->image);
	}

	switch (d->type) {
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO:
		d->user_info.username = g_strdup (data->user_info.username);
		d->user_info.url = g_strdup (data->user_info.url);
		d->user_info.playcount = g_strdup (data->user_info.playcount);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK:
		d->track.title = g_strdup (data->track.title);
		d->track.artist = g_strdup (data->track.artist);
		d->track.url = g_strdup (data->track.url);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST:
		d->artist.name = g_strdup (data->artist.name);
		d->artist.url = g_strdup (data->artist.url);
		break;
	}

	return d;
}

static void
rb_audioscrobbler_user_data_free (RBAudioscrobblerUserData *data)
{
	if (data->image != NULL) {
		g_object_unref (data->image);
	}
	switch (data->type) {
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO:
		g_free (data->user_info.username);
		g_free (data->user_info.url);
		g_free (data->user_info.playcount);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK:
		g_free (data->track.title);
		g_free (data->track.artist);
		g_free (data->track.url);
		break;
	case RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST:
		g_free (data->artist.name);
		g_free (data->artist.url);
		break;
	}

	g_slice_free (RBAudioscrobblerUserData, data);
}

GType
rb_audioscrobbler_user_data_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBAudioscrobblerUserData",
		                                     (GBoxedCopyFunc)rb_audioscrobbler_user_data_copy,
		                                     (GBoxedFreeFunc)rb_audioscrobbler_user_data_free);
	}

	return type;
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
	GPtrArray *recommended_artists;

	/* for image downloads */
	GHashTable *file_to_data_map;
	GHashTable *file_to_cancellable_map;
};

#define RB_AUDIOSCROBBLER_USER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_USER, RBAudioscrobblerUserPrivate))

static void rb_audioscrobbler_user_class_init (RBAudioscrobblerUserClass *klass);
static void rb_audioscrobbler_user_init (RBAudioscrobblerUser *user);
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

static void rb_audioscrobbler_user_load_from_cache (RBAudioscrobblerUser *user);

static char * rb_audioscrobbler_user_calculate_cached_response_path (RBAudioscrobblerUser *user,
                                                                     const char *request_name);
static void rb_audioscrobbler_user_save_response_to_cache (RBAudioscrobblerUser *user,
                                                           const char *request_name,
                                                           const char *data);

static void rb_audioscrobbler_user_load_cached_user_info (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_user_info (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_user_info_response_cb (SoupSession *session,
                                                          SoupMessage *msg,
                                                          gpointer user_data);
static RBAudioscrobblerUserData * rb_audioscrobbler_user_parse_user_info (RBAudioscrobblerUser *user,
                                                                          const char *data);

static void rb_audioscrobbler_user_load_cached_recent_tracks (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_recent_tracks (RBAudioscrobblerUser *user, int limit);
static void rb_audioscrobbler_user_recent_tracks_response_cb (SoupSession *session,
                                                              SoupMessage *msg,
                                                              gpointer user_data);
static GPtrArray * rb_audioscrobbler_user_parse_recent_tracks (RBAudioscrobblerUser *user,
                                                               const char *data);

static void rb_audioscrobbler_user_load_cached_top_tracks (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_top_tracks (RBAudioscrobblerUser *user, int limit);
static void rb_audioscrobbler_user_top_tracks_response_cb (SoupSession *session,
                                                           SoupMessage *msg,
                                                           gpointer user_data);
static GPtrArray * rb_audioscrobbler_user_parse_top_tracks (RBAudioscrobblerUser *user,
                                                            const char *data);

static void rb_audioscrobbler_user_load_cached_loved_tracks (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_loved_tracks (RBAudioscrobblerUser *user, int limit);
static void rb_audioscrobbler_user_loved_tracks_response_cb (SoupSession *session,
                                                             SoupMessage *msg,
                                                             gpointer user_data);
static GPtrArray * rb_audioscrobbler_user_parse_loved_tracks (RBAudioscrobblerUser *user,
                                                              const char *data);

static void rb_audioscrobbler_user_load_cached_top_artists (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_top_artists (RBAudioscrobblerUser *user, int limit);
static void rb_audioscrobbler_user_top_artists_response_cb (SoupSession *session,
                                                            SoupMessage *msg,
                                                            gpointer user_data);
static GPtrArray * rb_audioscrobbler_user_parse_top_artists (RBAudioscrobblerUser *user,
                                                             const char *data);

static void rb_audioscrobbler_user_load_cached_recommended_artists (RBAudioscrobblerUser *user);
static void rb_audioscrobbler_user_request_recommended_artists (RBAudioscrobblerUser *user, int limit);
static void rb_audioscrobbler_user_recommended_artists_response_cb (SoupSession *session,
                                                                    SoupMessage *msg,
                                                                    gpointer user_data);
static GPtrArray * rb_audioscrobbler_user_parse_recommended_artists (RBAudioscrobblerUser *user,
                                                                     const char *data);

static char * rb_audioscrobbler_user_calculate_cached_image_path (RBAudioscrobblerUser *user,
                                                                  RBAudioscrobblerUserData *data);
static void rb_audioscrobbler_user_download_image (RBAudioscrobblerUser *user,
                                                   const char *image_url,
                                                   RBAudioscrobblerUserData *data);
static void rb_audioscrobbler_user_image_download_cb (GObject *source_object,
                                                      GAsyncResult *res,
                                                      gpointer user_data);
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
	RECOMMENDED_ARTISTS_UPDATED,
	LAST_SIGNAL
};

static guint rb_audioscrobbler_user_signals[LAST_SIGNAL] = { 0 };

G_DEFINE_TYPE (RBAudioscrobblerUser, rb_audioscrobbler_user, G_TYPE_OBJECT)

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
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              RB_TYPE_AUDIOSCROBBLER_USER_DATA);

	rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED] =
		g_signal_new ("recent-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED] =
		g_signal_new ("top-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED] =
		g_signal_new ("loved-tracks-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED] =
		g_signal_new ("top-artists-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	rb_audioscrobbler_user_signals[RECOMMENDED_ARTISTS_UPDATED] =
		g_signal_new ("recommended-artists-updated",
		              G_OBJECT_CLASS_TYPE (object_class),
		              G_SIGNAL_RUN_LAST,
		              0,
		              NULL, NULL,
		              g_cclosure_marshal_VOID__BOXED,
		              G_TYPE_NONE,
		              1,
		              G_TYPE_PTR_ARRAY);

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerUserPrivate));
}

static void
rb_audioscrobbler_user_init (RBAudioscrobblerUser *user)
{
	user->priv = RB_AUDIOSCROBBLER_USER_GET_PRIVATE (user);

	user->priv->soup_session =
		soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
		                                     SOUP_TYPE_GNOME_FEATURES_2_26,
		                                     NULL);

	user->priv->file_to_data_map = g_hash_table_new (g_direct_hash, g_direct_equal);
	user->priv->file_to_cancellable_map = g_hash_table_new (g_direct_hash, g_direct_equal);
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
		g_boxed_free (RB_TYPE_AUDIOSCROBBLER_USER_DATA, user->priv->user_info);
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

	if (user->priv->recommended_artists != NULL) {
		g_ptr_array_unref (user->priv->recommended_artists);
		user->priv->recommended_artists = NULL;
	}

	if (user->priv->file_to_data_map != NULL) {
		g_hash_table_unref (user->priv->file_to_data_map);
		user->priv->file_to_data_map = NULL;
	}

	if (user->priv->file_to_cancellable_map != NULL) {
		GList *key;

		for (key = g_hash_table_get_keys (user->priv->file_to_cancellable_map);
		     key != NULL;
		     key = g_list_next (key)) {
			GCancellable *cancellable = g_hash_table_lookup (user->priv->file_to_cancellable_map, key->data);
			g_cancellable_cancel (cancellable);
			g_object_unref (cancellable);
		}
		g_list_free (key);

		g_hash_table_unref (user->priv->file_to_cancellable_map);
		user->priv->file_to_cancellable_map = NULL;
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

static gchar *
mkmd5 (char *string)
{
	GChecksum *checksum;
	gchar *md5_result;

	checksum = g_checksum_new (G_CHECKSUM_MD5);
	g_checksum_update (checksum, (guchar *)string, -1);

	md5_result = g_strdup (g_checksum_get_string (checksum));
	g_checksum_free (checksum);

	return md5_result;
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
	rb_audioscrobbler_user_load_from_cache (user);
}

void
rb_audioscrobbler_user_update (RBAudioscrobblerUser *user)
{
	if (user->priv->username != NULL) {
		rb_audioscrobbler_user_request_user_info (user);
		rb_audioscrobbler_user_request_recent_tracks (user, 15);
		rb_audioscrobbler_user_request_top_tracks (user, 15);
		rb_audioscrobbler_user_request_loved_tracks (user, 15);
		rb_audioscrobbler_user_request_top_artists (user, 15);
		rb_audioscrobbler_user_request_recommended_artists (user, 15);
	}
}

static void
rb_audioscrobbler_user_load_from_cache (RBAudioscrobblerUser *user)
{
	/* delete old data */
	if (user->priv->user_info != NULL) {
		g_boxed_free (RB_TYPE_AUDIOSCROBBLER_USER_DATA, user->priv->user_info);
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

	if (user->priv->recommended_artists != NULL) {
		g_ptr_array_unref (user->priv->recommended_artists);
		user->priv->recommended_artists = NULL;
	}

	/* if a username is set then attempt to load cached data */
	if (user->priv->username != NULL) {
		rb_audioscrobbler_user_load_cached_user_info (user);
		rb_audioscrobbler_user_load_cached_recent_tracks (user);
		rb_audioscrobbler_user_load_cached_top_tracks (user);
		rb_audioscrobbler_user_load_cached_loved_tracks (user);
		rb_audioscrobbler_user_load_cached_top_artists (user);
		rb_audioscrobbler_user_load_cached_recommended_artists (user);
	}
}

static char *
rb_audioscrobbler_user_calculate_cached_response_path (RBAudioscrobblerUser *user, const char *request_name)
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

static void
rb_audioscrobbler_user_save_response_to_cache (RBAudioscrobblerUser *user, const char *request_name, const char *data)
{
	char *filename;
	char *file_dir;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, request_name);
	file_dir = g_path_get_dirname (filename);

	g_mkdir_with_parents (file_dir, 0700);
	if (g_file_set_contents (filename, data, -1, NULL)) {
		rb_debug ("saved %s to cache", request_name);
	} else {
		rb_debug ("error saving %s to cache", request_name);
	}

	g_free (filename);
	g_free (file_dir);
}

/* user info */
static void
rb_audioscrobbler_user_load_cached_user_info (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "user_info");

	/* delete old data */
	if (user->priv->user_info != NULL) {
		g_boxed_free (RB_TYPE_AUDIOSCROBBLER_USER_DATA, user->priv->user_info);
		user->priv->user_info = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached user_info");
		user->priv->user_info = rb_audioscrobbler_user_parse_user_info (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[USER_INFO_UPDATED],
	               0, user->priv->user_info);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_user_info (RBAudioscrobblerUser *user)
{
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting user info");

	msg_url = g_strdup_printf ("%s?method=user.getInfo&user=%s&api_key=%s&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           user->priv->username,
	                           rb_audioscrobbler_service_get_api_key (user->priv->service));

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_user_info_response_cb,
	                            user);

	g_free (msg_url);
}

static void
rb_audioscrobbler_user_user_info_response_cb (SoupSession *session,
                                              SoupMessage *msg,
                                              gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		RBAudioscrobblerUserData *user_info;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		user_info = rb_audioscrobbler_user_parse_user_info (user, msg->response_body->data);

		if (user_info != NULL) {
			rb_debug ("user info request was successful");

			if (user->priv->user_info != NULL) {
				g_boxed_free (RB_TYPE_AUDIOSCROBBLER_USER_DATA, user->priv->user_info);
			}
			user->priv->user_info = user_info;

			rb_audioscrobbler_user_save_response_to_cache (user, "user_info", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[USER_INFO_UPDATED],
			               0, user->priv->user_info);
		} else {
			rb_debug ("invalid response from user info request");
		}
	} else {
		rb_debug ("user info request responded with error");
	}
}

static RBAudioscrobblerUserData *
rb_audioscrobbler_user_parse_user_info (RBAudioscrobblerUser *user, const char *data)
{
	RBAudioscrobblerUserData *user_info;
	JsonParser *parser;
	JsonObject *root_object;

	user_info = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));

	if (json_object_has_member (root_object, "user")) {
		JsonObject *user_object;

		user_object = json_object_get_object_member (root_object, "user");

		user_info = g_slice_new0 (RBAudioscrobblerUserData);
		user_info->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO;
		user_info->user_info.username = g_strdup (json_object_get_string_member (user_object, "name"));
		user_info->user_info.url = g_strdup (json_object_get_string_member (user_object, "url"));
		user_info->user_info.playcount = g_strdup (json_object_get_string_member (user_object, "playcount"));

		user_info->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, user_info),
		                                                     USER_PROFILE_IMAGE_SIZE, -1, NULL);
		if (user_info->image == NULL && json_object_has_member (user_object, "image") == TRUE) {
			JsonArray *image_array;
			JsonObject *image_object;

			image_array = json_object_get_array_member (user_object, "image");
			image_object = json_array_get_object_element (image_array, 2);
			rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), user_info);
		}
	}

	g_object_unref (parser);

	return user_info;
}

/* recent tracks */
static void
rb_audioscrobbler_user_load_cached_recent_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "recent_tracks");

	/* delete old data */
	if (user->priv->recent_tracks != NULL) {
		g_ptr_array_unref (user->priv->recent_tracks);
		user->priv->recent_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached recent tracks");
		user->priv->recent_tracks = rb_audioscrobbler_user_parse_recent_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED],
	               0, user->priv->recent_tracks);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_recent_tracks (RBAudioscrobblerUser *user, int limit)
{
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting recent tracks");

	msg_url = g_strdup_printf ("%s?method=user.getRecentTracks&user=%s&api_key=%s&limit=%i&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           user->priv->username,
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           limit);

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_recent_tracks_response_cb,
	                            user);

	g_free (msg_url);
}

static void
rb_audioscrobbler_user_recent_tracks_response_cb (SoupSession *session,
                                              SoupMessage *msg,
                                              gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		GPtrArray *recent_tracks;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		recent_tracks = rb_audioscrobbler_user_parse_recent_tracks (user, msg->response_body->data);

		if (recent_tracks != NULL) {
			rb_debug ("recent tracks request was successful");

			if (user->priv->recent_tracks != NULL) {
				g_ptr_array_unref (user->priv->recent_tracks);
			}
			user->priv->recent_tracks = recent_tracks;

			rb_audioscrobbler_user_save_response_to_cache (user, "recent_tracks", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[RECENT_TRACKS_UPDATED],
			               0, user->priv->recent_tracks);
		} else {
			rb_debug ("invalid response from recent tracks request");
		}
	} else {
		rb_debug ("recent tracks request responded with error");
	}
}

static GPtrArray *
rb_audioscrobbler_user_parse_recent_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *recent_tracks;
	JsonParser *parser;
	JsonObject *root_object;
	JsonObject *recent_tracks_object;

	recent_tracks = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));
	recent_tracks_object = json_object_get_object_member (root_object, "recenttracks");

	if (json_object_has_member (recent_tracks_object, "track") == TRUE) {
		JsonArray *track_array;
		int i;

		recent_tracks = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_free);

		track_array = json_object_get_array_member (recent_tracks_object, "track");
		for (i = 0; i < json_array_get_length (track_array); i++) {
			JsonObject *track_object;
			JsonObject *artist_object;
			RBAudioscrobblerUserData *track;

			track_object = json_array_get_object_element (track_array, i);

			track = g_slice_new0 (RBAudioscrobblerUserData);
			track->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK;
			track->track.title = g_strdup (json_object_get_string_member (track_object, "name"));
			artist_object = json_object_get_object_member (track_object, "artist");
			track->track.artist = g_strdup (json_object_get_string_member (artist_object, "#text"));
			track->track.url = g_strdup (json_object_get_string_member (track_object, "url"));

			g_ptr_array_add (recent_tracks, track);

			track->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, track),
		                                                         LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
			if (track->image == NULL && json_object_has_member (track_object, "image") == TRUE) {
				JsonArray *image_array;
				JsonObject *image_object;

				image_array = json_object_get_array_member (track_object, "image");
				image_object = json_array_get_object_element (image_array, 0);
				rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), track);
			}
		}
	}

	g_object_unref (parser);

	return recent_tracks;
}

/* top tracks */
static void
rb_audioscrobbler_user_load_cached_top_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "top_tracks");

	/* delete old data */
	if (user->priv->top_tracks != NULL) {
		g_ptr_array_unref (user->priv->top_tracks);
		user->priv->top_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached top tracks");
		user->priv->top_tracks = rb_audioscrobbler_user_parse_top_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED],
	               0, user->priv->top_tracks);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_top_tracks (RBAudioscrobblerUser *user, int limit)
{
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting top tracks");

	msg_url = g_strdup_printf ("%s?method=library.getTracks&user=%s&api_key=%s&limit=%i&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           user->priv->username,
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           limit);

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_top_tracks_response_cb,
	                            user);

	g_free (msg_url);
}

static void
rb_audioscrobbler_user_top_tracks_response_cb (SoupSession *session,
                                               SoupMessage *msg,
                                               gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		GPtrArray *top_tracks;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		top_tracks = rb_audioscrobbler_user_parse_top_tracks (user, msg->response_body->data);

		if (top_tracks != NULL) {
			rb_debug ("top tracks request was successful");

			if (user->priv->top_tracks != NULL) {
				g_ptr_array_unref (user->priv->top_tracks);
			}
			user->priv->top_tracks = top_tracks;

			rb_audioscrobbler_user_save_response_to_cache (user, "top_tracks", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_TRACKS_UPDATED],
			               0, user->priv->top_tracks);
		} else {
			rb_debug ("invalid response from top tracks request");
		}
	} else {
		rb_debug ("top tracks request responded with error");
	}
}

static GPtrArray *
rb_audioscrobbler_user_parse_top_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *top_tracks;
	JsonParser *parser;
	JsonObject *root_object;
	JsonObject *top_tracks_object;

	top_tracks = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));
	top_tracks_object = json_object_get_object_member (root_object, "tracks");

	if (json_object_has_member (top_tracks_object, "track") == TRUE) {
		JsonArray *track_array;
		int i;

		top_tracks = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_free);

		track_array = json_object_get_array_member (top_tracks_object, "track");
		for (i = 0; i < json_array_get_length (track_array); i++) {
			JsonObject *track_object;
			JsonObject *artist_object;
			RBAudioscrobblerUserData *track;

			track_object = json_array_get_object_element (track_array, i);

			track = g_slice_new0 (RBAudioscrobblerUserData);
			track->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK;
			track->track.title = g_strdup (json_object_get_string_member (track_object, "name"));
			artist_object = json_object_get_object_member (track_object, "artist");
			track->track.artist = g_strdup (json_object_get_string_member (artist_object, "name"));
			track->track.url = g_strdup (json_object_get_string_member (track_object, "url"));

			g_ptr_array_add (top_tracks, track);

			track->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, track),
		                                                         LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
			if (track->image == NULL && json_object_has_member (track_object, "image") == TRUE) {
				JsonArray *image_array;
				JsonObject *image_object;

				image_array = json_object_get_array_member (track_object, "image");
				image_object = json_array_get_object_element (image_array, 0);
				rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), track);
			}
		}
	}

	g_object_unref (parser);

	return top_tracks;
}

/* loved tracks */
static void
rb_audioscrobbler_user_load_cached_loved_tracks (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "loved_tracks");

	/* delete old data */
	if (user->priv->loved_tracks != NULL) {
		g_ptr_array_unref (user->priv->loved_tracks);
		user->priv->loved_tracks = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached loved tracks");
		user->priv->loved_tracks = rb_audioscrobbler_user_parse_loved_tracks (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED],
	               0, user->priv->loved_tracks);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_loved_tracks (RBAudioscrobblerUser *user, int limit)
{
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting loved tracks");

	msg_url = g_strdup_printf ("%s?method=user.getLovedTracks&user=%s&api_key=%s&limit=%i&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           user->priv->username,
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           limit);

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_loved_tracks_response_cb,
	                            user);

	g_free (msg_url);
}

static void
rb_audioscrobbler_user_loved_tracks_response_cb (SoupSession *session,
                                                 SoupMessage *msg,
                                                 gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		GPtrArray *loved_tracks;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		loved_tracks = rb_audioscrobbler_user_parse_loved_tracks (user, msg->response_body->data);

		if (loved_tracks != NULL) {
			rb_debug ("loved tracks request was successful");

			if (user->priv->loved_tracks != NULL) {
				g_ptr_array_unref (user->priv->loved_tracks);
			}
			user->priv->loved_tracks = loved_tracks;

			rb_audioscrobbler_user_save_response_to_cache (user, "loved_tracks", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[LOVED_TRACKS_UPDATED],
			               0, user->priv->loved_tracks);
		} else {
			rb_debug ("invalid response from loved tracks request");
		}
	} else {
		rb_debug ("loved tracks request responded with error");
	}
}

static GPtrArray *
rb_audioscrobbler_user_parse_loved_tracks (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *loved_tracks;
	JsonParser *parser;
	JsonObject *root_object;
	JsonObject *loved_tracks_object;

	loved_tracks = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));
	loved_tracks_object = json_object_get_object_member (root_object, "lovedtracks");

	if (json_object_has_member (loved_tracks_object, "track") == TRUE) {
		JsonArray *track_array;
		int i;

		loved_tracks = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_free);

		track_array = json_object_get_array_member (loved_tracks_object, "track");
		for (i = 0; i < json_array_get_length (track_array); i++) {
			JsonObject *track_object;
			JsonObject *artist_object;
			RBAudioscrobblerUserData *track;

			track_object = json_array_get_object_element (track_array, i);

			track = g_slice_new0 (RBAudioscrobblerUserData);
			track->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_TRACK;
			track->track.title = g_strdup (json_object_get_string_member (track_object, "name"));
			artist_object = json_object_get_object_member (track_object, "artist");
			track->track.artist = g_strdup (json_object_get_string_member (artist_object, "name"));
			track->track.url = g_strdup (json_object_get_string_member (track_object, "url"));

			g_ptr_array_add (loved_tracks, track);

			track->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, track),
		                                                         LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
			if (track->image == NULL && json_object_has_member (track_object, "image") == TRUE) {
				JsonArray *image_array;
				JsonObject *image_object;

				image_array = json_object_get_array_member (track_object, "image");
				image_object = json_array_get_object_element (image_array, 0);
				rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), track);
			}
		}
	}

	g_object_unref (parser);

	return loved_tracks;
}

/* top artists */
static void
rb_audioscrobbler_user_load_cached_top_artists (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "top_artists");

	/* delete old data */
	if (user->priv->top_artists != NULL) {
		g_ptr_array_unref (user->priv->top_artists);
		user->priv->top_artists = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached top artists");
		user->priv->top_artists = rb_audioscrobbler_user_parse_top_artists (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED],
	               0, user->priv->top_artists);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_top_artists (RBAudioscrobblerUser *user, int limit)
{
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting top artists");

	msg_url = g_strdup_printf ("%s?method=library.getArtists&user=%s&api_key=%s&limit=%i&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           user->priv->username,
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           limit);

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_top_artists_response_cb,
	                            user);

	g_free (msg_url);
}

static void
rb_audioscrobbler_user_top_artists_response_cb (SoupSession *session,
                                                SoupMessage *msg,
                                                gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		GPtrArray *top_artists;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		top_artists = rb_audioscrobbler_user_parse_top_artists (user, msg->response_body->data);

		if (top_artists != NULL) {
			rb_debug ("top artists request was successful");

			if (user->priv->top_artists != NULL) {
				g_ptr_array_unref (user->priv->top_artists);
			}
			user->priv->top_artists = top_artists;

			rb_audioscrobbler_user_save_response_to_cache (user, "top_artists", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[TOP_ARTISTS_UPDATED],
			               0, user->priv->top_artists);
		} else {
			rb_debug ("invalid response from top artists request");
		}
	} else {
		rb_debug ("top artists request responded with error");
	}
}

static GPtrArray *
rb_audioscrobbler_user_parse_top_artists (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *top_artists;
	JsonParser *parser;
	JsonObject *root_object;
	JsonObject *top_artists_object;

	top_artists = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));
	top_artists_object = json_object_get_object_member (root_object, "artists");

	if (json_object_has_member (top_artists_object, "artist") == TRUE) {
		JsonArray *artist_array;
		int i;

		top_artists = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_free);

		artist_array = json_object_get_array_member (top_artists_object, "artist");
		for (i = 0; i < json_array_get_length (artist_array); i++) {
			JsonObject *artist_object;
			RBAudioscrobblerUserData *artist;

			artist_object = json_array_get_object_element (artist_array, i);

			artist = g_slice_new0 (RBAudioscrobblerUserData);
			artist->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST;
			artist->artist.name = g_strdup (json_object_get_string_member (artist_object, "name"));
			artist->artist.url = g_strdup (json_object_get_string_member (artist_object, "url"));

			g_ptr_array_add (top_artists, artist);

			artist->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, artist),
		                                                          LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
			if (artist->image == NULL && json_object_has_member (artist_object, "image") == TRUE) {
				JsonArray *image_array;
				JsonObject *image_object;

				image_array = json_object_get_array_member (artist_object, "image");
				image_object = json_array_get_object_element (image_array, 0);
				rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), artist);
			}
		}
	}

	g_object_unref (parser);

	return top_artists;
}

/* recommended artists */
static void
rb_audioscrobbler_user_load_cached_recommended_artists (RBAudioscrobblerUser *user)
{
	char *filename;
	char *data;

	filename = rb_audioscrobbler_user_calculate_cached_response_path (user, "recommended_artists");

	/* delete old data */
	if (user->priv->recommended_artists != NULL) {
		g_ptr_array_unref (user->priv->recommended_artists);
		user->priv->recommended_artists = NULL;
	}

	/* load cached data if it exists */
	if (g_file_get_contents (filename, &data, NULL, NULL) == TRUE) {
		rb_debug ("loading cached recommended artists");
		user->priv->recommended_artists = rb_audioscrobbler_user_parse_recommended_artists (user, data);
	}

	/* emit updated signal */
	g_signal_emit (user, rb_audioscrobbler_user_signals[RECOMMENDED_ARTISTS_UPDATED],
	               0, user->priv->recommended_artists);

	g_free (filename);
	g_free (data);
}

static void
rb_audioscrobbler_user_request_recommended_artists (RBAudioscrobblerUser *user, int limit)
{
	char *sig_arg;
	char *sig;
	char *msg_url;
	SoupMessage *msg;

	rb_debug ("requesting recommended artists");

	sig_arg = g_strdup_printf ("api_key%slimit%imethoduser.getRecommendedArtistssk%s%s",
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           limit,
	                           user->priv->session_key,
	                           rb_audioscrobbler_service_get_api_secret (user->priv->service));
	sig = mkmd5 (sig_arg);

	msg_url = g_strdup_printf ("%s?method=user.getRecommendedArtists&api_key=%s&api_sig=%s&sk=%s&limit=%i&format=json",
	                           rb_audioscrobbler_service_get_api_url (user->priv->service),
	                           rb_audioscrobbler_service_get_api_key (user->priv->service),
	                           sig,
	                           user->priv->session_key,
	                           limit);

	msg = soup_message_new ("GET", msg_url);
	soup_session_queue_message (user->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_user_recommended_artists_response_cb,
	                            user);

	g_free (sig_arg);
	g_free (sig);
	g_free (msg_url);
}

static void
rb_audioscrobbler_user_recommended_artists_response_cb (SoupSession *session,
                                                        SoupMessage *msg,
                                                        gpointer user_data)
{
	if (SOUP_STATUS_IS_SUCCESSFUL (msg->status_code)) {
		RBAudioscrobblerUser *user;
		GPtrArray *recommended_artists;

		user = RB_AUDIOSCROBBLER_USER (user_data);
		recommended_artists = rb_audioscrobbler_user_parse_recommended_artists (user, msg->response_body->data);

		if (recommended_artists != NULL) {
			rb_debug ("recommended artists request was successful");

			if (user->priv->recommended_artists != NULL) {
				g_ptr_array_unref (user->priv->recommended_artists);
			}
			user->priv->recommended_artists = recommended_artists;

			rb_audioscrobbler_user_save_response_to_cache (user, "recommended_artists", msg->response_body->data);

			g_signal_emit (user, rb_audioscrobbler_user_signals[RECOMMENDED_ARTISTS_UPDATED],
			               0, user->priv->recommended_artists);
		} else {
			rb_debug ("invalid response from recommended artists request");
		}
	} else {
		rb_debug ("recommended artists request responded with error");
	}
}

static GPtrArray *
rb_audioscrobbler_user_parse_recommended_artists (RBAudioscrobblerUser *user, const char *data)
{
	GPtrArray *recommended_artists;
	JsonParser *parser;
	JsonObject *root_object;

	recommended_artists = NULL;

	parser = json_parser_new ();
	json_parser_load_from_data (parser, data, -1, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));

	if (json_object_has_member (root_object, "error")) {
		/* probably bad authentication. Unlike with scrobbling or radio playback,
		 * this is not a problem: we'll just live with no recommendations */
		rb_debug ("user.getRecommendedArtists failed due to bad authentication");
	} else {
		JsonObject *recommended_artists_object;
		recommended_artists_object = json_object_get_object_member (root_object, "recommendations");

		if (json_object_has_member (recommended_artists_object, "artist") == TRUE) {
			JsonArray *artist_array;
			int i;

			recommended_artists = g_ptr_array_new_with_free_func ((GDestroyNotify)rb_audioscrobbler_user_data_free);

			artist_array = json_object_get_array_member (recommended_artists_object, "artist");
			for (i = 0; i < json_array_get_length (artist_array); i++) {
				JsonObject *artist_object;
				RBAudioscrobblerUserData *artist;

				artist_object = json_array_get_object_element (artist_array, i);

				artist = g_slice_new0 (RBAudioscrobblerUserData);
				artist->type = RB_AUDIOSCROBBLER_USER_DATA_TYPE_ARTIST;
				artist->artist.name = g_strdup (json_object_get_string_member (artist_object, "name"));
				artist->artist.url = g_strdup (json_object_get_string_member (artist_object, "url"));

				g_ptr_array_add (recommended_artists, artist);

				artist->image = gdk_pixbuf_new_from_file_at_size (rb_audioscrobbler_user_calculate_cached_image_path (user, artist),
				                                                  LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
				if (artist->image == NULL && json_object_has_member (artist_object, "image") == TRUE) {
					JsonArray *image_array;
					JsonObject *image_object;

					image_array = json_object_get_array_member (artist_object, "image");
					image_object = json_array_get_object_element (image_array, 0);
					rb_audioscrobbler_user_download_image (user, json_object_get_string_member (image_object, "#text"), artist);
				}
			}
		}
	}

	g_object_unref (parser);

	return recommended_artists;
}

static char *
rb_audioscrobbler_user_calculate_cached_image_path (RBAudioscrobblerUser *user, RBAudioscrobblerUserData *data)
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
rb_audioscrobbler_user_download_image (RBAudioscrobblerUser *user, const char *image_url, RBAudioscrobblerUserData *data)
{
	GFile *src_file;

	/* check image_url is not null or empty */
	if (image_url == NULL || image_url[0] == '\0') {
		return;
	}

	src_file = g_file_new_for_uri (image_url);

	/* only start a download if the file is not already being downloaded */
	if (g_hash_table_lookup (user->priv->file_to_data_map, src_file) == NULL) {
		GCancellable *cancellable;
		char *dest_filename;
		char *dest_file_dir;
		GFile *dest_file;

		/* add data to map */
		g_hash_table_insert (user->priv->file_to_data_map, src_file, data);

		/* create a cancellable for this download */
		cancellable = g_cancellable_new ();
		g_hash_table_insert (user->priv->file_to_cancellable_map, src_file, cancellable);

		/* ensure the dest dir exists */
		dest_filename = rb_audioscrobbler_user_calculate_cached_image_path (user, data);
		dest_file_dir = g_path_get_dirname (dest_filename);
		g_mkdir_with_parents (dest_file_dir, 0700);

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
		                   rb_audioscrobbler_user_image_download_cb,
		                   user);

		g_free (dest_filename);
		g_free (dest_file_dir);
		g_object_unref (dest_file);
	}
}

static void
rb_audioscrobbler_user_image_download_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	GFile *src_file = G_FILE (source_object);

	if (g_file_copy_finish (src_file, res, NULL)) {
		RBAudioscrobblerUser *user;
		GCancellable *cancellable;
		RBAudioscrobblerUserData *data;
		char *dest_file_path;

		user = RB_AUDIOSCROBBLER_USER (user_data);

		/* free the cancellable */
		cancellable = g_hash_table_lookup (user->priv->file_to_cancellable_map, src_file);
		g_hash_table_remove (user->priv->file_to_cancellable_map, src_file);
		g_object_unref (cancellable);

		/* update the data */
		data = g_hash_table_lookup (user->priv->file_to_data_map, src_file);
		g_hash_table_remove (user->priv->file_to_data_map, src_file);

		dest_file_path = rb_audioscrobbler_user_calculate_cached_image_path (user, data);
		if (data->image != NULL) {
			g_object_unref (data->image);
		}

		/* load image at correct size for the data type */
		if (data->type == RB_AUDIOSCROBBLER_USER_DATA_TYPE_USER_INFO) {
			data->image = gdk_pixbuf_new_from_file_at_size (dest_file_path, USER_PROFILE_IMAGE_SIZE, -1, NULL);
		} else {
			data->image = gdk_pixbuf_new_from_file_at_size (dest_file_path, LIST_ITEM_IMAGE_SIZE, LIST_ITEM_IMAGE_SIZE, NULL);
		}

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
			if (user->priv->recommended_artists != NULL) {
				for (i = 0; i < user->priv->recommended_artists->len; i++) {
					if (g_ptr_array_index (user->priv->recommended_artists, i) == data) {
						g_signal_emit (user, rb_audioscrobbler_user_signals[RECOMMENDED_ARTISTS_UPDATED],
							       0, user->priv->recommended_artists);
					}
				}
			}
		}
	} else {
		rb_debug ("error downloading image. possibly due to cancellation");
	}
}
