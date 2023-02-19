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
 * along with this program; if not, see <http://www.gnu.org/licenses/>.
 */

#include <config.h>

#include <string.h>
#include <glib.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <gio/gio.h>

#include <libsoup/soup.h>

#include <libpeas-gtk/peas-gtk.h>

#include "rb-plugin-macros.h"
#include "rb-daap-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-builder-helpers.h"
#include "rb-uri-dialog.h"
#include "rb-display-page-group.h"
#include "rb-application.h"

#include "rb-daap-container-record.h"
#include "rb-daap-record-factory.h"
#include "rb-daap-record.h"
#include "rb-daap-source.h"
#include "rb-daap-sharing.h"
#include "rb-daap-src.h"
#include "rb-dacp-pairing-page.h"
#include "rb-dacp-player.h"
#include "rb-dmap-container-db-adapter.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-rhythmdb-query-model-dmap-db-adapter.h"

#include <libdmapsharing/dmap.h>

#define DAAP_DBUS_PATH	"/org/gnome/Rhythmbox3/DAAP"
#define DAAP_DBUS_IFACE "org.gnome.Rhythmbox3.DAAP"

static const char *rb_daap_dbus_iface =
"<node>"
"  <interface name='org.gnome.Rhythmbox3.DAAP'>"
"    <method name='AddDAAPSource'>"
"     <arg type='s' name='service_name'/>"
"      <arg type='s' name='host'/>"
"      <arg type='u' name='port'/>"
"    </method>"
"    <method name='RemoveDAAPSource'>"
"      <arg type='s' name='service_name'/>"
"    </method>"
"  </interface>"
"</node>";

struct _RBDaapPlugin
{
	PeasExtensionBase parent;

	GtkBuilder *builder;
	GtkWidget *preferences;
	gboolean sharing;
	gboolean shutdown;

	GSimpleAction *new_share_action;

	DmapMdnsBrowser *mdns_browser;

	DmapControlShare *dacp_share;
	gboolean dacp_share_started;

	GHashTable *source_lookup;

	GSettings *settings;
	GSettings *dacp_settings;

	GDBusConnection *bus;
	guint dbus_intf_id;
};

struct _RBDaapPluginClass
{
	PeasExtensionBaseClass parent;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_daap_plugin_init (RBDaapPlugin *plugin);

static void new_share_action_cb (GSimpleAction *, GVariant *, gpointer);

static void start_browsing (RBDaapPlugin *plugin);
static void stop_browsing (RBDaapPlugin *plugin);
static void settings_changed_cb (GSettings *settings,
				 const char *key,
				 RBDaapPlugin *plugin);
static void dacp_settings_changed_cb (GSettings *settings,
				      const char *key,
				      RBDaapPlugin *plugin);
static void libdmapsharing_debug (const char *domain,
				  GLogLevelFlags level,
				  const char *message,
				  gpointer data);

static void register_daap_dbus_iface (RBDaapPlugin *plugin);
static void unregister_daap_dbus_iface (RBDaapPlugin *plugin);

static void peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface);

RB_DEFINE_PLUGIN(RB_TYPE_DAAP_PLUGIN,
		 RBDaapPlugin,
		 rb_daap_plugin,
		 (G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_GTK_TYPE_CONFIGURABLE,
						peas_gtk_configurable_iface_init)))

static void
rb_daap_plugin_init (RBDaapPlugin *plugin)
{
	g_autoptr(GSettings) daap_settings = NULL;

	rb_debug ("RBDaapPlugin initialising");
	rb_daap_src_set_plugin (G_OBJECT (plugin));

	plugin->settings = g_settings_new ("org.gnome.rhythmbox.sharing");

	daap_settings = g_settings_new ("org.gnome.rhythmbox.plugins.daap");
	plugin->dacp_settings = g_settings_get_child (daap_settings, "dacp");

	rb_register_gst_plugin ();
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);
	gboolean no_registration;
	g_autoptr(RBShell) shell = NULL;
	GApplication *app;

	plugin->shutdown = FALSE;

	g_log_set_handler ("libdmapsharing",
			    G_LOG_LEVEL_MASK,
			    libdmapsharing_debug,
			    NULL);

	g_object_get (plugin, "object", &shell, NULL);

	g_signal_connect_object (plugin->settings, "changed", G_CALLBACK (settings_changed_cb), plugin, 0);

	g_signal_connect_object (plugin->dacp_settings, "changed", G_CALLBACK (dacp_settings_changed_cb), plugin, 0);

	if (g_settings_get_boolean (plugin->settings, "enable-browsing")) {
		start_browsing (plugin);
	}

	app = g_application_get_default ();
	plugin->new_share_action = g_simple_action_new ("daap-new-share", NULL);
	g_signal_connect (plugin->new_share_action, "activate", G_CALLBACK (new_share_action_cb), plugin);
	g_action_map_add_action (G_ACTION_MAP (app), G_ACTION (plugin->new_share_action));

	rb_application_add_plugin_menu_item (RB_APPLICATION (app),
					     "display-page-add",
					     "daap-new-share",
					     g_menu_item_new (_("Connect to DAAP share..."), "app.daap-new-share"));

	/*
	 * Don't use daap when the no-registration flag is set.
	 * This flag is only used to run multiple instances at the same time, and
	 * sharing from two instances would be silly
	 */
	g_object_get (shell, "no-registration", &no_registration, NULL);
	plugin->sharing = !no_registration;
	if (plugin->sharing)
		rb_daap_sharing_init (shell);

	plugin->dacp_share = rb_daap_create_dacp_share (G_OBJECT (plugin));
	plugin->dacp_share_started = FALSE;
	if (g_settings_get_boolean (plugin->dacp_settings, "enable-remote")) {
		GError *error = NULL;
		dmap_control_share_start_lookup (plugin->dacp_share, &error);
		plugin->dacp_share_started = TRUE;
	}

	register_daap_dbus_iface (plugin);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);
	g_autoptr(RBShell) shell = NULL;

	rb_debug ("Shutting down DAAP plugin");

	g_object_get (plugin, "object", &shell, NULL);

	unregister_daap_dbus_iface (plugin);
	plugin->shutdown = TRUE;

	rb_application_remove_plugin_menu_item (RB_APPLICATION (g_application_get_default ()),
						"display-page-add",
						"daap-new-share");
	if (plugin->sharing)
		rb_daap_sharing_shutdown (shell);

	if (plugin->mdns_browser) {
		stop_browsing (plugin);
	}

	g_clear_object (&plugin->settings);
	g_clear_object (&plugin->dacp_share);
	g_clear_pointer (&plugin->preferences, gtk_widget_destroy);
	g_clear_object (&plugin->builder);
	g_clear_object (&plugin->bus);
}

/* DAAP share icons */

GIcon *
rb_daap_plugin_get_icon (RBDaapPlugin *plugin,
			 gboolean password_protected,
			 gboolean connected)
{
	if (connected || (password_protected == FALSE)) {
		return g_themed_icon_new ("folder-remote-symbolic");
	} else {
		return g_themed_icon_new ("dialog-password-symbolic");
	}
}

/* mDNS browsing */

static RBSource *
find_source_by_service_name (RBDaapPlugin *plugin,
			     const char *service_name)
{
	RBSource *source;

	source = g_hash_table_lookup (plugin->source_lookup, service_name);

	return source;
}

static void
mdns_service_added (DmapMdnsBrowser *browser,
		    DmapMdnsService *service,
		    RBDaapPlugin *plugin)
{
	RBSource *source;
	g_autoptr(RBShell) shell = NULL;
	gchar *service_name = NULL;
	gchar *name         = NULL;
	gchar *host         = NULL;
	guint port;
	gboolean password_protected;

	g_object_get (service,
		      "service-name", &service_name,
		      "name", &name,
		      "host", &host,
		      "port", &port,
		      "password-protected", &password_protected,
		      NULL);

	rb_debug ("New service: %s name=%s host=%s port=%u password=%d",
		   service_name,
		   name,
		   host,
		   port,
		   password_protected);

	source = find_source_by_service_name (plugin, service_name);

	if (source == NULL) {
		g_object_get (plugin, "object", &shell, NULL);

		source = rb_daap_source_new (shell,
					     G_OBJECT (plugin),
					     service_name,
					     name,
					     host,
					     port,
					     password_protected);
		g_hash_table_insert (plugin->source_lookup, g_strdup (service_name), source);
		rb_shell_append_display_page (shell,
					      RB_DISPLAY_PAGE (source),
					      RB_DISPLAY_PAGE_GROUP_SHARED);
	} else {
		g_object_set (source,
			      "name", name,
			      "host", host,
			      "port", port,
			      "password-protected", password_protected,
			      NULL);
	}

	g_free (service_name);
	g_free (name);
	g_free (host);
}

static void
mdns_service_removed (DmapMdnsBrowser *browser,
		      const char        *service_name,
		      RBDaapPlugin	*plugin)
{
	RBSource *source;
	source = find_source_by_service_name (plugin, service_name);

	rb_debug ("DAAP source '%s' went away", service_name);
	if (source != NULL) {
		g_hash_table_remove (plugin->source_lookup, service_name);
	}
}

static void
remove_source (RBSource *source)
{
	char *service_name;

	g_object_get (source, "service-name", &service_name, NULL);
	rb_debug ("Removing DAAP source: %s", service_name);

	rb_daap_source_disconnect (RB_DAAP_SOURCE (source));
	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (source));

	g_free (service_name);
}

static void
start_browsing (RBDaapPlugin *plugin)
{
	GError *error;

	if (plugin->mdns_browser != NULL) {
		return;
	}

	plugin->mdns_browser = dmap_mdns_browser_new (DMAP_MDNS_SERVICE_TYPE_DAAP);
	if (plugin->mdns_browser == NULL) {
		g_warning ("Unable to start mDNS browsing");
		return;
	}

	g_signal_connect_object (plugin->mdns_browser,
				 "service-added",
				 G_CALLBACK (mdns_service_added),
				 plugin,
				 0);
	g_signal_connect_object (plugin->mdns_browser,
				 "service-removed",
				 G_CALLBACK (mdns_service_removed),
				 plugin,
				 0);

	error = NULL;
	dmap_mdns_browser_start (plugin->mdns_browser, &error);
	if (error != NULL) {
		g_warning ("Unable to start mDNS browsing: %s", error->message);
		g_error_free (error);
	}

	plugin->source_lookup = g_hash_table_new_full ((GHashFunc)g_str_hash,
							     (GEqualFunc)g_str_equal,
							     (GDestroyNotify)g_free,
							     (GDestroyNotify)remove_source);
}

static void
stop_browsing (RBDaapPlugin *plugin)
{
	g_autoptr(GError) error = NULL;

	if (plugin->mdns_browser == NULL) {
		return;
	}

	rb_debug ("Destroying DAAP source lookup");

	g_hash_table_destroy (plugin->source_lookup);
	plugin->source_lookup = NULL;

	g_signal_handlers_disconnect_by_func (plugin->mdns_browser, mdns_service_added, plugin);
	g_signal_handlers_disconnect_by_func (plugin->mdns_browser, mdns_service_removed, plugin);

	error = NULL;
	dmap_mdns_browser_stop (plugin->mdns_browser, &error);
	if (error != NULL)
		g_warning ("Unable to stop mDNS browsing: %s", error->message);

	g_clear_object (&plugin->mdns_browser);
}

static void
dacp_settings_changed_cb (GSettings *settings, const char *key, RBDaapPlugin *plugin)
{
	if (g_strcmp0 (key, "enable-remote") == 0) {
		GError *error = NULL;
		if (g_settings_get_boolean (settings, key) != plugin->dacp_share_started) {
			if (plugin->dacp_share_started) {
				dmap_control_share_stop_lookup (plugin->dacp_share, &error);
				plugin->dacp_share_started = FALSE;
			} else {
				dmap_control_share_start_lookup (plugin->dacp_share, &error);
				plugin->dacp_share_started = TRUE;
			}
		}
	}
}

static void
settings_changed_cb (GSettings *settings, const char *key, RBDaapPlugin *plugin)
{
	if (g_strcmp0 (key, "enable-browsing") == 0) {
		if (g_settings_get_boolean (settings, key)) {
			start_browsing (plugin);
		} else {
			stop_browsing (plugin);
		}
	}
}

static void
libdmapsharing_debug (const char *domain,
		      GLogLevelFlags level,
		      const char *message,
		      gpointer data)
{
	if ((level & G_LOG_LEVEL_DEBUG) != 0) {
		rb_debug ("%s", message);
	} else {
		g_log_default_handler (domain, level, message, data);
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
	DmapMdnsService *service;

	host = g_strdup (location);
	p = strrchr (host, ':');
	if (p != NULL) {
		port = strtoul (p+1, NULL, 10);
		*p = '\0';
	}

	rb_debug ("adding manually specified DAAP share at %s", location);
	service = g_object_new (DMAP_TYPE_MDNS_SERVICE,
				"service-name", location,
				"name", location,
				"host", host,
				"port", port,
				"password-protected", FALSE,
				NULL);

	mdns_service_added (NULL,
			    service,
			    plugin);

	g_free (host);
	g_object_unref (service);
}

static void
new_daap_share_response_cb (GtkDialog *dialog, int response, gpointer meh)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
new_share_action_cb (GSimpleAction *action, GVariant *parameter, gpointer data)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (data);
	GtkWidget *dialog;

	dialog = rb_uri_dialog_new (_("New DAAP share"), _("Host:port of DAAP share:"));
	g_signal_connect_object (dialog, "location-added",
				 G_CALLBACK (new_daap_share_location_added_cb),
				 plugin, 0);
	gtk_widget_show_all (dialog);
	g_signal_connect (dialog, "response", G_CALLBACK (new_daap_share_response_cb), NULL);
}

/* daap:// URI -> RBDAAPSource mapping */

static gboolean
source_host_and_port_find (const char *key,
		           RBDAAPSource *source,
		           const char *host_and_port)
{
	guint    source_port          = 0;
	char    *source_host          = NULL;
	char    *source_host_and_port = NULL;
	gboolean result               = FALSE;

	if (source == NULL || host_and_port == NULL) {
		goto out;
	}

	g_object_get (source, "host", &source_host, NULL);
	g_object_get (source, "port", &source_port, NULL);

	source_host_and_port = g_strdup_printf ("%s:%d", source_host, source_port);

	result = (strcmp (host_and_port, source_host_and_port) == 0);

out:
	g_free (source_host);
	g_free (source_host_and_port);

	return result;
}

RBDAAPSource *
rb_daap_plugin_find_source_for_uri (RBDaapPlugin *plugin, const char *uri)
{
	char         *host_and_port = NULL;
	char         *s             = NULL;
	RBDAAPSource *source        = NULL;

	if (NULL == uri) {
		goto out;
	}

	host_and_port = g_strdup (uri + 7); /* Skip daap://. */
	if (NULL == host_and_port) {
		goto out;
	}

	s = strchr (host_and_port, '/');  /* Include through port, but not path. */
	if (NULL != s) {
		*s = '\0';
	}

	source = (RBDAAPSource *)g_hash_table_find (plugin->source_lookup, (GHRFunc)source_host_and_port_find, host_and_port);

out:
	g_free (host_and_port);

	return source;
}

gboolean
rb_daap_plugin_shutdown (RBDaapPlugin *plugin)
{
	return plugin->shutdown;
}

/* preferences dialog */

/* should move this to a separate class really */

static void
forget_remotes_button_toggled_cb (GtkToggleButton *button,
				  gpointer data)
{
	g_autoptr(GSettings) dacp_settings = NULL;
	g_autoptr(GSettings) daap_settings = NULL;

	daap_settings = g_settings_new ("org.gnome.rhythmbox.plugins.daap");
	dacp_settings = g_settings_get_child (daap_settings, "dacp");
	g_settings_reset (dacp_settings, "known-remotes");
}

static gboolean
share_name_entry_focus_out_event_cb (GtkEntry *entry,
				     GdkEventFocus *event,
				     gpointer data)
{
	g_autoptr(GSettings) settings = NULL;
	g_autofree char *old_name = NULL;
	gboolean    changed;
	const char *name;

	settings = g_settings_new ("org.gnome.rhythmbox.sharing");
	name = gtk_entry_get_text (entry);
	old_name = g_settings_get_string (settings, "share-name");

	if (name == NULL && old_name == NULL) {
		changed = FALSE;
	} else if (name == NULL || old_name == NULL) {
		changed = TRUE;
	} else if (strcmp (name, old_name) != 0) {
		changed = TRUE;
	} else {
		changed = FALSE;
	}

	if (changed) {
		g_settings_set_string (settings, "share-name", name);
	}

	return FALSE;
}

static gboolean
share_password_entry_focus_out_event_cb (GtkEntry *entry,
					 GdkEventFocus *event,
					 RBDaapPlugin *plugin)
{
	g_autoptr(GSettings) settings = NULL;
	g_autofree char *old_pw = NULL;
	gboolean    changed;
	const char *pw;

	pw = gtk_entry_get_text (entry);
	settings = g_settings_new ("org.gnome.rhythmbox.sharing");
	old_pw = g_settings_get_string (settings, "share-password");

	if (pw == NULL && old_pw == NULL) {
		changed = FALSE;
	} else if (pw == NULL || old_pw == NULL) {
		changed = TRUE;
	} else if (strcmp (pw, old_pw) != 0) {
		changed = TRUE;
	} else {
		changed = FALSE;
	}

	if (changed) {
		g_settings_set_string (settings, "share-password", pw);
	}

	return FALSE;
}

static void
config_settings_changed_cb (GSettings *settings, const char *key, RBDaapPlugin *plugin)
{
	if (g_strcmp0 (key, "enable-sharing") == 0) {
		GtkToggleButton *password_check;
		GtkWidget *password_entry;
		gboolean enabled = g_settings_get_boolean (settings, key);

		password_check = GTK_TOGGLE_BUTTON (gtk_builder_get_object (plugin->builder, "daap_password_check"));
		password_entry = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_password_entry"));

		gtk_widget_set_sensitive (password_entry, enabled && gtk_toggle_button_get_active (password_check));
		gtk_widget_set_sensitive (GTK_WIDGET (password_check), enabled);
	}
}

static void
update_config_widget (RBDaapPlugin *plugin)
{
	GtkWidget *check;
	GtkWidget *remote_check;
	GtkWidget *name_entry;
	GtkWidget *password_entry;
	GtkWidget *password_check;
	GtkWidget *forget_remotes_button;
	char *name;
	char *password;

	check = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_enable_check"));
	remote_check = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "dacp_enable_check"));
	password_check = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_password_check"));
	name_entry = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_name_entry"));
	password_entry = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_password_entry"));
	forget_remotes_button = GTK_WIDGET (gtk_builder_get_object (plugin->builder, "forget_remotes_button"));

	g_settings_bind (plugin->settings, "enable-sharing", check, "active", G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (plugin->dacp_settings, "enable-remote", remote_check, "active", G_SETTINGS_BIND_DEFAULT);

	g_signal_connect_object (plugin->settings, "changed", G_CALLBACK (config_settings_changed_cb), plugin, 0);

	/*g_signal_connect (check, "toggled", G_CALLBACK (share_check_button_toggled_cb), plugin->builder);*/

	/* probably needs rethinking to deal with remotes.. */
	g_settings_bind (plugin->settings, "require-password", password_check, "active", G_SETTINGS_BIND_DEFAULT);
	g_settings_bind (plugin->settings, "require-password", password_entry, "sensitive", G_SETTINGS_BIND_NO_SENSITIVITY);

	g_signal_connect_object (forget_remotes_button, "clicked", G_CALLBACK (forget_remotes_button_toggled_cb), plugin, 0);

	name = g_settings_get_string (plugin->settings, "share-name");
	if (name == NULL || name[0] == '\0') {
		g_free (name);
		name = rb_daap_sharing_default_share_name ();
	}
	if (name != NULL) {
		gtk_entry_set_text (GTK_ENTRY (name_entry), name);
		g_free (name);
	}
	g_signal_connect (name_entry,
			  "focus-out-event",
			  G_CALLBACK (share_name_entry_focus_out_event_cb),
			  NULL);

	password = g_settings_get_string (plugin->settings, "share-password");
	if (password != NULL) {
		gtk_entry_set_text (GTK_ENTRY (password_entry), password);
		g_free (password);
	}
	g_signal_connect (password_entry,
			  "focus-out-event",
			  G_CALLBACK (share_password_entry_focus_out_event_cb),
			  NULL);

	/*gtk_widget_set_sensitive (password_entry, require_password);*/
}

static GtkWidget *
impl_create_configure_widget (PeasGtkConfigurable *bplugin)
{
	RBDaapPlugin *plugin = RB_DAAP_PLUGIN (bplugin);

	plugin->builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "daap-prefs.ui", NULL);
	update_config_widget (plugin);

	return GTK_WIDGET (gtk_builder_get_object (plugin->builder, "daap_vbox"));
}

static void
peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface)
{
	iface->create_configure_widget = impl_create_configure_widget;
}

/* DAAP DBus interface */

static void
daap_dbus_method_call (GDBusConnection *connection,
		       const char *sender,
		       const char *object_path,
		       const char *interface_name,
		       const char *method_name,
		       GVariant *parameters,
		       GDBusMethodInvocation *invocation,
		       RBDaapPlugin *plugin)
{
	if (plugin->shutdown) {
		rb_debug ("ignoring %s call", method_name);
		return;
	}

	if (g_strcmp0 (method_name, "AddDAAPSource") == 0) {
		DmapMdnsService *service;
		gchar *name = NULL;
		gchar *host = NULL;
		guint port;

		g_variant_get (parameters, "(&s&su)", &name, &host, &port);

		service = g_object_new (DMAP_TYPE_MDNS_SERVICE,
					"service-name", name,
					"name", name,
					"host", host,
					"port", port,
					"password-protected", FALSE,
					NULL);

		rb_debug ("adding DAAP source %s (%s:%d)", name, host, port);
		mdns_service_added (NULL, service, plugin);

		g_dbus_method_invocation_return_value (invocation, NULL);

		g_free(name);
		g_free(host);
		g_object_unref (service);
	} else if (g_strcmp0 (method_name, "RemoveDAAPSource") == 0) {
		const char *service_name;

		g_variant_get (parameters, "(&s)", &service_name);
		rb_debug ("removing DAAP source %s", service_name);
		mdns_service_removed (plugin->mdns_browser, service_name, plugin);

		g_dbus_method_invocation_return_value (invocation, NULL);
	}
}

static const GDBusInterfaceVTable daap_dbus_vtable = {
	(GDBusInterfaceMethodCallFunc) daap_dbus_method_call,
	NULL,
	NULL
};

static void
register_daap_dbus_iface (RBDaapPlugin *plugin)
{
	g_autoptr(GError) error = NULL;
	g_autoptr(GDBusNodeInfo) node_info = NULL;
	GDBusInterfaceInfo *iface_info;

	if (plugin->dbus_intf_id != 0) {
		rb_debug ("DAAP DBus interface already registered");
		return;
	}

	if (plugin->bus == NULL) {
		plugin->bus = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
		if (plugin->bus == NULL) {
			rb_debug ("Unable to register DAAP DBus interface: %s", error->message);
			return;
		}
	}

	node_info = g_dbus_node_info_new_for_xml (rb_daap_dbus_iface, &error);
	if (error != NULL) {
		rb_debug ("Unable to parse DAAP DBus spec: %s", error->message);
		return;
	}

	iface_info = g_dbus_node_info_lookup_interface (node_info, DAAP_DBUS_IFACE);
	plugin->dbus_intf_id =
		g_dbus_connection_register_object (plugin->bus,
						   DAAP_DBUS_PATH,
						   iface_info,
						   &daap_dbus_vtable,
						   g_object_ref (plugin),
						   g_object_unref,
						   &error);
	if (error != NULL)
		rb_debug ("Unable to register DAAP DBus interface: %s", error->message);
}

static void
unregister_daap_dbus_iface (RBDaapPlugin *plugin)
{
	if (plugin->dbus_intf_id == 0) {
		rb_debug ("DAAP DBus interface not registered");
		return;
	}

	if (plugin->bus == NULL) {
		rb_debug ("no bus connection");
		return;
	}

	g_dbus_connection_unregister_object (plugin->bus, plugin->dbus_intf_id);
	plugin->dbus_intf_id = 0;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_daap_plugin_register_type (G_TYPE_MODULE (module));
	_rb_daap_container_record_register_type (G_TYPE_MODULE (module));
	_rb_daap_record_factory_register_type (G_TYPE_MODULE (module));
	_rb_daap_record_register_type (G_TYPE_MODULE (module));
	_rb_daap_source_register_type (G_TYPE_MODULE (module));
	_rb_dacp_pairing_page_register_type (G_TYPE_MODULE (module));
	_rb_dacp_player_register_type (G_TYPE_MODULE (module));
	_rb_dmap_container_db_adapter_register_type (G_TYPE_MODULE (module));
	_rb_rhythmdb_dmap_db_adapter_register_type (G_TYPE_MODULE (module));
	_rb_rhythmdb_query_model_dmap_db_adapter_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_DAAP_PLUGIN);
	peas_object_module_register_extension_type (module,
						    PEAS_GTK_TYPE_CONFIGURABLE,
						    RB_TYPE_DAAP_PLUGIN);
}
