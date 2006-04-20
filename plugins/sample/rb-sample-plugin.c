/*
 * rb-sample-plugin.h
 * 
 * Copyright (C) 2002-2005 - Paolo Maggi
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

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"


#define RB_TYPE_SAMPLE_PLUGIN		(rb_sample_plugin_get_type ())
#define RB_SAMPLE_PLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SAMPLE_PLUGIN, RBSamplePlugin))
#define RB_SAMPLE_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SAMPLE_PLUGIN, RBSamplePluginClass))
#define RB_IS_SAMPLE_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SAMPLE_PLUGIN))
#define RB_IS_SAMPLE_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SAMPLE_PLUGIN))
#define RB_SAMPLE_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SAMPLE_PLUGIN, RBSamplePluginClass))

typedef struct
{
	gpointer dummy;
} RBSamplePluginPrivate;

typedef struct
{
	RBPlugin parent;
	RBSamplePluginPrivate *priv;
} RBSamplePlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBSamplePluginClass;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_sample_plugin_get_type		(void) G_GNUC_CONST;



static void rb_sample_plugin_init (RBSamplePlugin *plugin);
static void rb_sample_plugin_finalize (GObject *object);
static void impl_activate (RBPlugin *plugin, RBShell *shell);
static void impl_deactivate (RBPlugin *plugin, RBShell *shell);

RB_PLUGIN_REGISTER(RBSamplePlugin, rb_sample_plugin)
#define RB_SAMPLE_PLUGIN_GET_PRIVATE(object) (G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_SAMPLE_PLUGIN, RBSamplePluginPrivate))


static void
rb_sample_plugin_class_init (RBSamplePluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->finalize = rb_sample_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
	
	g_type_class_add_private (object_class, sizeof (RBSamplePluginPrivate));
}

static void
rb_sample_plugin_init (RBSamplePlugin *plugin)
{
	plugin->priv = RB_SAMPLE_PLUGIN_GET_PRIVATE (plugin);

	rb_debug ("RBSamplePlugin initialising");
}

static void
rb_sample_plugin_finalize (GObject *object)
{
/*
	RBSamplePlugin *plugin = RB_SAMPLE_PLUGIN (object);
*/
	rb_debug ("RBSamplePlugin finalising");

	G_OBJECT_CLASS (rb_sample_plugin_parent_class)->finalize (object);
}



static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	rb_error_dialog (NULL, _("Sample Plugin"), "Sample plugin activated, with shell %p", shell);
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell *shell)
{
	rb_error_dialog (NULL, _("Sample Plugin"), "Sample plugin deactivated");
}


