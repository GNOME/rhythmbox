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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include <lib/rb-builder-helpers.h>
#include <lib/rb-debug.h>
#include <sources/rb-display-page-group.h>
#include <shell/rb-plugin.h>
#include <shell/rb-shell.h>

#include "rb-audioscrobbler-service.h"
#include "rb-audioscrobbler-profile-page.h"

#define RB_TYPE_AUDIOSCROBBLER_PLUGIN		(rb_audioscrobbler_plugin_get_type ())
#define RB_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPlugin))
#define RB_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))
#define RB_IS_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_IS_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_AUDIOSCROBBLER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))

typedef struct
{
	RBPlugin parent;
	RBShell *shell;

	GtkWidget *config_dialog;

	/* Last.fm */
	GSettings *lastfm_settings;
	GtkWidget *lastfm_enabled_check;
	RBDisplayPage *lastfm_page;

	/* Libre.fm */
	GSettings *librefm_settings;
	GtkWidget *librefm_enabled_check;
	RBDisplayPage *librefm_page;
} RBAudioscrobblerPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBAudioscrobblerPluginClass;

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_audioscrobbler_plugin_get_type		(void) G_GNUC_CONST;

static void rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin);
static void rb_audioscrobbler_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);
static GtkWidget *impl_create_configure_dialog (RBPlugin *bplugin);
static void init_config_dialog (RBAudioscrobblerPlugin *plugin);
void config_dialog_response_cb (GtkWidget *dialog, gint response,
                                RBAudioscrobblerPlugin *plugin);
void lastfm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                      RBAudioscrobblerPlugin *plugin);
static void lastfm_settings_changed_cb (GSettings *settings,
					const char *key,
					RBAudioscrobblerPlugin *plugin);
void librefm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                       RBAudioscrobblerPlugin *plugin);
static void librefm_settings_changed_cb (GSettings *settings,
					 const char *key,
					 RBAudioscrobblerPlugin *plugin);

RB_PLUGIN_REGISTER(RBAudioscrobblerPlugin, rb_audioscrobbler_plugin)

static void
rb_audioscrobbler_plugin_class_init (RBAudioscrobblerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_audioscrobbler_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	plugin_class->create_configure_dialog = impl_create_configure_dialog;
}

static void
rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin)
{
	rb_debug ("RBAudioscrobblerPlugin initialising");
}

static void
rb_audioscrobbler_plugin_finalize (GObject *object)
{
	rb_debug ("RBAudioscrobblerPlugin finalising");

	G_OBJECT_CLASS (rb_audioscrobbler_plugin_parent_class)->finalize (object);
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	gboolean enabled;
	RBAudioscrobblerPlugin *plugin;

	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	plugin->shell = shell;
	init_config_dialog (plugin);

	plugin->lastfm_settings = g_settings_new_with_path (AUDIOSCROBBLER_SETTINGS_SCHEMA,
							    AUDIOSCROBBLER_SETTINGS_PATH "/Last.fm");
	g_signal_connect_object (plugin->lastfm_settings,
				 "changed",
				 G_CALLBACK (lastfm_settings_changed_cb),
				 plugin, 0);
	enabled = g_settings_get_boolean (plugin->lastfm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->lastfm_enabled_check), enabled);
	if (enabled) {
		RBAudioscrobblerService *lastfm;
		lastfm = rb_audioscrobbler_service_new_lastfm ();
		plugin->lastfm_page = rb_audioscrobbler_profile_page_new (plugin->shell,
									  RB_PLUGIN (plugin),
									  lastfm);
		g_object_unref (lastfm);
	}

	plugin->librefm_settings = g_settings_new_with_path (AUDIOSCROBBLER_SETTINGS_SCHEMA,
							     AUDIOSCROBBLER_SETTINGS_PATH "/Libre.fm");
	g_signal_connect_object (plugin->librefm_settings,
				 "changed",
				 G_CALLBACK (librefm_settings_changed_cb),
				 plugin, 0);
	enabled = g_settings_get_boolean (plugin->librefm_settings, AUDIOSCROBBLER_SERVICE_ENABLED_KEY);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->librefm_enabled_check), enabled);
	if (enabled) {
		RBAudioscrobblerService *librefm;
		librefm = rb_audioscrobbler_service_new_librefm ();
		plugin->librefm_page = rb_audioscrobbler_profile_page_new (plugin->shell,
		                                                           RB_PLUGIN (plugin),
		                                                           librefm);
		g_object_unref (librefm);
	}
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
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
		plugin->lastfm_page = NULL;
	}

	if (plugin->librefm_settings != NULL) {
		g_object_unref (plugin->librefm_settings);
		plugin->librefm_settings = NULL;
	}

	if (plugin->librefm_page != NULL) {
		rb_display_page_delete_thyself (plugin->librefm_page);
		plugin->librefm_page = NULL;
	}
}

static GtkWidget *
impl_create_configure_dialog (RBPlugin *bplugin)
{
	RBAudioscrobblerPlugin *plugin;
	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	if (plugin->config_dialog == NULL) {
		init_config_dialog (plugin);
	}

	gtk_widget_show_all (plugin->config_dialog);
	return plugin->config_dialog;
}

static void
init_config_dialog (RBAudioscrobblerPlugin *plugin)
{
	char *builderfile;
	GtkBuilder *builder;

	if (plugin->config_dialog != NULL) {
		return;
	}

	builderfile = rb_plugin_find_file (RB_PLUGIN (plugin), "audioscrobbler-preferences.ui");
	if (builderfile == NULL) {
		g_warning ("can't find audioscrobbler-preferences.ui");
		return;
	}

	builder = rb_builder_load (builderfile, plugin);
	g_free (builderfile);

	plugin->config_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "config_dialog"));
	gtk_widget_hide_on_delete (plugin->config_dialog);

	plugin->lastfm_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "lastfm_enabled_check"));
	plugin->librefm_enabled_check = GTK_WIDGET (gtk_builder_get_object (builder, "librefm_enabled_check"));

	g_object_unref (builder);
}

void
config_dialog_response_cb (GtkWidget *dialog, gint response,
                           RBAudioscrobblerPlugin *plugin)
{
	gtk_widget_hide (dialog);
}

void
lastfm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                 RBAudioscrobblerPlugin *plugin)
{
	g_settings_set_boolean (plugin->lastfm_settings,
				AUDIOSCROBBLER_SERVICE_ENABLED_KEY,
				gtk_toggle_button_get_active (togglebutton));
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
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->lastfm_enabled_check),
	                              enabled);
	if (enabled == TRUE && plugin->lastfm_page == NULL) {
		RBAudioscrobblerService *lastfm;
		lastfm = rb_audioscrobbler_service_new_lastfm ();
		plugin->lastfm_page = rb_audioscrobbler_profile_page_new (plugin->shell,
		                                                          RB_PLUGIN (plugin),
		                                                          lastfm);
		g_object_unref (lastfm);
	} else if (enabled == FALSE && plugin->lastfm_page != NULL) {
		rb_display_page_delete_thyself (plugin->lastfm_page);
		plugin->lastfm_page = NULL;
	}
}

void
librefm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                  RBAudioscrobblerPlugin *plugin)
{
	g_settings_set_boolean (plugin->librefm_settings,
				AUDIOSCROBBLER_SERVICE_ENABLED_KEY,
				gtk_toggle_button_get_active (togglebutton));
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
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->librefm_enabled_check),
	                              enabled);

	if (enabled == TRUE && plugin->librefm_page == NULL) {
		RBAudioscrobblerService *librefm;

		librefm = rb_audioscrobbler_service_new_librefm ();
		plugin->librefm_page = rb_audioscrobbler_profile_page_new (plugin->shell,
		                                                           RB_PLUGIN (plugin),
		                                                           librefm);
		g_object_unref (librefm);
	} else if (enabled == FALSE && plugin->librefm_page != NULL) {
		rb_display_page_delete_thyself (plugin->librefm_page);
		plugin->librefm_page = NULL;
	}
}
