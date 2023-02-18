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

#include "config.h"
#include <string.h>
#include <unistd.h>
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>
#include <glib/gi18n.h>
#include <glib/gstdio.h>

#include <totem-pl-parser.h>

#include "rb-audioscrobbler-radio-source.h"
#include "rb-audioscrobbler-radio-track-entry-type.h"
#include "rb-audioscrobbler-play-order.h"
#include "rb-debug.h"
#include "rb-display-page-tree.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-source-toolbar.h"
#include "rb-ext-db.h"


/* radio type stuff */
static const char* radio_types[] = {
	/* Translators: describes a radio stream playing tracks similar to those by an artist.
	 * Followed by a text entry box for the artist name.
	 */
	N_("Similar to Artist:"),
	/* Translators: describes a radio stream playing tracks listened to by the top fans of
	 * a particular artist.  Followed by a text entry box for the artist name.
	 */
	N_("Top Fans of Artist:"),
	/* Translators: describes a radio stream playing tracks from the library of a particular
	 * user.  Followed by a text entry box for the user name.
	 */
	N_("Library of User:"),
	/* Translators: describes a radio stream playing tracks played by users similar to a
	 * particular user.  Followed by a text entry box for the user name.
	 */
	N_("Neighbourhood of User:"),
	/* Translators: describes a radio stream playing tracks that a particular user has marked
	 * as loved.  Followed by a text entry box for the user name.
	 */
	N_("Tracks Loved by User:"),
	/* Translators: describes a radio stream playing tracks recommended to a particular user.
	 * Followed by a text entry box for the user name.
	 */
	N_("Recommendations for User:"),
	/* Translators: a type of station named "Mix Radio" by Last.fm.
	 * See http://blog.last.fm/2010/10/29/mix-radio-a-new-radio-station for a description of it.
	 * Followed by a text entry box for the user name.
	 */
	N_("Mix Radio for User:"),
	/* Translators: describes a radio stream playing tracks tagged with a particular tag.
	 * Followed by a text entry box for the tag.
	 */
	N_("Tracks Tagged with:"),
	/* Translators: describes a radio stream playing tracks often listened to by members of
	 * a particular group. Followed by a text entry box for the group name.
	 */
	N_("Listened by Group:"),
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_text (RBAudioscrobblerRadioType type)
{
	return _(radio_types[type]);
}

static const char* radio_urls[] = {
	"lastfm://artist/%s/similarartists",
	"lastfm://artist/%s/fans",
	"lastfm://user/%s/library",
	"lastfm://user/%s/neighbours",
	"lastfm://user/%s/loved",
	"lastfm://user/%s/recommended",
	"lastfm://user/%s/mix",
	"lastfm://globaltags/%s",
	"lastfm://group/%s",
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_url (RBAudioscrobblerRadioType type)
{
	return radio_urls[type];
}

static const char* radio_names[] = {

	/* Translators: I have chosen these names for the radio stations based upon
	 * what last.fm's website uses or what I thought to be sensible.
	 */
	/* Translators: station is built from artists similar to the artist %s */
	N_("%s Radio"),
	/* Translators: station is built from the artist %s's top fans */
	N_("%s Fan Radio"),
	/* Translators: station is built from the library of the user %s */
	N_("%s's Library"),
	/* Translators: station is built from the "neighbourhood" of the user %s.
	 * Last.fm uses "neighbourhood" to mean other users with similar music tastes */
	N_("%s's Neighbourhood"),
	/* Translators: station is built from the tracks which have been "loved" by the user %s */
	N_("%s's Loved Tracks"),
	/* Translators: station is built from the tracks which are recommended to the user %s */
	N_("%s's Recommended Radio"),
	/* Translators: station is the "Mix Radio" for the user %s.
	 * See http://blog.last.fm/2010/10/29/mix-radio-a-new-radio-station for description. */
	N_("%s's Mix Radio"),
	/* Translators: station is built from the tracks which have been "tagged" with %s.
	 * Last.fm lets users "tag" songs with any string they wish. Tags are usually genres,
	 * but nationalities, record labels, decades and very random words are also common */
	N_("%s Tag Radio"),
	/* Translators: station is built from the library of the group %s */
	N_("%s Group Radio"),
	NULL
};

const char *
rb_audioscrobbler_radio_type_get_default_name (RBAudioscrobblerRadioType type)
{
	return _(radio_names[type]);
}

/* source declarations */
struct _RBAudioscrobblerRadioSourcePrivate
{
	RBAudioscrobblerProfilePage *parent;

	RBAudioscrobblerService *service;
	char *username;
	char *session_key;
	char *station_url;

	SoupSession *soup_session;

	GtkWidget *error_info_bar;
	GtkWidget *error_info_bar_label;

	RBEntryView *track_view;
	RhythmDBQueryModel *track_model;

	gboolean is_busy;

	RBPlayOrder *play_order;

	/* the currently playing entry from this source, if there is one */
	RhythmDBEntry *playing_entry;

	RBExtDB *art_store;
};

#define RB_AUDIOSCROBBLER_RADIO_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE, RBAudioscrobblerRadioSourcePrivate))

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

static void playing_song_changed_cb (RBShellPlayer *player,
                                     RhythmDBEntry *entry,
                                     RBAudioscrobblerRadioSource *source);

/* last.fm api requests */
static void tune (RBAudioscrobblerRadioSource *source);
static void tune_response_cb (SoupSession *session,
                              GAsyncResult *result,
                              RBAudioscrobblerRadioSource *source);
static void parse_tune_response (RBAudioscrobblerRadioSource *source,
                                 const char *data,
                                 gsize data_size);
static void fetch_playlist (RBAudioscrobblerRadioSource *source);
static void fetch_playlist_response_cb (SoupSession *session,
                                        GAsyncResult *result,
                                        RBAudioscrobblerRadioSource *source);
static void parse_playlist_response (RBAudioscrobblerRadioSource *source,
                                     const char *data,
                                     gsize data_size);
static void xspf_entry_parsed (TotemPlParser *parser,
                               const char *uri,
                               GHashTable *metadata,
                               RBAudioscrobblerRadioSource *source);

/* info bar related things */
static void display_error_info_bar (RBAudioscrobblerRadioSource *source,
                                    const char *message);

/* RBDisplayPage implementations */
static void impl_selected (RBDisplayPage *page);
static void impl_delete_thyself (RBDisplayPage *page);
static gboolean impl_can_remove (RBDisplayPage *page);
static void impl_remove (RBDisplayPage *page);

/* RBSource implementations */
static RBEntryView *impl_get_entry_view (RBSource *asource);
static RBSourceEOFType impl_handle_eos (RBSource *asource);
static void impl_get_playback_status (RBSource *source, char **text, float *progress);

enum {
	PROP_0,
	PROP_PARENT,
	PROP_SERVICE,
	PROP_USERNAME,
	PROP_SESSION_KEY,
	PROP_STATION_URL,
	PROP_PLAY_ORDER
};

G_DEFINE_DYNAMIC_TYPE (RBAudioscrobblerRadioSource, rb_audioscrobbler_radio_source, RB_TYPE_STREAMING_SOURCE)

RBSource *
rb_audioscrobbler_radio_source_new (RBAudioscrobblerProfilePage *parent,
                                    RBAudioscrobblerService *service,
                                    const char *username,
                                    const char *session_key,
                                    const char *station_name,
                                    const char *station_url)
{
	RBSource *source;
	RBShell *shell;
	GObject *plugin;
	RhythmDB *db;
	GMenu *toolbar_menu;

	g_object_get (parent, "shell", &shell, "plugin", &plugin, NULL);
	g_object_get (shell, "db", &db, NULL);

	if (RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK == NULL) {
		rb_audioscrobbler_radio_track_register_entry_type (db);
	}

	g_object_get (parent, "toolbar-menu", &toolbar_menu, NULL);

	source = g_object_new (RB_TYPE_AUDIOSCROBBLER_RADIO_SOURCE,
	                       "shell", shell,
	                       "plugin", plugin,
	                       "name", station_name,
	                       "entry-type", RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK,
	                       "parent", parent,
	                       "service", service,
                               "username", username,
	                       "session-key", session_key,
	                       "station-url", station_url,
			       "toolbar-menu", toolbar_menu,
	                       NULL);

	g_object_unref (shell);
	g_object_unref (plugin);
	g_object_unref (db);
	g_object_unref (toolbar_menu);

	return source;
}

static void
rb_audioscrobbler_radio_source_class_init (RBAudioscrobblerRadioSourceClass *klass)
{
	GObjectClass *object_class;
	RBDisplayPageClass *page_class;
	RBSourceClass *source_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->constructed = rb_audioscrobbler_radio_source_constructed;
	object_class->dispose = rb_audioscrobbler_radio_source_dispose;
	object_class->finalize = rb_audioscrobbler_radio_source_finalize;
	object_class->get_property = rb_audioscrobbler_radio_source_get_property;
	object_class->set_property = rb_audioscrobbler_radio_source_set_property;

	page_class = RB_DISPLAY_PAGE_CLASS (klass);
	page_class->selected = impl_selected;
	page_class->delete_thyself = impl_delete_thyself;
	page_class->can_remove = impl_can_remove;
	page_class->remove = impl_remove;

	source_class = RB_SOURCE_CLASS (klass);
	source_class->can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->get_entry_view = impl_get_entry_view;
	source_class->handle_eos = impl_handle_eos;
	source_class->get_playback_status = impl_get_playback_status;

	g_object_class_install_property (object_class,
	                                 PROP_PARENT,
	                                 g_param_spec_object ("parent",
	                                                      "Parent",
	                                                      "Profile page that created this radio source",
	                                                      RB_TYPE_AUDIOSCROBBLER_PROFILE_PAGE,
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
rb_audioscrobbler_radio_source_class_finalize (RBAudioscrobblerRadioSourceClass *klass)
{
}

static void
rb_audioscrobbler_radio_source_init (RBAudioscrobblerRadioSource *source)
{
	source->priv = RB_AUDIOSCROBBLER_RADIO_SOURCE_GET_PRIVATE (source);

	source->priv->soup_session = soup_session_new ();
}

static void
rb_audioscrobbler_radio_source_constructed (GObject *object)
{
	RBAudioscrobblerRadioSource *source;
	RBShell *shell;
	RBShellPlayer *shell_player;
	RhythmDB *db;
	GtkWidget *main_vbox;
	GtkWidget *error_info_bar_content_area;
	GtkAccelGroup *accel_group;
	RBSourceToolbar *toolbar;

	RB_CHAIN_GOBJECT_METHOD (rb_audioscrobbler_radio_source_parent_class, constructed, object);

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (object);
	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell,
		      "db", &db,
		      "shell-player", &shell_player,
		      "accel-group", &accel_group,
		      NULL);

	source->priv->art_store = rb_ext_db_new ("album-art");

	main_vbox = gtk_box_new (GTK_ORIENTATION_VERTICAL, 4);
	gtk_widget_show (main_vbox);
	gtk_container_add (GTK_CONTAINER (source), main_vbox);

	/* toolbar */
	toolbar = rb_source_toolbar_new (RB_DISPLAY_PAGE (source), accel_group);
	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (toolbar), FALSE, FALSE, 0);
	gtk_widget_show_all (GTK_WIDGET (toolbar));

	/* error info bar */
	source->priv->error_info_bar = gtk_info_bar_new ();
	source->priv->error_info_bar_label = gtk_label_new ("");
	error_info_bar_content_area = gtk_info_bar_get_content_area (GTK_INFO_BAR (source->priv->error_info_bar));
	gtk_container_add (GTK_CONTAINER (error_info_bar_content_area), source->priv->error_info_bar_label);
	gtk_box_pack_start (GTK_BOX (main_vbox), source->priv->error_info_bar, FALSE, FALSE, 0);

	/* entry view */
	source->priv->track_view = rb_entry_view_new (db, G_OBJECT (shell_player), FALSE, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->track_view, RB_ENTRY_VIEW_COL_DURATION, FALSE);
	rb_entry_view_set_columns_clickable (source->priv->track_view, FALSE);
	gtk_widget_show_all (GTK_WIDGET (source->priv->track_view));

	gtk_box_pack_start (GTK_BOX (main_vbox), GTK_WIDGET (source->priv->track_view), TRUE, TRUE, 0);

	rb_source_bind_settings (RB_SOURCE (source), GTK_WIDGET (source->priv->track_view), NULL, NULL, TRUE);

	/* query model */
	source->priv->track_model = rhythmdb_query_model_new_empty (db);
	rb_entry_view_set_model (source->priv->track_view, source->priv->track_model);
	g_object_set (source, "query-model", source->priv->track_model, NULL);

	/* play order */
	source->priv->play_order = rb_audioscrobbler_play_order_new (shell_player);

	/* signals */
	g_signal_connect_object (shell_player,
				 "playing-song-changed",
				 G_CALLBACK (playing_song_changed_cb),
				 source, 0);

	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (source), RB_DISPLAY_PAGE (source->priv->parent));

	g_object_unref (shell);
	g_object_unref (shell_player);
	g_object_unref (db);
	g_object_unref (accel_group);
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

	if (source->priv->art_store != NULL) {
		g_object_unref (source->priv->art_store);
		source->priv->art_store = NULL;
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

static void
playing_song_changed_cb (RBShellPlayer *player,
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

	/* check if the new playing entry is from this source */
	if (rhythmdb_query_model_entry_to_iter (source->priv->track_model, entry, &playing_iter) == TRUE) {
		RBAudioscrobblerRadioTrackData *track_data;
		RBExtDBKey *key;
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
			tune (source);
		}

		/* provide cover art */
		key = rb_ext_db_key_create_storage ("album", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
		rb_ext_db_key_add_field (key, "artist", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
		track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA(entry, RBAudioscrobblerRadioTrackData);
		rb_ext_db_store_uri (source->priv->art_store,
				     key,
				     RB_EXT_DB_SOURCE_SEARCH,
				     track_data->image_url);
		rb_ext_db_key_free (key);
	}

	rhythmdb_commit (db);

	g_object_unref (db);
}

static void
tune (RBAudioscrobblerRadioSource *source)
{
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	char *url;
	SoupMessage *msg;
	SoupMessageHeaders *hdrs;

	/* only go through the tune + get playlist process once at a time */
	if (source->priv->is_busy == TRUE) {
		return;
	}

	source->priv->is_busy = TRUE;
	gtk_widget_hide (source->priv->error_info_bar);

	api_key = rb_audioscrobbler_service_get_api_key (source->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (source->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (source->priv->service);

	sig_arg = g_strdup_printf ("api_key%smethodradio.tunesk%sstation%s%s",
	                           api_key,
	                           source->priv->session_key,
	                           source->priv->station_url,
	                           api_sec);

	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "radio.tune",
				  "station", source->priv->station_url,
				  "api_key", api_key,
				  "api_sig", sig,
				  "sk", source->priv->session_key,
				  NULL);

	/* The format parameter needs to go here instead of in the request body */
	url = g_strdup_printf ("%s?format=json", api_url);

	rb_debug ("sending tune request: %s", query);
	msg = soup_message_new_from_encoded_form (SOUP_METHOD_POST, url, query);
	g_return_if_fail (msg != NULL);

	hdrs = soup_message_get_request_headers (msg);
	soup_message_headers_set_content_type (hdrs, "application/x-www-form-urlencoded", NULL);

	soup_session_send_and_read_async (source->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) tune_response_cb,
					  source);

	g_free (sig_arg);
	g_free (sig);
	g_free (url);
}

static void
tune_response_cb (SoupSession *session,
                  GAsyncResult *result,
                  RBAudioscrobblerRadioSource *source)
{
	GBytes *bytes;
	const char *body;
	gsize size;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, &size);
	} else {
		body = NULL;
		size = 0;
	}

	parse_tune_response (source, body, size);

	if (bytes != NULL) {
		g_bytes_unref (bytes);
	}
}

static void
parse_tune_response (RBAudioscrobblerRadioSource *source,
                     const char *body,
                     gsize body_size)
{
	JsonParser *parser;

	parser = json_parser_new ();

	if (body == NULL) {
		rb_debug ("no response from tune request");
		display_error_info_bar (source, _("Error tuning station: no response"));
		source->priv->is_busy = FALSE;

	} else if (json_parser_load_from_data (parser, body, (gssize)body_size, NULL)) {
		JsonObject *root_object;
		root_object = json_node_get_object (json_parser_get_root (parser));

		/* Noticed on 2010-08-12 that Last.fm now responds with a "{ status:ok }"
		 * instead of providing a "station" object with various properties.
		 * Checking for a "station" or "status" member ensures compatibility with
		 * both Last.fm and Libre.fm.
		 */
		if (json_object_has_member (root_object, "station") ||
		    json_object_has_member (root_object, "status")) {
			rb_debug ("tune request was successful");

			/* get the playlist */
			fetch_playlist (source);
		} else if (json_object_has_member (root_object, "error")) {
			int code;
			const char *message;

			code = json_object_get_int_member (root_object, "error");
			message = json_object_get_string_member (root_object, "message");

			rb_debug ("tune request responded with error: %s", message);

			/* show appropriate error message */
			char *error_message = NULL;

			if (code == 6) {
				/* Invalid station url */
				error_message = g_strdup (_("Invalid station URL"));
			} else if (code == 12) {
				/* Subscriber only station */
				/* Translators: %s is the name of the audioscrobbler service, for example "Last.fm".
				 * This message indicates that to listen to this radio station the user needs to be
				 * a paying subscriber to the service. */
				error_message = g_strdup_printf (_("This station is only available to %s subscribers"),
								 rb_audioscrobbler_service_get_name (source->priv->service));
			} else if (code == 20) {
				/* Not enough content */
				error_message = g_strdup (_("Not enough content to play station"));
			} else if (code == 27) {
				/* Deprecated station */
				/* Translators: %s is the name of the audioscrobbler service, for example "Last.fm".
				 * This message indicates that the service has deprecated this type of station. */
				error_message = g_strdup_printf (_("%s no longer supports this type of station"),
								 rb_audioscrobbler_service_get_name (source->priv->service));
			} else {
				/* Other error */
				error_message = g_strdup_printf (_("Error tuning station: %i - %s"), code, message);
			}

			display_error_info_bar (source, error_message);

			g_free (error_message);

			source->priv->is_busy = FALSE;
		} else {
			rb_debug ("unexpected response from tune request: %s", body);
			display_error_info_bar(source, _("Error tuning station: unexpected response"));
			source->priv->is_busy = FALSE;
		}
	} else {
		rb_debug ("invalid response from tune request: %s", body);
		display_error_info_bar(source, _("Error tuning station: invalid response"));
		source->priv->is_busy = FALSE;
	}

	g_object_unref (parser);
}

static void
fetch_playlist (RBAudioscrobblerRadioSource *source)
{
	const char *api_key;
	const char *api_sec;
	const char *api_url;
	char *sig_arg;
	char *sig;
	char *query;
	SoupMessage *msg;
	SoupMessageHeaders *hdrs;

	api_key = rb_audioscrobbler_service_get_api_key (source->priv->service);
	api_sec = rb_audioscrobbler_service_get_api_secret (source->priv->service);
	api_url = rb_audioscrobbler_service_get_api_url (source->priv->service);

	sig_arg = g_strdup_printf ("api_key%smethodradio.getPlaylistrawtruesk%s%s",
	                           api_key,
	                           source->priv->session_key,
	                           api_sec);

	sig = g_compute_checksum_for_string (G_CHECKSUM_MD5, sig_arg, -1);

	query = soup_form_encode ("method", "radio.getPlaylist",
				  "api_key", api_key,
				  "api_sig", sig,
				  "sk", source->priv->session_key,
				  "raw", "true",
				  NULL);

	rb_debug ("sending playlist request: %s", query);
	msg = soup_message_new_from_encoded_form (SOUP_METHOD_POST, api_url, query);
	g_return_if_fail (msg != NULL);

	hdrs = soup_message_get_request_headers (msg);
	soup_message_headers_set_content_type (hdrs, "application/x-www-form-urlencoded", NULL);

	soup_session_send_and_read_async (source->priv->soup_session,
					  msg,
					  G_PRIORITY_DEFAULT,
					  NULL,
					  (GAsyncReadyCallback) fetch_playlist_response_cb,
					  source);

	g_free (sig_arg);
	g_free (sig);
}

static void
fetch_playlist_response_cb (SoupSession *session,
                            GAsyncResult *result,
                            RBAudioscrobblerRadioSource *source)
{
	GBytes *bytes;
	const char *body;
	gsize size;

	bytes = soup_session_send_and_read_finish (session, result, NULL);
	if (bytes != NULL) {
		body = g_bytes_get_data (bytes, &size);
	} else {
		body = NULL;
		size = 0;
	}

	parse_playlist_response (source, body, size);

	if (bytes != NULL) {
		g_bytes_unref (bytes);
	}
}

static void
parse_playlist_response (RBAudioscrobblerRadioSource *source,
                         const char *body,
                         gsize body_size)
{
	int tmp_fd;
	char *tmp_name;
	char *tmp_uri = NULL;
	GIOChannel *channel = NULL;
	TotemPlParser *parser = NULL;
	TotemPlParserResult result;
	GError *error = NULL;

	source->priv->is_busy = FALSE;

	if (body == NULL) {
		rb_debug ("no response from get playlist request");
		return;
	}

	/* until totem-pl-parser can parse playlists from in-memory data, we save it to a
	 * temporary file.
	 */

	tmp_fd = g_file_open_tmp ("rb-audioscrobbler-playlist-XXXXXX.xspf", &tmp_name, &error);
	if (error != NULL) {
		rb_debug ("unable to save playlist: %s", error->message);
		goto cleanup;
	}

	channel = g_io_channel_unix_new (tmp_fd);
	g_io_channel_write_chars (channel, body, (gssize)body_size, NULL, &error);
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
	g_signal_connect_data (parser, "entry-parsed",
	                       G_CALLBACK (xspf_entry_parsed),
	                       source, NULL, 0);
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
xspf_entry_parsed (TotemPlParser *parser,
                   const char *uri,
                   GHashTable *metadata,
                   RBAudioscrobblerRadioSource *source)
{
	RBShell *shell;
	RhythmDBEntryType *entry_type;
	RhythmDB *db;

	RhythmDBEntry *entry;
	RBAudioscrobblerRadioTrackData *track_data;
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
	track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioscrobblerRadioTrackData);
	track_data->service = source->priv->service;

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
display_error_info_bar (RBAudioscrobblerRadioSource *source,
                        const char *message)
{
	gtk_label_set_label (GTK_LABEL (source->priv->error_info_bar_label), message);
	gtk_info_bar_set_message_type (GTK_INFO_BAR (source->priv->error_info_bar), GTK_MESSAGE_WARNING);
	gtk_widget_show_all (source->priv->error_info_bar);
}

static gboolean
impl_can_remove (RBDisplayPage *page)
{
	return TRUE;
}

static void
impl_remove (RBDisplayPage *page)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (page);
	rb_audioscrobbler_profile_page_remove_radio_station (source->priv->parent, RB_SOURCE (page));
}

static void
impl_selected (RBDisplayPage *page)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (page);

	RB_DISPLAY_PAGE_CLASS (rb_audioscrobbler_radio_source_parent_class)->selected (page);

	/* if the query model is empty then attempt to add some tracks to it */
	if (rhythmdb_query_model_get_duration (source->priv->track_model) == 0) {
		tune (source);
	}
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBAudioscrobblerRadioSource *source = RB_AUDIOSCROBBLER_RADIO_SOURCE (asource);

	return source->priv->track_view;
}

static void
impl_get_playback_status (RBSource *source, char **text, float *progress)
{
	rb_streaming_source_get_progress (RB_STREAMING_SOURCE (source), text, progress);
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_NEXT;
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
	RBAudioscrobblerRadioSource *source;
	RBShell *shell;
	RhythmDB *db;
	GtkTreeIter iter;
	gboolean loop;

	rb_debug ("deleting radio source");

	source = RB_AUDIOSCROBBLER_RADIO_SOURCE (page);

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	/* Ensure playing entry isn't deleted twice */
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

void
_rb_audioscrobbler_radio_source_register_type (GTypeModule *module)
{
	rb_audioscrobbler_radio_source_register_type (module);
}
