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

#include "rb-daap-connection.h"
#include "rb-daap-mdns-browser.h"
#include "rb-daap-dialog.h"

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
static const char * rb_daap_source_get_paned_key (RBBrowserSource *source);
static void rb_daap_source_get_status (RBSource *source, char **text, char **progress_text, float *progress);
static void rb_daap_source_cmd_disconnect (GtkAction *action, RBShell *shell);
static void rb_daap_source_disconnect (RBDAAPSource *daap_source);

#define CONF_ENABLE_BROWSING CONF_PREFIX "/sharing/enable_browsing"
#define CONF_STATE_SORTING CONF_PREFIX "/state/daap/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/daap/paned_position"
#define CONF_STATE_SHOW_BROWSER CONF_PREFIX "/state/daap/show_browser"

static RBDaapMdnsBrowser *mdns_browser = NULL;

static GHashTable *source_lookup = NULL;

static guint enable_browsing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;

static GdkPixbuf *daap_share_pixbuf        = NULL;
static GdkPixbuf *daap_share_locked_pixbuf = NULL;
static gboolean daap_was_shutdown = FALSE;

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

static GtkActionEntry rb_daap_source_actions [] =
{
	{ "DaapSourceDisconnect", GTK_STOCK_DISCONNECT, N_("_Disconnect"), NULL,
	  N_("Disconnect from DAAP share"),
	  G_CALLBACK (rb_daap_source_cmd_disconnect) },
};
GtkActionGroup *daap_action_group;
guint daap_ui_merge_id;

static void
rb_daap_source_class_init (RBDAAPSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_source_class = RB_BROWSER_SOURCE_CLASS (klass);

	object_class->dispose      = rb_daap_source_dispose;
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
rb_daap_source_dispose (GObject *object)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	if (source->priv) {
		/* we should already have been disconnected */
		g_assert (source->priv->connection == NULL);

		g_free (source->priv->service_name);
		g_free (source->priv->host);
	}

	G_OBJECT_CLASS (rb_daap_source_parent_class)->dispose (object);
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

static GdkPixbuf *
rb_daap_get_icon (gboolean password_protected,
		  gboolean connected)
{
	GdkPixbuf *icon;
	GtkIconTheme *theme;
	gint size;

	g_return_val_if_fail (daap_share_pixbuf != NULL, NULL);
	g_return_val_if_fail (daap_share_locked_pixbuf != NULL, NULL);

	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);

	if (! password_protected) {
		icon = g_object_ref (daap_share_pixbuf);
	} else if (connected) {
		icon = g_object_ref (daap_share_pixbuf);
	} else {
		icon = g_object_ref (daap_share_locked_pixbuf);
	}

	return icon;
}

static RBSource *
rb_daap_source_new (RBShell *shell,
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

	g_object_get (G_OBJECT (shell), "db", &db, NULL);
	type = rhythmdb_entry_register_type (db, NULL);
	g_object_unref (G_OBJECT (db));

	icon = rb_daap_get_icon (password_protected, FALSE);

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
					  NULL));

	if (icon != NULL) {
		g_object_unref (icon);
	}

	rb_shell_register_entry_type_for_source (shell, source,
						 type);

	return source;
}

static RBSource *
find_source_by_service_name (const char *service_name)
{
	RBSource *source;

	source = g_hash_table_lookup (source_lookup, service_name);

	return source;
}

static void
mdns_service_added (RBDaapMdnsBrowser *browser,
		    const char        *service_name,
		    const char        *name,
		    const char        *host,
		    guint              port,
		    gboolean           password_protected,
		    RBShell           *shell)
{
	RBSource *source;

#if 0
	g_message ("New service: %s name=%s host=%s port=%u password=%d",
		   service_name, name, host, port, password_protected);
#endif

	source = find_source_by_service_name (service_name);

	if (source == NULL) {
		source = rb_daap_source_new (shell, service_name, name, host, port, password_protected);
		g_hash_table_insert (source_lookup, g_strdup (service_name), source);
		rb_shell_append_source (shell, source, NULL);
	} else {
		g_object_set (G_OBJECT (source),
			      "name", name,
			      "host", host,
			      "port", port,
			      "password-protected", password_protected,
			      NULL);
	}
}

static void
mdns_service_removed (RBDaapMdnsBrowser *browser,
		      const char        *service_name,
		      RBShell           *shell)
{
	RBSource *source;

	source = find_source_by_service_name (service_name);

	rb_debug ("DAAP source '%s' went away", service_name);
	if (source == NULL) {
		return;
	}

	g_hash_table_remove (source_lookup, service_name);
}

static void
remove_source (RBSource *source)
{
	rb_debug ("Removing DAAP source: %s", RB_DAAP_SOURCE (source)->priv->service_name);
	rb_daap_source_disconnect (RB_DAAP_SOURCE (source));
	rb_source_delete_thyself (source);
}

static void
start_browsing (RBShell *shell)
{
	GError *error;

	if (mdns_browser != NULL) {
		return;
	}

	mdns_browser = rb_daap_mdns_browser_new ();
	if (mdns_browser == NULL) {
		g_warning ("Unable to start mDNS browsing");
		return;
	}

	g_signal_connect_object (mdns_browser,
				 "service-added",
				 G_CALLBACK (mdns_service_added),
				 shell,
				 0);
	g_signal_connect_object (mdns_browser,
				 "service-removed",
				 G_CALLBACK (mdns_service_removed),
				 shell,
				 0);

	error = NULL;
	rb_daap_mdns_browser_start (mdns_browser, &error);
	if (error != NULL) {
		g_warning ("Unable to start mDNS browsing: %s", error->message);
		g_error_free (error);
	}

	source_lookup = g_hash_table_new_full ((GHashFunc)g_str_hash,
					       (GEqualFunc)g_str_equal,
					       (GDestroyNotify)g_free,
					       (GDestroyNotify)remove_source);

}

static void
stop_browsing (RBShell *shell)
{
	GError *error;

	if (mdns_browser == NULL) {
		return;
	}

	rb_debug ("Destroying DAAP source lookup");

	g_hash_table_destroy (source_lookup);
	source_lookup = NULL;

	g_signal_handlers_disconnect_by_func (mdns_browser, mdns_service_added, shell);
	g_signal_handlers_disconnect_by_func (mdns_browser, mdns_service_removed, shell);

	error = NULL;
	rb_daap_mdns_browser_stop (mdns_browser, &error);
	if (error != NULL) {
		g_warning ("Unable to stop mDNS browsing: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (mdns_browser);
	mdns_browser = NULL;
}

static void
enable_browsing_changed_cb (GConfClient *client,
			    guint cnxn_id,
			    GConfEntry *entry,
			    RBShell *shell)
{
	gboolean enabled = eel_gconf_get_boolean (CONF_ENABLE_BROWSING);

	if (enabled) {
		start_browsing (shell);
	} else {
		stop_browsing (shell);
	}
}

static GdkPixbuf *
composite_icons (const GdkPixbuf *src1,
		 const GdkPixbuf *src2)
{
	GdkPixbuf *dest;
	GdkPixbuf *scaled;
	gint       w1, w2, h1, h2;
	gint       dest_x, dest_y;
	gboolean   do_scale;

	if (! src1) {
		return NULL;
	}

	dest = gdk_pixbuf_copy (src1);

	if (! src2) {
		return dest;
	}

	w1 = gdk_pixbuf_get_width (src1);
	h1 = gdk_pixbuf_get_height (src1);
	w2 = gdk_pixbuf_get_width (src2);
	h2 = gdk_pixbuf_get_height (src2);

	do_scale = ((float)w1 * 0.8) < w2;

	/* scale the emblem down if it will obscure the entire bottom image */
	if (do_scale) {
		scaled = gdk_pixbuf_scale_simple (src2, w1 / 2, h1 / 2, GDK_INTERP_BILINEAR);
	} else {
		scaled = (GdkPixbuf *)src2;
	}

	w2 = gdk_pixbuf_get_width (scaled);
	h2 = gdk_pixbuf_get_height (scaled);

	dest_x = w1 - w2;
	dest_y = h1 - h2;

	gdk_pixbuf_composite (scaled, dest,
			      dest_x, dest_y,
			      w2, h2,
			      dest_x, dest_y,
			      1.0, 1.0,
			      GDK_INTERP_BILINEAR, 0xFF);

	if (do_scale) {
		g_object_unref (scaled);
	}

	return dest;
}

static void
create_pixbufs (void)
{
	GdkPixbuf    *emblem;
	GtkIconTheme *theme;
	gint          size;

	theme = gtk_icon_theme_get_default ();

	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	daap_share_pixbuf = gtk_icon_theme_load_icon (theme, "gnome-fs-network", size, 0, NULL);

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &size, NULL);
	emblem = gtk_icon_theme_load_icon (theme, "stock_lock", size, 0, NULL);

	daap_share_locked_pixbuf = composite_icons (daap_share_pixbuf, emblem);

	if (emblem != NULL) {
		g_object_unref (emblem);
	}
}

static void
destroy_pixbufs (void)
{
	if (daap_share_pixbuf != NULL) {
		g_object_unref (daap_share_pixbuf);
	}
	daap_share_pixbuf = NULL;

	if (daap_share_locked_pixbuf != NULL) {
		g_object_unref (daap_share_locked_pixbuf);
	}
	daap_share_locked_pixbuf = NULL;
}

RBSource *
rb_daap_sources_init (RBShell *shell)
{
	gboolean enabled = TRUE;
	GConfValue *value;
	GConfClient *client = eel_gconf_client_get_global ();
	GtkUIManager *uimanager = NULL;

	value = gconf_client_get_without_default (client,
						  CONF_ENABLE_BROWSING, NULL);
	if (value != NULL) {
		enabled = gconf_value_get_bool (value);
		gconf_value_free (value);
	}

	g_object_ref (shell);

	if (enabled) {
		start_browsing (shell);
	}

	enable_browsing_notify_id =
		eel_gconf_notification_add (CONF_ENABLE_BROWSING,
					    (GConfClientNotifyFunc) enable_browsing_changed_cb,
					    shell);

	create_pixbufs ();

	/* add UI */
	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);
	daap_action_group = gtk_action_group_new ("DaapActions");
	gtk_action_group_set_translation_domain (daap_action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (daap_action_group,
				      rb_daap_source_actions, G_N_ELEMENTS (rb_daap_source_actions),
				      shell);
	gtk_ui_manager_insert_action_group (uimanager, daap_action_group, 0);
	daap_ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
							    rb_file ("daap-ui.xml"),
							    NULL);
	g_object_unref (G_OBJECT (uimanager));

	return NULL;
}

void
rb_daap_sources_shutdown (RBShell *shell)
{
	GtkUIManager *uimanager = NULL;

	rb_debug ("Shutting down DAAP sources");

	g_assert (!daap_was_shutdown);
	daap_was_shutdown = TRUE;

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);

	if (mdns_browser) {
		stop_browsing (shell);
	}

	if (enable_browsing_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (enable_browsing_notify_id);
		enable_browsing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}

	gtk_ui_manager_remove_ui (uimanager, daap_ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, daap_action_group);

	g_object_unref (G_OBJECT (uimanager));
	g_object_unref (shell);

	destroy_pixbufs ();
}

static char *
connection_auth_cb (RBDAAPConnection *connection,
		    const char       *name,
		    RBSource         *source)
{
	char      *password;
	GtkWindow *parent;

	parent = GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (source)));
	password = rb_daap_password_dialog_new_run (parent, name);

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
	icon = rb_daap_get_icon (source->priv->password_protected, is_connected);
	g_object_set (source, "icon", icon, NULL);
	if (icon != NULL) {
		g_object_unref (icon);
	}
}

static void
connection_disconnected_cb (RBDAAPConnection *connection,
			    RBDAAPSource     *source)
{
	GdkPixbuf *icon;

	rb_debug ("DAAP connection disconnected");

	if (daap_was_shutdown) {
		return;
	}

	icon = rb_daap_get_icon (source->priv->password_protected, FALSE);
	g_object_set (source, "icon", icon, NULL);
	if (icon != NULL) {
		g_object_unref (icon);
	}
}

static void
release_connection (RBDAAPSource *daap_source)
{
	rb_debug ("Releasing connection");

	g_object_unref (G_OBJECT (daap_source->priv->connection));
	/* Weak pointer will set to NULL for us */
	/*daap_source->priv->connection = NULL;*/
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

	g_object_get (G_OBJECT (daap_source),
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	playlists = rb_daap_connection_get_playlists (RB_DAAP_CONNECTION (daap_source->priv->connection));
	for (l = playlists; l != NULL; l = g_slist_next (l)) {
		RBDAAPPlaylist *playlist = l->data;
		RBSource *playlist_source;

		playlist_source = rb_static_playlist_source_new (shell, playlist->name, FALSE, entry_type);
		rb_static_playlist_source_add_locations (RB_STATIC_PLAYLIST_SOURCE (playlist_source), playlist->uris);

		rb_shell_append_source (shell, playlist_source, RB_SOURCE (daap_source));
		daap_source->priv->playlist_sources = g_slist_prepend (daap_source->priv->playlist_sources, playlist_source);
	}
	g_object_unref (G_OBJECT (shell));
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

	g_object_get (G_OBJECT (daap_source),
		      "shell", &shell,
		      "entry-type", &type,
		      "name", &name,
		      NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);

	daap_source->priv->connection = rb_daap_connection_new (name,
								daap_source->priv->host,
								daap_source->priv->port,
								daap_source->priv->password_protected,
								db,
								type);
	g_object_add_weak_pointer (G_OBJECT (daap_source->priv->connection), (gpointer *)&daap_source->priv->connection);

	rb_daap_connection_connect (RB_DAAP_CONNECTION (daap_source->priv->connection),
				    (RBDAAPConnectionCallback) rb_daap_source_connection_cb,
				    source);

	g_object_unref (G_OBJECT (db));
	g_object_unref (G_OBJECT (shell));

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

static void
rb_daap_source_cmd_disconnect (GtkAction *action,
			       RBShell   *shell)
{
	RBSource *source;

	g_object_get (G_OBJECT (shell),
		      "selected-source", &source,
		      NULL);

	if (!RB_IS_DAAP_SOURCE (source)) {
		g_critical ("got non-Daap source for Daap action");
		return;
	}

	rb_daap_source_disconnect (RB_DAAP_SOURCE (source));
}

static void
rb_daap_source_disconnect (RBDAAPSource *daap_source)
{
	if (daap_source->priv->connection) {
		GSList *l;
		RBShell *shell;
		RhythmDB *db;
		RhythmDBEntryType type;

		rb_debug ("Disconnecting source");

		daap_source->priv->disconnecting = TRUE;

		g_object_get (G_OBJECT (daap_source), "shell", &shell, "entry-type", &type, NULL);
		g_object_get (G_OBJECT (shell), "db", &db, NULL);
		g_object_unref (G_OBJECT (shell));

		rhythmdb_entry_delete_by_type (db, type);
		rhythmdb_commit (db);
		g_object_unref (G_OBJECT (db));

		for (l = daap_source->priv->playlist_sources; l!= NULL; l = l->next) {
			RBSource *playlist_source = RB_SOURCE (l->data);

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
	}
}

static gboolean
rb_daap_source_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/DAAPSourcePopup");
	return TRUE;
}

static gboolean
source_host_find (const char *key,
		  RBDAAPSource *source,
		  const char *host)
{
	if (source == NULL || host == NULL) {
		return FALSE;
	}

	if (strcmp (host, source->priv->host) == 0) {
		return TRUE;
	}

	return FALSE;
}

RBDAAPSource *
rb_daap_source_find_for_uri (const char *uri)
{
	char *ip;
	char *s;
	RBDAAPSource *source = NULL;

	if (uri == NULL) {
		return NULL;
	}

	ip = strdup (uri + 7); /* daap:// */
	s = strchr (ip, ':');
	*s = '\0';

	source = (RBDAAPSource *)g_hash_table_find (source_lookup, (GHRFunc)source_host_find, ip);

	g_free (ip);

	return source;
}

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

		g_object_get (G_OBJECT (source), "shell", &shell, NULL);
		g_object_get (G_OBJECT (shell), "db", &db, NULL);

		entry = rhythmdb_entry_lookup_by_location (db, uri);

		g_object_unref (G_OBJECT (shell));
		g_object_unref (G_OBJECT (db));

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

static char *
rb_daap_source_get_browser_key (RBSource *source)
{
	return g_strdup (CONF_STATE_SHOW_BROWSER);
}

static const char *
rb_daap_source_get_paned_key (RBBrowserSource *source)
{
	return CONF_STATE_PANED_POSITION;
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
