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

#include <string.h> /* For strlen */
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin-macros.h"
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
	PeasExtensionBase parent;
} RBSamplePlugin;

typedef struct
{
	PeasExtensionBaseClass parent_class;
} RBSamplePluginClass;


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void rb_sample_plugin_init (RBSamplePlugin *plugin);

RB_DEFINE_PLUGIN(RB_TYPE_SAMPLE_PLUGIN, RBSamplePlugin, rb_sample_plugin,)

static void
rb_sample_plugin_init (RBSamplePlugin *plugin)
{
	rb_debug ("RBSamplePlugin initialising");
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBShell *shell;

	g_object_get (plugin, "object", &shell, NULL);
	rb_error_dialog (NULL, _("Sample Plugin"), "Sample plugin activated, with shell %p", shell);
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *plugin)
{
	rb_error_dialog (NULL, _("Sample Plugin"), "Sample plugin deactivated");
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_sample_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_SAMPLE_PLUGIN);
}
