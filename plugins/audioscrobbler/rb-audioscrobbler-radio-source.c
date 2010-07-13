/*
 * rb-audioscrobbler-radio-source.c
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

#include <string.h>
#include <unistd.h>
#include <libsoup/soup.h>
#include <libsoup/soup-gnome.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <totem-pl-parser.h>

#include "rb-audioscrobbler-radio-source.h"
#include "rb-lastfm-play-order.h"
#include "rb-debug.h"
#include "rb-util.h"


/* radio type stuff */
static const char* radio_types[] = {
	N_("Similar Artists"),
	N_("Top Fans"),
	N_("Library"),
	N_("Neighbourhood"),
	N_("Loved Tracks"),
	N_("Recommendations"),
	N_("Tag Radio"),
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_text (RBAudioscrobblerRadioType type)
{
	return radio_types[type];
}

static const char* radio_urls[] = {
	"lastfm://artist/%s/similarartists",
	"lastfm://artist/%s/fans",
	"lastfm://user/%s/library",
	"lastfm://user/%s/neighbours",
	"lastfm://user/%s/loved",
	"lastfm://user/%s/recommended",
	"lastfm://globaltags/%s",
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_url (RBAudioscrobblerRadioType type)
{
	return radio_urls[type];
}

static const char* radio_names[] = {
	N_("%s Radio"),
	N_("%s Fan Radio"),
	N_("%s's Library"),
	N_("%s's Neighbourhood"),
	N_("%s's Loved Tracks"),
	N_("%s's Recommended Radio"),
	N_("%s Tag Radio"),
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_default_name (RBAudioscrobblerRadioType type)
{
	return radio_names[type];
}

/* entry data stuff */

typedef struct
{
	char *image_url;
	char *track_auth;
	char *download_url;
} RBAudioscrobblerRadioTrackEntryData;

static void
destroy_track_data (RhythmDBEntry *entry, gpointer meh)
{
	RBAudioscrobblerRadioTrackEntryData *data;

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA(entry, RBAudioscrobblerRadioTrackEntryData);
	g_free (data->image_url);
	g_free (data->track_auth);
	g_free (data->download_url);
}

/* source declerations */

struct _RBAudioscrobblerRadioSourcePrivate
{
	RBAudioscrobblerProfileSource *parent;

	RBAudioscrobblerService *service;
	char *username;
	char *session_key;
	char *station_url;

	SoupSession *soup_session;

	GtkWidget *info_bar;
	GtkWidget *info_bar_label;

	RBEntryView *track_view;
	RhythmDBQueryModel *track_model;

	gboolean is_fetching_playlist;

	RBPlayOrder *play_order;

	/* the currently playing entry of this source, if there is one */
	RhythmDBEntry *playing_entry;

	guint emit_coverart_id;

	GtkActionGroup *action_group;
};

#define RB_AUDIOSCROBBLER_RADIO_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE, RBAudioscrobblerRadioSourcePrivate))

static void rb_audioscrobbler_radio_source_class_init (RBAudioscrobblerRadioSourceClass *klass);
static void rb_audioscrobbler_radio_source_init (RBAudioscrobblerRadioSource *source);
static void rb_audioscrobbler_radio_source_constructed (GObject *object);
static void rb_audioscrobbler_radio_source_dispose (GObject *object);
static void rb_audioscrobbler_radio_source_finalize (GObject *object);
static void rb_audioscrobbler_radio_source_get_property (GObject *object,
                                                         guint prop_id,
                                                         GValue *value,
                                                         GParamSpec *pspec);
static void rb_audioscrobbler_radio_source_set_property (GObject *object,
                                                         guint prop_id,
                                                         const GValue *value,
                                                         GParamSpec *pspec);

static void rb_audioscrobbler_radio_source_tune (RBAudioscrobblerRadioSource *source);
static void rb_audioscrobbler_radio_source_tune_response_cb (SoupSession *session,
                                                             SoupMessage *msg,
                                                             gpointer user_data);

static void rb_audioscrobbler_radio_source_fetch_playlist (RBAudioscrobblerRadioSource *source);
static void rb_audioscrobbler_radio_source_fetch_playlist_response_cb (SoupSession *session,
                                                                       SoupMessage *msg,
                                                                       gpointer user_data);

static void rb_audioscrobbler_radio_source_playing_song_changed_cb (RBShellPlayer *player,
                                                                    RhythmDBEntry *entry,
                                                                    RBAudioscrobblerRadioSource *source);

static void rb_audioscrobbler_radio_source_delete_station_action_cb (GtkAction *action,
                                                                     RBAudioscrobblerRadioSource *source);

/* cover art */
static GValue *coverart_uri_request (RhythmDB *db,
                                     RhythmDBEntry *entry,
                                     RBAudioscrobblerRadioSource *source);
static void extra_metadata_gather_cb (RhythmDB *db,
                                      RhythmDBEntry *entry,
                                      RBStringValueMap *map,
                                      RBAudioscrobblerRadioSource *source);
static gboolean emit_coverart_uri_cb (RBAudioscrobblerRadioSource *source);

/* RBSource implementations */
static void impl_activate (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *asource);
static void impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress);
static RBSourceEOFType impl_handle_eos (RBSource *asource);
static GList *impl_get_ui_actions (RBSource *asource);
static gboolean impl_show_popup (RBSource *asource);
static void impl_delete_thyself (RBSource *asource);

enum {
	PROP_0,
	PROP_PARENT,
	PROP_SERVICE,
	PROP_USERNAME,
	PROP_SESSION_KEY,
	PROP_STATION_URL,
	PROP_PLAY_ORDER
};

#define AUDIOSCROBBLER_RADIO_SOURCE_POPUP_PATH "/AudioscrobblerRadioSourcePopup"

static GtkActionEntry rb_audioscrobbler_radio_source_actions [] =
{
	{ "AudioscrobblerRadioDeleteStation", GTK_STOCK_DELETE, N_("Delete Station"), NULL,
	  N_("Delete the selected station"),
	  G_CALLBACK (rb_audioscrobbler_radio_source_delete_station_action_cb) }
};

G_DEFINE_TYPE (RBAudioscrobblerRadioSource, rb_audioscrobbler_radio_source, RB_TYPE_STREAMING_SOURCE)

RBSource *
rb_audioscrobbler_radio_source_new (RBAudioscrobblerProfileSource *parent,
                                    RBAudioscrobblerService *service,
                                    const char *username,
                                    const char *session_key,
                                    const char *station_name,
                                    const char *station_url)
{
	RBSource *source;
	RBShell *shell;
	RBPlugin *plugin;
	RhythmDB *db;
	RhythmDBEntryType track_entry_type;

	g_object_get (parent, "shell", &shell, "plugin", &plugin, NULL);
	g_object_get (shell, "db", &db, NULL);

	track_entry_type = rhythmdb_entry_type_get_by_name (db, "audioscrobbler-radio-track");
	if (track_entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		track_entry_type = rhythmdb_entry_register_type (db, "audioscrobbler-radio-track");
		track_entry_type->save_to_disk = FALSE;
		track_entry_type->category = RHYTHMDB_ENTRY_NORMAL;

		track_entry_type->entry_type_data_size = sizeof (RBAudioscrobblerRadioTrackEntryData);
		track_entry_type->pre_entry_destroy = destroy_track_data;
	}

	source = g_object_new (RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE,
	                       "shell", shell,
	                       "plugin", plugin,
	                       "name", station_name,
	                       "entry-type", track_entry_type,
	                       "parent", parent,
	                       "service", service,
                               "username", username,
	                       "session-key", session_key,
	                       "station-url", station_url,
	                       NULL);

	g_object_unref (shell);
	g_object_unref (plugin);
	g_object_unref (db);

	return source;
}

static void
rb_audioscrobbler_radio_source_class_init (RBAudioscrobblerRadioSourceClass *klass)
{
	GObjectClass *object_class;
	RBSourceClass *source_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = rb_audioscrobbler_radio_source_constructed;
	object_class->dispose = rb_audioscrobbler_radio_source_dispose;
	object_class->finalize = rb_audioscrobbler_radio_source_finalize;
	object_class->get_property = rb_audioscrobbler_radio_source_get_property;
	object_class->set_property = rb_audioscrobbler_radio_source_set_property;

	source_class = RB_SOURCE_CLASS (klass);
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_activate = impl_activate;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_status = impl_get_status;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;

	g_object_class_install_property (object_class,
	                                 PROP_PARENT,
	                                 g_param_spec_object ("parent",
	                                                      "Parent",
	                                                      "Profile source which created this radio source",
	                                                      RB_TYPE_AUDIOSCROBBLER_PROFILE_SOURCE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_SERVICE,
	                                 g_param_spec_object ("service",
	                                                      "Service",
	                                                      "Service to stream radio from",
	                                                      RB_TYPE_AUDIOSCROBBLER_SERVICE,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_USERNAME,
	                                 g_param_spec_string ("username",
	                                                      "Username",
	                                                      "Username of the user who is streaming radio",
	                                                      NULL,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_SESSION_KEY,
	                                 g_param_spec_string ("session-key",
	                                                      "Session Key",
	                                                      "Session key used to authenticate the user",
	                                                      NULL,
                                                              G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
	                                 PROP_STATION_URL,
	                                 g_param_spec_string ("station-url",
	                                                      "Station URL",
	                                                      "Last.fm radio URL of the station this source will stream",
	                                                      NULL,
                                                              G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_override_property (object_class,
					  PROP_PLAY_ORDER,
					  "play-order");

	g_type_class_add_private (klass, sizeof (RBAudioscrobblerRadioSourcePrivate));
}

static void
rb_audioscrobbler_radio_source_init (RBAudioscrobblerRadioSource *source)
{
	source->priv = RB_AUDIOSCROBBLER_RADIO_SOURCE_GET_PRIVATE (source);

	source->priv->soup_session =
		soup_session_async_new_with_options (SOUP_SESSION_ADD_FEATURE_BY_TYPE,
		                                     SOUP_TYPE_GNOME_FEATURES_2_26,
		                                     NULL);

	/* one connection at a time means getPlaylist will only be sent after tune has returned */
	g_object_set (source->priv->soup_session, "max-conns", 1, NULL);
}

static void
rb_audioscrobbler_radio_source_constructed (GObject *object)
{
	RBAudioscrobblerRadioSource *source;
	RBShell *shell;
	RhythmDB *db;
	GtkWidget *main_vbox;
	GtkWidget *info_bar_content_area;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_radio_source_parent_class, constructed, object);

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	main_vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (main_vbox);
	gtk_container_add (GTK_CONTAINER (source), main_vbox);

	source->priv->info_bar = gtk_info_bar_new ();
	source->priv->info_bar_label = gtk_label_new ("");
	info_bar_content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (source->priv->info_bar));
	gtk_container_add (GTK_CONTAINER (info_bar_content_area), source->priv->info_bar_label);
	gtk_box_pack_start (GTK_BOX (main_vbox), source->priv->info_bar, FALSE, FALSE, 0);

	/* entry view */
	source->priv->track_view = rb_entry_view_new (db, rb_shell_get_player (shell), NULL, FALSE, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_DURATION, FALSE);
	rb_entry_view_set_columns_clickable (source->priv->track_view, FALSE);
	gtk_widget_show_all (GTK_WIDGET (source->priv->track_view));

	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (source->priv->track_view), TRUE, TRUE, 0);

	/* query model */
	source->priv->track_model = rhythmdb_query_model_new_empty (db);
	rb_entry_view_set_model (source->priv->track_view, source->priv->track_model);
	g_object_set (source, "query-model", source->priv->track_model, NULL);

	/* play order */
	source->priv->play_order = rb_lastfm_play_order_new (RB_SHELL_PLAYER (rb_shell_get_player (shell)));

	/* signals */
	g_signal_connect_object (rb_shell_get_player (shell),
				 "playing-song-changed",
				 G_CALLBACK (rb_audioscrobbler_radio_source_playing_song_changed_cb),
				 source, 0);
	g_signal_connect_object (db,
				 "entry-extra-metadata-request::" RHYTHMDB_PROP_COVER_ART_URI,
				 G_CALLBACK (coverart_uri_request),
				 source, 0);
	g_signal_connect_object (db,
				 "entry-extra-metadata-gather",
				 G_CALLBACK (extra_metadata_gather_cb),
				 source, 0);

	/* actions */
	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "AudioscrobblerRadioActions",
								       NULL, 0,
								       shell);
	_rb_action_group_add_source_actions (source->priv->action_group,
					     G_OBJECT (shell),
					     rb_audioscrobbler_radio_source_actions,
					     G_N_ELEMENTS (rb_audioscrobbler_radio_source_actions));

	rb_shell_append_source (shell, RB_SOURCE (source), RB_SOURCE (source->priv->parent));

	g_object_unref (shell);
	g_object_unref (db);
}

static void
rb_audioscrobbler_radio_source_dispose (GObject *object)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);

	if (source->priv->soup_session != NULL) {
		soup_session_abort (source->priv->soup_session);
		g_object_unref (source->priv->soup_session);
		source->priv->soup_session = NULL;
	}

	if (source->priv->service != NULL) {
		g_object_unref (source->priv->service);
		source->priv->service = NULL;
	}

	if (source->priv->track_model != NULL) {
		g_object_unref (source->priv->track_model);
		source->priv->track_model = NULL;
	}

	if (source->priv->play_order != NULL) {
		g_object_unref (source->priv->play_order);
		source->priv->play_order = NULL;
	}

	G_OBJECT_CLASS (rb_audioscrobbler_radio_source_parent_class)->dispose (object);
}

static void
rb_audioscrobbler_radio_source_finalize (GObject *object)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);

	g_free (source->priv->username);
	g_free (source->priv->session_key);
	g_free (source->priv->station_url);

	G_OBJECT_CLASS (rb_audioscrobbler_radio_source_parent_class)->finalize (object);
}

static void
rb_audioscrobbler_radio_source_get_property (GObject *object,
                                             guint prop_id,
                                             GValue *value,
                                             GParamSpec *pspec)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);
	switch (prop_id) {
	case PROP_STATION_URL:
		g_value_set_string (value, source->priv->station_url);
		break;
	case PROP_PLAY_ORDER:
		g_value_set_object (value, source->priv->play_order);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_audioscrobbler_radio_source_set_property (GObject *object,
                                             guint prop_id,
                                             const GValue *value,
                                             GParamSpec *pspec)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);
	switch (prop_id) {
	case PROP_PARENT:
		source->priv->parent = g_value_get_object (value);
		break;
	case PROP_SERVICE:
		source->priv->service = g_value_dup_object (value);
		break;
	case PROP_USERNAME:
		source->priv->username = g_value_dup_string (value);
		break;
	case PROP_SESSION_KEY:
		source->priv->session_key = g_value_dup_string (value);
		break;
	case PROP_STATION_URL:
		source->priv->station_url = g_value_dup_string (value);
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

static void
rb_audioscrobbler_radio_source_tune (RBAudioscrobblerRadioSource *source)
{
	char *sig_arg;
	char *sig;
	char *request;
	char *msg_url;
	SoupMessage *msg;

	sig_arg = g_strdup_printf ("api_key%smethodradio.tunesk%sstation%s%s",
	                           rb_audioscrobbler_service_get_api_key (source->priv->service),
	                           source->priv->session_key,
	                           source->priv->station_url,
	                           rb_audioscrobbler_service_get_api_secret (source->priv->service));

	sig = mkmd5 (sig_arg);

	request = g_strdup_printf ("method=radio.tune&station=%s&api_key=%s&api_sig=%s&sk=%s",
	                           source->priv->station_url,
	                           rb_audioscrobbler_service_get_api_key (source->priv->service),
	                           sig,
	                           source->priv->session_key);

	/* The format parameter needs to go here instead of in the request body */
	msg_url = g_strdup_printf ("%s?format=json",
	                           rb_audioscrobbler_service_get_api_url (source->priv->service));

	msg = soup_message_new ("POST", msg_url);
	soup_message_set_request (msg,
	                          "application/x-www-form-urlencoded",
	                          SOUP_MEMORY_COPY,
	                          request,
	                          strlen (request));
	soup_session_queue_message (source->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_radio_source_tune_response_cb,
	                            source);

	rb_audioscrobbler_radio_source_fetch_playlist (source);

	g_free (sig_arg);
	g_free (sig);
	g_free (request);
	g_free (msg_url);
}

static void
rb_audioscrobbler_radio_source_tune_response_cb (SoupSession *session,
                                                 SoupMessage *msg,
                                                 gpointer user_data)
{
	RBAudioscrobblerRadioSource *source;
	JsonParser *parser;
	JsonObject *root_object;

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (user_data);
	parser = json_parser_new ();
	json_parser_load_from_data (parser, msg->response_body->data, msg->response_body->length, NULL);
	root_object = json_node_get_object (json_parser_get_root (parser));

	if (json_object_has_member (root_object, "station")) {
		JsonObject *station_object;

		station_object = json_object_get_object_member (root_object, "station");
		/* TODO: do something fun with this information */

	} else if (json_object_has_member (root_object, "error")) {
		int code;
		const char *message;

		code = json_object_get_int_member (root_object, "error");
		message = json_object_get_string_member (root_object, "message");

		rb_debug ("radio.tune responded with error: %s", message);

		if (code == 12) {
			/* Subscriber only station */
			char *info_message;

			info_message = g_strdup_printf (_("This station is only available to %s subscribers"),
			                                rb_audioscrobbler_service_get_name (source->priv->service));
			gtk_label_set_label (GTK_LABEL (source->priv->info_bar_label), info_message);
			gtk_widget_show_all (source->priv->info_bar);

			g_free (info_message);
		} else if (code == 4) {
			/* different: The station is not a subscriber-only station, just Last.fm will not stream any stations
			 * to non subscribers. TODO: Fall back to using old API
			 */
			char *info_message;

			info_message = g_strdup_printf (_("This station is only available to %s subscribers"),
			                                rb_audioscrobbler_service_get_name (source->priv->service));
			gtk_label_set_label (GTK_LABEL (source->priv->info_bar_label), info_message);
			gtk_widget_show_all (source->priv->info_bar);

			g_free (info_message);
		} else {
			char *info_message;

			info_message = g_strdup_printf ("Error tuning station: %i - %s", code, message);
			gtk_label_set_label (GTK_LABEL (source->priv->info_bar_label), info_message);
			gtk_widget_show_all (source->priv->info_bar);

			g_free (info_message);
		}
	}
}

static void
rb_audioscrobbler_radio_source_fetch_playlist (RBAudioscrobblerRadioSource *source)
{
	char *sig_arg;
	char *sig;
	char *request;
	SoupMessage *msg;

	if (source->priv->is_fetching_playlist == TRUE) {
		rb_debug ("already fetching playlist");
		return;
	}

	source->priv->is_fetching_playlist = TRUE;

	sig_arg = g_strdup_printf ("api_key%smethodradio.getPlaylistrawtruesk%s%s",
	                           rb_audioscrobbler_service_get_api_key (source->priv->service),
	                           source->priv->session_key,
	                           rb_audioscrobbler_service_get_api_secret (source->priv->service));

	sig = mkmd5 (sig_arg);

	request = g_strdup_printf ("method=radio.getPlaylist&api_key=%s&api_sig=%s&sk=%s&raw=true",
	                           rb_audioscrobbler_service_get_api_key (source->priv->service),
	                           sig,
	                           source->priv->session_key);

	msg = soup_message_new ("POST", rb_audioscrobbler_service_get_api_url (source->priv->service));
	soup_message_set_request (msg,
	                          "application/x-www-form-urlencoded",
	                          SOUP_MEMORY_COPY,
	                          request,
	                          strlen (request));
	soup_session_queue_message (source->priv->soup_session,
	                            msg,
	                            rb_audioscrobbler_radio_source_fetch_playlist_response_cb,
	                            source);

	g_free (sig_arg);
	g_free (sig);
	g_free (request);
}

static void
xspf_entry_parsed (TotemPlParser *parser, const char *uri, GHashTable *metadata, RBAudioscrobblerRadioSource *source)
{
	RBShell *shell;
	RhythmDBEntryType entry_type;
	RhythmDB *db;

	RhythmDBEntry *entry;
	RBAudioscrobblerRadioTrackEntryData *track_data;
	const char *value;
	GValue v = {0,};
	int i;
	struct {
		const char *field;
		RhythmDBPropType prop;
	} field_mapping[] = {
		{ TOTEM_PL_PARSER_FIELD_TITLE, RHYTHMDB_PROP_TITLE },
		{ TOTEM_PL_PARSER_FIELD_AUTHOR, RHYTHMDB_PROP_ARTIST },
		{ TOTEM_PL_PARSER_FIELD_ALBUM, RHYTHMDB_PROP_ALBUM },
	};

	g_object_get (source, "shell", &shell, "entry-type", &entry_type, NULL);
	g_object_get (shell, "db", &db, NULL);

	/* create db entry if it doesn't already exist */
	entry = rhythmdb_entry_lookup_by_location (db, uri);
	if (entry == NULL) {
		rb_debug ("creating new track entry for %s", uri);
		entry = rhythmdb_entry_new (db, entry_type, uri);
	} else {
		rb_debug ("track entry %s already exists", uri);
	}
	track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioscrobblerRadioTrackEntryData);

	/* straightforward string copying */
	for (i = 0; i < G_N_ELEMENTS (field_mapping); i++) {
		value = g_hash_table_lookup (metadata, field_mapping[i].field);
		if (value != NULL) {
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, value);
			rhythmdb_entry_set (db, entry, field_mapping[i].prop, &v);
			g_value_unset (&v);
		}
	}

	/* duration needs some conversion */
	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_DURATION_MS);
	if (value != NULL) {
		gint64 duration;

		duration = totem_pl_parser_parse_duration (value, FALSE);
		if (duration > 0) {
			g_value_init (&v, G_TYPE_ULONG);
			g_value_set_ulong (&v, (gulong) duration / 1000);		/* ms -> s */
			rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_DURATION, &v);
			g_value_unset (&v);
		}
	}

	/* image URL and track auth ID are stored in entry type specific data */
	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_IMAGE_URI);
	if (value != NULL) {
		track_data->image_url = g_strdup (value);
	}

	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_ID);
	if (value != NULL) {
		track_data->track_auth = g_strdup (value);
	}

	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_DOWNLOAD_URI);
	if (value != NULL) {
		track_data->download_url = g_strdup (value);
		rb_debug ("track %s has a download url: %s", uri, track_data->download_url);
	}

	rhythmdb_query_model_add_entry (source->priv->track_model, entry, -1);

	g_object_unref (shell);
	g_object_unref (db);
}

static void
rb_audioscrobbler_radio_source_fetch_playlist_response_cb (SoupSession *session,
                                                           SoupMessage *msg,
                                                           gpointer user_data)
{
	RBAudioscrobblerRadioSource *source;
	int tmp_fd;
	char *tmp_name;
	char *tmp_uri = NULL;
	GIOChannel *channel = NULL;
	TotemPlParser *parser = NULL;
	TotemPlParserResult result;
	GError *error = NULL;

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (user_data);

	source->priv->is_fetching_playlist = FALSE;

	if (msg->response_body->length == 0) {
		rb_debug ("didn't get a response");
		return;
	}

	rb_debug ("%s", msg->response_body->data);

	/* until totem-pl-parser can parse playlists from in-memory data, we save it to a
	 * temporary file.
	 */

	tmp_fd = g_file_open_tmp ("rb-audioscrobbler-playlist-XXXXXX.xspf", &tmp_name, &error);
	if (error != NULL) {
		rb_debug ("unable to save playlist: %s", error->message);
		goto cleanup;
	}

	channel = g_io_channel_unix_new (tmp_fd);
	g_io_channel_write_chars (channel, msg->response_body->data, msg->response_body->length, NULL, &error);
	if (error != NULL) {
		rb_debug ("unable to save playlist: %s", error->message);
		goto cleanup;
	}
	g_io_channel_flush (channel, NULL);		/* ignore errors.. */

	tmp_uri = g_filename_to_uri (tmp_name, NULL, &error);
	if (error != NULL) {
		rb_debug ("unable to parse playlist: %s", error->message);
		goto cleanup;
	}

	rb_debug ("parsing playlist %s", tmp_uri);

	parser = totem_pl_parser_new ();
	g_signal_connect_data (parser, "entry-parsed", G_CALLBACK (xspf_entry_parsed), source, NULL, 0);
	result = totem_pl_parser_parse (parser, tmp_uri, FALSE);

	switch (result) {
	default:
	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
	case TOTEM_PL_PARSER_RESULT_IGNORED:
	case TOTEM_PL_PARSER_RESULT_ERROR:
		rb_debug ("playlist didn't parse");
		break;

	case TOTEM_PL_PARSER_RESULT_SUCCESS:
		rb_debug ("playlist parsed successfully");
		break;
	}

 cleanup:
	if (channel != NULL) {
		g_io_channel_unref (channel);
	}
	if (parser != NULL) {
		g_object_unref (parser);
	}
	if (error != NULL) {
		g_error_free (error);
	}
	close (tmp_fd);
	g_unlink (tmp_name);
	g_free (tmp_name);
	g_free (tmp_uri);
}

static void
rb_audioscrobbler_radio_source_playing_song_changed_cb (RBShellPlayer *player,
                                                        RhythmDBEntry *entry,
                                                        RBAudioscrobblerRadioSource *source)
{
	RhythmDB *db;
	GtkTreeIter playing_iter;

	g_object_get (player, "db", &db, NULL);

	/* delete old entry */
	if (source->priv->playing_entry != NULL) {
		rhythmdb_query_model_remove_entry (source->priv->track_model, source->priv->playing_entry);
		rhythmdb_entry_delete (db, source->priv->playing_entry);
		source->priv->playing_entry = NULL;
	}

	/* stop requesting cover art for old entry */
	if (source->priv->emit_coverart_id != 0) {
		g_source_remove (source->priv->emit_coverart_id);
		source->priv->emit_coverart_id = 0;
	}

	/* check if the new playing entry is from this source */
	if (rhythmdb_query_model_entry_to_iter (source->priv->track_model, entry, &playing_iter) == TRUE) {
		GtkTreeIter iter;
		gboolean reached_playing = FALSE;
		int entries_after_playing = 0;
		GList *remove = NULL;
		GList *i;

		/* update our playing entry */
		source->priv->playing_entry = entry;

		/* mark invalidated entries for removal and count remaining */
		gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->track_model), &iter);
		do {
			RhythmDBEntry *iter_entry;
			iter_entry = rhythmdb_query_model_iter_to_entry (source->priv->track_model, &iter);

			if (reached_playing == TRUE) {
				entries_after_playing++;
			} else if (iter_entry == entry) {
				reached_playing = TRUE;
			} else {
				/* add to list of entries marked for removal */
				remove = g_list_append (remove, iter_entry);
			}

			rhythmdb_entry_unref (iter_entry);

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->track_model), &iter));

		/* remove invalidated entries */
		for (i = remove; i != NULL; i = i->next) {
			rhythmdb_query_model_remove_entry (source->priv->track_model, i->data);
			rhythmdb_entry_delete (db, i->data);
		}

		/* request more if needed */
		if (entries_after_playing <= 2) {
			rb_audioscrobbler_radio_source_tune (source);
			rb_audioscrobbler_radio_source_fetch_playlist (source);
		}

		/* emit cover art notification */
		source->priv->emit_coverart_id = g_idle_add ((GSourceFunc) emit_coverart_uri_cb, source);
	}

	rhythmdb_commit (db);

	g_object_unref (db);
}

static void
rb_audioscrobbler_radio_source_delete_station_action_cb (GtkAction *action,
                                                         RBAudioscrobblerRadioSource *source)
{
	rb_debug ("deleting station %s", source->priv->station_url);
	rb_audioscrobbler_profile_source_remove_radio_station (source->priv->parent, RB_SOURCE (source));
}

/* cover art */
static const char *
get_image_url_for_entry (RBAudioscrobblerRadioSource *source, RhythmDBEntry *entry)
{
	RBAudioscrobblerRadioTrackEntryData *data;
	RhythmDBEntryType entry_type;

	if (entry == NULL) {
		return NULL;
	}

	g_object_get (source, "entry-type", &entry_type, NULL);

	if (rhythmdb_entry_get_entry_type (entry) != entry_type) {
		return NULL;
	}

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA(entry, RBAudioscrobblerRadioTrackEntryData);
	return data->image_url;
}

static GValue *
coverart_uri_request (RhythmDB *db, RhythmDBEntry *entry, RBAudioscrobblerRadioSource *source)
{
	const char *image_url;

	image_url = get_image_url_for_entry (source, entry);
	if (image_url != NULL) {
		GValue *v;
		v = g_new0 (GValue, 1);
		g_value_init (v, G_TYPE_STRING);
		rb_debug ("requested cover image %s", image_url);
		g_value_set_string (v, image_url);
		return v;
	}

	return NULL;
}

static void
extra_metadata_gather_cb (RhythmDB *db, RhythmDBEntry *entry, RBStringValueMap *map, RBAudioscrobblerRadioSource *source)
{
	const char *image_url;

	image_url = get_image_url_for_entry (source, entry);
	if (image_url != NULL) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, image_url);

		rb_debug ("gathered cover image %s", image_url);
		rb_string_value_map_set (map, "rb:coverArt-uri", &v);
		g_value_unset (&v);
	}
}

static gboolean
emit_coverart_uri_cb (RBAudioscrobblerRadioSource *source)
{
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntry *entry;
	const char *image_url;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	source->priv->emit_coverart_id = 0;

	entry = rb_shell_player_get_playing_entry (RB_SHELL_PLAYER (rb_shell_get_player (shell)));
	image_url = get_image_url_for_entry (source, entry);
	if (image_url != NULL) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, image_url);
		rhythmdb_emit_entry_extra_metadata_notify (db,
							   entry,
							   "rb:coverArt-uri",
							   &v);
		g_value_unset (&v);
	}

	g_object_unref (shell);
	g_object_unref (db);

	return FALSE;
}

/* RBSource implementations */
static void
impl_activate (RBSource *asource)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	/* if the query model is empty then attempt to add some tracks to it */
	if (rhythmdb_query_model_get_duration (source->priv->track_model) == 0) {
		rb_audioscrobbler_radio_source_tune (source);
		rb_audioscrobbler_radio_source_fetch_playlist (source);
	}
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	return source->priv->track_view;
}

static void
impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	/* pulse progressbar if we're busy, otherwise see what the streaming source part of us has to say */
	if (source->priv->is_fetching_playlist) {
		/* Actually, we could be calling either radio.tune or radio.getPlaylist methods,
		 * but "Tuning station" seems like a user friendly message to display.
		 */
		*progress_text = g_strdup (_("Tuning station"));
		*progress = -1.0f;
	} else {
		rb_streaming_source_get_progress (RB_STREAMING_SOURCE (source), progress_text, progress);
	}
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_NEXT;
}

static GList *
impl_get_ui_actions (RBSource *asource)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	return rb_source_get_ui_actions (RB_SOURCE (source->priv->parent));
}

static gboolean
impl_show_popup (RBSource *asource)
{
	_rb_source_show_popup (asource, AUDIOSCROBBLER_RADIO_SOURCE_POPUP_PATH);
	return TRUE;
}

static void
impl_delete_thyself (RBSource *asource)
{
	RBAudioscrobblerRadioSource *source;
	RBShell *shell;
	RhythmDB *db;
	GtkTreeIter iter;
	gboolean loop;

	rb_debug ("deleting radio source");

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	/* Ensure playing entry is only deleted here, and not also when the playing entry changes
	 * because it is deleted here.
	 */
	source->priv->playing_entry = NULL;

	/* delete all entries */
	loop = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->track_model), &iter);
	while (loop) {
		RhythmDBEntry *entry;

		entry = rhythmdb_query_model_iter_to_entry (source->priv->track_model, &iter);
		rhythmdb_entry_delete (db, entry);
		rhythmdb_entry_unref (entry);

		loop = gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->track_model), &iter);
	}

	rhythmdb_commit (db);

	g_object_unref (shell);
	g_object_unref (db);
}
