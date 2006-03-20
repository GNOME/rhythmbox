/*
 * rb-audioscrobbler-plugin.c
 * 
 * Copyright (C) 2006 James Livingston <jrl@ids.org.au>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-audioscrobbler.h"
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
	gboolean no_registration;

	g_assert (plugin->audioscrobbler == NULL);
	g_object_get (G_OBJECT (shell), 
		      "proxy-config", &proxy_config, 
		      "no-registration", &no_registration,
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
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBAudioscrobblerPlugin *plugin = RB_AUDIOSCROBBLER_PLUGIN (bplugin);

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

		widget =  rb_audioscrobbler_get_config_widget (plugin->audioscrobbler);

		plugin->preferences = gtk_dialog_new_with_buttons (_("Audioscrobbler preferences"),
								   NULL,
								   GTK_DIALOG_DESTROY_WITH_PARENT,
								   GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
								   NULL);
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

