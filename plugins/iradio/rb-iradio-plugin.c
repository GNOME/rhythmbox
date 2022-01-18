/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * rb-iradio-plugin.c
 *
 * Copyright (C) 2006  Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>

#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-dialog.h"
#include "rb-iradio-source.h"
#include "rb-iradio-source-search.h"
#include "rb-station-properties-dialog.h"
#include "rb-file-helpers.h"
#include "rb-display-page-group.h"


#define RB_TYPE_IRADIO_PLUGIN		(rb_iradio_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBIRadioPlugin, rb_iradio_plugin, RB, IRADIO_PLUGIN, PeasExtensionBase)

struct _RBIRadioPlugin
{
	PeasExtensionBase parent;

	RBSource *source;
};

struct _RBIRadioPluginClass
{
	PeasExtensionBaseClass parent_class;
};


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

RB_DEFINE_PLUGIN(RB_TYPE_IRADIO_PLUGIN, RBIRadioPlugin, rb_iradio_plugin,)

static void
rb_iradio_plugin_init (RBIRadioPlugin *plugin)
{
	rb_debug ("RBIRadioPlugin initialising");
}

static void
impl_activate (PeasActivatable *plugin)
{
	RBIRadioPlugin *pi = RB_IRADIO_PLUGIN (plugin);
	RBShell *shell;

	g_object_get (pi, "object", &shell, NULL);
	pi->source = rb_iradio_source_new (shell, G_OBJECT (plugin));
	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (pi->source), RB_DISPLAY_PAGE_GROUP_LIBRARY);

	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *plugin)
{
	RBIRadioPlugin *pi = RB_IRADIO_PLUGIN (plugin);

	rb_display_page_delete_thyself (RB_DISPLAY_PAGE (pi->source));
	pi->source = NULL;
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_iradio_plugin_register_type (G_TYPE_MODULE (module));
	_rb_iradio_source_register_type (G_TYPE_MODULE (module));
	_rb_iradio_source_search_register_type (G_TYPE_MODULE (module));
	_rb_station_properties_dialog_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_IRADIO_PLUGIN);
}
