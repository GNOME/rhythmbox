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
#define RB_AUDIOSCROBBLER_PLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPlugin))
#define RB_AUDIOSCROBBLER_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))
#define RB_IS_AUDIOSCROBBLER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_IS_AUDIOSCROBBLER_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_AUDIOSCROBBLER_PLUGIN))
#define RB_AUDIOSCROBBLER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginClass))

typedef struct
{
	RBAudioscrobbler *audioscrobbler;
	GtkWidget *preferences;
} RBAudioscrobblerPluginPrivate;

typedef struct
{
	RBPlugin parent;
	RBAudioscrobblerPluginPrivate *priv;
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
#define RB_AUDIOSCROBBLER_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_AUDIOSCROBBLER_PLUGIN, RBAudioscrobblerPluginPrivate))


static void
rb_audioscrobbler_plugin_class_init (RBAudioscrobblerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_audioscrobbler_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	plugin_class->create_configure_dialog = impl_create_configure_dialog;
	
	g_type_class_add_private (object_class, sizeof (RBAudioscrobblerPluginPrivate));
}

static void
rb_audioscrobbler_plugin_init (RBAudioscrobblerPlugin *plugin)
{
	plugin->priv = RB_AUDIOSCROBBLER_PLUGIN_GET_PRIVATE (plugin);

	rb_debug ("RBAudioscrobblerPlugin initialising");
}

static void
rb_audioscrobbler_plugin_finalize (GObject *object)
{
	RBAudioscrobblerPluginPrivate *priv = RB_AUDIOSCROBBLER_PLUGIN (object)->priv;

	rb_debug ("RBAudioscrobblerPlugin finalising");

	g_assert (priv->audioscrobbler == NULL);

	if (priv->preferences)
		gtk_widget_destroy (priv->preferences);

	G_OBJECT_CLASS (rb_audioscrobbler_plugin_parent_class)->finalize (object);
}



static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	RBAudioscrobblerPluginPrivate *priv = RB_AUDIOSCROBBLER_PLUGIN (plugin)->priv;

	g_assert (priv->audioscrobbler == NULL);
	priv->audioscrobbler = rb_audioscrobbler_new (RB_SHELL_PLAYER (rb_shell_get_player (shell)));
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell *shell)
{
	RBAudioscrobblerPluginPrivate *priv = RB_AUDIOSCROBBLER_PLUGIN (plugin)->priv;

	g_assert (priv->audioscrobbler != NULL);
	g_object_unref (priv->audioscrobbler);
	priv->audioscrobbler = NULL;
}

static void
preferences_response_cb (GtkWidget *dialog, gint response, RBPlugin *plugin)
{
	gtk_widget_hide (dialog);
}

static GtkWidget*
impl_create_configure_dialog (RBPlugin *plugin)
{
	RBAudioscrobblerPluginPrivate *priv = RB_AUDIOSCROBBLER_PLUGIN (plugin)->priv;

	if (priv->preferences == NULL) {
		GtkWidget *widget;

		widget =  rb_audioscrobbler_get_config_widget (priv->audioscrobbler);

		priv->preferences = gtk_dialog_new_with_buttons (_("Audioscrobbler preferences"),
								 NULL,
								 GTK_DIALOG_DESTROY_WITH_PARENT,
								 GTK_STOCK_CLOSE, GTK_RESPONSE_CLOSE,
								 NULL);
		g_signal_connect (G_OBJECT (priv->preferences),
				  "response",
				  G_CALLBACK (preferences_response_cb),
				  plugin);
		gtk_widget_hide_on_delete (priv->preferences);
		
		gtk_container_add (GTK_CONTAINER (GTK_DIALOG (priv->preferences)->vbox), widget);
	}
	
	gtk_widget_show_all (priv->preferences);
	return priv->preferences;
}
