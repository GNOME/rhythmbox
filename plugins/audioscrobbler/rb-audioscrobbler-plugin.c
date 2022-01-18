/*
 * rb-audioscrobbler-plugin.c
 *
 * Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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

#include <config.h>

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <libpeas-gtk/peas-gtk.h>

#include <lib/rb-builder-helpers.h>
#include <lib/rb-debug.h>
#include <lib/rb-file-helpers.h>
#include <sources/rb-display-page-group.h>
#include <plugins/rb-plugin-macros.h>
#include <shell/rb-shell.h>

#include "rb-audioscrobbler-account.h"
#include "rb-audioscrobbler.h"
#include "rb-audioscrobbler-play-order.h"
#include "rb-audioscrobbler-profile-page.h"
#include "rb-audioscrobbler-radio-source.h"
#include "rb-audioscrobbler-radio-track-entry-type.h"
#include "rb-audioscrobbler-service.h"
#include "rb-audioscrobbler-user.h"

#define RB_TYPE_AUDIOSCROBBLER_PLUGIN		(rb_audioscrobbler_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBAudioscrobblerPlugin, rb_audioscrobbler_plugin, RB, AUDIOSCROBBLER_PLUGIN, PeasExtensionBase)

struct _RBAudioscrobblerPlugin
{
	PeasExtensionBase parent;

	GtkWidget *config_dialog;

	/* Last.fm */
	GSettings *lastfm_settings;
	GtkWidget *lastfm_enabled_check;
	RBDisplayPage *lastfm_page;

	/* Libre.fm */
	GSettings *librefm_settings;
	GtkWidget *librefm_enabled_check;
	RBDisplayPage *librefm_page;
};

struct _RBAudioscrobblerPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static GtkWidget *impl_create_configure_widget (PeasGtkConfigurable *bplugin);
static void peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface);

static void lastfm_settings_changed_cb (GSettings *settings,
					const char *key,
					RBAudioscrobblerPlugin *plugin);
static void librefm_settings_changed_cb (GSettings *settings,
					 const char *key,
					 RBAudioscrobblerPlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_AUDIOSCROBBLER_PLUGIN,
		 RBAudioscrobblerPlugin,
		 rb_audioscrobbler_plugin,
		 (G_IMPLEMENT_INTERFACE_DYNAMIC (PEAS_GTK_TYPE_CONFIGURABLE,
						peas_gtk_configurable_iface_init)))

static void
rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin)
{
	rb_debug ("RBAudioscrobblerPlugin initialising");

	plugin->lastfm_settings = g_settings_new_with_path (AUDIOSCROBBLER_SETTINGS_SCHEMA,
							    AUDIOSCROBBLER_SETTINGS_PATH "/Last.fm/");
	plugin->librefm_settings = g_settings_new_with_path (AUDIOSCROBBLER_SETTINGS_SCHEMA,
							     AUDIOSCROBBLER_SETTINGS_PATH "/Libre.fm/");
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBAudioscrobblerPlugin *plugin;
	PeasPluginInfo *plugin_info;
	GtkIconTheme *theme;
	char *icondir;

	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	g_object_get (plugin, "plugin-info", &plugin_info, NULL);
	theme = gtk_icon_theme_get_default ();

	/* installed icon dir */
	icondir = g_build_filename (peas_plugin_info_get_data_dir (plugin_info), "icons", NULL);
	gtk_icon_theme_append_search_path (theme, icondir);
	g_free (icondir);

	g_signal_connect_object (plugin->lastfm_settings,
				 "changed",
				 G_CALLBACK (lastfm_settings_changed_cb),
				 plugin, 0);
	lastfm_settings_changed_cb (plugin->lastfm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY, plugin);

	g_signal_connect_object (plugin->librefm_settings,
				 "changed",
				 G_CALLBACK (librefm_settings_changed_cb),
				 plugin, 0);
	librefm_settings_changed_cb (plugin->librefm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY, plugin);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	if (plugin->config_dialog != NULL) {
		gtk_widget_destroy (plugin->config_dialog);
		plugin->config_dialog = NULL;
	}

	if (plugin->lastfm_settings != NULL) {
		g_object_unref (plugin->lastfm_settings);
		plugin->lastfm_settings = NULL;
	}

	if (plugin->lastfm_page != NULL) {
		rb_display_page_delete_thyself (plugin->lastfm_page);
		g_object_unref (plugin->lastfm_page);
		plugin->lastfm_page = NULL;
	}

	if (plugin->librefm_settings != NULL) {
		g_object_unref (plugin->librefm_settings);
		plugin->librefm_settings = NULL;
	}

	if (plugin->librefm_page != NULL) {
		rb_display_page_delete_thyself (plugin->librefm_page);
		g_object_unref (plugin->librefm_page);
		plugin->librefm_page = NULL;
	}
}

static GtkWidget *
impl_create_configure_widget (PeasGtkConfigurable *bplugin)
{
	RBAudioscrobblerPlugin *plugin;
	char *builderfile;
	GtkBuilder *builder;
	GtkWidget *widget;

	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	builderfile = rb_find_plugin_data_file (G_OBJECT (plugin), "audioscrobbler-preferences.ui");
	if (builderfile == NULL) {
		g_warning ("can't find audioscrobbler-preferences.ui");
		return NULL;
	}

	builder = rb_builder_load (builderfile, plugin);
	g_free (builderfile);

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "config"));
	g_object_ref_sink (widget);

	plugin->lastfm_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "lastfm_enabled_check"));
	g_settings_bind (plugin->lastfm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY, plugin->lastfm_enabled_check, "active", G_SETTINGS_BIND_DEFAULT);
	plugin->librefm_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "librefm_enabled_check"));
	g_settings_bind (plugin->librefm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY, plugin->librefm_enabled_check, "active", G_SETTINGS_BIND_DEFAULT);

	g_object_unref (builder);
	return widget;
}

static void
peas_gtk_configurable_iface_init (PeasGtkConfigurableInterface *iface)
{
	iface->create_configure_widget = impl_create_configure_widget;
}

static void
lastfm_settings_changed_cb (GSettings *settings,
			    const char *key,
			    RBAudioscrobblerPlugin *plugin)
{
	gboolean enabled;
	if (g_strcmp0 (key, AUDIOSCROBBLER_SERVICE_ENABLED_KEY) != 0) {
		return;
	}

	enabled = g_settings_get_boolean (settings, key);
	if (enabled == TRUE && plugin->lastfm_page == NULL) {
		RBAudioscrobblerService *lastfm;
		RBShell *shell;

		lastfm = rb_audioscrobbler_service_new_lastfm ();
		g_object_get (plugin, "object", &shell, NULL);
		plugin->lastfm_page = rb_audioscrobbler_profile_page_new (shell,
		                                                          G_OBJECT (plugin),
		                                                          lastfm);
		g_object_unref (shell);
		g_object_unref (lastfm);
		g_object_ref (plugin->lastfm_page);
	} else if (enabled == FALSE && plugin->lastfm_page != NULL) {
		rb_display_page_delete_thyself (plugin->lastfm_page);
		g_object_unref (plugin->lastfm_page);
		plugin->lastfm_page = NULL;
	}
}

static void
librefm_settings_changed_cb (GSettings *settings,
			     const char *key,
			     RBAudioscrobblerPlugin *plugin)
{
	gboolean enabled;
	if (g_strcmp0 (key, AUDIOSCROBBLER_SERVICE_ENABLED_KEY) != 0) {
		return;
	}

	enabled = g_settings_get_boolean (settings, key);
	if (enabled == TRUE && plugin->librefm_page == NULL) {
		RBAudioscrobblerService *librefm;
		RBShell *shell;

		librefm = rb_audioscrobbler_service_new_librefm ();
		g_object_get (plugin, "object", &shell, NULL);
		plugin->librefm_page = rb_audioscrobbler_profile_page_new (shell,
		                                                           G_OBJECT (plugin),
		                                                           librefm);
		g_object_unref (librefm);
		g_object_unref (shell);
		g_object_ref (plugin->librefm_page);
	} else if (enabled == FALSE && plugin->librefm_page != NULL) {
		rb_display_page_delete_thyself (plugin->librefm_page);
		g_object_unref (plugin->librefm_page);
		plugin->librefm_page = NULL;
	}
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_audioscrobbler_plugin_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_account_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_play_order_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_profile_page_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_radio_source_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_radio_track_entry_type_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_service_register_type (G_TYPE_MODULE (module));
	_rb_audioscrobbler_user_register_type (G_TYPE_MODULE (module));

	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_AUDIOSCROBBLER_PLUGIN);
	peas_object_module_register_extension_type (module,
						    PEAS_GTK_TYPE_CONFIGURABLE,
						    RB_TYPE_AUDIOSCROBBLER_PLUGIN);
}
