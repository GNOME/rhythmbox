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
#include <libgnomeui/gnome-password-dialog.h>

#ifdef WITH_GNOME_KEYRING
#include <gnome-keyring.h>
#endif

#include "rhythmdb.h"
#include "rb-shell.h"
#include "eel-gconf-extensions.h"
#include "rb-daap-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-preferences.h"
#ifdef HAVE_GSTREAMER
#include "rb-daap-src.h"
#endif

#include "rb-daap-connection.h"
#include "rb-daap-mdns-browser.h"
#include "rb-daap-dialog.h"
#include "rb-daap-plugin.h"

#include "rb-static-playlist-source.h"

static void rb_daap_source_dispose (GObject *object);
static void rb_daap_source_set_property  (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_daap_source_get_property  (GObject *object,
					  guint prop_id,
					  GValue *value,
				 	  GParamSpec *pspec);
static void rb_daap_source_activate (RBSource *source);

static gboolean rb_daap_source_show_popup (RBSource *source);
static char * rb_daap_source_get_browser_key (RBSource *source);
static char * rb_daap_source_get_paned_key (RBBrowserSource *source);
static void rb_daap_source_get_status (RBSource *source, char **text, char **progress_text, float *progress);

#define CONF_STATE_SORTING CONF_PREFIX "/state/daap/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/daap/paned_position"
#define CONF_STATE_SHOW_BROWSER CONF_PREFIX "/state/daap/show_browser"

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

G_DEFINE_TYPE (RBDAAPSource, rb_daap_source, RB_TYPE_BROWSER_SOURCE)

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
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose      = rb_daap_source_dispose;
	object_class->finalize     = rb_daap_source_finalize;
	object_class->get_property = rb_daap_source_get_property;
	object_class->set_property = rb_daap_source_set_property;

	source_class->impl_activate = rb_daap_source_activate;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_paste = NULL;
	source_class->impl_receive_drag = NULL;
	source_class->impl_delete = NULL;
	source_class->impl_show_popup = rb_daap_source_show_popup;
	source_class->impl_get_config_widget = NULL;
	source_class->impl_get_browser_key = rb_daap_source_get_browser_key;
	source_class->impl_get_status = rb_daap_source_get_status;

	browser_source_class->impl_get_paned_key = rb_daap_source_get_paned_key;
	browser_source_class->impl_has_drop_support = (RBBrowserSourceFeatureFunc) rb_false_function;

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
		    RBPlugin *plugin,
		    const char *service_name,
		    const char *name,
		    const char *host,
		    guint port,
		    gboolean password_protected)
{
	RBSource *source;
	RhythmDBEntryType type;
	GdkPixbuf *icon;
	RhythmDB *db;

	g_object_get (shell, "db", &db, NULL);
	type = rhythmdb_entry_register_type (db, NULL);
	type->save_to_disk = FALSE;
	type->category = RHYTHMDB_ENTRY_NORMAL;
	g_object_unref (db);

	icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin), password_protected, FALSE);
	source = RB_SOURCE (g_object_new (RB_TYPE_DAAP_SOURCE,
					  "service-name", service_name,
					  "name", name,
					  "host", host,
					  "port", port,
					  "entry-type", type,
					  "icon", icon,
					  "shell", shell,
					  "visibility", TRUE,
					  "sorting-key", CONF_STATE_SORTING,
					  "password-protected", password_protected,
					  "sourcelist-group", RB_SOURCELIST_GROUP_TRANSIENT,
					  "plugin", RB_PLUGIN (plugin),
					  NULL));

	if (icon != NULL) {
		g_object_unref (icon);
	}

	rb_shell_register_entry_type_for_source (shell, source,
						 type);

	return source;
}

static char *
connection_auth_cb (RBDAAPConnection *connection,
		    const char       *name,
		    RBDAAPSource     *source)
{
	gchar *password;
#ifdef WITH_GNOME_KEYRING
	GnomeKeyringResult keyringret;
	gchar *keyring;
	GList *list;

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

		pwd_data = (GnomeKeyringNetworkPasswordData*)list->data;
		password = g_strdup (pwd_data->password);
		source->priv->tried_password = TRUE;
	} else {
#endif
		GtkWindow *parent;
		GnomePasswordDialog *dialog;
		char *message;

		parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source)));

		message = g_strdup_printf (_("The music share '%s' requires a password to connect"), name);
		dialog = GNOME_PASSWORD_DIALOG (gnome_password_dialog_new (
					_("Password Required"), message,
					NULL, NULL, TRUE));
		gtk_window_set_transient_for (GTK_WINDOW (dialog), parent);
		g_free (message);

		gnome_password_dialog_set_domain (dialog, "DAAP");
		gnome_password_dialog_set_show_username (dialog, FALSE);
		gnome_password_dialog_set_show_domain (dialog, FALSE);
		gnome_password_dialog_set_show_userpass_buttons (dialog, FALSE);
#ifdef WITH_GNOME_KEYRING
		gnome_password_dialog_set_show_remember (dialog, gnome_keyring_is_available ());
#endif

		if (gnome_password_dialog_run_and_block (dialog)) {
			password = gnome_password_dialog_get_password (dialog);

#ifdef WITH_GNOME_KEYRING
			switch (gnome_password_dialog_get_remember (dialog)) {
			case GNOME_PASSWORD_DIALOG_REMEMBER_SESSION:
				g_free (keyring);
				keyring = g_strdup ("session");
				/* fall through */
			case GNOME_PASSWORD_DIALOG_REMEMBER_FOREVER:
			{
				guint32 item_id;
				gnome_keyring_set_network_password_sync (keyring,
					NULL,
					"DAAP", name,
					NULL, "daap",
					NULL, 0,
					password,
					&item_id);
				break;
			}
			default:
				break;
			}
#endif
		} else {
			password = NULL;
		}

		/* buggered if I know why we don't do this...
		g_object_unref (G_OBJECT (dialog)); */
#ifdef WITH_GNOME_KEYRING
	}

	/* or these...
	if (list)
		gnome_keyring_network_password_list_free (list); */
	g_free (keyring);
#endif

	return password;
}

static void
connection_connecting_cb (RBDAAPConnection     *connection,
			  RBDAAPConnectionState state,
			  float		        progress,
			  RBDAAPSource         *source)
{
	GdkPixbuf *icon;
	gboolean   is_connected;
	RBPlugin  *plugin;

	rb_debug ("DAAP connection status: %d/%f", state, progress);

	switch (state) {
	case DAAP_GET_INFO:
	case DAAP_GET_PASSWORD:
	case DAAP_LOGIN:
		source->priv->connection_status = _("Connecting to music share");
		break;
	case DAAP_GET_REVISION_NUMBER:
	case DAAP_GET_DB_INFO:
	case DAAP_GET_SONGS:
	case DAAP_GET_PLAYLISTS:
	case DAAP_GET_PLAYLIST_ENTRIES:
		source->priv->connection_status = _("Retrieving songs from music share");
		break;
	case DAAP_LOGOUT:
	case DAAP_DONE:
		source->priv->connection_status = NULL;
		break;
	}

	source->priv->connection_progress = progress;

	rb_source_notify_status_changed (RB_SOURCE (source));

	is_connected = rb_daap_connection_is_connected (connection);

	g_object_get (source, "plugin", &plugin, NULL);
	g_assert (plugin != NULL);

	icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin),
					source->priv->password_protected,
					is_connected);
	g_object_set (source, "icon", icon, NULL);
	if (icon != NULL) {
		g_object_unref (icon);
	}

	g_object_unref (plugin);
}

static void
connection_disconnected_cb (RBDAAPConnection *connection,
			    RBDAAPSource     *source)
{
	GdkPixbuf *icon;
	RBPlugin  *plugin;
	gboolean   daap_shutdown;

	rb_debug ("DAAP connection disconnected");

	g_object_get (source, "plugin", &plugin, NULL);
	g_assert (plugin != NULL);

	g_object_get (plugin, "shutdown", &daap_shutdown, NULL);
	if (daap_shutdown == FALSE) {

		icon = rb_daap_plugin_get_icon (RB_DAAP_PLUGIN (plugin),
						source->priv->password_protected,
						FALSE);
		g_object_set (source, "icon", icon, NULL);
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
}

static void
_add_location_to_playlist (RBRefString *uri, RBStaticPlaylistSource *source)
{
	rb_static_playlist_source_add_location (source, rb_refstring_get (uri), -1);
}

static void
rb_daap_source_connection_cb (RBDAAPConnection *connection,
			      gboolean          result,
			      const char       *reason,
			      RBSource         *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	RBShell *shell = NULL;
	GSList *playlists;
	GSList *l;
	RhythmDBEntryType entry_type;

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
	playlists = rb_daap_connection_get_playlists (RB_DAAP_CONNECTION (daap_source->priv->connection));
	for (l = playlists; l != NULL; l = g_slist_next (l)) {
		RBDAAPPlaylist *playlist = l->data;
		RBSource *playlist_source;

		playlist_source = rb_static_playlist_source_new (shell, playlist->name, FALSE, entry_type);
		g_list_foreach (playlist->uris, (GFunc)_add_location_to_playlist, playlist_source);

		rb_shell_append_source (shell, playlist_source, RB_SOURCE (daap_source));
		daap_source->priv->playlist_sources = g_slist_prepend (daap_source->priv->playlist_sources, playlist_source);
	}

	g_object_unref (shell);
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
}

static void
rb_daap_source_activate (RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	RBShell *shell = NULL;
	RhythmDB *db = NULL;
	char *name = NULL;
	RhythmDBEntryType type;

	if (daap_source->priv->connection != NULL) {
		return;
	}

	g_object_get (daap_source,
		      "shell", &shell,
		      "entry-type", &type,
		      "name", &name,
		      NULL);
	g_object_get (shell, "db", &db, NULL);

	daap_source->priv->connection = rb_daap_connection_new (name,
								daap_source->priv->host,
								daap_source->priv->port,
								daap_source->priv->password_protected,
								db,
								type);
	g_object_add_weak_pointer (G_OBJECT (daap_source->priv->connection), (gpointer *)&daap_source->priv->connection);

	g_free (name);

        g_signal_connect (daap_source->priv->connection,
			  "authenticate",
                          G_CALLBACK (connection_auth_cb),
			  source);
        g_signal_connect (daap_source->priv->connection,
			  "connecting",
                          G_CALLBACK (connection_connecting_cb),
			  source);
        g_signal_connect (daap_source->priv->connection,
			  "disconnected",
                          G_CALLBACK (connection_disconnected_cb),
			  source);

	rb_daap_connection_connect (RB_DAAP_CONNECTION (daap_source->priv->connection),
				    (RBDAAPConnectionCallback) rb_daap_source_connection_cb,
				    source);

	g_object_unref (G_OBJECT (db));
	g_object_unref (G_OBJECT (shell));
}

static void
rb_daap_source_disconnect_cb (RBDAAPConnection *connection,
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
	RhythmDBEntryType type;

	if (daap_source->priv->connection == NULL) {
		return;
	}

	rb_debug ("Disconnecting source");

	daap_source->priv->disconnecting = TRUE;

	g_object_get (daap_source, "shell", &shell, "entry-type", &type, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	rhythmdb_entry_delete_by_type (db, type);
	rhythmdb_commit (db);

	g_object_unref (db);

	for (l = daap_source->priv->playlist_sources; l!= NULL; l = l->next) {
		RBSource *playlist_source = RB_SOURCE (l->data);
		char *name;

		g_object_get (playlist_source, "name", &name, NULL);
		rb_debug ("destroying DAAP playlist %s", name);
		g_free (name);

		rb_source_delete_thyself (playlist_source);
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
	rb_daap_connection_disconnect (daap_source->priv->connection,
				       (RBDAAPConnectionCallback) rb_daap_source_disconnect_cb,
				       daap_source);

	/* wait until disconnected */
	rb_debug ("Waiting for DAAP connection to finish");
	while (daap_source->priv->connection != NULL) {
		rb_debug ("Waiting for DAAP connection to finish...");
		gtk_main_iteration ();
	}
	rb_debug ("DAAP connection finished");
}

static gboolean
rb_daap_source_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/DAAPSourcePopup");
	return TRUE;
}

#ifdef HAVE_GSTREAMER_0_8
char *
rb_daap_source_get_headers (RBDAAPSource *source,
			    const char *uri,
			    glong time,
			    gint64 *bytes_out)
{
	gint64 bytes = 0;

	if (bytes_out != NULL) {
		*bytes_out = 0;
	}

	/* If there is no connection then bail */
	if (source->priv->connection == NULL) {
		return NULL;
	}

	if (time != 0) {
		RhythmDB *db;
		RBShell *shell;
		RhythmDBEntry *entry;
		gulong bitrate;

		g_object_get (source, "shell", &shell, NULL);
		g_object_get (shell, "db", &db, NULL);

		entry = rhythmdb_entry_lookup_by_location (db, uri);

		g_object_unref (shell);
		g_object_unref (db);

		bitrate = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE);
		/* bitrate is kilobits per second */
		/* a kilobit is 128 bytes */
		bytes = (time * bitrate) * 128;
	}

	if (bytes_out != NULL) {
		*bytes_out = bytes;
	}

	return rb_daap_connection_get_headers (source->priv->connection, uri, bytes);
}
#else
char *
rb_daap_source_get_headers (RBDAAPSource *source,
			    const char *uri,
			    gint64 bytes)
{
	/* If there is no connection then bail */
	if (source->priv->connection == NULL) {
		return NULL;
	}

	return rb_daap_connection_get_headers (source->priv->connection, uri, bytes);
}
#endif

static char *
rb_daap_source_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static char *
rb_daap_source_get_paned_key (RBBrowserSource *source)
{
	return g_strdup (CONF_STATE_PANED_POSITION);
}

static void
rb_daap_source_get_status (RBSource *source,
			   char    **text,
			   char    **progress_text,
			   float    *progress)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);

	if (text != NULL) {
		*text = NULL;
	}
	if (progress_text != NULL) {
		*progress_text = NULL;
	}
	if (progress != NULL) {
		*progress = 0.0;
	}

	if (daap_source->priv->connection_status != NULL) {
		if (text != NULL) {
			*text = g_strdup (daap_source->priv->connection_status);
		}

		if (progress != NULL) {
			*progress = daap_source->priv->connection_progress;
		}

		return;
	}

	RB_SOURCE_CLASS (rb_daap_source_parent_class)->impl_get_status (source, text, progress_text, progress);
}
