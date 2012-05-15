/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementation of DAAP (iTunes Music Sharing) source object
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#ifdef WITH_GNOME_KEYRING
#include <gnome-keyring.h>
#endif

#include "rhythmdb.h"
#include "rb-shell.h"
#include "rb-daap-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-daap-src.h"
#include "rb-daap-record-factory.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-display-page.h"

#include "rb-daap-plugin.h"

#include "rb-static-playlist-source.h"

#include <libdmapsharing/dmap.h>

typedef struct _RhythmDBEntryType RBDAAPEntryType;
typedef struct _RhythmDBEntryTypeClass RBDAAPEntryTypeClass;

static void rb_daap_source_dispose (GObject *object);
static void rb_daap_source_set_property  (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_daap_source_get_property  (GObject *object,
					  guint prop_id,
					  GValue *value,
				 	  GParamSpec *pspec);

static void rb_daap_source_selected (RBDisplayPage *page);
static gboolean rb_daap_source_show_popup (RBDisplayPage *page);
static void rb_daap_source_get_status (RBDisplayPage *page, char **text, char **progress_text, float *progress);

static void rb_daap_entry_type_class_init (RBDAAPEntryTypeClass *klass);
static void rb_daap_entry_type_init (RBDAAPEntryType *etype);
GType rb_daap_entry_type_get_type (void);

struct RBDAAPSourcePrivate
{
	GtkActionGroup *action_group;

	char *service_name;
	char *host;
	guint port;
	gboolean password_protected;

	gpointer connection;

	GSList *playlist_sources;

	const char *connection_status;
	float connection_progress;

	gboolean tried_password;
	gboolean disconnecting;
};

enum {
	PROP_0,
	PROP_SERVICE_NAME,
	PROP_HOST,
	PROP_PORT,
	PROP_PASSWORD_PROTECTED
};

G_DEFINE_DYNAMIC_TYPE (RBDAAPSource, rb_daap_source, RB_TYPE_BROWSER_SOURCE);

G_DEFINE_DYNAMIC_TYPE (RBDAAPEntryType, rb_daap_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static char *
rb_daap_entry_type_get_playback_uri (RhythmDBEntryType *etype, RhythmDBEntry *entry)
{
	const char *location;

	location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT);
	if (location == NULL) {
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	}

	return g_strdup (location);
}

static void
rb_daap_entry_type_class_init (RBDAAPEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);
	etype_class->get_playback_uri = rb_daap_entry_type_get_playback_uri;
}

static void
rb_daap_entry_type_class_finalize (RBDAAPEntryTypeClass *klass)
{
}

static void
rb_daap_entry_type_init (RBDAAPEntryType *etype)
{
}

static void
rb_daap_source_dispose (GObject *object)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	/* we should already have been disconnected */
	g_assert (source->priv->connection == NULL);

	G_OBJECT_CLASS (rb_daap_source_parent_class)->dispose (object);
}

static void
rb_daap_source_finalize (GObject *object)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	g_free (source->priv->service_name);
	g_free (source->priv->host);

	G_OBJECT_CLASS (rb_daap_source_parent_class)->finalize (object);
}

static void
rb_daap_source_class_init (RBDAAPSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose      = rb_daap_source_dispose;
	object_class->finalize     = rb_daap_source_finalize;
	object_class->get_property = rb_daap_source_get_property;
	object_class->set_property = rb_daap_source_set_property;

	page_class->selected = rb_daap_source_selected;
	page_class->get_status = rb_daap_source_get_status;
	page_class->show_popup = rb_daap_source_show_popup;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;

	browser_source_class->has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_SERVICE_NAME,
					 g_param_spec_string ("service-name",
						 	      "Service name",
							      "mDNS/DNS-SD service name of the share",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_HOST,
					 g_param_spec_string ("host",
						 	      "Host",
							      "Host IP address",
							      NULL,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_PORT,
					 g_param_spec_uint ("port",
						 	    "Port",
							    "Port of DAAP server on host",
							    0,
							    G_MAXUINT,
							    0,
							    G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PASSWORD_PROTECTED,
					 g_param_spec_boolean ("password-protected",
							       "Password Protected",
							       "Whether the share is password protected",
							       FALSE,
							       G_PARAM_READWRITE));

	g_type_class_add_private (klass, sizeof (RBDAAPSourcePrivate));
}

static void
rb_daap_source_class_finalize (RBDAAPSourceClass *klass)
{
}

static void
rb_daap_source_init (RBDAAPSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source,
						    RB_TYPE_DAAP_SOURCE,
						    RBDAAPSourcePrivate);
}

static void
rb_daap_source_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	switch (prop_id) {
		case PROP_SERVICE_NAME:
			source->priv->service_name = g_value_dup_string (value);
			break;
		case PROP_HOST:
			if (source->priv->host) {
				g_free (source->priv->host);
			}
			source->priv->host = g_value_dup_string (value);
			/* FIXME what do we do if its already connected and we
			 * get a new host? */
			break;
		case PROP_PORT:
			source->priv->port = g_value_get_uint (value);
			break;
		case PROP_PASSWORD_PROTECTED:
			source->priv->password_protected = g_value_get_boolean (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_source_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	switch (prop_id) {
		case PROP_SERVICE_NAME:
			g_value_set_string (value, source->priv->service_name);
			break;
		case PROP_HOST:
			g_value_set_string (value, source->priv->host);
			break;
		case PROP_PORT:
			g_value_set_uint (value, source->priv->port);
			break;
		case PROP_PASSWORD_PROTECTED:
			g_value_set_boolean (value, source->priv->password_protected);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}


RBSource *
rb_daap_source_new (RBShell *shell,
		    GObject *plugin,
		    const char *service_name,
		    const char *name,
		    const char *host,
		    guint port,
		    gboolean password_protected)
{
	RBSource *source;
	RhythmDBEntryType *entry_type;
	GdkPixbuf *icon;
	RhythmDB *db;
	char *entry_type_name;
	GSettings *settings;

	g_object_get (shell, "db", &db, NULL);
	entry_type_name = g_strdup_printf ("daap:%s:%s:%s", service_name, name, host);

	entry_type = g_object_new (rb_daap_entry_type_get_type (),
				   "db", db,
				   "name", entry_type_name,
				   "save-to-disk", FALSE,
				   "category", RHYTHMDB_ENTRY_NORMAL,
				   NULL);
	rhythmdb_register_entry_type (db, entry_type);
	g_object_unref (db);
	g_free (entry_type_name);

	icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin), password_protected, FALSE);

	settings = g_settings_new ("org.gnome.rhythmbox.plugins.daap");
	source = RB_SOURCE (g_object_new (RB_TYPE_DAAP_SOURCE,
					  "service-name", service_name,
					  "name", name,
					  "host", host,
					  "port", port,
					  "entry-type", entry_type,
					  "pixbuf", icon,
					  "shell", shell,
					  "visibility", TRUE,
					  "password-protected", password_protected,
					  "plugin", G_OBJECT (plugin),
					  "load-status", RB_SOURCE_LOAD_STATUS_NOT_LOADED,
					  "settings", g_settings_get_child (settings, "source"),
					  "toolbar-path", "/DAAPSourceToolBar",
					  NULL));
	g_object_unref (settings);

	if (icon != NULL) {
		g_object_unref (icon);
	}

	rb_shell_register_entry_type_for_source (shell, source,
						 entry_type);

	return source;
}

typedef struct {
	RBDAAPSource *source;
	DMAPConnection *connection;
	SoupSession *session;
	SoupMessage *message;
	SoupAuth *auth;
	char *name;
} AuthData;

static void
mount_op_reply_cb (GMountOperation *op,
		   GMountOperationResult result,
		   AuthData *auth_data)
{
	const char *password;
	gchar *keyring;
#ifdef WITH_GNOME_KEYRING
	guint32 item_id;
#endif

	rb_debug ("mount op reply: %d", result);
	password = g_mount_operation_get_password (op);

#ifdef WITH_GNOME_KEYRING
	switch (g_mount_operation_get_password_save (op)) {
	case G_PASSWORD_SAVE_NEVER:
		break;

	case G_PASSWORD_SAVE_FOR_SESSION:
		keyring = "session";
		/* fall through */

	case G_PASSWORD_SAVE_PERMANENTLY:
		gnome_keyring_set_network_password_sync (keyring,
			NULL,
			"DAAP", auth_data->name,
			NULL, "daap",
			NULL, 0,
			password,
			&item_id);
		break;

	default:
		g_assert_not_reached ();
	}
#endif

	if (password) {
		dmap_connection_authenticate_message (auth_data->connection,
						      auth_data->session,
						      auth_data->message,
						      auth_data->auth,
						      password);
	} else {
		rb_daap_source_disconnect (auth_data->source);
	}

	g_object_unref (auth_data->source);
	g_free (auth_data->name);
	g_free (auth_data);
	g_object_unref (op);
}

static void
ask_password (RBDAAPSource *source, const char *name, const char *keyring, SoupSession *session, SoupMessage *msg, SoupAuth *auth)
{
	GtkWindow *parent;
	GMountOperation *mount_op;
	GAskPasswordFlags flags;
	AuthData *auth_data;
	char *message;

	g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_WAITING, NULL);
	parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source)));

	mount_op = gtk_mount_operation_new (parent);
	auth_data = g_new0 (AuthData, 1);
	auth_data->source = g_object_ref (source);
	auth_data->connection = source->priv->connection;
	auth_data->session = session;
	auth_data->message = msg;
	auth_data->auth = auth;
	auth_data->name = g_strdup (name);
	g_signal_connect (mount_op, "reply", G_CALLBACK (mount_op_reply_cb), auth_data);

	flags = G_ASK_PASSWORD_NEED_PASSWORD;
#ifdef WITH_GNOME_KEYRING
	if (gnome_keyring_is_available ()) {
		flags |= G_ASK_PASSWORD_SAVING_SUPPORTED;
	}
#endif
	message = g_strdup_printf (_("The music share '%s' requires a password to connect"), name);
	g_signal_emit_by_name (mount_op, "ask-password", message, NULL, "DAAP", flags);
	g_free (message);
}

static void
connection_auth_cb (DMAPConnection *connection,
                    const char     *name,
                    SoupSession    *session,
                    SoupMessage    *msg,
                    SoupAuth       *auth,
                    gboolean        retrying,
		    RBDAAPSource   *source)
{
	gchar *password = NULL;
#ifdef WITH_GNOME_KEYRING
	GnomeKeyringResult keyringret;
	gchar *keyring;
	GList *list = NULL;

	keyring = NULL;
	if (!source->priv->tried_password) {
		gnome_keyring_get_default_keyring_sync (&keyring);
		keyringret = gnome_keyring_find_network_password_sync (
				NULL,
				"DAAP", name,
				NULL, "daap",
				NULL, 0, &list);
	} else {
		keyringret = GNOME_KEYRING_RESULT_CANCELLED;
	}

	if (keyringret == GNOME_KEYRING_RESULT_OK) {
		GnomeKeyringNetworkPasswordData *pwd_data;

		if (list != NULL) {
			pwd_data = (GnomeKeyringNetworkPasswordData*)list->data;
			password = g_strdup (pwd_data->password);
		}
		source->priv->tried_password = TRUE;
	}

	if (password == NULL) {
		ask_password (source, name, keyring, session, msg, auth);
	} else {
		dmap_connection_authenticate_message (connection, session, msg, auth, password);
	}

	if (list)
		gnome_keyring_network_password_list_free (list);
	g_free (keyring);
#else
	ask_password (source, name, NULL, session, msg, auth);
#endif
}

static void
connection_connecting_cb (DMAPConnection       *connection,
			  DMAPConnectionState   state,
			  float		        progress,
			  RBDAAPSource         *source)
{
	GdkPixbuf *icon;
	gboolean   is_connected;
	GObject *plugin;

	rb_debug ("DAAP connection status: %d/%f", state, progress);

	switch (state) {
	case DMAP_GET_INFO:
	case DMAP_LOGIN:
		source->priv->connection_status = _("Connecting to music share");
		break;
	case DMAP_GET_REVISION_NUMBER:
		g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADING, NULL);
	case DMAP_GET_DB_INFO:
	case DMAP_GET_SONGS:
	case DMAP_GET_PLAYLISTS:
	case DMAP_GET_PLAYLIST_ENTRIES:
		source->priv->connection_status = _("Retrieving songs from music share");
		break;
	case DMAP_DONE:
		g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);
	case DMAP_LOGOUT:
		source->priv->connection_status = NULL;
		break;
	}

	source->priv->connection_progress = progress;

	rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));

	is_connected = dmap_connection_is_connected (DMAP_CONNECTION (connection));

	g_object_get (source, "plugin", &plugin, NULL);
	g_assert (plugin != NULL);

	icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin),
					source->priv->password_protected,
					is_connected);
	g_object_set (source, "pixbuf", icon, NULL);
	if (icon != NULL) {
		g_object_unref (icon);
	}

	g_object_unref (plugin);
}

static void
connection_disconnected_cb (DMAPConnection   *connection,
			    RBDAAPSource     *source)
{
	GdkPixbuf *icon;
	GObject   *plugin;

	rb_debug ("DAAP connection disconnected");

	g_object_get (source, "plugin", &plugin, NULL);
	g_assert (plugin != NULL);

	if (rb_daap_plugin_shutdown (RB_DAAP_PLUGIN (plugin)) == FALSE) {

		icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin),
						source->priv->password_protected,
						FALSE);
		g_object_set (source, "pixbuf", icon, NULL);
		if (icon != NULL) {
			g_object_unref (icon);
		}
	}

	g_object_unref (plugin);
}

static void
release_connection (RBDAAPSource *daap_source)
{
	rb_debug ("Releasing connection");

	g_object_unref (daap_source->priv->connection);
	daap_source->priv->connection = NULL;
}

static void
_add_location_to_playlist (const char *uri, RBStaticPlaylistSource *source)
{
	rb_static_playlist_source_add_location (source, uri, -1);
}

static void
rb_daap_source_connection_cb (DMAPConnection   *connection,
			      gboolean          result,
			      const char       *reason,
			      RBSource         *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	RBShell *shell = NULL;
	GSList *playlists;
	GSList *l;
	RhythmDBEntryType *entry_type;

	rb_debug ("Connection callback result: %s", result ? "success" : "failure");
	daap_source->priv->tried_password = FALSE;

	if (result == FALSE) {
		if (reason != NULL) {
			rb_error_dialog	(NULL, _("Could not connect to shared music"), "%s", reason);
		}

		/* Don't release the connection if we are already disconnecting */
		if (! daap_source->priv->disconnecting) {
			release_connection (daap_source);
		}

		return;
	}

	g_object_get (daap_source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	playlists = dmap_connection_get_playlists (DMAP_CONNECTION (daap_source->priv->connection));
	for (l = playlists; l != NULL; l = g_slist_next (l)) {
		DMAPPlaylist *playlist = l->data;
		RBSource *playlist_source;
		char *settings_name;

		/* Construct a unique settings name for this playlist, as <Share Name>_<Playlist> */
		/* XXX disabled for now, not sure it's a good idea */
		/*settings_name = g_strjoin (NULL, daap_source->priv->service_name, "_", playlist->name, NULL); */

		settings_name = "/org/gnome/rhythmbox/plugins/daap/source";

		playlist_source = rb_static_playlist_source_new (shell, playlist->name, settings_name, FALSE, entry_type);
		/*g_free (settings_name);*/

		g_list_foreach (playlist->uris, (GFunc)_add_location_to_playlist, playlist_source);

		rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (playlist_source), RB_DISPLAY_PAGE (daap_source));
		daap_source->priv->playlist_sources = g_slist_prepend (daap_source->priv->playlist_sources, playlist_source);
	}

	g_object_unref (shell);
	g_object_unref (entry_type);
}

static void
rb_daap_source_selected (RBDisplayPage *page)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (page);
	RBShell *shell = NULL;
	DMAPRecordFactory *factory;
	RhythmDB *rdb = NULL;
	DMAPDb *db = NULL;
	char *name = NULL;
	RhythmDBEntryType *entry_type;

	RB_DISPLAY_PAGE_CLASS (rb_daap_source_parent_class)->selected (page);

	if (daap_source->priv->connection != NULL) {
		return;
	}

	g_object_get (daap_source,
		      "shell", &shell,
		      "name", &name,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell, "db", &rdb, NULL);
	db = DMAP_DB (rb_rhythmdb_dmap_db_adapter_new (rdb, entry_type));

	factory = DMAP_RECORD_FACTORY (rb_daap_record_factory_new ());

	daap_source->priv->connection = daap_connection_new (name,
							     daap_source->priv->host,
							     daap_source->priv->port,
							     db,
							     factory);
	g_object_unref (entry_type);
	g_object_add_weak_pointer (G_OBJECT (daap_source->priv->connection), (gpointer *)&daap_source->priv->connection);

	g_free (name);

        g_signal_connect (daap_source->priv->connection,
			  "authenticate",
                          G_CALLBACK (connection_auth_cb),
			  page);
        g_signal_connect (daap_source->priv->connection,
			  "connecting",
                          G_CALLBACK (connection_connecting_cb),
			  page);
        g_signal_connect (daap_source->priv->connection,
			  "disconnected",
                          G_CALLBACK (connection_disconnected_cb),
			  page);

	dmap_connection_connect (DMAP_CONNECTION (daap_source->priv->connection),
				 (DMAPConnectionCallback) rb_daap_source_connection_cb,
				 page);

	g_object_unref (rdb);
	g_object_unref (shell);
}

static void
rb_daap_source_disconnect_cb (DMAPConnection   *connection,
			      gboolean          result,
			      const char       *reason,
			      RBSource         *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);

	rb_debug ("DAAP source disconnected");

	release_connection (daap_source);

	g_object_unref (source);
}

void
rb_daap_source_disconnect (RBDAAPSource *daap_source)
{
	GSList *l;
	RBShell *shell;
	RhythmDB *db;
	RhythmDBEntryType *entry_type;

	if (daap_source->priv->connection == NULL
	 || daap_source->priv->disconnecting == TRUE) {
		return;
	}

	rb_debug ("Disconnecting source");

	daap_source->priv->disconnecting = TRUE;

	g_object_get (daap_source, "shell", &shell, "entry-type", &entry_type, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	rhythmdb_entry_delete_by_type (db, entry_type);
        g_object_unref (entry_type);
	rhythmdb_commit (db);

	g_object_unref (db);

	for (l = daap_source->priv->playlist_sources; l!= NULL; l = l->next) {
		RBSource *playlist_source = RB_SOURCE (l->data);
		char *name;

		g_object_get (playlist_source, "name", &name, NULL);
		rb_debug ("destroying DAAP playlist %s", name);
		g_free (name);

		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (playlist_source));
	}

	g_slist_free (daap_source->priv->playlist_sources);
	daap_source->priv->playlist_sources = NULL;

	/* we don't want these firing while we are disconnecting */
	g_signal_handlers_disconnect_by_func (daap_source->priv->connection,
					      G_CALLBACK (connection_connecting_cb),
					      daap_source);
	g_signal_handlers_disconnect_by_func (daap_source->priv->connection,
					      G_CALLBACK (connection_auth_cb),
					      daap_source);

	/* keep the source alive until the disconnect completes */
	g_object_ref (daap_source);
	dmap_connection_disconnect (daap_source->priv->connection,
				       (DMAPConnectionCallback) rb_daap_source_disconnect_cb,
				       daap_source);

	/* wait until disconnected */
	rb_debug ("Waiting for DAAP connection to finish");
	while (daap_source->priv->connection != NULL) {
		rb_debug ("Waiting for DAAP connection to finish...");
		GDK_THREADS_ENTER ();
		gtk_main_iteration ();
		GDK_THREADS_LEAVE ();
	}

	daap_source->priv->disconnecting = FALSE;
	rb_debug ("DAAP connection finished");
}

static gboolean
rb_daap_source_show_popup (RBDisplayPage *page)
{
	_rb_display_page_show_popup (page, "/DAAPSourcePopup");
	return TRUE;
}

SoupMessageHeaders *
rb_daap_source_get_headers (RBDAAPSource *source,
			    const char *uri)
{
	/* If there is no connection then bail */
	if (source->priv->connection == NULL) {
		return NULL;
	}

	return dmap_connection_get_headers (source->priv->connection, uri);
}

static void
rb_daap_source_get_status (RBDisplayPage *page,
			   char **text,
			   char **progress_text,
			   float *progress)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (page);

	if (daap_source->priv->connection_status != NULL) {
		if (text != NULL) {
			*text = g_strdup (daap_source->priv->connection_status);
		}

		if (progress != NULL) {
			*progress = daap_source->priv->connection_progress;
		}

		return;
	}

	RB_DISPLAY_PAGE_CLASS (rb_daap_source_parent_class)->get_status (page, text, progress_text, progress);
}

void
_rb_daap_source_register_type (GTypeModule *module)
{
	rb_daap_source_register_type (module);
	rb_daap_entry_type_register_type (module);
}
