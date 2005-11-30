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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

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
#include "rb-dialog.h"
#include "rb-preferences.h"

#include "rb-daap-connection.h"
#include "rb-daap-mdns.h"
#include "rb-daap-src.h"

#include "rb-playlist-source.h"


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
static void rb_daap_source_connection_cb (RBDAAPConnection *connection,
					  gboolean result,
					  RBSource *source);
static gboolean rb_daap_source_disconnect (RBSource *source);
static gboolean rb_daap_source_show_popup (RBSource *source);
static const gchar * rb_daap_source_get_browser_key (RBSource *source);
static const gchar * rb_daap_source_get_paned_key (RBLibrarySource *source);


#define CONF_ENABLE_BROWSING CONF_PREFIX "/sharing/enable_browsing"
#define CONF_STATE_SORTING CONF_PREFIX "/state/daap/sorting"
#define CONF_STATE_PANED_POSITION CONF_PREFIX "/state/daap/paned_position"
#define CONF_STATE_SHOW_BROWSER CONF_PREFIX "/state/daap/show_browser"


static RBDAAPmDNSBrowser browser = 0;
static GHashTable *service_name_to_resolver = NULL;
static GSList *sources = NULL;
static guint enable_browsing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;


struct RBDAAPSourcePrivate
{
	gchar *service_name;
	gchar *host;
	guint port;
	gboolean password_protected;

	RBDAAPConnection *connection;
	GSList *playlist_sources;
};

#define RB_DAAP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DAAP_SOURCE, RBDAAPSourcePrivate))

enum {
	PROP_0,
	PROP_SERVICE_NAME,
	PROP_HOST,
	PROP_PORT,
	PROP_PASSWORD_PROTECTED
};

G_DEFINE_TYPE (RBDAAPSource, rb_daap_source, RB_TYPE_LIBRARY_SOURCE)


static RhythmDBEntryType
rhythmdb_entry_daap_type_new (void)
{
	return rhythmdb_entry_register_type ();
}

static void
rb_daap_source_class_init (RBDAAPSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBLibrarySourceClass *library_source_class = RB_LIBRARY_SOURCE_CLASS (klass);

	object_class->dispose = rb_daap_source_dispose;
	object_class->get_property = rb_daap_source_get_property;
	object_class->set_property = rb_daap_source_set_property;

	source_class->impl_activate = rb_daap_source_activate;
	source_class->impl_disconnect = rb_daap_source_disconnect;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_paste = NULL;
	source_class->impl_receive_drag = NULL;
	source_class->impl_delete = NULL;
	source_class->impl_show_popup = rb_daap_source_show_popup;
	source_class->impl_get_config_widget = NULL;
	source_class->impl_get_browser_key = rb_daap_source_get_browser_key;

	library_source_class->impl_get_paned_key = rb_daap_source_get_paned_key;
	library_source_class->impl_has_first_added_column = (RBLibrarySourceFeatureFunc) rb_false_function;
	library_source_class->impl_has_drop_support = (RBLibrarySourceFeatureFunc) rb_false_function;


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
	source->priv = RB_DAAP_SOURCE_GET_PRIVATE (source);
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
rb_daap_get_icon (void)
{
	GdkPixbuf *icon;
	GtkIconTheme *theme;
	gint size;

	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	icon = gtk_icon_theme_load_icon (theme, "gnome-fs-network", size, 0, NULL);

	return icon;
}

static RBSource *
rb_daap_source_new (RBShell *shell,
		    const gchar *service_name,
		    const gchar *name,
		    const gchar *host,
		    guint port,
		    gboolean password_protected)
{
	RBSource *source;
	RhythmDBEntryType type;

	type = rhythmdb_entry_daap_type_new ();

	source = RB_SOURCE (g_object_new (RB_TYPE_DAAP_SOURCE,
					  "service-name", service_name,
					  "name", name,
					  "host", host,
					  "port", port,
					  "entry-type", type,
					  "icon", rb_daap_get_icon (),
					  "shell", shell,
					  "visibility", TRUE,
					  "sorting-key", CONF_STATE_SORTING,
					  "password-protected", password_protected,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source,
						 type);

	return source;
}


static RBSource *
find_source_by_service_name (const gchar *service_name)
{
	GSList *l;

	for (l = sources; l != NULL; l = l->next) {
		RBSource *source = l->data;

		if (strcmp (service_name, RB_DAAP_SOURCE (source)->priv->service_name) == 0) {
			return source;
		}
	}

	return NULL;
}

static void
resolve_cb (RBDAAPmDNSResolver resolver,
	    RBDAAPmDNSResolverStatus status,
	    const gchar *service_name,
	    gchar *name,
	    gchar *host,
	    guint port,
	    gboolean password_protected,
	    RBShell *shell)
{
	if (status == RB_DAAP_MDNS_RESOLVER_FOUND) {
		RBSource *source = find_source_by_service_name (service_name);

		if (source == NULL) {
			source = rb_daap_source_new (shell, service_name, name, host, port, password_protected);
			sources = g_slist_prepend (sources, source);
			rb_shell_append_source (shell, source, NULL);
		} else {
			g_object_set (G_OBJECT (source),
				      "name", name,
				      "host", host,
				      "port", port,
				      "password-protected", password_protected,
				      NULL);
		}
	} else if (status == RB_DAAP_MDNS_RESOLVER_TIMEOUT) {
		g_warning ("Unable to resolve %s", service_name);
	}
}


static void
browse_cb (RBDAAPmDNSBrowser b,
	   RBDAAPmDNSBrowserStatus status,
	   const gchar *service_name,
	   RBShell *shell)
{
	if (status == RB_DAAP_MDNS_BROWSER_ADD_SERVICE) {
		gboolean ret;
		RBDAAPmDNSResolver *resolver;

		if (find_source_by_service_name (service_name)) {
			rb_debug ("Ignoring duplicate DAAP source");
			return;
		}

		rb_debug ("New DAAP (music sharing) source '%s' discovered", service_name);

		/* the resolver takes care of ignoring our own shares,
		 * if this is us, the callback won't fire
		 */
		resolver = g_new0 (RBDAAPmDNSResolver, 1);
		g_hash_table_insert (service_name_to_resolver, g_strdup (service_name), resolver);
		ret = rb_daap_mdns_resolve (resolver,
					    service_name,
					    (RBDAAPmDNSResolverCallback) resolve_cb,
					    shell);
		if (!ret) {
			g_warning ("could not start resolving host");
		}

	} else if (status == RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE) {
		RBSource *source = find_source_by_service_name (service_name);

		rb_debug ("DAAP source '%s' went away", service_name);
		if (source == NULL) {
			/* if this happens, its because the user's own share
			 * went away.  since that one doesnt resolve,
			 * it doesnt get added to the sources list
			 * it does have a resolver tho, so we should remove
			 * that.
			 */
			g_hash_table_remove (service_name_to_resolver, service_name);

			return;
		}

		g_hash_table_remove (service_name_to_resolver, service_name);
		sources = g_slist_remove (sources, source);

		rb_daap_source_disconnect (source);
		rb_source_delete_thyself (source);
		/* unref is done via gtk in rb_shell_source_delete_cb at
		 * gtk_notebook_remove_page */
	}
}

static void
stop_resolver (RBDAAPmDNSResolver *resolver)
{
	rb_daap_mdns_resolve_cancel (*resolver);
	g_free (resolver);

	resolver = NULL;
}


static void
start_browsing (RBShell *shell)
{
	if (service_name_to_resolver != NULL)
		return;

	gboolean ret = rb_daap_mdns_browse (&browser,
					   (RBDAAPmDNSBrowserCallback) browse_cb,
					   shell);

	if (!ret) {
		g_warning ("Unable to start mDNS browsing");
		return;
	}

	service_name_to_resolver = g_hash_table_new_full ((GHashFunc)g_str_hash,
		       				 	  (GEqualFunc)g_str_equal, 
							  (GDestroyNotify)g_free, 
							  (GDestroyNotify)stop_resolver);

	rb_daap_src_init ();
}

static void
stop_browsing (RBShell *shell)
{
	GSList *l;

	if (service_name_to_resolver == NULL)
		return;

	g_hash_table_destroy (service_name_to_resolver);
	service_name_to_resolver = NULL;

	for (l = sources; l != NULL; l = l->next) {
		RBSource *source = l->data;

		rb_daap_source_disconnect (source);
		rb_source_delete_thyself (source);
	}

	g_slist_free (sources);
	sources = NULL;

	if (browser) {
		rb_daap_mdns_browse_cancel (browser);
		browser = 0;
	}

	rb_daap_src_shutdown ();
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

RBSource *
rb_daap_sources_init (RBShell *shell)
{
	gboolean enabled = TRUE;
	GConfValue *value;
	GConfClient *client = eel_gconf_client_get_global ();

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

	return NULL;
}

void
rb_daap_sources_shutdown (RBShell *shell)
{
	if (browser) {
		stop_browsing (shell);
	}

	if (enable_browsing_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (enable_browsing_notify_id);
		enable_browsing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}

	g_object_unref (shell);
}


static void
rb_daap_source_activate (RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	RBShell *shell = NULL;
	RhythmDB *db = NULL;
	gchar *name = NULL;
	RhythmDBEntryType type;

	if (daap_source->priv->connection != NULL) 
		return;

	g_object_get (G_OBJECT (daap_source), 
		      "shell", &shell, 
		      "entry-type", &type, 
		      "name", &name, 
		      NULL);
	g_object_get (G_OBJECT (shell), "db", &db, NULL);

	daap_source->priv->connection =
		rb_daap_connection_new (name,
					daap_source->priv->host,
					daap_source->priv->port,
					daap_source->priv->password_protected,
					db,
					type,
					(RBDAAPConnectionCallback) rb_daap_source_connection_cb,
					source);
	g_object_unref (G_OBJECT (db));
	g_object_unref (G_OBJECT (shell));
	if (daap_source->priv->connection == NULL) {
		/* XXX can this still happen? */
		daap_source->priv->playlist_sources = NULL;
		return;
	}
}

static void
rb_daap_source_connection_cb (RBDAAPConnection *connection,
			      gboolean result,
			      RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	RBShell *shell = NULL;
	GSList *playlists;
	GSList *l;
	RhythmDBEntryType entry_type;

	if (result == FALSE) {
		/* FIXME display error?  should get more info from the connection.. */
		return;
	}

	g_object_get (G_OBJECT (daap_source), 
		      "shell", &shell, 
		      "entry-type", &entry_type,
		      NULL);
	playlists = rb_daap_connection_get_playlists (daap_source->priv->connection);
	for (l = playlists; l != NULL; l = g_slist_next (l)) {
		RBDAAPPlaylist *playlist = l->data;
		RBSource *playlist_source;

		playlist_source = RB_SOURCE (g_object_new (RB_TYPE_PLAYLIST_SOURCE,
							   "name", playlist->name,
							   "shell", shell,
							   "visibility", TRUE,
							   "is-local", FALSE,
							   "entry-type", entry_type,
							   NULL));
		/* this is set here instead of in construction so that
		 * rb_playlist_source_constructor has a chance to be run to set things up */
		rb_playlist_source_add_locations (RB_PLAYLIST_SOURCE (playlist_source), playlist->uris);

		rb_shell_append_source (shell, playlist_source, RB_SOURCE (daap_source));
		daap_source->priv->playlist_sources = g_slist_prepend (daap_source->priv->playlist_sources, playlist_source);
	}
	g_object_unref (G_OBJECT (shell));
}

static void
rb_daap_source_disconnect_cb (RBDAAPConnection *connection,
			      gboolean result,
			      RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	g_object_unref (G_OBJECT (connection));
	daap_source->priv->connection = NULL;
	g_object_unref (source);
}

static gboolean
rb_daap_source_disconnect (RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);

	if (daap_source->priv->connection) {
		GSList *l;
		RBShell *shell;
		RhythmDB *db;
		RhythmDBEntryType type;

		rb_debug ("Disconnecting source");

		g_object_get (G_OBJECT (source), "shell", &shell, "entry-type", &type, NULL);
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

		/* keep the source alive until the disconnect completes */
		g_object_ref (daap_source);
		rb_daap_connection_logout (daap_source->priv->connection,
					   (RBDAAPConnectionCallback) rb_daap_source_disconnect_cb,
					   daap_source);
	}

	return TRUE;
}

static gboolean
rb_daap_source_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/DAAPSourcePopup");
	return TRUE;
}

RBDAAPSource *
rb_daap_source_find_for_uri (const gchar *uri)
{
	GSList *l;
	gchar *ip;
	gchar *s;
	RBDAAPSource *found_source = NULL;

	ip = strdup (uri + 7); // daap://
	s = strchr (ip, ':');
	*s = '\0';

	for (l = sources; l != NULL; l = l->next) {
		RBDAAPSource *source = l->data;

		if (strcmp (ip, source->priv->host) == 0) {
			found_source = source;
			break;
		}

	}

	g_free (ip);

	return found_source;
}

gchar *
rb_daap_source_get_headers (RBDAAPSource *source,
			    const gchar *uri,
			    glong time,
			    gint64 *bytes_out)
{
	gint64 bytes = 0;

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
		// bitrate is kilobits per second
		// a kilobit is 128 bytes
		bytes = (time * bitrate) * 128; 
	}
	
	*bytes_out = bytes;
	return rb_daap_connection_get_headers (source->priv->connection, uri, bytes);
}


static const gchar * 
rb_daap_source_get_browser_key (RBSource *source)
{
	return CONF_STATE_SHOW_BROWSER;
}

static const gchar * 
rb_daap_source_get_paned_key (RBLibrarySource *source)
{
	return CONF_STATE_PANED_POSITION;
}

