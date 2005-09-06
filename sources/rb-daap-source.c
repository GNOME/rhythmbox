/*
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

#include <gtk/gtktreeview.h>
#include <gtk/gtkicontheme.h>
#include <string.h>
#include "rhythmdb.h"
#include "rb-shell.h"
#include <libgnome/gnome-i18n.h>
#include "eel-gconf-extensions.h"
#include "rb-daap-source.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-dialog.h"

#include "rb-daap-connection.h"
#include "rb-daap-mdns.h"
#include "rb-daap-src.h"

#include "rb-daap-playlist-source.h"

#define CONF_ENABLE_BROWSING "/apps/rhythmbox/sharing/enable_browsing"
#define CONF_ENABLE_SHARING "/apps/rhythmbox/sharing/enable_sharing"
#define CONF_SHARE_NAME "/apps/rhythmbox/sharing/share_name"

static void rb_daap_source_init (RBDAAPSource *source);
static void rb_daap_source_finalize (GObject *object);
static void rb_daap_source_class_init (RBDAAPSourceClass *klass);
static void rb_daap_source_activate (RBSource *source);
static gboolean rb_daap_source_disconnect (RBSource *source);
static gboolean rb_daap_source_show_popup (RBSource *source);

struct RBDAAPSourcePrivate
{
	gboolean resolved;
	gchar *host;
	gint port;

	RBDAAPConnection *connection;
	GSList *playlist_sources;

	gboolean connected;
};


static RhythmDBEntryType 
rhythmdb_entry_daap_type_new (void) 
{
	return rhythmdb_entry_register_type ();
}

GType
rb_daap_source_get_type (void)
{
	static GType rb_daap_source_type = 0;

	if (rb_daap_source_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (RBDAAPSourceClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_daap_source_class_init,
			NULL,
			NULL,
			sizeof (RBDAAPSource),
			0,
			(GInstanceInitFunc) rb_daap_source_init
		};

		rb_daap_source_type = g_type_register_static (RB_TYPE_LIBRARY_SOURCE,
							      "RBDAAPSource",
							      &our_info, 0);

	}

	return rb_daap_source_type;
}

static void
rb_daap_source_class_init (RBDAAPSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	
	object_class->finalize = rb_daap_source_finalize;

	source_class->impl_activate = rb_daap_source_activate;
	source_class->impl_disconnect = rb_daap_source_disconnect;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_paste = NULL;
	source_class->impl_receive_drag = NULL;
	source_class->impl_delete = NULL;
	source_class->impl_receive_drag = NULL;
	source_class->impl_show_popup = rb_daap_source_show_popup;
	source_class->impl_get_config_widget = NULL;
	
	return;
}

static void
rb_daap_source_init (RBDAAPSource *source)
{
	source->priv = g_new0 (RBDAAPSourcePrivate, 1);

	return;
}


static void 
rb_daap_source_finalize (GObject *object)
{
	RBDAAPSource *source = RB_DAAP_SOURCE (object);

	rb_daap_source_disconnect (RB_SOURCE (source));
	
	if (source->priv) {
		g_free (source->priv->host);
	
		g_free (source->priv);
		source->priv = NULL;
	}

	return;
}

static GdkPixbuf *
rb_daap_get_icon (void)
{
	GdkPixbuf *icon;
	GtkIconTheme *theme;

	theme = gtk_icon_theme_get_default ();
	icon = gtk_icon_theme_load_icon (theme, "gnome-fs-network", 24, 0, NULL);

	return icon;
}

static RBSource *
rb_daap_source_new (RBShell *shell,
		    const gchar *name)
{
	RBSource *source;
	RhythmDBEntryType type;
	
	type = rhythmdb_entry_daap_type_new ();

	source = RB_SOURCE (g_object_new (RB_TYPE_DAAP_SOURCE,
					  "name", name,
					  "entry-type", type,
					  "icon", rb_daap_get_icon (),
					  "shell", shell,
					  "visibility", TRUE,
					  NULL));

	rb_shell_register_entry_type_for_source (shell, source, 
						 type);


	return source;
}

static RBDAAPmDNSBrowser browser = 0;
static RBDAAPmDNSResolver resolver = 0;

static GSList *sources = NULL;

static RBSource * 
find_source_by_name (const gchar *name)
{
	GSList *l;
	RBSource *source = NULL;

	for (l = sources; l; l = l->next) {
		gchar *s;
		
		source = l->data;
		g_object_get (G_OBJECT (source), "name", &s, NULL);
		
		if (strcmp (name, s) == 0) {
			g_free (s);
			return source;
		}

		g_free (s);
	}

	return NULL;
}

static void
browse_cb (RBDAAPmDNSBrowser b,
	   RBDAAPmDNSBrowserStatus status,
	   const gchar *name,
	   RBShell *shell)
{
	if (status == RB_DAAP_MDNS_BROWSER_ADD_SERVICE) {
		RBSource *source = find_source_by_name (name);

		rb_debug ("New DAAP (music sharing) source '%s' discovered", name);
		
		if (source) {
			rb_debug ("Ignoring duplicate DAAP source");
			return;
		}

		/* Make sure it isn't us. 
		 * This will fail if we're using howl for mDNS and someone
		 * has already taken our name, forcing us to use a suffix 
		 * like (2).  In that case, the other share (with our preferred
		 * name) will be removed from the sourcelist, and our share
		 * will show up.  Stupid howl.
		 * http://lists.porchdogsoft.com/pipermail/howl-users/Week-of-Mon-20041206/000487.html
		 * FIXME try resolving it right away and seeing if its us
		 */
  		if (eel_gconf_get_boolean (CONF_ENABLE_SHARING)) {
			gchar *my_name;

			my_name = eel_gconf_get_string (CONF_SHARE_NAME);

			if (strcmp (my_name, name) == 0) {
				return;
			}
		}
		
		source = rb_daap_source_new (shell, name);

		sources = g_slist_prepend (sources, source);

		rb_shell_append_source (shell, source, NULL);

	} else if (status == RB_DAAP_MDNS_BROWSER_REMOVE_SERVICE) {
		RBSource *source = find_source_by_name (name);

		rb_debug ("DAAP source '%s' went away", name);
		if (source == NULL) {
			/* if this happens, its almost always because the user
			 * turned sharing off in the preferences, and we've 
			 * been notified that our own daap server has gone away
			 * fancy that.
			 *
			 * probably no need for any error messages
			 */
//			g_warning ("notification of removed daap source that does not exist in rhythmbox");
			return;
		}
		
		sources = g_slist_remove (sources, source);
		
		rb_daap_source_disconnect (source);
		rb_source_delete_thyself (source);
		/* unref is done via gtk in rb_shell_source_delete_cb at
		 * gtk_notebook_remove_page */
	}

	return;
}


static void 
start_browsing (RBShell *shell)
{
	gboolean ret;

	ret = rb_daap_mdns_browse (&browser, 
				   (RBDAAPmDNSBrowserCallback) browse_cb,
				   shell);

	if (ret == FALSE) {
		g_warning ("Unable to start mDNS browsing");
		return;
	}

	rb_daap_src_init ();
	
	return;
}

static void 
stop_browsing (RBShell *shell)
{
	GSList *l;
	
	for (l = sources; l; l = l->next) {
		RBSource *source = l->data;

		rb_daap_source_disconnect (source);
		rb_source_delete_thyself (source);
	}
	
	sources = NULL;
	
	if (browser) {
		rb_daap_mdns_browse_cancel (browser);
		browser = 0;
	}

	rb_daap_src_shutdown ();
	
	return;
}

static void 
enable_browsing_changed_cb (GConfClient *client, 
			    guint cnxn_id, 
			    GConfEntry *entry, 
			    RBShell *shell)
{
	gboolean enabled;

	enabled = eel_gconf_get_boolean (CONF_ENABLE_BROWSING);

	if (enabled) {
		start_browsing (shell);
	} else {
		stop_browsing (shell);
	}

	return;
}

RBSource * 
rb_daap_sources_init (RBShell *shell)
{
	gboolean enabled;
	
	g_object_ref (shell);
	
	enabled = eel_gconf_get_boolean (CONF_ENABLE_BROWSING);
	
	if (enabled) {
		start_browsing (shell);
	}

	eel_gconf_notification_add (CONF_ENABLE_BROWSING,
				    (GConfClientNotifyFunc) enable_browsing_changed_cb,
				    shell);

	return NULL;
}	

void 
rb_daap_sources_shutdown (RBShell *shell)
{
	g_object_unref (shell);
	
	if (browser) {
		stop_browsing (shell);
	}
	
	return;
}

static void
resolve_cb (RBDAAPmDNSResolver resolver,
	    RBDAAPmDNSResolverStatus status,
	    const gchar *name,
	    gchar *host,
	    guint port,
	    RBDAAPSource *daap_source)
{
	RBShell *shell = NULL;
	RhythmDB *db = NULL;
	RhythmDBEntryType type;

	if (status == RB_DAAP_MDNS_RESOLVER_FOUND) {
		daap_source->priv->host = host;
		daap_source->priv->port = port;
	
		daap_source->priv->resolved = TRUE;

		if (daap_source->priv->connection == NULL) {
			GSList *playlists;
			GSList *l;
			
			g_object_get (G_OBJECT (daap_source), "shell", &shell, "entry-type", &type, NULL);
			g_object_get (G_OBJECT (shell), "db", &db, NULL);
	
			/* FIXME FIXME FIXME
			 * this is the call that takes a long time 
			 */
			daap_source->priv->connection = rb_daap_connection_new (daap_source->priv->host, daap_source->priv->port, db, type);
	
			playlists = rb_daap_connection_get_playlists (daap_source->priv->connection);
			for (l = playlists; l; l = l->next) {
				RBDAAPPlaylist *playlist = l->data;
				RBSource *playlist_source;
		
	
				playlist_source = RB_SOURCE (g_object_new (RB_TYPE_DAAP_PLAYLIST_SOURCE,
						  "name", playlist->name,
						  "shell", shell,
						  "visibility", TRUE,
						  NULL));
/* this is set here instead of in construction so that 
 * rb_playlist_source_constructor has a chance to be run to set things up */
				g_object_set (G_OBJECT (playlist_source), "playlist", playlist, NULL); 
	
				rb_shell_append_source (shell, playlist_source, RB_SOURCE (daap_source));
				daap_source->priv->playlist_sources = g_slist_prepend (daap_source->priv->playlist_sources, playlist_source);
			}
	
			g_object_unref (G_OBJECT (db));
			g_object_unref (G_OBJECT (shell));
	
			daap_source->priv->connected = TRUE;
		}
	} else if (status == RB_DAAP_MDNS_RESOLVER_TIMEOUT) {
		g_warning ("Unable to resolve %s", name);
	}

	return;
}

static void 
rb_daap_source_activate (RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);
	gchar *name;
	
	g_object_get (G_OBJECT (source), "name", &name, NULL);
	
	if (daap_source->priv->resolved == FALSE) {
		gboolean ret;

		ret = rb_daap_mdns_resolve (&resolver,
					    name,
					    (RBDAAPmDNSResolverCallback) resolve_cb,
					    source);

		if (ret == FALSE) {
			g_warning ("could not start resolving host");
		}
	}

	g_free (name);

	return;
}

static gboolean 
rb_daap_source_disconnect (RBSource *source)
{
	RBDAAPSource *daap_source = RB_DAAP_SOURCE (source);

	if (daap_source->priv->connected) {
		GSList *l;
		RBShell *shell;
		RhythmDB *db;
		RhythmDBEntryType type;

		g_object_get (G_OBJECT (source), "shell", &shell, "entry-type", &type, NULL);
		g_object_get (G_OBJECT (shell), "db", &db, NULL);
		rhythmdb_entry_delete_by_type (db, type);
		rhythmdb_commit (db);
		g_object_unref (G_OBJECT (db));
		g_object_unref (G_OBJECT (shell));

		for (l = daap_source->priv->playlist_sources; l; l = l->next) {
			RBSource *playlist_source = RB_SOURCE (l->data);
	
			rb_source_delete_thyself (playlist_source);	
		}
		
		g_slist_free (daap_source->priv->playlist_sources);
		daap_source->priv->playlist_sources = NULL;
	
		if (daap_source->priv->connection) {
			rb_daap_connection_destroy (daap_source->priv->connection);
			daap_source->priv->connection = NULL;
		}

		daap_source->priv->connected = FALSE;
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

	for (l = sources; l; l = l->next) {
		RBDAAPSource *source = l->data;

		if (source->priv->resolved == TRUE && strcmp (ip, source->priv->host) == 0) {
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

