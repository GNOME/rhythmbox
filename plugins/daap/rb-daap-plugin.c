/*
 * rb-daap-plugin.c
 *
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
 * Copyright (C) 2008 Alban Crequy <alban.crequy@collabora.co.uk>
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
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <dbus/dbus-glib.h>

#include <libsoup/soup.h>

#include "rb-daap-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "eel-gconf-extensions.h"
#include "rb-daap-source.h"
#include "rb-daap-sharing.h"
#include "rb-daap-src.h"
#include "rb-uri-dialog.h"

#include "rb-daap-mdns-browser.h"

/* preferences */
#define CONF_DAAP_PREFIX  	CONF_PREFIX "/plugins/daap"
#define CONF_ENABLE_BROWSING 	CONF_DAAP_PREFIX "/enable_browsing"

#define DAAP_DBUS_PATH	"/org/gnome/Rhythmbox/DAAP"

struct RBDaapPluginPrivate
{
	RBShell *shell;

	GtkBuilder *builder;
	GtkWidget *preferences;
	gboolean sharing;
	gboolean shutdown;
	gboolean dbus_intf_added;

	GtkActionGroup *daap_action_group;
	guint daap_ui_merge_id;

	RBDaapMdnsBrowser *mdns_browser;

	GHashTable *source_lookup;

	guint enable_browsing_notify_id;

	GdkPixbuf *daap_share_pixbuf;
	GdkPixbuf *daap_share_locked_pixbuf;
};

enum
{
	PROP_0,
	PROP_SHUTDOWN
};

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);

static void rb_daap_plugin_get_property (GObject *object,
					 guint prop_id,
					 GValue *value,
					 GParamSpec *pspec);

static void rb_daap_plugin_init (RBDaapPlugin *plugin);
static void rb_daap_plugin_dispose (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

static GtkWidget* impl_create_configure_dialog (RBPlugin *plugin);
static void rb_daap_plugin_cmd_disconnect (GtkAction *action, RBDaapPlugin *plugin);
static void rb_daap_plugin_cmd_connect (GtkAction *action, RBDaapPlugin *plugin);

static void create_pixbufs (RBDaapPlugin *plugin);
static void start_browsing (RBDaapPlugin *plugin);
static void stop_browsing (RBDaapPlugin *plugin);
static void enable_browsing_changed_cb (GConfClient *client,
					guint cnxn_id,
					GConfEntry *entry,
					RBDaapPlugin *plugin);

gboolean rb_daap_add_source (RBDaapPlugin *plugin, gchar *service_name, gchar *host, unsigned int port, GError **error);
gboolean rb_daap_remove_source (RBDaapPlugin *plugin, gchar *service_name, GError **error);
#include "rb-daap-glue.h"

RB_PLUGIN_REGISTER(RBDaapPlugin, rb_daap_plugin)

static GtkActionEntry rb_daap_source_actions [] =
{
	{ "DaapSourceDisconnect", GTK_STOCK_DISCONNECT, N_("_Disconnect"), NULL,
	  N_("Disconnect from DAAP share"),
	  G_CALLBACK (rb_daap_plugin_cmd_disconnect) },
	{ "MusicNewDAAPShare", GTK_STOCK_CONNECT, N_("Connect to _DAAP share..."), NULL,
	  N_("Connect to a new DAAP share"),
	  G_CALLBACK (rb_daap_plugin_cmd_connect) },
};

static void
rb_daap_plugin_class_init (RBDaapPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->dispose = rb_daap_plugin_dispose;
	object_class->get_property = rb_daap_plugin_get_property;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	plugin_class->create_configure_dialog = impl_create_configure_dialog;

	g_object_class_install_property (object_class,
					 PROP_SHUTDOWN,
					 g_param_spec_boolean ("shutdown",
							       "shutdown",
							       "Whether the DAAP plugin has been shut down",
							       FALSE,
							       G_PARAM_READABLE));

	g_type_class_add_private (object_class, sizeof (RBDaapPluginPrivate));
}

static void
rb_daap_plugin_init (RBDaapPlugin *plugin)
{
	rb_debug ("RBDaapPlugin initialising");
	plugin->priv = G_TYPE_INSTANCE_GET_PRIVATE (plugin, RB_TYPE_DAAP_PLUGIN, RBDaapPluginPrivate);

	rb_daap_src_set_plugin (RB_PLUGIN (plugin));
}

static void
rb_daap_plugin_dispose (GObject *object)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (object);

	rb_debug ("RBDaapPlugin dispose");

	if (plugin->priv->preferences) {
		gtk_widget_destroy (plugin->priv->preferences);
		plugin->priv->preferences = NULL;
	}

	if (plugin->priv->builder) {
		g_object_unref (plugin->priv->builder);
		plugin->priv->builder = NULL;
	}

	G_OBJECT_CLASS (rb_daap_plugin_parent_class)->dispose (object);
}


static void
rb_daap_plugin_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (object);

	switch (prop_id) {
	case PROP_SHUTDOWN:
		g_value_set_boolean (value, plugin->priv->shutdown);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);
	gboolean no_registration;
	gboolean enabled = TRUE;
	GConfValue *value;
	GConfClient *client = eel_gconf_client_get_global ();
	GtkUIManager *uimanager = NULL;
	char *uifile;
	DBusGConnection *conn;
	GError *error = NULL;

	plugin->priv->shutdown = FALSE;
	plugin->priv->shell = g_object_ref (shell);

	value = gconf_client_get_without_default (client,
						  CONF_ENABLE_BROWSING, NULL);
	if (value != NULL) {
		enabled = gconf_value_get_bool (value);
		gconf_value_free (value);
	}

	if (enabled) {
		start_browsing (plugin);
	}

	plugin->priv->enable_browsing_notify_id =
		eel_gconf_notification_add (CONF_ENABLE_BROWSING,
					    (GConfClientNotifyFunc) enable_browsing_changed_cb,
					    plugin);

	create_pixbufs (plugin);

	g_object_get (shell,
		      "ui-manager", &uimanager,
		      NULL);

	/* add actions */
	plugin->priv->daap_action_group = gtk_action_group_new ("DaapActions");
	gtk_action_group_set_translation_domain (plugin->priv->daap_action_group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (plugin->priv->daap_action_group,
				      rb_daap_source_actions, G_N_ELEMENTS (rb_daap_source_actions),
				      plugin);
	gtk_ui_manager_insert_action_group (uimanager, plugin->priv->daap_action_group, 0);

	/* add UI */
	uifile = rb_plugin_find_file (bplugin, "daap-ui.xml");
	if (uifile != NULL) {
		plugin->priv->daap_ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager, uifile, NULL);
		g_free (uifile);
	}

	g_object_unref (uimanager);

	/*
	 * Don't use daap when the no-registration flag is set.
	 * This flag is only used to run multiple instances at the same time, and
	 * sharing from two instances would be silly
	 */
	g_object_get (G_OBJECT (shell),
		      "no-registration", &no_registration,
		      NULL);
	plugin->priv->sharing = !no_registration;
	if (plugin->priv->sharing)
		rb_daap_sharing_init (shell);

	/*
	 * Add dbus interface
	 */
	if (plugin->priv->dbus_intf_added == FALSE) {
		conn = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
		if (conn != NULL) {
			dbus_g_object_type_install_info (RB_TYPE_DAAP_PLUGIN,
							 &dbus_glib_rb_daap_object_info);
			dbus_g_connection_register_g_object (conn, DAAP_DBUS_PATH,
							     G_OBJECT (bplugin));
			plugin->priv->dbus_intf_added = TRUE;
		}
		else
			rb_debug ("No session D-Bus. DAAP interface on D-Bus disabled.");
	}
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;

	rb_debug ("Shutting down DAAP plugin");

	plugin->priv->shutdown = TRUE;

	if (plugin->priv->sharing)
		rb_daap_sharing_shutdown (shell);

	if (plugin->priv->mdns_browser) {
		stop_browsing (plugin);
	}

	if (plugin->priv->enable_browsing_notify_id != EEL_GCONF_UNDEFINED_CONNECTION) {
		eel_gconf_notification_remove (plugin->priv->enable_browsing_notify_id);
		plugin->priv->enable_browsing_notify_id = EEL_GCONF_UNDEFINED_CONNECTION;
	}

	g_object_get (shell,
		      "ui-manager", &uimanager,
		      NULL);

	gtk_ui_manager_remove_ui (uimanager, plugin->priv->daap_ui_merge_id);
	gtk_ui_manager_remove_action_group (uimanager, plugin->priv->daap_action_group);

	g_object_unref (uimanager);

	if (plugin->priv->daap_share_pixbuf != NULL) {
		g_object_unref (plugin->priv->daap_share_pixbuf);
		plugin->priv->daap_share_pixbuf = NULL;
	}

	if (plugin->priv->daap_share_locked_pixbuf != NULL) {
		g_object_unref (plugin->priv->daap_share_locked_pixbuf);
		plugin->priv->daap_share_locked_pixbuf = NULL;
	}

	if (plugin->priv->shell) {
		g_object_unref (plugin->priv->shell);
		plugin->priv->shell = NULL;
	}
}

/* DAAP share icons */

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
create_pixbufs (RBDaapPlugin *plugin)
{
	GdkPixbuf    *emblem;
	GtkIconTheme *theme;
	gint          size;

	theme = gtk_icon_theme_get_default ();

	gtk_icon_size_lookup (RB_SOURCE_ICON_SIZE, &size, NULL);
	plugin->priv->daap_share_pixbuf =
		gtk_icon_theme_load_icon (theme, "gnome-fs-network", size, 0, NULL);

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &size, NULL);
	emblem = gtk_icon_theme_load_icon (theme, "stock_lock", size, 0, NULL);

	plugin->priv->daap_share_locked_pixbuf = composite_icons (plugin->priv->daap_share_pixbuf, emblem);

	if (emblem != NULL) {
		g_object_unref (emblem);
	}
}

GdkPixbuf *
rb_daap_plugin_get_icon (RBDaapPlugin *plugin,
			 gboolean password_protected,
			 gboolean connected)
{
	GdkPixbuf *icon;

	g_return_val_if_fail (plugin->priv->daap_share_pixbuf != NULL, NULL);
	g_return_val_if_fail (plugin->priv->daap_share_locked_pixbuf != NULL, NULL);

	if (password_protected == FALSE) {
		icon = g_object_ref (plugin->priv->daap_share_pixbuf);
	} else if (connected) {
		icon = g_object_ref (plugin->priv->daap_share_pixbuf);
	} else {
		icon = g_object_ref (plugin->priv->daap_share_locked_pixbuf);
	}

	return icon;
}

/* mDNS browsing */

static RBSource *
find_source_by_service_name (RBDaapPlugin *plugin,
			     const char *service_name)
{
	RBSource *source;

	source = g_hash_table_lookup (plugin->priv->source_lookup, service_name);

	return source;
}

static void
mdns_service_added (RBDaapMdnsBrowser *browser,
		    const char        *service_name,
		    const char        *name,
		    const char        *host,
		    guint              port,
		    gboolean           password_protected,
		    RBDaapPlugin      *plugin)
{
	RBSource *source;

	rb_debug ("New service: %s name=%s host=%s port=%u password=%d",
		   service_name, name, host, port, password_protected);

	GDK_THREADS_ENTER ();

	source = find_source_by_service_name (plugin, service_name);

	if (source == NULL) {
		source = rb_daap_source_new (plugin->priv->shell, RB_PLUGIN (plugin), service_name, name, host, port, password_protected);
		g_hash_table_insert (plugin->priv->source_lookup, g_strdup (service_name), source);
		rb_shell_append_source (plugin->priv->shell, source, NULL);
	} else {
		g_object_set (G_OBJECT (source),
			      "name", name,
			      "host", host,
			      "port", port,
			      "password-protected", password_protected,
			      NULL);
	}

	GDK_THREADS_LEAVE ();
}

static void
mdns_service_removed (RBDaapMdnsBrowser *browser,
		      const char        *service_name,
		      RBDaapPlugin	*plugin)
{
	RBSource *source;

	GDK_THREADS_ENTER ();

	source = find_source_by_service_name (plugin, service_name);

	rb_debug ("DAAP source '%s' went away", service_name);
	if (source != NULL) {
		g_hash_table_remove (plugin->priv->source_lookup, service_name);
	}

	GDK_THREADS_LEAVE ();
}

static void
remove_source (RBSource *source)
{
	char *service_name;

	g_object_get (source, "service-name", &service_name, NULL);
	rb_debug ("Removing DAAP source: %s", service_name);

	rb_daap_source_disconnect (RB_DAAP_SOURCE (source));
	rb_source_delete_thyself (source);

	g_free (service_name);
}

static void
start_browsing (RBDaapPlugin *plugin)
{
	GError *error;

	if (plugin->priv->mdns_browser != NULL) {
		return;
	}

	plugin->priv->mdns_browser = rb_daap_mdns_browser_new ();
	if (plugin->priv->mdns_browser == NULL) {
		g_warning ("Unable to start mDNS browsing");
		return;
	}

	g_signal_connect_object (plugin->priv->mdns_browser,
				 "service-added",
				 G_CALLBACK (mdns_service_added),
				 plugin,
				 0);
	g_signal_connect_object (plugin->priv->mdns_browser,
				 "service-removed",
				 G_CALLBACK (mdns_service_removed),
				 plugin,
				 0);

	error = NULL;
	rb_daap_mdns_browser_start (plugin->priv->mdns_browser, &error);
	if (error != NULL) {
		g_warning ("Unable to start mDNS browsing: %s", error->message);
		g_error_free (error);
	}

	plugin->priv->source_lookup = g_hash_table_new_full ((GHashFunc)g_str_hash,
							     (GEqualFunc)g_str_equal,
							     (GDestroyNotify)g_free,
							     (GDestroyNotify)remove_source);
}

static void
stop_browsing (RBDaapPlugin *plugin)
{
	GError *error;

	if (plugin->priv->mdns_browser == NULL) {
		return;
	}

	rb_debug ("Destroying DAAP source lookup");

	g_hash_table_destroy (plugin->priv->source_lookup);
	plugin->priv->source_lookup = NULL;

	g_signal_handlers_disconnect_by_func (plugin->priv->mdns_browser, mdns_service_added, plugin);
	g_signal_handlers_disconnect_by_func (plugin->priv->mdns_browser, mdns_service_removed, plugin);

	error = NULL;
	rb_daap_mdns_browser_stop (plugin->priv->mdns_browser, &error);
	if (error != NULL) {
		g_warning ("Unable to stop mDNS browsing: %s", error->message);
		g_error_free (error);
	}

	g_object_unref (plugin->priv->mdns_browser);
	plugin->priv->mdns_browser = NULL;
}

static void
enable_browsing_changed_cb (GConfClient *client,
			    guint cnxn_id,
			    GConfEntry *entry,
			    RBDaapPlugin *plugin)
{
	gboolean enabled = eel_gconf_get_boolean (CONF_ENABLE_BROWSING);

	if (enabled) {
		start_browsing (plugin);
	} else {
		stop_browsing (plugin);
	}
}

/* daap share connect/disconnect commands */

static void
rb_daap_plugin_cmd_disconnect (GtkAction *action,
			       RBDaapPlugin *plugin)
{
	RBSource *source;

	g_object_get (plugin->priv->shell,
		      "selected-source", &source,
		      NULL);

	if (!RB_IS_DAAP_SOURCE (source)) {
		g_critical ("got non-Daap source for Daap action");
		return;
	}

	rb_daap_source_disconnect (RB_DAAP_SOURCE (source));

	if (source != NULL) {
		g_object_unref (source);
	}
}

static void
new_daap_share_location_added_cb (RBURIDialog *dialog,
				  const char *location,
				  RBDaapPlugin *plugin)
{
	char *host;
	char *p;
	int port = 3689;

	host = g_strdup (location);
	p = strrchr (host, ':');
	if (p != NULL) {
		port = strtoul (p+1, NULL, 10);
		*p = '\0';
	}

	rb_debug ("adding manually specified DAAP share at %s", location);
	mdns_service_added (NULL,
			    g_strdup (location),
			    g_strdup (location),
			    g_strdup (host),
			    port,
			    FALSE,
			    plugin);

	g_free (host);

}

static void
rb_daap_plugin_cmd_connect (GtkAction *action,
			    RBDaapPlugin *plugin)
{
	GtkWidget *dialog;

	dialog = rb_uri_dialog_new (_("New DAAP share"), _("Host:port of DAAP share:"));
	g_signal_connect_object (dialog, "location-added",
				 G_CALLBACK (new_daap_share_location_added_cb),
				 plugin, 0);
	gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
}


/* daap:// URI -> RBDAAPSource mapping */

static gboolean
source_host_find (const char *key,
		  RBDAAPSource *source,
		  const char *host)
{
	char *source_host;
	gboolean result;

	if (source == NULL || host == NULL) {
		return FALSE;
	}

	g_object_get (source, "host", &source_host, NULL);

	result = (strcmp (host, source_host) == 0);
	g_free (source_host);

	return result;
}

RBDAAPSource *
rb_daap_plugin_find_source_for_uri (RBDaapPlugin *plugin, const char *uri)
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

	source = (RBDAAPSource *)g_hash_table_find (plugin->priv->source_lookup, (GHRFunc)source_host_find, ip);

	g_free (ip);

	return source;
}

/* preferences dialog */

static void
preferences_response_cb (GtkWidget *dialog, gint response, RBPlugin *plugin)
{
	gtk_widget_hide (dialog);
}

static void
share_check_button_toggled_cb (GtkToggleButton *button,
			       GtkWidget *widget)
{
	gboolean b;

	b = gtk_toggle_button_get_active (button);

	eel_gconf_set_boolean (CONF_DAAP_ENABLE_SHARING, b);

	gtk_widget_set_sensitive (widget, b);
}

static void
password_check_button_toggled_cb (GtkToggleButton *button,
				  GtkWidget *widget)
{
	gboolean b;

	b = gtk_toggle_button_get_active (button);

	eel_gconf_set_boolean (CONF_DAAP_REQUIRE_PASSWORD, b);

	gtk_widget_set_sensitive (widget, b);
}

static gboolean
share_name_entry_focus_out_event_cb (GtkEntry *entry,
				     GdkEventFocus *event,
				     gpointer data)
{
	gboolean    changed;
	const char *name;
	char       *old_name;

	name = gtk_entry_get_text (entry);
	old_name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);

	if (name == NULL && old_name == NULL) {
		changed = FALSE;
	} else if (name == NULL || old_name == NULL) {
		changed = TRUE;
	} else if (strcmp (name, old_name) != 0) {
		changed = TRUE;
	} else {
		changed = FALSE;
	}

	if (changed)
		eel_gconf_set_string (CONF_DAAP_SHARE_NAME, name);

	g_free (old_name);

	return FALSE;
}

static gboolean
share_password_entry_focus_out_event_cb (GtkEntry *entry,
					 GdkEventFocus *event,
					 gpointer data)
{
	gboolean    changed;
	const char *pw;
	char       *old_pw;

	pw = gtk_entry_get_text (entry);
	old_pw = eel_gconf_get_string (CONF_DAAP_SHARE_PASSWORD);

	if (pw == NULL && old_pw == NULL) {
		changed = FALSE;
	} else if (pw == NULL || old_pw == NULL) {
		changed = TRUE;
	} else if (strcmp (pw, old_pw) != 0) {
		changed = TRUE;
	} else {
		changed = FALSE;
	}

	if (changed)
		eel_gconf_set_string (CONF_DAAP_SHARE_PASSWORD, pw);

	g_free (old_pw);

	return FALSE;
}

static void
update_config_widget (RBDaapPlugin *plugin)
{
	GtkWidget *check;
	GtkWidget *name_entry;
	GtkWidget *password_entry;
	GtkWidget *password_check;
	GtkWidget *box;
	gboolean sharing_enabled;
	gboolean require_password;
	char *name;
	char *password;

	check = GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_enable_check"));
	password_check = GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_password_check"));
	name_entry = GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_name_entry"));
	password_entry = GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_password_entry"));
	box = GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_box"));

	sharing_enabled = eel_gconf_get_boolean (CONF_DAAP_ENABLE_SHARING);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (check), sharing_enabled);
	g_signal_connect (check, "toggled", G_CALLBACK (share_check_button_toggled_cb), box);

	require_password = eel_gconf_get_boolean (CONF_DAAP_REQUIRE_PASSWORD);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (password_check), require_password);
	g_signal_connect (password_check, "toggled", G_CALLBACK (password_check_button_toggled_cb), password_entry);

	name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);
	if (name == NULL || name[0] == '\0')
		name = rb_daap_sharing_default_share_name ();
	if (name != NULL)
		gtk_entry_set_text (GTK_ENTRY (name_entry), name);
	g_free (name);
	g_signal_connect (name_entry, "focus-out-event",
			  G_CALLBACK (share_name_entry_focus_out_event_cb), NULL);

	password = eel_gconf_get_string (CONF_DAAP_SHARE_PASSWORD);
	if (password != NULL)
		gtk_entry_set_text (GTK_ENTRY (password_entry), password);
	g_free (password);
	g_signal_connect (password_entry, "focus-out-event",
			  G_CALLBACK (share_password_entry_focus_out_event_cb), NULL);

	gtk_widget_set_sensitive (box, sharing_enabled);
	gtk_widget_set_sensitive (password_entry, require_password);
}

static GtkWidget *
make_config_widget (RBDaapPlugin *plugin)
{
	char *builder_file;

	builder_file = rb_plugin_find_file (RB_PLUGIN (plugin), "daap-prefs.ui");
	if (builder_file == NULL) {
		return NULL;
	}

	plugin->priv->builder = rb_builder_load (builder_file, NULL);
	g_free (builder_file);

	update_config_widget (plugin);
	return GTK_WIDGET (gtk_builder_get_object (plugin->priv->builder, "daap_vbox"));
}


static GtkWidget*
impl_create_configure_dialog (RBPlugin *bplugin)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);

	if (plugin->priv->preferences == NULL) {
		GtkWidget *widget;

		widget = make_config_widget (plugin);

		plugin->priv->preferences = gtk_dialog_new_with_buttons (_("DAAP Music Sharing Preferences"),
								   NULL,
								   GTK_DIALOG_DESTROY_WITH_PARENT,
								   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
								   NULL);
		g_signal_connect (G_OBJECT (plugin->priv->preferences),
				  "response",
				  G_CALLBACK (preferences_response_cb),
				  plugin);
		gtk_widget_hide_on_delete (plugin->priv->preferences);

		gtk_container_add (GTK_CONTAINER (GTK_DIALOG (plugin->priv->preferences)->vbox), widget);
	} else {
		update_config_widget (plugin);
	}

	gtk_widget_show_all (plugin->priv->preferences);
	return plugin->priv->preferences;
}

gboolean
rb_daap_add_source (RBDaapPlugin *plugin, gchar *service_name, gchar *host, unsigned int port, GError **error)
{

	if (plugin->priv->shutdown)
		return FALSE;

	rb_debug ("Add DAAP source %s (%s:%d)", service_name, host, port);

	rb_debug ("adding manually specified DAAP share at %s", service_name);
	mdns_service_added (NULL,
			    g_strdup (service_name),
			    g_strdup (service_name),
			    g_strdup (host),
			    port,
			    FALSE,
			    plugin);

	return TRUE;
}

gboolean
rb_daap_remove_source (RBDaapPlugin *plugin, gchar *service_name, GError **error)
{
	if (plugin->priv->shutdown)
		return FALSE;
	rb_debug ("Remove DAAP source %s", service_name);
	mdns_service_removed (plugin->priv->mdns_browser, service_name, plugin);
	return TRUE;
}

