/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of last.fm station source object
 *
 *  Copyright (C) 2006 Matt Novenstern <fisxoj@gmail.com>
 *  Copyright (C) 2008 Jonathan Matthew <jonathan@d14n.org>
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

/*  The author would like to extend thanks to Iain Holmes, author of Last Exit,
 *   an alternative last.fm player written in C#, the code of which was
 *   extraordinarily useful in the creation of this code
 */

/* TODO List
 * - "recommendation radio" with percentage setting (0=obscure, 100=popular)
 * - watch username gconf entries, create/update neighbour station
*/


#include <config.h>

#include <string.h>
#include <math.h>
#include <unistd.h>

#include <glib/gi18n.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <gconf/gconf-value.h>

#include <totem-pl-parser.h>

#include <libsoup/soup.h>

#include "eel-gconf-extensions.h"

#include "rb-proxy-config.h"
#include "rb-preferences.h"

#include "rb-audioscrobbler.h"
#include "rb-lastfm-source.h"

#include "rhythmdb-query-model.h"
#include "rb-glade-helpers.h"
#include "rb-stock-icons.h"
#include "rb-entry-view.h"
#include "rb-property-view.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "gedit-message-area.h"
#include "rb-shell-player.h"
#include "rb-play-order.h"
#include "rb-lastfm-play-order.h"

#define LASTFM_URL "ws.audioscrobbler.com"
#define RB_LASTFM_PLATFORM "linux"
#define RB_LASTFM_VERSION "1.5"

#define USER_AGENT "Rhythmbox/" VERSION

#define LASTFM_NO_COVER_IMAGE "http://cdn.last.fm/depth/catalogue/noimage/cover_med.gif"

#define EPSILON (0.0001f)

/* request queue stuff */

typedef SoupMessage *(*CreateRequestFunc) (RBLastfmSource *source, RhythmDBEntry *entry);
typedef void (*HandleResponseFunc) (RBLastfmSource *source, const char *body, RhythmDBEntry *entry);

typedef struct
{
	RBLastfmSource *source;
	RhythmDBEntry *entry;

	CreateRequestFunc create_request;
	HandleResponseFunc handle_response;

	const char *description;
} RBLastfmAction;

static void free_action (RBLastfmAction *action);
static void queue_action (RBLastfmSource *source,
			  CreateRequestFunc create_request,
			  HandleResponseFunc handle_response,
			  RhythmDBEntry *entry,
			  const char *description);

static void process_queue (RBLastfmSource *source);
static void queue_handshake (RBLastfmSource *source);
static void queue_change_station (RBLastfmSource *source, RhythmDBEntry *station); 
static void queue_get_playlist (RBLastfmSource *source, RhythmDBEntry *station);
static void queue_get_playlist_and_skip (RBLastfmSource *source, RhythmDBEntry *station);
static void queue_love_track (RBLastfmSource *source);
static void queue_ban_track (RBLastfmSource *source);


static void rb_lastfm_source_class_init (RBLastfmSourceClass *klass);
static void rb_lastfm_source_init (RBLastfmSource *source);
static GObject *rb_lastfm_source_constructor (GType type, guint n_construct_properties,
					      GObjectConstructParam *construct_properties);
static void rb_lastfm_source_finalize (GObject *object);
static void rb_lastfm_source_set_property (GObject *object,
			                  guint prop_id,
			                  const GValue *value,
			                  GParamSpec *pspec);
static void rb_lastfm_source_get_property (GObject *object,
			                  guint prop_id,
			                  GValue *value,
			                  GParamSpec *pspec);

static void rb_lastfm_source_songs_view_sort_order_changed_cb (RBEntryView *view,
							       RBLastfmSource *source);
/* source-specific methods */

static void rb_lastfm_source_drag_cb (GtkWidget *widget,
				      GdkDragContext *dc,
				      gint x, gint y,
				      GtkSelectionData *selection_data,
				      guint info, guint time,
				      RBLastfmSource *source);
static void rb_lastfm_source_station_selection_cb (RBEntryView *stations,
						   RBLastfmSource *source);
static void rb_lastfm_source_station_activated_cb (RBEntryView *stations,
						   RhythmDBEntry *station,
						   RBLastfmSource *source);

static void rb_lastfm_source_dispose (GObject *object);

/* RBSource implementation methods */
static void impl_delete (RBSource *asource);
static GList *impl_get_ui_actions (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *asource);
static void impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static void impl_activate (RBSource *source);
static gboolean impl_show_popup (RBSource *source);
static guint impl_want_uri (RBSource *source, const char *uri);
static gboolean impl_add_uri (RBSource *source, const char *uri, const char *title, const char *genre);
static RBSourceEOFType impl_handle_eos (RBSource *asource);

static void rb_lastfm_source_new_station (const char *uri, const char *title, RBLastfmSource *source);
static void rb_lastfm_source_love_track (GtkAction *action, RBLastfmSource *source);
static void rb_lastfm_source_ban_track (GtkAction *action, RBLastfmSource *source);
static void rb_lastfm_source_download_track (GtkAction *action, RBLastfmSource *source);
static void rb_lastfm_source_delete_station (GtkAction *action, RBLastfmSource *source);
static char *rb_lastfm_source_title_from_uri (const char *uri);
static void rb_lastfm_source_add_station_cb (GtkButton *button, gpointer *data);
static void rb_lastfm_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry, RBLastfmSource *source);

static void show_entry_popup (RBEntryView *view,
			      gboolean over_entry,
			      RBSource *source);
static void playing_song_changed_cb (RBShellPlayer *player,
				     RhythmDBEntry *entry,
				     RBLastfmSource *source);
static GValue * coverart_uri_request (RhythmDB *db,
				      RhythmDBEntry *entry,
				      RBLastfmSource *source);
static void extra_metadata_gather_cb (RhythmDB *db,
				      RhythmDBEntry *entry,
				      RBStringValueMap *map,
				      RBLastfmSource *source);

static const char* const radio_options[][3] = {
	{N_("Similar Artists radio"), "lastfm://artist/%s/similarartists", N_("Artists similar to %s")},
	{N_("Tag radio"), "lastfm://globaltags/%s", N_("Tracks tagged with %s")},
	{N_("Artist Fan radio"), "lastfm://artist/%s/fans", N_("Artists liked by fans of %s")},
	{N_("Group radio"), "lastfm://group/%s", N_("Tracks liked by the %s group")},
	{N_("Neighbour radio"), "lastfm://user/%s/neighbours", N_("%s's Neighbour Radio")},
	{N_("Personal radio"), "lastfm://user/%s/personal", N_("%s's Personal Radio")},
	{N_("Loved tracks"), "lastfm://user/%s/loved", N_("%s's Loved Tracks")},
	{N_("Recommended tracks"), "lastfm://user/%s/recommended/100", N_("Tracks recommended to %s")},
	{NULL, NULL, NULL}
};

typedef struct 
{
	gboolean played;		/* tracks can only be played once */
	char *image_url;
	char *track_auth;		/* not used yet; for submission protocol 1.2 */
	char *download_url;
} RBLastfmTrackEntryData;


struct RBLastfmSourcePrivate
{
	GtkWidget *main_box;
	GtkWidget *paned;
	GtkWidget *message_area;
	GtkWidget *txtbox;
	GtkWidget *typecombo;
	GtkWidget *config_widget;
	RhythmDB  *db;

	GtkActionGroup *action_group;

	RBEntryView *stations;
	RBEntryView *tracks;

	RBShellPlayer *shell_player;
	RhythmDBEntryType station_entry_type;
	RhythmDBEntryType track_entry_type;
	char *session_id;
	RhythmDBEntry *current_station;
	RBPlayOrder *play_order;

	RhythmDBQueryModel *query_model;
	RhythmDBEntry *last_entry;

	gboolean subscriber;
	char *base_url;
	char *base_path;

	enum {
		NOT_CONNECTED = 0,
		CONNECTED,
		BANNED,
		LOGIN_FAILED,
		STATION_FAILED
	} state;

	guint notification_username_id;
	guint notification_password_id;

	GQueue *action_queue;
	gboolean request_outstanding;
	const char *request_description;
	const char *station_failed_reason;

	SoupSession *soup_session;
	RBProxyConfig *proxy_config;

	guint emit_coverart_id;
};

/* these are just for debug output */
static const char *state_name[] = {
	"not logged in",
	"connected",
	"client is banned",
	"login failed",
	"station unavailable"
};

G_DEFINE_TYPE (RBLastfmSource, rb_lastfm_source, RB_TYPE_STREAMING_SOURCE);

enum
{
	PROP_0,
	PROP_ENTRY_TYPE,
	PROP_STATION_ENTRY_TYPE,
	PROP_PROXY_CONFIG,
	PROP_PLAY_ORDER
};

static GtkActionEntry rb_lastfm_source_actions [] =
{
	{ "LastfmLoveSong", "emblem-favorite", N_("Love"), NULL,
	  N_("Mark this song as loved"),
	  G_CALLBACK (rb_lastfm_source_love_track) },
	{ "LastfmBanSong", GTK_STOCK_CANCEL, N_("Ban"), NULL,
	  N_("Ban the current track from being played again"),
	  G_CALLBACK (rb_lastfm_source_ban_track) },
	{ "LastfmStationDelete", GTK_STOCK_DELETE, N_("Delete Station"), NULL,
	  N_("Delete the selected station"),
	  G_CALLBACK (rb_lastfm_source_delete_station) },
	{ "LastfmDownloadSong", NULL, N_("Download song"), NULL,
	  N_("Download this song"),
	  G_CALLBACK (rb_lastfm_source_download_track) }
};

static const GtkTargetEntry lastfm_drag_types[] = {
	{  "text/plain", 0, 0 },
	{  "_NETSCAPE_URL", 0, 1 }
};

static void
rb_lastfm_source_class_init (RBLastfmSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->finalize = rb_lastfm_source_finalize;
	object_class->dispose = rb_lastfm_source_dispose;
	object_class->constructor = rb_lastfm_source_constructor;

	object_class->set_property = rb_lastfm_source_set_property;
	object_class->get_property = rb_lastfm_source_get_property;

	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_delete = impl_delete;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_get_status = impl_get_status;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_receive_drag = impl_receive_drag;
	source_class->impl_activate = impl_activate;
	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_want_uri = impl_want_uri;
	source_class->impl_add_uri = impl_add_uri;
	source_class->impl_handle_eos = impl_handle_eos;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
							     "Entry type",
							     "Entry type for last.fm tracks",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_STATION_ENTRY_TYPE,
					 g_param_spec_boxed ("station-entry-type",
							     "Entry type",
							     "Entry type for last.fm stations",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PROXY_CONFIG,
					 g_param_spec_object ("proxy-config",
							      "RBProxyConfig",
							      "RBProxyConfig object",
							      RB_TYPE_PROXY_CONFIG,
							      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_override_property (object_class,
					  PROP_PLAY_ORDER,
					  "play-order");


	g_type_class_add_private (klass, sizeof (RBLastfmSourcePrivate));
}

static void
on_gconf_changed_cb (GConfClient    *client,
		     guint           cnxn_id,
		     GConfEntry     *entry,
		     RBLastfmSource *source)
{
	rb_debug ("GConf key updated: \"%s\"", entry->key);


	if (source->priv->state == CONNECTED) {
		return;
	}

	if (strcmp (entry->key, CONF_AUDIOSCROBBLER_USERNAME) == 0
	    || strcmp (entry->key, CONF_AUDIOSCROBBLER_PASSWORD) == 0) {
		source->priv->state = NOT_CONNECTED;
		queue_handshake (source);
	} else {
		rb_debug ("Unhandled GConf key updated: \"%s\"", entry->key);
	}
}

static void
rb_lastfm_source_init (RBLastfmSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE ((source), RB_TYPE_LASTFM_SOURCE,  RBLastfmSourcePrivate);

	source->priv->action_queue = g_queue_new ();

	source->priv->notification_username_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_USERNAME,
					    (GConfClientNotifyFunc) on_gconf_changed_cb,
					    source);
	source->priv->notification_password_id =
		eel_gconf_notification_add (CONF_AUDIOSCROBBLER_PASSWORD,
					    (GConfClientNotifyFunc) on_gconf_changed_cb,
					    source);

}

static void
rb_lastfm_source_dispose (GObject *object)
{
	RBLastfmSource *source;

	source = RB_LASTFM_SOURCE (object);

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	if (source->priv->proxy_config != NULL) {
		g_object_unref (source->priv->proxy_config);
		source->priv->proxy_config = NULL;
	}

	if (source->priv->soup_session != NULL) {
		soup_session_abort (source->priv->soup_session);
		g_object_unref (source->priv->soup_session);
		source->priv->soup_session = NULL;
	}

	if (source->priv->play_order != NULL) {
		g_object_unref (source->priv->play_order);
		source->priv->play_order = NULL;
	}

	if (source->priv->query_model != NULL) {
		g_object_unref (source->priv->query_model);
		source->priv->query_model = NULL;
	}

	if (source->priv->notification_username_id != 0) {
		eel_gconf_notification_remove (source->priv->notification_username_id);
		source->priv->notification_username_id = 0;
	}
	if (source->priv->notification_password_id != 0) {
		eel_gconf_notification_remove (source->priv->notification_password_id);
		source->priv->notification_password_id = 0;
	}

	/* kill entries here? */

	G_OBJECT_CLASS (rb_lastfm_source_parent_class)->dispose (object);
}

static void
rb_lastfm_source_finalize (GObject *object)
{
	RBLastfmSource *source;
	source = RB_LASTFM_SOURCE (object);

	/* get rid of any pending actions */
	g_queue_foreach (source->priv->action_queue,
			 (GFunc) free_action,
			 NULL);
	g_queue_free (source->priv->action_queue);

	g_free (source->priv->session_id);

	G_OBJECT_CLASS (rb_lastfm_source_parent_class)->finalize (object);
}

static GObject *
rb_lastfm_source_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBLastfmSource *source;
	RBLastfmSourceClass *klass;
	RBShell *shell;
	GtkWidget *editor_vbox;
	GtkWidget *editor_box;
	GtkWidget *add_button;
	GtkWidget *instructions;
	GPtrArray *query;
	RhythmDBQueryModel *station_query_model;
	int i;

	klass = RB_LASTFM_SOURCE_CLASS (g_type_class_peek (RB_TYPE_LASTFM_SOURCE));

	source = RB_LASTFM_SOURCE (G_OBJECT_CLASS (rb_lastfm_source_parent_class)
			->constructor (type, n_construct_properties, construct_properties));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell),
		      "db", &source->priv->db,
		      "shell-player", &source->priv->shell_player,
		      NULL);
	g_object_unref (G_OBJECT (shell));

	g_signal_connect_object (source->priv->db,
				 "entry-added",
				 G_CALLBACK (rb_lastfm_source_entry_added_cb),
				 source, 0);
	g_signal_connect_object (source->priv->db,
				 "entry-extra-metadata-request::" RHYTHMDB_PROP_COVER_ART_URI,
				 G_CALLBACK (coverart_uri_request),
				 source, 0);
	g_signal_connect_object (source->priv->db,
				 "entry-extra-metadata-gather",
				 G_CALLBACK (extra_metadata_gather_cb),
				 source, 0);
	g_signal_connect_object (source->priv->shell_player,
				 "playing-song-changed",
				 G_CALLBACK (playing_song_changed_cb),
				 source, 0);

	/* set up station tuner */
	editor_vbox = gtk_vbox_new (FALSE, 5);
	editor_box = gtk_hbox_new (FALSE, 5);

	/* awful */
	instructions = gtk_label_new (_("Enter the item to build a Last.fm station out of:"));
	g_object_set (instructions, "xalign", 0.0, NULL);

	add_button = gtk_button_new_with_label (_("Add"));
	g_signal_connect_object (G_OBJECT (add_button),
				 "clicked",
				 G_CALLBACK (rb_lastfm_source_add_station_cb),
				 source, 0);

	source->priv->typecombo = gtk_combo_box_new_text ();
	for (i = 0; radio_options[i][0] != NULL; i++) {
		gtk_combo_box_append_text (GTK_COMBO_BOX (source->priv->typecombo), _(radio_options[i][0]));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->typecombo), 0);

	source->priv->txtbox = gtk_entry_new ();
	g_signal_connect_object (G_OBJECT (source->priv->txtbox),
				 "activate",
				 G_CALLBACK (rb_lastfm_source_add_station_cb),
				 source, 0);

	gtk_box_pack_end (GTK_BOX (editor_box), add_button, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (editor_box), source->priv->txtbox, TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (editor_box), source->priv->typecombo, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (editor_vbox), editor_box, TRUE, TRUE, 0);
	gtk_box_pack_end (GTK_BOX (editor_vbox), instructions, TRUE, TRUE, 0);

	source->priv->paned = gtk_vpaned_new ();

	/* set up stations view */
	source->priv->stations = rb_entry_view_new (source->priv->db,
						    G_OBJECT (source->priv->shell_player),
						    NULL,		/* sort key? */
						    FALSE, FALSE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_RATING, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_LAST_PLAYED, TRUE);
	g_signal_connect_object (source->priv->stations,
				 "sort-order-changed",
				 G_CALLBACK (rb_lastfm_source_songs_view_sort_order_changed_cb),
				 source, 0);
	g_signal_connect_object (source->priv->stations,
				 "show_popup",
				 G_CALLBACK (show_entry_popup),
				 source, 0);
	g_signal_connect_object (source->priv->stations,
				 "drag_data_received",
				 G_CALLBACK (rb_lastfm_source_drag_cb),
				 source, 0);
	g_signal_connect_object (source->priv->stations,
				 "entry-activated",
				 G_CALLBACK (rb_lastfm_source_station_activated_cb),
				 source, 0);
	g_signal_connect_object (source->priv->stations,
				 "selection-changed",
				 G_CALLBACK (rb_lastfm_source_station_selection_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->stations),
			   GTK_DEST_DEFAULT_ALL,
			   lastfm_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	/* tracklist view */
	source->priv->tracks = rb_entry_view_new (source->priv->db,
						  G_OBJECT (source->priv->shell_player),
						  NULL,
						  FALSE, FALSE);
	rb_entry_view_append_column (source->priv->tracks, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->tracks, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->tracks, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->tracks, RB_ENTRY_VIEW_COL_DURATION, FALSE);
	rb_entry_view_set_columns_clickable (source->priv->tracks, FALSE);

	gtk_paned_pack1 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->stations), TRUE, TRUE);
	gtk_paned_pack2 (GTK_PANED (source->priv->paned), GTK_WIDGET (source->priv->tracks), TRUE, TRUE);

	source->priv->main_box = gtk_vbox_new (FALSE, 5);
	gtk_box_pack_start (GTK_BOX (source->priv->main_box), editor_vbox, FALSE, FALSE, 5);
	gtk_box_pack_start (GTK_BOX (source->priv->main_box), source->priv->paned, TRUE, TRUE, 0);
	gtk_container_add (GTK_CONTAINER (source), source->priv->main_box);

	gtk_widget_show_all (GTK_WIDGET (source));

	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "LastfmActions",
								       rb_lastfm_source_actions,
								       G_N_ELEMENTS (rb_lastfm_source_actions),
								       source);

	/* play order */
	source->priv->play_order = rb_lastfm_play_order_new (source->priv->shell_player);

	/* set up station query model */
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      source->priv->station_entry_type,
				      RHYTHMDB_QUERY_END);
	station_query_model = rhythmdb_query_model_new_empty (source->priv->db);
	rhythmdb_do_full_query_parsed (source->priv->db,
				       RHYTHMDB_QUERY_RESULTS (station_query_model),
				       query);

	rhythmdb_query_free (query);

	rb_entry_view_set_model (source->priv->stations, station_query_model);
	g_object_unref (station_query_model);

	source->priv->query_model = rhythmdb_query_model_new_empty (source->priv->db);
	rb_entry_view_set_model (source->priv->tracks, source->priv->query_model);
	
	g_object_set (source, "query-model", source->priv->query_model, NULL);

	return G_OBJECT (source);
}

static void
rb_lastfm_source_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (object);

	switch (prop_id) {
	case PROP_ENTRY_TYPE:
		source->priv->track_entry_type = g_value_get_boxed (value);
		break;
	case PROP_STATION_ENTRY_TYPE:
		source->priv->station_entry_type = g_value_get_boxed (value);
		break;
	case PROP_PROXY_CONFIG:
		source->priv->proxy_config = g_value_get_object (value);
		g_object_ref (G_OBJECT (source->priv->proxy_config));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_lastfm_source_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (object);

	switch (prop_id) {
	case PROP_ENTRY_TYPE:
		g_value_set_boxed (value, source->priv->track_entry_type);
		break;
	case PROP_STATION_ENTRY_TYPE:
		g_value_set_boxed (value, source->priv->station_entry_type);
		break;
	case PROP_PLAY_ORDER:
		g_value_set_object (value, source->priv->play_order);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/* entry data stuff */

static void
destroy_track_data (RhythmDBEntry *entry, gpointer meh)
{
	RBLastfmTrackEntryData *data;

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA(entry, RBLastfmTrackEntryData);
	g_free (data->image_url);
	g_free (data->track_auth);
	g_free (data->download_url);
}


RBSource *
rb_lastfm_source_new (RBPlugin *plugin,
		      RBShell  *shell)
{
	RBSource *source;
	RBProxyConfig *proxy_config;
	RhythmDBEntryType station_entry_type;
	RhythmDBEntryType track_entry_type;
	RhythmDB *db;

	g_object_get (G_OBJECT (shell), "db", &db, NULL);

	/* register entry types if they're not already registered */
	station_entry_type = rhythmdb_entry_type_get_by_name (db, "lastfm-station");
	if (station_entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		station_entry_type = rhythmdb_entry_register_type (db, "lastfm-station");
		station_entry_type->save_to_disk = TRUE;
		station_entry_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc) rb_true_function;
		station_entry_type->sync_metadata = (RhythmDBEntrySyncFunc) rb_null_function;
		station_entry_type->get_playback_uri = (RhythmDBEntryStringFunc) rb_null_function;	/* can't play stations, exactly */
		station_entry_type->category = RHYTHMDB_ENTRY_CONTAINER;
	}

	track_entry_type = rhythmdb_entry_type_get_by_name (db, "lastfm-track");
	if (track_entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		track_entry_type = rhythmdb_entry_register_type (db, "lastfm-track");
		track_entry_type->save_to_disk = FALSE;
		track_entry_type->category = RHYTHMDB_ENTRY_NORMAL;

		track_entry_type->entry_type_data_size = sizeof (RBLastfmTrackEntryData);
		track_entry_type->pre_entry_destroy = destroy_track_data;
	}

	g_object_get (G_OBJECT (shell), "proxy-config", &proxy_config, NULL);

	source = RB_SOURCE (g_object_new (RB_TYPE_LASTFM_SOURCE,
					  "plugin", plugin,
					  "name", _("Last.fm"),
					  "shell", shell,
					  "station-entry-type", station_entry_type,
					  "entry-type", track_entry_type,
					  "proxy-config", proxy_config,
					  "source-group", RB_SOURCE_GROUP_LIBRARY,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, track_entry_type);

	g_object_unref (db);
	g_object_unref (proxy_config);
	return source;
}

static GList*
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("LastfmLoveSong"));
	actions = g_list_prepend (actions, g_strdup ("LastfmBanSong"));

	return actions;
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);

	return source->priv->tracks;
}

static void
set_message_area_text_and_icon (RBLastfmSource *source,
				const char     *icon_stock_id,
				const char     *primary_text,
				const char     *secondary_text)
{
	GtkWidget *hbox_content;
	GtkWidget *image;
	GtkWidget *vbox;
	char      *primary_markup;
	char      *secondary_markup;
	GtkWidget *primary_label;
	GtkWidget *secondary_label;

	hbox_content = gtk_hbox_new (FALSE, 8);
	gtk_widget_show (hbox_content);

	image = gtk_image_new_from_stock (icon_stock_id, GTK_ICON_SIZE_DIALOG);
	gtk_widget_show (image);
	gtk_box_pack_start (GTK_BOX (hbox_content), image, FALSE, FALSE, 0);
	gtk_misc_set_alignment (GTK_MISC (image), 0.5, 0);

	vbox = gtk_vbox_new (FALSE, 6);
	gtk_widget_show (vbox);
	gtk_box_pack_start (GTK_BOX (hbox_content), vbox, TRUE, TRUE, 0);

	primary_markup = g_strdup_printf ("<b>%s</b>", primary_text);
	primary_label = gtk_label_new (primary_markup);
	g_free (primary_markup);
	gtk_widget_show (primary_label);
	gtk_box_pack_start (GTK_BOX (vbox), primary_label, TRUE, TRUE, 0);
	gtk_label_set_use_markup (GTK_LABEL (primary_label), TRUE);
	gtk_label_set_line_wrap (GTK_LABEL (primary_label), TRUE);
	gtk_misc_set_alignment (GTK_MISC (primary_label), 0, 0.5);
	GTK_WIDGET_SET_FLAGS (primary_label, GTK_CAN_FOCUS);
	gtk_label_set_selectable (GTK_LABEL (primary_label), TRUE);

  	if (secondary_text != NULL) {
  		secondary_markup = g_strdup_printf ("<small>%s</small>",
  						    secondary_text);
		secondary_label = gtk_label_new (secondary_markup);
		g_free (secondary_markup);
		gtk_widget_show (secondary_label);
		gtk_box_pack_start (GTK_BOX (vbox), secondary_label, TRUE, TRUE, 0);
		GTK_WIDGET_SET_FLAGS (secondary_label, GTK_CAN_FOCUS);
		gtk_label_set_use_markup (GTK_LABEL (secondary_label), TRUE);
		gtk_label_set_line_wrap (GTK_LABEL (secondary_label), TRUE);
		gtk_label_set_selectable (GTK_LABEL (secondary_label), TRUE);
		gtk_misc_set_alignment (GTK_MISC (secondary_label), 0, 0.5);
	}

	gtk_widget_show (source->priv->message_area);
	gedit_message_area_set_contents (GEDIT_MESSAGE_AREA (source->priv->message_area),
					 hbox_content);
}

static void
set_message_area (RBLastfmSource *source,
		  GtkWidget      *area)
{
	if (source->priv->message_area == area) {
		return;
	}

	if (source->priv->message_area) {
		gtk_widget_destroy (source->priv->message_area);
	}
	source->priv->message_area = area;

	if (area == NULL) {
		return;
	}

	gtk_box_pack_end (GTK_BOX (source->priv->main_box),
			  source->priv->message_area,
			  FALSE, FALSE, 0);
#if 0
	gtk_box_reorder_child (GTK_BOX (source->priv->view_box),
			       source->priv->message_area, 0);
#endif
	g_object_add_weak_pointer (G_OBJECT (source->priv->message_area),
				   (gpointer) &(source->priv->message_area));
}

static void
on_message_area_response (GeditMessageArea *area,
			  int               response_id,
			  RBLastfmSource   *source)
{
	RBPlugin  *plugin;
	GtkWidget *dialog;

	g_object_get (source, "plugin", &plugin, NULL);
	dialog = rb_plugin_create_configure_dialog (plugin);
	g_object_unref (plugin);
}

static void
show_error_message (RBLastfmSource *source,
		    const char     *primary_text,
		    const char     *secondary_text)
{
	GtkWidget *area;

	if (source->priv->message_area != NULL) {
		return;
	}

	area = gedit_message_area_new_with_buttons (_("Account Settings"),
						    GTK_RESPONSE_ACCEPT,
						    NULL);
	set_message_area (source, area);
	set_message_area_text_and_icon (source,
					"gtk-dialog-error",
					primary_text,
					secondary_text);
	g_signal_connect (area,
			  "response",
			  G_CALLBACK (on_message_area_response),
			  source);
}

static void
update_message_area (RBLastfmSource *source)
{
	char *primary_text;
	char *secondary_text;

	primary_text = NULL;
	secondary_text = NULL;

	switch (source->priv->state) {
	case LOGIN_FAILED:
		primary_text = g_strdup (_("Account details are needed before you can connect.  Check your settings."));
		break;

	case BANNED:
		primary_text = g_strdup (_("This version of Rhythmbox has been banned from Last.fm."));
		break;

	case STATION_FAILED:
		primary_text = g_strdup (_("Unable to connect"));
		secondary_text = g_strdup (source->priv->station_failed_reason);
		break;

	case NOT_CONNECTED:
	case CONNECTED:
		set_message_area (source, NULL);
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	if (primary_text != NULL) {
		show_error_message (source, primary_text, secondary_text);
	}
}

static void
impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);
	RhythmDBQueryModel *model;

	switch (source->priv->state) {
	case LOGIN_FAILED:
	case BANNED:
	case STATION_FAILED:
		break;
	case NOT_CONNECTED:
	case CONNECTED:
		g_object_get (asource, "query-model", &model, NULL);
		*text = rhythmdb_query_model_compute_status_normal (model, "%d songs", "%d songs");
		g_object_unref (model);
		break;
	}

	update_message_area (source);

	rb_streaming_source_get_progress (RB_STREAMING_SOURCE (source), progress_text, progress);
	
	/* pulse progressbar if there's something going on */
	if (source->priv->request_outstanding && fabsf (*progress) < EPSILON) {
		*progress_text = g_strdup (source->priv->request_description);
		*progress = -1.0f;
	}
}

static void
impl_delete (RBSource *asource)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);
	GList *sel;
	GList *l;

	/* this one is meant to delete tracks.. but maybe that shouldn't be possible? */

	sel = rb_entry_view_get_selected_entries (source->priv->tracks);
	for (l = sel; l != NULL; l = g_list_next (l)) {
		RhythmDBEntry *track;
		RBLastfmTrackEntryData *track_data;

		track = (RhythmDBEntry *)l->data;
		track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (track, RBLastfmTrackEntryData);

		rhythmdb_entry_delete (source->priv->db, track);
	}
	rhythmdb_commit (source->priv->db);

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
rb_lastfm_source_songs_view_sort_order_changed_cb (RBEntryView *view,
						   RBLastfmSource *source)
{
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (view);
}

static void
rb_lastfm_source_new_station (const char *uri, const char *title, RBLastfmSource *source)
{
	RhythmDBEntry *entry;
	GValue v = {0,};

	rb_debug ("adding lastfm: %s, %s", uri, title);

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
	if (entry) {
		rb_debug ("uri %s already in db", uri);
		return;
	}

	entry = rhythmdb_entry_new (source->priv->db, source->priv->station_entry_type, uri);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, title);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &v);
	g_value_unset (&v);

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 0.0);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_RATING, &v);

	rhythmdb_commit (source->priv->db);
}

/* cover art bits */

static const char *
get_image_url_for_entry (RBLastfmSource *source, RhythmDBEntry *entry)
{
	RBLastfmTrackEntryData *data;

	if (entry == NULL) {
		return NULL;
	}

	if (rhythmdb_entry_get_entry_type (entry) != source->priv->track_entry_type) {
		return NULL;
	}

	data = RHYTHMDB_ENTRY_GET_TYPE_DATA(entry, RBLastfmTrackEntryData);
	return data->image_url;
}

static GValue *
coverart_uri_request (RhythmDB *db, RhythmDBEntry *entry, RBLastfmSource *source)
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
extra_metadata_gather_cb (RhythmDB *db, RhythmDBEntry *entry, RBStringValueMap *map, RBLastfmSource *source)
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
emit_coverart_uri_cb (RBLastfmSource *source)
{
	RhythmDBEntry *entry;
	const char *image_url;

	source->priv->emit_coverart_id = 0;

	entry = rb_shell_player_get_playing_entry (source->priv->shell_player);
	image_url = get_image_url_for_entry (source, entry);
	if (image_url != NULL) {
		GValue v = {0,};
		g_value_init (&v, G_TYPE_STRING);
		g_value_set_string (&v, image_url);
		rhythmdb_emit_entry_extra_metadata_notify (source->priv->db,
							   entry,
							   "rb:coverArt-uri",
							   &v);
		g_value_unset (&v);
	}

	return FALSE;
}

static void
playing_song_changed_cb (RBShellPlayer *player,
			 RhythmDBEntry *entry,
			 RBLastfmSource *source)
{
	GtkAction *action;

	/* re-enable love/ban */
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmLoveSong");
	gtk_action_set_sensitive (action, TRUE);
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmBanSong");
	gtk_action_set_sensitive (action, TRUE);

	if (source->priv->emit_coverart_id != 0) {
		g_source_remove (source->priv->emit_coverart_id);
		source->priv->emit_coverart_id = 0;
	}

	if (entry != NULL && rhythmdb_entry_get_entry_type (entry) == source->priv->track_entry_type) {
		/* look through the playlist for the current station.
		 * if all tracks have been played, update the playlist.
		 */
		RBLastfmTrackEntryData *track_data;

		track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBLastfmTrackEntryData);
		if (track_data->played == FALSE) {

			if (source->priv->current_station != NULL && entry == source->priv->last_entry) {

				GList *sel;
				RhythmDBEntry *selected_station = NULL;
				/* if a new station has been selected, change station before
				 * refreshing the playlist.
				 */
				sel = rb_entry_view_get_selected_entries (source->priv->stations);
				if (sel != NULL) {
					selected_station = (RhythmDBEntry *)sel->data;
					if (selected_station != source->priv->current_station) {
						rb_debug ("changing to station %s",
							  rhythmdb_entry_get_string (selected_station, RHYTHMDB_PROP_LOCATION));
						queue_change_station (source, selected_station);
					}
					queue_get_playlist (source, selected_station);
				} else {
					queue_get_playlist (source, source->priv->current_station);
				}
				g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
				g_list_free (sel);
			}
			track_data->played = TRUE;
		}

		/* emit cover art notification */
		source->priv->emit_coverart_id = g_idle_add ((GSourceFunc) emit_coverart_uri_cb, source);
	}
}

static void
rb_lastfm_source_love_track (GtkAction *run_action, RBLastfmSource *source)
{
	GtkAction *action;

	queue_love_track (source);

	/* disable love/ban */
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmLoveSong");
	gtk_action_set_sensitive (action, FALSE);
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmBanSong");
	gtk_action_set_sensitive (action, FALSE);
}

static void
rb_lastfm_source_ban_track (GtkAction *run_action, RBLastfmSource *source)
{
	GtkAction *action;

	queue_ban_track (source);

	/* disable love/ban */
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmLoveSong");
	gtk_action_set_sensitive (action, FALSE);
	action = gtk_action_group_get_action (source->priv->action_group, "LastfmBanSong");
	gtk_action_set_sensitive (action, FALSE);

	rb_shell_player_do_next (source->priv->shell_player, NULL);
}

static void
rb_lastfm_source_delete_station (GtkAction *run_action, RBLastfmSource *asource)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);
	GList *sel;
	GList *l;

	sel = rb_entry_view_get_selected_entries (source->priv->stations);
	for (l = sel; l != NULL; l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
	}
	rhythmdb_commit (source->priv->db);

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
rb_lastfm_source_download_track (GtkAction *action, RBLastfmSource *source)
{
	/* etc. */
}

static void
rb_lastfm_source_drag_cb (GtkWidget *widget,
			  GdkDragContext *dc,
			  gint x, gint y,
			  GtkSelectionData *selection_data,
			  guint info, guint time,
			  RBLastfmSource *source)
{
	impl_receive_drag (RB_SOURCE (source), selection_data);
}

static gboolean
impl_receive_drag (RBSource *asource, GtkSelectionData *selection_data)
{
	char *uri;
	char *title = NULL;
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);

	uri = (char *)selection_data->data;
	rb_debug ("parsing uri %s", uri);

	if (strstr (uri, "lastfm://") == NULL)
		return FALSE;

	title = rb_lastfm_source_title_from_uri (uri);

	rb_lastfm_source_new_station (uri, title, source);
	return TRUE;
}

static void
rb_lastfm_source_station_activated_cb (RBEntryView *stations, RhythmDBEntry *station, RBLastfmSource *source)
{
	queue_change_station (source, station);
	queue_get_playlist_and_skip (source, station);
}

static void
rb_lastfm_source_station_selection_cb (RBEntryView *stations,
				       RBLastfmSource *source)
{
	GList *sel;
	RhythmDBEntry *selected;

	sel = rb_entry_view_get_selected_entries (stations);
	if (sel == NULL) {
		return;
	}

	selected = (RhythmDBEntry *)sel->data;

	if (source->priv->current_station == selected) {
		rb_debug ("station %s already selected",
			  rhythmdb_entry_get_string (selected, RHYTHMDB_PROP_LOCATION));
	} else {
		rb_debug ("station %s selected",
			  rhythmdb_entry_get_string (selected, RHYTHMDB_PROP_LOCATION));

		/* if this is the first station selected, update the playlist */
		if (source->priv->last_entry == NULL) {
			queue_change_station (source, selected);
			queue_get_playlist (source, selected);
		}
	}
	
	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static char *
rb_lastfm_source_title_from_uri (const char *uri)
{
	char *title = NULL;
	char *unesc_title;
	gchar **data = g_strsplit (uri, "/", 0);

	if (strstr (uri, "globaltags") != NULL)
		title = g_strdup_printf (_("Global Tag %s"), data[3]);

	if (title == NULL && strcmp (data[2], "artist") == 0) {
		/* Check if the station is from an artist page, if not, it is a similar
		 * artist station, and the server should return a name that change_station
		 *  will handle for us.
		 */
		if (data[4] != NULL) {
			if (strcmp (data[4], "similarartists") == 0) {
				title = g_strdup_printf (_("Artists similar to %s"), data[3]);
			} if (strcmp (data[4], "fans") == 0) {
				title = g_strdup_printf (_("Artists liked by fans of %s"), data[3]);
			}
		}

	}

	if (title == NULL && strcmp (data[2], "user") == 0) {
		if (strcmp (data[4], "neighbours") == 0) {
			title = g_strdup_printf (_("%s's Neighbour Radio"), data[3]);
		} else if (strcmp (data[4], "recommended") == 0) {
			title = g_strdup_printf (_("%s's Recommended Radio: %s percent"), data[3], data[5]);
		} else if (strcmp (data[4], "personal") == 0) {
			title = g_strdup_printf (_("%s's Personal Radio"), data[3]);
		} else if (strcmp (data[4], "loved") == 0) {
			title = g_strdup_printf (_("%s's Loved Tracks"), data[3]);
		} else if (strcmp (data[4], "playlist") == 0) {
			title = g_strdup_printf (_("%s's Playlist"), data[3]);
		}
	}

	if (title == NULL && strcmp (data[2], "usertags") == 0) {
		/* Translators: variables are 1: user name, 2: tag name; for user tag radio */
		title = g_strdup_printf (_("%s's %s Radio"), data[3], data[4]);
	}

	if (title == NULL && strcmp(data[2], "group") == 0) {
		title = g_strdup_printf (_("%s Group Radio"), data[3]);
	}

	if (title == NULL) {
		title = g_strstrip (g_strdup (uri));
	}

	g_strfreev (data);
	unesc_title = g_uri_unescape_string (title, NULL);
	g_free (title);
	return unesc_title;
}

static void
rb_lastfm_source_entry_added_cb (RhythmDB *db,
				 RhythmDBEntry *entry,
				 RBLastfmSource *source)
{
	const char *title;
	const char *genre;
	GValue v = {0,};

	if (rhythmdb_entry_get_entry_type (entry) != source->priv->station_entry_type)
		return;

	/* move station name from genre to title */

	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	if (title != NULL && title[0] != '\0')
		return;

	genre = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
	if (genre == NULL || genre[0] == '\0')
		return;

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, genre);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &v);
	g_value_unset (&v);

	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, "");
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_GENRE, &v);
	g_value_unset (&v);

	/* recursive commit?  really? */
	rhythmdb_commit (source->priv->db);
}

static void
rb_lastfm_source_add_station_cb (GtkButton *button, gpointer *data)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (data);
	const gchar *add;
	char *title;
	char *uri;
	int selection;

	add = gtk_entry_get_text (GTK_ENTRY (source->priv->txtbox));
	if (add == NULL || *add == '\0')
		return;

	selection = gtk_combo_box_get_active (GTK_COMBO_BOX (source->priv->typecombo));

	uri = g_strdup_printf(radio_options[selection][1], add);
	title = g_strdup_printf(radio_options[selection][2], add);
	rb_lastfm_source_new_station (uri, title, source);

	gtk_entry_set_text (GTK_ENTRY (source->priv->txtbox), "");

	g_free(uri);
	g_free(title);
}

static void
impl_activate (RBSource *source)
{
	queue_handshake (RB_LASTFM_SOURCE (source));
}

static gboolean
impl_show_popup (RBSource *source)
{
	/*_rb_source_show_popup (source, "/LastfmSourcePopup");*/
	return TRUE;
}

static void
show_entry_popup (RBEntryView *view,
		  gboolean over_entry,
		  RBSource *source)
{
	if (over_entry) {
		_rb_source_show_popup (source, "/LastfmStationViewPopup");
	} else {
		rb_source_show_popup (source);
	}
}

guint
impl_want_uri (RBSource *source, const char *uri)
{
	if (g_str_has_prefix (uri, "lastfm://"))
		return 100;

	return 0;
}

static gboolean
impl_add_uri (RBSource *source, const char *uri, const char *title, const char *genre)
{
	char *name;

	if (strstr (uri, "lastfm://") == NULL)
		return FALSE;

	name = rb_lastfm_source_title_from_uri (uri);

	rb_lastfm_source_new_station (uri, name, RB_LASTFM_SOURCE (source));
	return TRUE;
}

static RBSourceEOFType
impl_handle_eos (RBSource *asource)
{
	return RB_SOURCE_EOF_NEXT;
}

/* request queue */

static void
free_action (RBLastfmAction *action)
{
	if (action->entry != NULL) {
		rhythmdb_entry_unref (action->entry);
	}

	g_free (action);
}

static void
http_response_cb (SoupSession *session, SoupMessage *req, gpointer user_data)
{
	RBLastfmAction *action = (RBLastfmAction *)user_data;
	RBLastfmSource *source = action->source;
	char *free_body;
	const char *body;

	free_body = NULL;
	if (req->response_body->length == 0) {
		rb_debug ("server failed to respond");
		body = NULL;
	} else {
		body = req->response_body->data;
	}

	/* call the action's response handler */
	if (action->handle_response != NULL) {
		(*action->handle_response) (source, body, action->entry);
	}
	g_free (free_body);

	free_action (action);

	source->priv->request_outstanding = FALSE;
	process_queue (source);
}

static void
proxy_config_changed_cb (RBProxyConfig *config,
			 RBLastfmSource *source)
{
	SoupURI *uri;

	if (source->priv->soup_session) {
		uri = rb_proxy_config_get_libsoup_uri (config);
		g_object_set (G_OBJECT (source->priv->soup_session),
					"proxy-uri", uri,
					NULL);
		if (uri)
			soup_uri_free (uri);
	}
}

static void
process_queue (RBLastfmSource *source)
{
	RBLastfmAction *action;
	SoupMessage *msg;

	if (source->priv->request_outstanding) {
		rb_debug ("request already in progress");
		return;
	}

	msg = NULL;
	while (msg == NULL) {
		/* grab an action to perform */
		action = g_queue_pop_head (source->priv->action_queue);
		if (action == NULL) {
			/* ran out */
			break;
		}

		/* create the HTTP request */
		msg = (*action->create_request) (source, action->entry);
		if (msg == NULL) {
			rb_debug ("action didn't want to create a message..");
			free_action (action);
		}

	}

	if (msg == NULL) {
		rb_debug ("request queue is empty");
		return;
	}

	if (source->priv->soup_session == NULL) {
		SoupURI *uri;

		uri = rb_proxy_config_get_libsoup_uri (source->priv->proxy_config);
		source->priv->soup_session = soup_session_async_new_with_options ("proxy-uri", uri, NULL);
		if (uri)
			soup_uri_free (uri);
		
		g_signal_connect_object (G_OBJECT (source->priv->proxy_config),
					 "config-changed",
					 G_CALLBACK (proxy_config_changed_cb),
					 source, 0);
	}


	soup_message_headers_append (msg->request_headers, "User-Agent", USER_AGENT);

	soup_session_queue_message (source->priv->soup_session,
				    msg,
				    http_response_cb,
				    action);
	source->priv->request_outstanding = TRUE;
	source->priv->request_description = action->description;
	
	rb_source_notify_status_changed (RB_SOURCE(source));
}

static void
queue_action (RBLastfmSource *source,
	      CreateRequestFunc create_request,
	      HandleResponseFunc handle_response,
	      RhythmDBEntry *entry,
	      const char *description)
{
	RBLastfmAction *action;

	action = g_new0 (RBLastfmAction, 1);
	action->source = source;		/* hmm, needs a ref? */
	action->create_request = create_request;
	action->handle_response = handle_response;
	action->entry = entry;			/* must already have been ref'd */
	action->description = description;

	g_queue_push_tail (source->priv->action_queue, action);

	process_queue (source);
}


/* common protocol utility stuff */

static char *
auth_challenge (RBLastfmSource *source)
{
	/* um, yeah, having the client generate the auth challenge
	 * seems a bit dumb, unless they've got some replay
	 * protection on the server side..
	 */
	return g_strdup_printf ("%ld", time (NULL));
}

static gchar *
mkmd5 (char *string, char *string2)
{
	GChecksum *checksum;
	gchar *md5_result;
	
	checksum = g_checksum_new(G_CHECKSUM_MD5);	
	g_checksum_update(checksum, (guchar *)string, -1);
	
	if (string2 != NULL) {
		g_checksum_update(checksum, (guchar *)string2, -1);		
	}
	
	md5_result = g_strdup(g_checksum_get_string(checksum));
	g_checksum_free(checksum);
	
	return (md5_result);
}

static gboolean
station_is_subscriber_only (const char *uri)
{
	/* loved-tracks radio */
	if (g_str_has_prefix (uri, "lastfm://user/") &&
	    g_str_has_suffix (uri, "/loved")) {
		return TRUE;
	}

	/* user tag radio */
	if (g_str_has_prefix (uri, "lastfm://usertags/"))
		return TRUE;

	/* anything else? */

	return FALSE;
}

/* handshake request */

static SoupMessage *
create_handshake_request (RBLastfmSource *source, RhythmDBEntry *entry)
{
	SoupMessage *req;
	char *password;
	char *username;
	char *md5password;
	char *handshake_url;
	
	switch (source->priv->state) {
	case NOT_CONNECTED:
		rb_debug ("logging in");
		break;

	case CONNECTED:
		rb_debug ("already logged in");
		return NULL;

	default:
		rb_debug ("can't log in: %s",
			  state_name[source->priv->state]);
		return NULL;
	}

	username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	if (username == NULL) {
		rb_debug ("no last.fm username");
		source->priv->state = LOGIN_FAILED;
		return NULL;
	}

	password = eel_gconf_get_string (CONF_AUDIOSCROBBLER_PASSWORD);
	if (password == NULL) {
		rb_debug ("no last.fm password");
		source->priv->state = LOGIN_FAILED;
		return NULL;
	}

	md5password = mkmd5 (password, NULL);
	g_free (password);

	handshake_url = g_strdup_printf ("http://%s/radio/handshake.php?"
					 "version=" RB_LASTFM_VERSION "&"
					 "platform=" RB_LASTFM_PLATFORM "&"
					 "username=%s&"
					 "passwordmd5=%s&"
					 "debug=0&"
					 "partner=",
					 LASTFM_URL,
					 username,
					 md5password);
	g_free (username);
	g_free (md5password);

	req = soup_message_new ("GET", handshake_url);
	g_free (handshake_url);
	return req;
}

static void
_subscriber_station_visibility_cb (RhythmDBEntry *entry, RBLastfmSource *source)
{
	gboolean hidden;
	const char *uri;
	GValue v = {0,};

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	if (source->priv->subscriber) {
		hidden = FALSE;
	} else {
		hidden = station_is_subscriber_only (uri);
	}

	g_value_init (&v, G_TYPE_BOOLEAN);
	g_value_set_boolean (&v, hidden);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_HIDDEN, &v);
	g_value_unset (&v);
}

static void
handle_handshake_response (RBLastfmSource *source, const char *body, RhythmDBEntry *entry)
{
	char *username;
	char **pieces;
	int i;

	if (body == NULL) {
		rb_debug ("login failed: no response");
		source->priv->state = NOT_CONNECTED;
		return;
	}

	rb_debug ("response body: %s", body);

	pieces = g_strsplit (body, "\n", 0);
	for (i = 0; pieces[i] != NULL; i++) {
		gchar **values = g_strsplit (pieces[i], "=", 2);
		if (values[0] == NULL) {
			rb_debug ("unexpected response content: %s", pieces[i]);
		} else if (strcmp (values[0], "session") == 0) {
			if (strcmp (values[1], "FAILED") == 0) {
				source->priv->state = LOGIN_FAILED;
				rb_debug ("login failed");
			} else {
				source->priv->state = CONNECTED;
				g_free (source->priv->session_id);
				source->priv->session_id = g_strdup (values[1]);
				rb_debug ("session ID: %s", source->priv->session_id);
			}
		} else if (strcmp (values[0], "stream_url") == 0) {
			/* don't really care about the stream url now */
			/*source->priv->stream_url = g_strdup (values[1]);*/
			rb_debug ("stream url: %s", values[1]);
		} else if (strcmp (values[0], "subscriber") == 0) {
			if (strcmp (values[1], "0") == 0) {
				source->priv->subscriber = FALSE;
			} else {
				source->priv->subscriber = TRUE;
			}
		} else if (strcmp (values[0], "base_url") ==0) {
			source->priv->base_url = g_strdup (values[1]);
		} else if (strcmp (values[0], "base_path") ==0) {
			source->priv->base_path = g_strdup (values[1]);
		} else if (strcmp (values[0], "banned") ==0) {
			if (strcmp (values[1], "0") != 0) {
				source->priv->state = BANNED;
			}
		}

		g_strfreev (values);
	}

	g_strfreev (pieces);

	if (source->priv->state != CONNECTED) {
		return;
	}

	/* create default stations */
	username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	if (username != NULL) {
		char *uri;
		RhythmDBEntry *entry;

		/* neighbour radio */
		uri = g_strdup_printf ("lastfm://user/%s/neighbours", username);
		entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
		if (entry == NULL) {
			rb_lastfm_source_new_station (uri, _("Neighbour Radio"), RB_LASTFM_SOURCE (source));
		}
		g_free (uri);

		/* personal radio (subscriber only) */
		uri = g_strdup_printf ("lastfm://user/%s/personal", username);
		entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
		if (entry == NULL) {
			rb_lastfm_source_new_station (uri, _("Personal Radio"), RB_LASTFM_SOURCE (source));
		}
		g_free (uri);

		g_free (username);
	}

	/* update subscriber-only station visibility */
	rhythmdb_entry_foreach_by_type (source->priv->db,
					source->priv->station_entry_type,
					(GFunc) _subscriber_station_visibility_cb,
					source);
	rhythmdb_commit (source->priv->db);
}

static void
queue_handshake (RBLastfmSource *source)
{
	queue_action (source,
		      create_handshake_request,
		      handle_handshake_response,
		      NULL,
		      _("Logging in"));
}

/* change station */

static SoupMessage *
create_station_request (RBLastfmSource *source, RhythmDBEntry *entry)
{
	SoupMessage *req;
	char *url;
	char *lastfm_url;
	
	if (source->priv->state != CONNECTED &&
	    source->priv->state != STATION_FAILED) {
		rb_debug ("can't change station: %s",
			  state_name[source->priv->state]);
		return NULL;
	}

	if (source->priv->current_station == entry) {
		rb_debug ("already on station %s",
			  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
		return NULL;
	}

	lastfm_url = g_uri_escape_string (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
					  G_URI_RESERVED_CHARS_ALLOWED_IN_PATH,
					  FALSE);

	url = g_strdup_printf("http://%s%s/adjust.php?session=%s&url=%s&debug=0",
			      source->priv->base_url ? source->priv->base_url : LASTFM_URL,
			      source->priv->base_path,
			      source->priv->session_id,
			      lastfm_url);
	rb_debug ("change station request: %s", url);

	req = soup_message_new ("GET", url);
	g_free (url);
	g_free (lastfm_url);
	return req;
}

static void
set_station_failed_reason (RBLastfmSource *source, RhythmDBEntry *station, const char *reason)
{
	GValue v = {0,};

	/* set playback error on the station entry */
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, reason);
	rhythmdb_entry_set (source->priv->db, station, RHYTHMDB_PROP_PLAYBACK_ERROR, &v);
	g_value_unset (&v);

	/* set our status */
	source->priv->state = STATION_FAILED;
	source->priv->station_failed_reason = reason;

	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
handle_station_response (RBLastfmSource *source, const char *body, RhythmDBEntry *entry)
{
	char **pieces;
	int i;

	if (body == NULL) {
		rb_debug ("couldn't change session: no response");
		set_station_failed_reason (source, entry, _("Server did not respond"));	/* crap message */
		return;
	}

	rb_debug ("response body: %s", body);

	pieces = g_strsplit (body, "\n", 0);
	for (i = 0; pieces[i] != NULL; i++) {
		gchar **values = g_strsplit (pieces[i], "=", 2);

		if (values[0] == NULL) {
			rb_debug ("unexpected response content: %s", pieces[i]);
		} else if (strcmp (values[0], "response") == 0) {
			if (source->priv->current_station != NULL) {
				rhythmdb_entry_unref (source->priv->current_station);
				source->priv->current_station = NULL;
			}

			if (strcmp (values[1], "OK") == 0) {
				RhythmDBEntry *playing_entry;
				GtkTreeIter iter;
				GList *remove = NULL;
				GList *i;

				source->priv->state = CONNECTED;

				source->priv->current_station = rhythmdb_entry_ref (entry);

				/* remove existing unplayed entries, as they
				 * will have been invalidated when we switched away from it.
				 * (it sort of seems like this is no longer true, actually)
				 */
				playing_entry = rb_shell_player_get_playing_entry (source->priv->shell_player);
				if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->query_model), &iter)) {
					do {
						RhythmDBEntry *track;
						track = rhythmdb_query_model_iter_to_entry (source->priv->query_model, &iter);
						if (track == playing_entry) {
							rhythmdb_entry_unref (track);
						} else if (track != NULL) {
							remove = g_list_prepend (remove, track);
						}
					} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->query_model), &iter));
				}

				for (i = remove; i != NULL; i = i->next) {
					RhythmDBEntry *track;
					RBLastfmTrackEntryData *track_data;

					track = (RhythmDBEntry *)i->data;
					track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (track, RBLastfmTrackEntryData);

					rhythmdb_entry_delete (source->priv->db, track);
					rhythmdb_entry_unref (track);
				}
				rhythmdb_commit (source->priv->db);
			}
		} else if (strcmp (values[0], "error") == 0) {
			int errorcode;

			errorcode = strtoul (values[1], NULL, 0);
			switch (errorcode) {
			case 1:		/* not enough content */
			case 2:		/* not enough members in group */
			case 3:		/* not enough fans of artist */
			case 4:		/* unavailable */
			case 6:		/* too few neighbours */
				set_station_failed_reason (source, entry,
					_("There is not enough content available to play this station."));
				break;

			case 5:		/* subscriber only */
				set_station_failed_reason (source, entry,
					_("This station is available to subscribers only."));
				break;

			case 7:
			case 8:
			default:
				set_station_failed_reason (source, entry,
					_("The streaming system is offline for maintenance, please try again later."));
				break;
			}

		} else if (strcmp (values[0], "url") == 0) {
			/* might have some use for this, I guess? */
		} else if (strcmp (values[0], "stationname") == 0) {
			/* um, might want to use this stuff at some point, I guess
			gchar **data = g_strsplit (g_strdown(pieces[i - 1]), "=",2);
			RhythmDBEntry *entry;
			GValue titlestring = {0,};

			rb_debug ("Received station name from server: %s", values[1]);
			entry = rhythmdb_entry_lookup_by_location (source->priv->db, data[1]);
			g_value_init (&titlestring, G_TYPE_STRING);
			g_value_set_string (&titlestring, values[1]);

			if (entry == NULL) {
				entry = rhythmdb_entry_new (source->priv->db, source->priv->station_entry_type, data[1]);
			}
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &titlestring);
			g_value_unset (&titlestring);
			rhythmdb_commit (source->priv->db);
			*/
		}

		g_strfreev (values);
	}

	g_strfreev (pieces);
}

static void
queue_change_station (RBLastfmSource *source, RhythmDBEntry *station)
{
	queue_action (source,
		      create_station_request,
		      handle_station_response,
		      rhythmdb_entry_ref (station),
		      _("Changing station"));
}

/* get playlist */

static SoupMessage *
create_playlist_request (RBLastfmSource *source, RhythmDBEntry *entry)
{
	SoupMessage *req;
	char *xspf_url;
	
	if (source->priv->state != CONNECTED &&
	    source->priv->state != STATION_FAILED) {
		rb_debug ("can't get playlist: %s",
			  state_name[source->priv->state]);
		return NULL;
	}

	if (source->priv->current_station != entry) {
		rb_debug ("can't get playlist: station not selected");
		return NULL;
	}

	xspf_url = g_strdup_printf ("http://%s%s/xspf.php?sk=%s&discovery=0&desktop=%s",
				    source->priv->base_url ? source->priv->base_url : LASTFM_URL,
				    source->priv->base_path,
				    source->priv->session_id,
				    RB_LASTFM_VERSION);

	rb_debug ("playlist request: %s", xspf_url);
	req = soup_message_new ("GET", xspf_url);
	g_free (xspf_url);

	return req;
}

static void
xspf_entry_parsed (TotemPlParser *parser, const char *uri, GHashTable *metadata, RBLastfmSource *source)
{
	RhythmDBEntry *track_entry;
	RBLastfmTrackEntryData *track_data;
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

	/* create db entry if it doesn't already exist */
	track_entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
	if (track_entry == NULL) {
		rb_debug ("creating new track entry for %s", uri);
		track_entry = rhythmdb_entry_new (source->priv->db, 
						  source->priv->track_entry_type,
						  uri);
	} else {
		rb_debug ("track entry %s already exists", uri);
	}
	track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (track_entry, RBLastfmTrackEntryData);

	/* straightforward string copying */
	for (i = 0; i < G_N_ELEMENTS (field_mapping); i++) {
		value = g_hash_table_lookup (metadata, field_mapping[i].field);
		if (value != NULL) {
			g_value_init (&v, G_TYPE_STRING);
			g_value_set_string (&v, value);
			rhythmdb_entry_set (source->priv->db, track_entry, field_mapping[i].prop, &v);
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
			rhythmdb_entry_set (source->priv->db, track_entry, RHYTHMDB_PROP_DURATION, &v);
			g_value_unset (&v);
		}
	}

	/* image URL and track auth ID are stored in entry type specific data */
	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_IMAGE_URL);
	if (value != NULL && (strcmp (value, LASTFM_NO_COVER_IMAGE) != 0)) {
		track_data->image_url = g_strdup (value);
	}

	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_ID);
	if (value != NULL) {
		track_data->track_auth = g_strdup (value);
	}

	value = g_hash_table_lookup (metadata, TOTEM_PL_PARSER_FIELD_DOWNLOAD_URL);
	if (value != NULL) {
		track_data->download_url = g_strdup (value);
		rb_debug ("track %s has a download url: %s", uri, track_data->download_url);
	}

	/* what happens if it's already in there? need to use move_entry instead? */
	rhythmdb_query_model_add_entry (source->priv->query_model, track_entry, -1);

	source->priv->last_entry = track_entry;
}

static gboolean
handle_playlist_response (RBLastfmSource *source, const char *body, RhythmDBEntry *station)
{
	int tmp_fd;
	char *tmp_name;
	char *tmp_uri = NULL;
	GIOChannel *channel = NULL;
	TotemPlParser *parser = NULL;
	TotemPlParserResult result;
	GError *error = NULL;
	gboolean ret = FALSE;
	time_t now;
	GValue value = {0,};

	if (body == NULL) {
		rb_debug ("didn't get a response");
		return FALSE;
	}
	
	/* until totem-pl-parser can parse playlists from in-memory data, we save it to a
	 * temporary file.
	 */

	tmp_fd = g_file_open_tmp ("rb-lastfm-playlist-XXXXXX.xspf", &tmp_name, &error);
	if (error != NULL) {
		rb_debug ("unable to save playlist: %s", error->message);
		goto cleanup;
	}

	channel = g_io_channel_unix_new (tmp_fd);
	g_io_channel_write_chars (channel, body, strlen (body), NULL, &error);
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
	case TOTEM_PL_PARSER_RESULT_UNHANDLED:
	case TOTEM_PL_PARSER_RESULT_IGNORED:
	case TOTEM_PL_PARSER_RESULT_ERROR:
		rb_debug ("playlist didn't parse");
		break;

	case TOTEM_PL_PARSER_RESULT_SUCCESS:
		/* update the station's last played time */
		g_value_init (&value, G_TYPE_ULONG);
		time (&now);
		g_value_set_ulong (&value, now);
		rhythmdb_entry_set (source->priv->db,
				    source->priv->current_station,
				    RHYTHMDB_PROP_LAST_PLAYED,
				    &value);
		g_value_unset (&value);

		rhythmdb_commit (source->priv->db);
		
		ret = TRUE;
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
	return ret;
}

static void
handle_playlist_response_and_skip (RBLastfmSource *source, const char *body, RhythmDBEntry *station)
{
	if (handle_playlist_response (source, body, station)) {
		/* ignore errors, sadly */
		rb_shell_player_do_next (source->priv->shell_player, NULL);
	}
}


static void
queue_get_playlist (RBLastfmSource *source, RhythmDBEntry *station)
{
	queue_action (source,
		      create_playlist_request,
		      (HandleResponseFunc) handle_playlist_response,
		      rhythmdb_entry_ref (station),
		      _("Retrieving playlist"));
}

static void
queue_get_playlist_and_skip (RBLastfmSource *source, RhythmDBEntry *station)
{
	queue_action (source,
		      create_playlist_request,
		      handle_playlist_response_and_skip,
		      rhythmdb_entry_ref (station),
		      _("Retrieving playlist"));
}

/* XMLRPC requests */

static SoupMessage *
create_action_request (RBLastfmSource *source, RhythmDBEntry *entry, const char *action)
{
	SoupMessage *req;
	char *url;
	char *username;
	char *password;
	char *md5password;
	char *challenge;
	char *md5challenge;

	if (source->priv->state != CONNECTED) {
		rb_debug ("can't perform %s action: %s",
			  action, state_name[source->priv->state]);
		return NULL;
	}
	
	username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	if (username == NULL) {
		rb_debug ("no last.fm username");
		return NULL;
	}

	password = eel_gconf_get_string (CONF_AUDIOSCROBBLER_PASSWORD);
	if (password == NULL) {
		rb_debug ("no last.fm password");
		return NULL;
	}
	md5password = mkmd5 (password, NULL);

	challenge = auth_challenge (source);
	md5challenge = mkmd5 (md5password, challenge);

	url = g_strdup_printf ("http://%s/1.0/rw/xmlrpc.php",
			       source->priv->base_url ? source->priv->base_url : LASTFM_URL);

	req = soup_xmlrpc_request_new (url, action,
				       G_TYPE_STRING, username,
				       G_TYPE_STRING, challenge,
				       G_TYPE_STRING, md5challenge,
				       G_TYPE_STRING, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
				       G_TYPE_STRING, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
				       G_TYPE_INVALID);

	g_free (username);
	g_free (password);
	g_free (md5password);
	g_free (md5challenge);
	g_free (url);
	return req;
}

static void
handle_xmlrpc_response (RBLastfmSource *source, const char *body, RhythmDBEntry *entry)
{
	GError *error = NULL;
	GValue v = {0,};

	if (body == NULL) {
		rb_debug ("didn't get a response to an xmlrpc request");
		return;
	}

	soup_xmlrpc_parse_method_response (body, strlen (body), &v, &error);
	if (error != NULL) {
		rb_debug ("got error in xmlrpc response: %s", error->message);
		g_error_free (error);
	}

	/* do something with the return value? */

	g_value_unset (&v);
}

/* XMLRPC: banTrack */

static SoupMessage *
create_ban_request (RBLastfmSource *source, RhythmDBEntry *track)
{
	return create_action_request (source, track, "banTrack");
}

static void
queue_ban_track (RBLastfmSource *source)
{
	queue_action (source,
		      create_ban_request,
		      handle_xmlrpc_response,
		      rb_shell_player_get_playing_entry (source->priv->shell_player),
		      _("Banning song"));
}

/* XMLRPC: loveTrack */

static SoupMessage *
create_love_request (RBLastfmSource *source, RhythmDBEntry *track)
{
	return create_action_request (source, track, "loveTrack");
}

static void
queue_love_track (RBLastfmSource *source)
{
	queue_action (source,
		      create_love_request,
		      handle_xmlrpc_response,
		      rb_shell_player_get_playing_entry (source->priv->shell_player),
		      _("Adding song to your Loved tracks"));		/* ugh */
}

/* and maybe some more */

