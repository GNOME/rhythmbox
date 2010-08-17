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

#include "rb-audioscrobbler-service.h"
#include "rb-audioscrobbler-profile-source.h"
#include "rb-plugin.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "eel-gconf-extensions.h"


#define RB_TYPE_AUDIOSCROBBLER_PLUGIN		(rb_audioscrobbler_plugin_get_type ())
#define RB_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPlugin))
#define RB_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))
#define RB_IS_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_IS_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_AUDIOSCROBBLER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))

#define CONF_AUDIOSCROBBLER_PREFIX CONF_PREFIX "/plugins/audioscrobbler"
#define CONF_LASTFM_ENABLED CONF_AUDIOSCROBBLER_PREFIX "/Last.fm/enabled"
#define CONF_LIBREFM_ENABLED CONF_AUDIOSCROBBLER_PREFIX "/Libre.fm/enabled"

typedef struct
{
	RBPlugin parent;
	RBShell *shell;

	GtkWidget *config_dialog;

	/* Last.fm */
	GtkWidget *lastfm_enabled_check;
	guint lastfm_enabled_notification_id;
	RBSource *lastfm_source;

	/* Libre.fm */
	GtkWidget *librefm_enabled_check;
	guint librefm_enabled_notification_id;
	RBSource *librefm_source;
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
static void lastfm_enabled_changed_cb (GConfClient *client,
                                       guint cnxn_id,
                                       GConfEntry *entry,
                                       RBAudioscrobblerPlugin *plugin);
void librefm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                       RBAudioscrobblerPlugin *plugin);
static void librefm_enabled_changed_cb (GConfClient *client,
                                        guint cnxn_id,
                                        GConfEntry *entry,
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
	RBAudioscrobblerPlugin *plugin;
	plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

	plugin->shell = shell;
	init_config_dialog (plugin);

	plugin->lastfm_enabled_notification_id =
		eel_gconf_notification_add (CONF_LASTFM_ENABLED,
		                            (GConfClientNotifyFunc) lastfm_enabled_changed_cb,
		                            plugin);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->lastfm_enabled_check),
	                              eel_gconf_get_boolean (CONF_LASTFM_ENABLED));
	if (eel_gconf_get_boolean (CONF_LASTFM_ENABLED) == TRUE) {
		RBAudioscrobblerService *lastfm;
		lastfm = rb_audioscrobbler_service_new_lastfm ();
		plugin->lastfm_source = rb_audioscrobbler_profile_source_new (plugin->shell,
		                                                              RB_PLUGIN (plugin),
		                                                              lastfm);
		g_object_unref (lastfm);
	}

	plugin->librefm_enabled_notification_id =
		eel_gconf_notification_add (CONF_LIBREFM_ENABLED,
		                            (GConfClientNotifyFunc) librefm_enabled_changed_cb,
		                            plugin);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->librefm_enabled_check),
	                              eel_gconf_get_boolean (CONF_LIBREFM_ENABLED));
	if (eel_gconf_get_boolean (CONF_LIBREFM_ENABLED) == TRUE) {
		RBAudioscrobblerService *librefm;
		librefm = rb_audioscrobbler_service_new_librefm ();
		plugin->librefm_source = rb_audioscrobbler_profile_source_new (plugin->shell,
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

	if (plugin->lastfm_source != NULL) {
		rb_source_delete_thyself (plugin->lastfm_source);
		plugin->lastfm_source = NULL;
	}

	if (plugin->librefm_source != NULL) {
		rb_source_delete_thyself (plugin->librefm_source);
		plugin->librefm_source = NULL;
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
	eel_gconf_set_boolean (CONF_LASTFM_ENABLED,
			       gtk_toggle_button_get_active (togglebutton));
}

static void
lastfm_enabled_changed_cb (GConfClient *client,
                           guint cnxn_id,
                           GConfEntry *entry,
                           RBAudioscrobblerPlugin *plugin)
{
	gboolean enabled;
	enabled = gconf_value_get_bool (entry->value);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->lastfm_enabled_check),
	                              enabled);
	if (enabled == TRUE && plugin->lastfm_source == NULL) {
		RBAudioscrobblerService *lastfm;
		lastfm = rb_audioscrobbler_service_new_lastfm ();
		plugin->lastfm_source = rb_audioscrobbler_profile_source_new (plugin->shell,
		                                                              RB_PLUGIN (plugin),
		                                                              lastfm);
		g_object_unref (lastfm);
	} else if (enabled == FALSE && plugin->lastfm_source != NULL) {
		rb_source_delete_thyself (plugin->lastfm_source);
		plugin->lastfm_source = NULL;
	}
}

void
librefm_enabled_check_toggled_cb (GtkToggleButton *togglebutton,
                                  RBAudioscrobblerPlugin *plugin)
{
	eel_gconf_set_boolean (CONF_LIBREFM_ENABLED,
			       gtk_toggle_button_get_active (togglebutton));
}

static void
librefm_enabled_changed_cb (GConfClient *client,
                            guint cnxn_id,
                            GConfEntry *entry,
                            RBAudioscrobblerPlugin *plugin)
{
	gboolean enabled;
	enabled = gconf_value_get_bool (entry->value);

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->librefm_enabled_check),
	                              enabled);

	if (enabled == TRUE && plugin->librefm_source == NULL) {
		RBAudioscrobblerService *librefm;

		librefm = rb_audioscrobbler_service_new_librefm ();
		plugin->librefm_source = rb_audioscrobbler_profile_source_new (plugin->shell,
		                                                               RB_PLUGIN (plugin),
		                                                               librefm);
		g_object_unref (librefm);
	} else if (enabled == FALSE && plugin->librefm_source != NULL) {
		rb_source_delete_thyself (plugin->librefm_source);
		plugin->librefm_source = NULL;
	}
}
