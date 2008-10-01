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

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-audioscrobbler.h"
#include "rb-lastfm-source.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"


#define RB_TYPE_AUDIOSCROBBLER_PLUGIN		(rb_audioscrobbler_plugin_get_type ())
#define RB_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPlugin))
#define RB_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))
#define RB_IS_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_IS_AUDIOSCROBBLER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_AUDIOSCROBBLER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))

typedef struct
{
	RBPlugin parent;
	RBAudioscrobbler *audioscrobbler;
	GtkWidget *preferences;
	guint ui_merge_id;

	RBSource *lastfm_source;
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
static GtkWidget* impl_create_configure_dialog (RBPlugin *plugin);

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
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (object);

	rb_debug ("RBAudioscrobblerPlugin finalising");

	g_assert (plugin->audioscrobbler == NULL);

	if (plugin->preferences)
		gtk_widget_destroy (plugin->preferences);

	G_OBJECT_CLASS (rb_audioscrobbler_plugin_parent_class)->finalize (object);
}



static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);
	RBProxyConfig *proxy_config;
	GtkUIManager *uimanager = NULL;
	gboolean no_registration;
	char *file;

    /* Source icon data */
    gchar *icon_filename;
    gint icon_size;
    GdkPixbuf *icon_pixbuf;

	g_assert (plugin->audioscrobbler == NULL);
	g_object_get (G_OBJECT (shell),
		      "proxy-config", &proxy_config,
		      "no-registration", &no_registration,
		      "ui-manager", &uimanager,
		      NULL);

	/*
	 * Don't use audioscrobbler when the no-registration flag is set.
	 * This flag is only used to run multiple instances at the same time, and
	 * last.fm only allows one active client per user.
	 */
	if (!no_registration) {
		plugin->audioscrobbler = rb_audioscrobbler_new (RB_SHELL_PLAYER (rb_shell_get_player (shell)),
								proxy_config);
	}
	g_object_unref (G_OBJECT (proxy_config));

	file = rb_plugin_find_file (bplugin, "audioscrobbler-ui.xml");
	plugin->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
							       file,
							       NULL);
	g_free (file);

	plugin->lastfm_source = rb_lastfm_source_new (bplugin, shell);
    
    icon_filename = rb_plugin_find_file (bplugin, "as-icon.png");
    gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_size, NULL);
    icon_pixbuf = gdk_pixbuf_new_from_file_at_size (icon_filename,
                                                    icon_size, icon_size,
                                                    NULL);
    g_free (icon_filename);
    rb_source_set_pixbuf (plugin->lastfm_source, icon_pixbuf);
    g_object_unref (icon_pixbuf);
    
	rb_shell_append_source (shell, plugin->lastfm_source, NULL);

	g_object_unref (G_OBJECT (uimanager));
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);
	GtkUIManager *uimanager = NULL;

	g_object_get (G_OBJECT (shell),
		      "ui-manager", &uimanager,
		      NULL);

	rb_source_delete_thyself (plugin->lastfm_source);
	plugin->lastfm_source = NULL;

	gtk_ui_manager_remove_ui (uimanager, plugin->ui_merge_id);
	g_object_unref (G_OBJECT (uimanager));

	if (plugin->audioscrobbler) {
		g_object_unref (plugin->audioscrobbler);
		plugin->audioscrobbler = NULL;
	}
}

static void
preferences_response_cb (GtkWidget *dialog, gint response, RBPlugin *plugin)
{
	gtk_widget_hide (dialog);
}

static GtkWidget*
impl_create_configure_dialog (RBPlugin *bplugin)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);
	if (plugin->audioscrobbler == NULL)
		return NULL;

	if (plugin->preferences == NULL) {
		GtkWidget *widget;

		widget =  rb_audioscrobbler_get_config_widget (plugin->audioscrobbler, bplugin);

		plugin->preferences = gtk_dialog_new_with_buttons (_("Last.fm Preferences"),
								   NULL,
								   GTK_DIALOG_DESTROY_WITH_PARENT,
								   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
								   NULL);
                gtk_dialog_set_has_separator (GTK_DIALOG (plugin->preferences), FALSE);
                gtk_container_set_border_width (GTK_CONTAINER (plugin->preferences), 5);
                gtk_window_set_resizable (GTK_WINDOW (plugin->preferences), FALSE);

		g_signal_connect (G_OBJECT (plugin->preferences),
				  "response",
				  G_CALLBACK (preferences_response_cb),
				  plugin);
		gtk_widget_hide_on_delete (plugin->preferences);

		gtk_container_add (GTK_CONTAINER (GTK_DIALOG (plugin->preferences)->vbox), widget);
	}

	gtk_widget_show_all (plugin->preferences);
	return plugin->preferences;
}

