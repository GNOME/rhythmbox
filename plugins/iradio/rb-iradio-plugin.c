/*
 * rb-iradio-plugin.c
 * 
 * Copyright (C) 2006  Jonathan Matthew
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
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

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-iradio-source.h"
#include "rb-file-helpers.h"


#define RB_TYPE_IRADIO_PLUGIN		(rb_iradio_plugin_get_type ())
#define RB_IRADIO_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IRADIO_PLUGIN, RBIRadioPlugin))
#define RB_IRADIO_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IRADIO_PLUGIN, RBIRadioPluginClass))
#define RB_IS_IRADIO_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IRADIO_PLUGIN))
#define RB_IS_IRADIO_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IRADIO_PLUGIN))
#define RB_IRADIO_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IRADIO_PLUGIN, RBIRadioPluginClass))

typedef struct
{
	RBPlugin parent;
	RBSource *source;
	guint ui_merge_id;
} RBIRadioPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBIRadioPluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_iradio_plugin_get_type		(void) G_GNUC_CONST;


static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER(RBIRadioPlugin, rb_iradio_plugin)

static void
rb_iradio_plugin_init (RBIRadioPlugin *plugin)
{
	rb_debug ("RBIRadioPlugin initialising");
}

static void
rb_iradio_plugin_finalize (GObject *object)
{
/*
	RBIRadioPlugin *plugin = RB_IRADIO_PLUGIN (object);
*/
	rb_debug ("RBIRadioPlugin finalising");

	G_OBJECT_CLASS (rb_iradio_plugin_parent_class)->finalize (object);
}



static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	RBIRadioPlugin *pi = RB_IRADIO_PLUGIN (plugin);
	GtkUIManager *uimanager;

	pi->source = rb_iradio_source_new (shell);
	rb_shell_append_source (shell, pi->source, NULL);

	g_object_get (G_OBJECT (shell), "ui-manager", &uimanager, NULL);
	pi->ui_merge_id = gtk_ui_manager_add_ui_from_file (uimanager,
							   rb_plugin_find_file (plugin, "iradio-ui.xml"),
							   NULL);
	g_object_unref (G_OBJECT (uimanager));
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell *shell)
{
	RBIRadioPlugin *pi = RB_IRADIO_PLUGIN (plugin);
	GtkUIManager *uimanager;

	g_object_get (G_OBJECT (shell), "ui-manager", &uimanager, NULL);
	gtk_ui_manager_remove_ui (uimanager, pi->ui_merge_id);
	g_object_unref (G_OBJECT (uimanager));

	rb_source_delete_thyself (pi->source);
	g_object_unref (G_OBJECT (pi->source));
	pi->source = NULL;
}


static void
rb_iradio_plugin_class_init (RBIRadioPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_iradio_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

