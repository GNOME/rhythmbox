/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of last.fm station source object
 *
 *  Copyright (C) 2006 Matt Novenstern <fisxoj@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

/*  The author would like to extend thanks to Ian Holmes, author of Last Exit,
 *   an alternative last.fm player written in C#, the code of which was
 *   extraordinarily useful in the creation of this code
 */

/* TODO List
 * - if subscriber, make user radio (low priority)
 * - "recommendation radio" with percentage setting (0=obscure, 100=popular)
 * - watch username gconf entries, create/update neighbour station
*/


#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>

#include <gconf/gconf-value.h>

#include <libsoup/soup.h>
#include <libsoup/soup-uri.h>

#include "md5.h"

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
#include "rb-station-properties-dialog.h"
#include "rb-new-station-dialog.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-shell-player.h"

#define LASTFM_URL "http://ws.audioscrobbler.com"
#define PLATFORM_STRING "linux"
#define RB_LASTFM_VERSION "1.1.1"
#define EXTRA_URI_ENCODE_CHARS	"&+"


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
static void rb_lastfm_source_do_query (RBLastfmSource *source);

/* source-specific methods */
static void rb_lastfm_source_do_handshake (RBLastfmSource *source);
static char* rb_lastfm_source_get_playback_uri (RhythmDBEntry *entry, gpointer data);
static void rb_lastfm_perform (RBLastfmSource *lastfm,
			       const char *url,
			       char *post_data, /* this takes ownership */
			       SoupMessageCallbackFn response_handler);
static void rb_lastfm_message_cb (SoupMessage *req, gpointer user_data);
static void rb_lastfm_change_station (RBLastfmSource *source, const char *station);

static void rb_lastfm_proxy_config_changed_cb (RBProxyConfig *config,
					       RBLastfmSource *source);
static void rb_lastfm_source_drag_cb (GtkWidget *widget,
				      GdkDragContext *dc,
				      gint x, gint y,
				      GtkSelectionData *selection_data,
				      guint info, guint time,
				      RBLastfmSource *source);

static void rb_lastfm_source_dispose (GObject *object);

/* RBSource implementation methods */
static void impl_delete (RBSource *asource);
static GList *impl_get_ui_actions (RBSource *source);
static RBEntryView *impl_get_entry_view (RBSource *asource);
static void impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress);
static gboolean impl_receive_drag (RBSource *source, GtkSelectionData *data);
static void impl_activate (RBSource *source);

static void rb_lastfm_source_new_station (char *uri, char *title, RBLastfmSource *source);
static void rb_lastfm_source_skip_track (GtkAction *action, RBLastfmSource *source);
static void rb_lastfm_source_love_track (GtkAction *action, RBLastfmSource *source);
static void rb_lastfm_source_ban_track (GtkAction *action, RBLastfmSource *source);
static char *rb_lastfm_source_title_from_uri (char *uri);
static void rb_lastfm_source_add_station_cb (GtkButton *button, gpointer *data);
static void rb_lastfm_source_entry_added_cb (RhythmDB *db, RhythmDBEntry *entry, RBLastfmSource *source);

static void rb_lastfm_source_new_song_cb (GObject *player_backend, gpointer data, RBLastfmSource *source);
static void rb_lastfm_song_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBLastfmSource *source);

#ifdef HAVE_GSTREAMER_0_10
/* can't be bothered creating a whole header file just for this: */
GType rb_lastfm_src_get_type (void);
#endif

struct RBLastfmSourcePrivate
{
	GtkWidget *vbox;
	GtkWidget *paned;
	GtkWidget *vbox2;
	GtkWidget *hbox;
	/*GtkWidget *tuner;*/
	GtkWidget *txtbox;
	GtkWidget *gobutton;
	GtkWidget *typecombo;
	GtkWidget *label;
	RhythmDB *db;

	GtkActionGroup *action_group;

	RBEntryView *stations;

	RBShellPlayer *shell_player;
	RhythmDBEntryType entry_type;
	char *session;

	gboolean subscriber;
	char *base_url;
	char *base_path;
	char *stream_url;
	gboolean framehack;
	char *update_url;
	gboolean banned;
	gboolean connected;

	/*RhythmDBEntry *pending_entry;*/

	enum {
		OK = 0,
		COMMUNICATING,
		FAILED,
		NO_ARTIST,
		BANNED
	} status;

	SoupSession *soup_session;
	RBProxyConfig *proxy_config;
};

G_DEFINE_TYPE (RBLastfmSource, rb_lastfm_source, RB_TYPE_STREAMING_SOURCE);
#define RB_LASTFM_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LASTFM_SOURCE, RBLastfmSourcePrivate))

enum
{
	PROP_0,
	PROP_ENTRY_TYPE,
	PROP_PROXY_CONFIG
};

static GtkActionEntry rb_lastfm_source_actions [] =
{
	{ "LastfmSkipSong", GTK_STOCK_MEDIA_FORWARD, N_("Next Song"), NULL,
	  N_("Skip the current track"),
	  G_CALLBACK (rb_lastfm_source_skip_track) },
	{ "LastfmLoveSong", GTK_STOCK_ADD, N_("Love Song"), NULL,
	  N_("Mark this song as loved"),
	  G_CALLBACK (rb_lastfm_source_love_track) },
	{ "LastfmBanSong", GTK_STOCK_CANCEL, N_("Ban Song"), NULL,
	  N_("Ban the current track from being played again"),
	  G_CALLBACK (rb_lastfm_source_ban_track) }
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

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
							     "Entry type",
							     "Type of the entries which should be displayed by this source",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PROXY_CONFIG,
					 g_param_spec_object ("proxy-config",
							      "RBProxyConfig",
							      "RBProxyConfig object",
							      RB_TYPE_PROXY_CONFIG,
							      G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY));
	g_type_class_add_private (klass, sizeof (RBLastfmSourcePrivate));

#ifdef HAVE_GSTREAMER_0_10
	rb_lastfm_src_get_type ();
#endif
}

static void
rb_lastfm_source_init (RBLastfmSource *source)
{
	gint size;
	GdkPixbuf *pixbuf;

	source->priv = RB_LASTFM_SOURCE_GET_PRIVATE (source);

	source->priv->vbox = gtk_vbox_new (FALSE, 5);

	gtk_container_add (GTK_CONTAINER (source), source->priv->vbox);

	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   "stock_channel",
					   size,
					   0, NULL);
	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
}

static void
rb_lastfm_source_finalize (GObject *object)
{
	RBLastfmSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LASTFM_SOURCE (object));

	source = RB_LASTFM_SOURCE (object);

	g_return_if_fail (source->priv != NULL);

	rb_debug ("finalizing lastfm source");

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	g_object_unref (G_OBJECT (source->priv->proxy_config));

	G_OBJECT_CLASS (rb_lastfm_source_parent_class)->finalize (object);
}

static GObject *
rb_lastfm_source_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBLastfmSource *source;
	RBLastfmSourceClass *klass;
	RBShell *shell;
	GObject *player_backend;

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

	/* Set up station tuner */
	/*source->priv->tuner = gtk_vbox_new (FALSE, 5); */
	source->priv->vbox2 = gtk_vbox_new (FALSE, 5);
	source->priv->hbox = gtk_hbox_new (FALSE, 5);

	source->priv->label = gtk_label_new (_("Enter the artist or global tag to build a radio station out of:"));
	g_object_set (source->priv->label, "xalign", 0.0, NULL);

	source->priv->gobutton = gtk_button_new_with_label (_("Add"));
	g_signal_connect_object (G_OBJECT (source->priv->gobutton),
				 "clicked",
				 G_CALLBACK (rb_lastfm_source_add_station_cb),
				 source, 0);
	source->priv->typecombo = gtk_combo_box_new_text ();
	gtk_combo_box_append_text (GTK_COMBO_BOX (source->priv->typecombo), _("Artist"));
	gtk_combo_box_append_text (GTK_COMBO_BOX (source->priv->typecombo), _("Tag"));
	gtk_combo_box_set_active (GTK_COMBO_BOX (source->priv->typecombo), 0);

	source->priv->txtbox = gtk_entry_new ();

	gtk_box_pack_end_defaults (GTK_BOX (source->priv->hbox),
								 GTK_WIDGET (source->priv->gobutton));

	gtk_box_pack_end_defaults (GTK_BOX (source->priv->hbox),
								 GTK_WIDGET (source->priv->txtbox));

	gtk_box_pack_start_defaults (GTK_BOX (source->priv->hbox),
								 GTK_WIDGET (source->priv->typecombo));

	gtk_box_pack_end_defaults (GTK_BOX (source->priv->vbox2),
								 GTK_WIDGET (source->priv->hbox));

	gtk_box_pack_end_defaults (GTK_BOX (source->priv->vbox2),
								 GTK_WIDGET (source->priv->label));

	/* set up stations view */
	source->priv->stations = rb_entry_view_new (source->priv->db,
						    G_OBJECT (source->priv->shell_player),
						    NULL,
						    FALSE, FALSE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_RATING, TRUE);
	rb_entry_view_append_column (source->priv->stations, RB_ENTRY_VIEW_COL_LAST_PLAYED, TRUE);
	g_signal_connect_object (G_OBJECT (source->priv->stations),
				 "sort-order-changed",
				 G_CALLBACK (rb_lastfm_source_songs_view_sort_order_changed_cb),
				 source, 0);

	/* Drag and drop URIs */
	g_signal_connect_object (G_OBJECT (source->priv->stations),
				 "drag_data_received",
				 G_CALLBACK (rb_lastfm_source_drag_cb),
				 source, 0);
	g_signal_connect_object (G_OBJECT (source->priv->shell_player),
				 "playing-song-changed",
				 G_CALLBACK (rb_lastfm_song_changed_cb),
				 source, 0);

	gtk_drag_dest_set (GTK_WIDGET (source->priv->stations),
			   GTK_DEST_DEFAULT_ALL,
			   lastfm_drag_types, 2,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);

	/* Pack the vbox */
	/*gtk_paned_pack1 (GTK_PANED (source->priv->paned),
					 GTK_WIDGET (source->priv->tuner), FALSE, FALSE); */

	/*source->priv->paned = gtk_vpaned_new ();
	gtk_paned_pack2 (GTK_PANED (source->priv->paned),
			 GTK_WIDGET (source->priv->stations), TRUE, FALSE); */

	gtk_box_pack_start (GTK_BOX (source->priv->vbox), GTK_WIDGET (source->priv->vbox2), FALSE, FALSE, 5);
	gtk_box_pack_start_defaults (GTK_BOX (source->priv->vbox), GTK_WIDGET (source->priv->stations));


	gtk_widget_show_all (GTK_WIDGET (source));


	source->priv->action_group = _rb_source_register_action_group (RB_SOURCE (source),
								       "LastfmActions",
								       rb_lastfm_source_actions,
								       G_N_ELEMENTS (rb_lastfm_source_actions),
								       source);

	rb_lastfm_source_do_query (source);

	g_object_get (source->priv->shell_player, "player", &player_backend, NULL);
	g_signal_connect_object (player_backend,
				 "event::rb-lastfm-new-song",
				 G_CALLBACK (rb_lastfm_source_new_song_cb),
				 source,
				 0);

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
		source->priv->entry_type = g_value_get_boxed (value);
		break;
	case PROP_PROXY_CONFIG:
		source->priv->proxy_config = g_value_get_object (value);
		g_object_ref (G_OBJECT (source->priv->proxy_config));
		g_signal_connect_object (G_OBJECT (source->priv->proxy_config),
					 "config-changed",
					 G_CALLBACK (rb_lastfm_proxy_config_changed_cb),
					 source, 0);
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
		g_value_set_boxed (value, source->priv->entry_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static gchar *
mkmd5 (char *string)
{
	md5_state_t md5state;
	guchar md5pword[16];
	gchar md5_response[33];

	int j = 0;

	memset (md5_response, 0, sizeof (md5_response));

	md5_init (&md5state);
	md5_append (&md5state, (unsigned char*)string, strlen (string));
	md5_finish (&md5state, md5pword);

	for (j = 0; j < 16; j++) {
		char a[3];
		sprintf (a, "%02x", md5pword[j]);
		md5_response[2*j] = a[0];
		md5_response[2*j+1] = a[1];
	}

	return (g_strdup (md5_response));
}

RBSource *
rb_lastfm_source_new (RBShell *shell)
{
	RBSource *source;
	RBProxyConfig *proxy_config;
	RhythmDBEntryType entry_type;
	char *uri;
	RhythmDB *db;
	char *username;

	g_object_get (G_OBJECT (shell), "db", &db, NULL);

	/* register entry type if it's not already registered */
	entry_type = rhythmdb_entry_type_get_by_name (db, "lastfm-station");
	if (entry_type == RHYTHMDB_ENTRY_TYPE_INVALID) {
		entry_type = rhythmdb_entry_register_type (db, "lastfm-station");
		entry_type->save_to_disk = TRUE;
		entry_type->can_sync_metadata = (RhythmDBEntryCanSyncFunc) rb_true_function;
		entry_type->sync_metadata = (RhythmDBEntrySyncFunc) rb_null_function;
		entry_type->get_playback_uri = (RhythmDBEntryStringFunc) rb_lastfm_source_get_playback_uri;
	}

	g_object_get (G_OBJECT (shell), "proxy-config", &proxy_config, NULL);

	source = RB_SOURCE (g_object_new (RB_TYPE_LASTFM_SOURCE,
					  "name", _("Last.fm"),
					  "shell", shell,
					  "entry-type", entry_type,
					  "proxy-config", proxy_config,
					  NULL));
	rb_shell_register_entry_type_for_source (shell, source, entry_type);

	entry_type->get_playback_uri_data = source;

	/* create default neighbour radio station */
	username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	if (username != NULL) {
		RhythmDBEntry *entry;

		uri = g_strdup_printf ("lastfm://user/%s/neighbours", username);
		entry = rhythmdb_entry_lookup_by_location (db, uri);
		if (entry == NULL) {
			rb_lastfm_source_new_station (uri, _("Neighbour Radio"), RB_LASTFM_SOURCE (source));
		} else {
			rhythmdb_entry_unref (entry);
		}
		g_free (uri);
		g_free (username);
	}

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
	actions = g_list_prepend (actions, g_strdup ("LastfmSkipSong"));

	return actions;
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);

	return source->priv->stations;
}

static void
impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);

	switch (source->priv->status) {
	case NO_ARTIST:
		*text = g_strdup (_("No such artist.  Check your spelling"));
		break;

	case FAILED:
		*text = g_strdup (_("Handshake failed"));
		break;

	case BANNED:
		*text = g_strdup (_("The server marked you as banned"));
		break;

	case COMMUNICATING:
	case OK:
		{
			RhythmDBQueryModel *model;
			guint num_entries;

			g_object_get (asource, "query-model", &model, NULL);
			num_entries = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
			g_object_unref (model);

			*text = g_strdup_printf (ngettext ("%d station", "%d stations", num_entries), num_entries);
			break;
		}
	}
	rb_streaming_source_get_progress (RB_STREAMING_SOURCE (source), progress_text, progress);
}

static void
impl_delete (RBSource *asource)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (asource);
	GList *l;

	for (l = rb_entry_view_get_selected_entries (source->priv->stations); l != NULL; l = g_list_next (l)) {
		rhythmdb_entry_delete (source->priv->db, l->data);
	}

	rhythmdb_commit (source->priv->db);
}

static void
rb_lastfm_source_songs_view_sort_order_changed_cb (RBEntryView *view,
						   RBLastfmSource *source)
{
	rb_debug ("sort order changed");

	rb_entry_view_resort_model (view);
}

static void
rb_lastfm_source_do_query (RBLastfmSource *source)
{
	RhythmDBQueryModel *station_query_model;
	GPtrArray *query;

	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      source->priv->entry_type,
				      RHYTHMDB_QUERY_END);
	station_query_model = rhythmdb_query_model_new_empty (source->priv->db);
	rhythmdb_do_full_query_parsed (source->priv->db,
				       RHYTHMDB_QUERY_RESULTS (station_query_model),
				       query);

	rhythmdb_query_free (query);
	query = NULL;

	rb_entry_view_set_model (source->priv->stations, station_query_model);
	g_object_set (G_OBJECT (source), "query-model", station_query_model, NULL);

	g_object_unref (G_OBJECT (station_query_model));
}

static void
rb_lastfm_source_do_handshake (RBLastfmSource *source)
{
	char *password;
	char *username;
	char *md5password;
	char *handshake_url;

	if (source->priv->connected) {
		return;
	}

	username = eel_gconf_get_string (CONF_AUDIOSCROBBLER_USERNAME);
	if (username == NULL) {
		rb_debug ("no last.fm username");
		return;
	}

	password = eel_gconf_get_string (CONF_AUDIOSCROBBLER_PASSWORD);
	if (password == NULL) {
		rb_debug ("no last.fm password");
		return;
	}

	md5password = mkmd5 (password);
	g_free (password);

	handshake_url = g_strdup_printf ("%s/radio/handshake.php?version=1.1.1&platform=linux&"
					 "username=%s&passwordmd5=%s&debug=0&partner=",
					 LASTFM_URL,
					 username,
					 md5password);
	rb_debug ("Last.fm sending handshake");
	g_object_ref (source);
	rb_lastfm_perform (source, handshake_url, NULL, rb_lastfm_message_cb);
	g_free (handshake_url);
	g_free (username);
	g_free (md5password);
}

static char *
rb_lastfm_source_get_playback_uri (RhythmDBEntry *entry, gpointer data)
{
	char *location;
	RBLastfmSource *source;

	if (entry == NULL) {
		rb_debug ("NULL entry");
		return NULL;
	}

	source = RB_LASTFM_SOURCE (data);
	if (source == NULL) {
		rb_debug ("NULL source pointer");
		return NULL;
	}


	if (!source->priv->connected) {
		rb_debug ("not connected");
		return NULL;
	}
	source = RB_LASTFM_SOURCE (data);

	location = g_strdup_printf ("xrblastfm://%s", source->priv->stream_url + strlen("http://"));
	rb_debug ("playback uri: %s", location);
	return location;
}

static void
rb_lastfm_perform (RBLastfmSource *source,
		   const char *url,
		   char *post_data,
		   SoupMessageCallbackFn response_handler)
{
	SoupMessage *msg;
	msg = soup_message_new ("GET", url);

	if (msg == NULL)
		return;

	soup_message_set_http_version (msg, SOUP_HTTP_1_1);

	rb_debug ("Last.fm communicating with %s", url);

	if (post_data != NULL) {
		rb_debug ("POST data: %s", post_data);
		soup_message_set_request (msg,
					  "application/x-www-form-urlencoded",
					  SOUP_BUFFER_SYSTEM_OWNED,
					  post_data,
					  strlen (post_data));
	}

	/* create soup session, if we haven't got one yet */
	if (!source->priv->soup_session) {
		SoupUri *uri;

		uri = rb_proxy_config_get_libsoup_uri (source->priv->proxy_config);
		source->priv->soup_session = soup_session_async_new_with_options (
					"proxy-uri", uri,
					NULL);
		if (uri)
			soup_uri_free (uri);
	}

	soup_session_queue_message (source->priv->soup_session,
				    msg,
				    (SoupMessageCallbackFn) response_handler,
				    source);
	source->priv->status = COMMUNICATING;
	rb_source_notify_status_changed (RB_SOURCE(source));
}

static void
rb_lastfm_message_cb (SoupMessage *req, gpointer user_data)
{
	RBLastfmSource *source = RB_LASTFM_SOURCE (user_data);
	char *body;
	char **pieces;
	int i;

	if ((req->response).body == NULL) {
		rb_debug ("Lastfm: Server failed to respond");
		return;
	}

	body = g_malloc0 ((req->response).length + 1);
	memcpy (body, (req->response).body, (req->response).length);

	rb_debug ("response body: %s", body);

	if (strstr (body, "ERROR - no such artist") != NULL) {
		source->priv->status = NO_ARTIST;
	}

	g_strstrip (body);
	pieces = g_strsplit (body, "\n", 6);
	for (i = 0; pieces[i] != NULL; i++) {
		gchar **values = g_strsplit (pieces[i], "=", 2);
		if (strcmp (values[0], "session") == 0) {
			if (strcmp (values[1], "FAILED") == 0) {
				source->priv->status = FAILED;
				rb_debug ("Lastfm failed to connect to the server");
				break;
			}
			source->priv->status = OK;
			source->priv->session = g_strdup (values[1]);
			rb_debug ("session ID: %s", source->priv->session);
			source->priv->connected = TRUE;
		} else if (strcmp (values[0], "stream_url") == 0) {
			source->priv->stream_url = g_strdup (values[1]);
			rb_debug ("stream url: %s", source->priv->stream_url);
		} else if (strcmp (values[0], "subscriber") == 0) {
			if (strcmp (values[1], "0") == 0) {
				source->priv->subscriber = FALSE;
			} else {
				source->priv->subscriber = TRUE;
			}
		} else if (strcmp (values[0], "framehack") ==0 ) {
			if (strcmp (values[1], "0") == 0) {
				source->priv->framehack = FALSE;
			} else {
				source->priv->framehack = TRUE;
			}
		} else if (strcmp (values[0], "base_url") ==0) {
			source->priv->base_url = g_strdup (values[1]);
		} else if (strcmp (values[0], "base_path") ==0) {
			source->priv->base_path = g_strdup (values[1]);
		} else if (strcmp (values[0], "update_url") ==0) {
			source->priv->update_url = g_strdup (values[1]);
		} else if (strcmp (values[0], "banned") ==0) {
			if (strcmp (values[1], "0") ==0) {
				source->priv->banned = FALSE;
			} else {
				source->priv->status = BANNED;
				source->priv->banned = TRUE;
				source->priv->connected = FALSE;
			}
		} else if (strcmp (values[0], "response") == 0) {
			if (strcmp (values[1], "OK") == 0) {
				source->priv->status = OK;
				rb_debug ("Successfully communicated");
				source->priv->connected = TRUE;
			} else {
				source->priv->connected = FALSE;
			}
		} else if (strcmp (values[0], "stationname") == 0) {
			gchar **data = g_strsplit (g_strdown(pieces[i - 1]), "=",2);
			RhythmDBEntry *entry;
			GValue titlestring = {0,};

			rb_debug ("Received station name from server: %s", values[1]);
			entry = rhythmdb_entry_lookup_by_location (source->priv->db, data[1]);
			g_value_init (&titlestring, G_TYPE_STRING);
			g_value_set_string (&titlestring, values[1]);

			if (entry == NULL) {
				entry = rhythmdb_entry_new (source->priv->db, source->priv->entry_type, data[1]);
			}
			rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &titlestring);
			g_value_unset (&titlestring);
			rhythmdb_commit (source->priv->db);

		}
	}

	g_strfreev (pieces);
	g_free (body);

	/* doesn't work yet
	if (source->priv->pending_entry) {
		rb_shell_player_play_entry (source->priv->shell_player,
					    source->priv->pending_entry,
					    NULL);
		rhythmdb_entry_unref (source->priv->pending_entry);
		source->priv->pending_entry = NULL;
	}
	*/

	rb_source_notify_status_changed (RB_SOURCE (source));
	g_object_unref (source);
}

static void
rb_lastfm_change_station (RBLastfmSource *source, const char *station)
{
	char *url;
	if (!source->priv->connected) {
		rb_lastfm_source_do_handshake (source);
		return;
	}

	url = g_strdup_printf("%s/radio/adjust.php?session=%s&url=%s&debug=0",
			      LASTFM_URL,
			      source->priv->session,
			      station);

	g_object_ref (source);
	rb_lastfm_perform (source, url, NULL, rb_lastfm_message_cb);
	g_free (url);
}

static void
rb_lastfm_proxy_config_changed_cb (RBProxyConfig *config,
					   RBLastfmSource *source)
{
	SoupUri *uri;

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
rb_lastfm_source_new_station (char *uri, char *title, RBLastfmSource *source)
{
	RhythmDBEntry *entry;
	GValue v = {0,};

	rb_debug ("adding lastfm: %s, %s",uri, title);

	entry = rhythmdb_entry_lookup_by_location (source->priv->db, uri);
	if (entry) {
		rb_debug ("uri %s already in db", uri);
		return;
	}

	entry = rhythmdb_entry_new (source->priv->db, source->priv->entry_type, uri);
	g_value_init (&v, G_TYPE_STRING);
	g_value_set_string (&v, title);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_TITLE, &v);
	g_value_unset (&v);

	g_value_init (&v, G_TYPE_DOUBLE);
	g_value_set_double (&v, 0.0);
	rhythmdb_entry_set (source->priv->db, entry, RHYTHMDB_PROP_RATING, &v);

	rhythmdb_commit (source->priv->db);
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

	G_OBJECT_CLASS (rb_lastfm_source_parent_class)->dispose (object);
}

static void
rb_lastfm_source_command (RBLastfmSource *source, const char *query_string, const char *status)
{
	char *url;
	if (!source->priv->connected) {
		rb_lastfm_source_do_handshake (source);
		return;
	}

	url = g_strdup_printf ("%s/radio/control.php?session=%s&debug=0&%s",
			       LASTFM_URL,
			       source->priv->session,
			       query_string);
	g_object_ref (source);
	rb_lastfm_perform (source, url, NULL, rb_lastfm_message_cb);
	g_free (url);

	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
rb_lastfm_source_love_track (GtkAction *action, RBLastfmSource *source)
{
	rb_lastfm_source_command (source, "command=love", _("Marking song loved..."));
}

static void
rb_lastfm_source_skip_track (GtkAction *action, RBLastfmSource *source)
{
	rb_lastfm_source_command (source, "command=skip", _("Skipping song..."));
}

static void
rb_lastfm_source_ban_track (GtkAction *action, RBLastfmSource *source)
{
	rb_lastfm_source_command (source, "command=ban", _("Banning song..."));
}


static void
rb_lastfm_source_drag_cb (GtkWidget *widget,
				     GdkDragContext *dc,
				     gint x, gint y,
				     GtkSelectionData *selection_data,
				     guint info, guint time,
				     RBLastfmSource *source)
{
	impl_receive_drag (RB_SOURCE (source) , selection_data);
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


static char *
rb_lastfm_source_title_from_uri (char *uri)
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
			if (strcmp (data[4], "similarartists") == 0)
				title = g_strdup_printf (_("Artists similar to %s"), data[3]);
			if (strcmp (data[4], "fans") == 0)
				title = g_strdup_printf (_("Artists liked by fans of %s"), data[3]);
		}

	}

	if (title == NULL && strcmp (data[2], "user") == 0) {
		if (strcmp(data[4], "neighbours") == 0)
			title = g_strdup_printf (_("%s's Neighbour Radio"), data[3]);
		if (strcmp(data[4], "recommended") == 0)
			title = g_strdup_printf (_("%s's Recommended Radio: %s percent"), data[3], data[5]);
		/* subscriber? */
	}

	if (title == NULL) {
		title = g_strstrip (uri);
	}

	g_strfreev (data);
	unesc_title = gnome_vfs_unescape_string (title, NULL);
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

	if (rhythmdb_entry_get_entry_type (entry) != source->priv->entry_type)
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

	add = gtk_entry_get_text (GTK_ENTRY (source->priv->txtbox));

	if (strcmp (gtk_combo_box_get_active_text (GTK_COMBO_BOX (source->priv->typecombo)), "Artist") == 0) {
		uri = g_strdup_printf ("lastfm://artist/%s/similarartists", add);
	} else {
		uri = g_strdup_printf ("lastfm://globaltags/%s", add);
	}

	gtk_entry_set_text (GTK_ENTRY (source->priv->txtbox), "");
	title = rb_lastfm_source_title_from_uri (uri);
	rb_lastfm_source_new_station (uri, title, source);

	g_free(uri);
	g_free(title);
}

static void
rb_lastfm_source_metadata_cb (SoupMessage *req, RBLastfmSource *source)
{
	char *body;
	char **pieces;
	int p;
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (source->priv->shell_player);
	if (entry == NULL || rhythmdb_entry_get_entry_type (entry) != source->priv->entry_type) {
		rb_debug ("got response to metadata request, but not playing from this source");
		return;
	}

	rb_debug ("got response to metadata request");
	body = g_malloc0 ((req->response).length + 1);
	memcpy (body, (req->response).body, (req->response).length);

	g_strstrip (body);
	pieces = g_strsplit (body, "\n", 0);

	for (p = 0; pieces[p] != NULL; p++) {
		gchar **values;

		values = g_strsplit (pieces[p], "=", 2);
		if (strcmp (values[0], "station") == 0) {
		} else if (strcmp (values[0], "station_url") == 0) {
		} else if (strcmp (values[0], "stationfeed") == 0) {
		} else if (strcmp (values[0], "stationfeed_url") == 0) {
		} else if (strcmp (values[0], "artist") == 0) {
			rb_debug ("artist -> %s", values[1]);
			rb_streaming_source_set_streaming_artist (RB_STREAMING_SOURCE (source), values[1]);
		} else if (strcmp (values[0], "album") == 0) {
			rb_debug ("album -> %s", values[1]);
			rb_streaming_source_set_streaming_album (RB_STREAMING_SOURCE (source), values[1]);
		} else if (strcmp (values[0], "track") == 0) {
			rb_debug ("track -> %s", values[1]);
			rb_streaming_source_set_streaming_title (RB_STREAMING_SOURCE (source), values[1]);
		} else if (strcmp (values[0], "albumcover_small") == 0) {
		} else if (strcmp (values[0], "albumcover_medium") == 0) {
		} else if (strcmp (values[0], "albumcover_large") == 0) {
		} else if (strcmp (values[0], "trackprogress") == 0) {
		} else if (strcmp (values[0], "trackduration") == 0) {
		} else if (strcmp (values[0], "artist_url") == 0) {
		} else if (strcmp (values[0], "album_url") == 0) {
		} else if (strcmp (values[0], "track_url") == 0) {
		} else if (strcmp (values[0], "discovery") == 0) {
		} else {
			rb_debug ("got unknown value: %s", values[0]);
		}

		g_strfreev (values);
	}

	g_strfreev (pieces);
	g_free (body);

	source->priv->status = OK;
	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
rb_lastfm_source_new_song_cb (GObject *player_backend,
			      gpointer data,
			      RBLastfmSource *source)
{
	char *uri;
	rb_debug ("got new song");

	uri = g_strdup_printf ("http://%s%s/np.php?session=%s&debug=0",
			       source->priv->base_url,
			       source->priv->base_path,
			       source->priv->session);
	rb_lastfm_perform (source, uri, NULL, (SoupMessageCallbackFn) rb_lastfm_source_metadata_cb);
	g_free (uri);
}

static gboolean
check_entry_type (RBLastfmSource *source, RhythmDBEntry *entry)
{
	RhythmDBEntryType entry_type;
	gboolean matches = FALSE;

	g_object_get (source, "entry-type", &entry_type, NULL);
	if (entry != NULL && rhythmdb_entry_get_entry_type (entry) == entry_type)
		matches = TRUE;
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

	return matches;
}

static void
rb_lastfm_song_changed_cb (RBShellPlayer *player,
			   RhythmDBEntry *entry,
			   RBLastfmSource *source)
{
	const char *location;

	if (check_entry_type (source, entry)) {
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		/* this bit doesn't work */
		/*
		if (!source->priv->connected) {
			rb_lastfm_source_do_handshake (source);
			source->priv->pending_entry = rhythmdb_entry_ref (entry);
			rb_debug ("will play station %s once connected", location);
		} else {
			rb_debug ("switching to station %s", location);
			rb_lastfm_change_station (source, location);
		}
		*/
		rb_lastfm_change_station (source, location);
	} else {
		rb_debug ("non-lastfm entry being played");
	}
}

static void
impl_activate (RBSource *source)
{
	rb_lastfm_source_do_handshake (RB_LASTFM_SOURCE (source));
}

