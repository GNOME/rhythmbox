/*
 * heavily based on code from Gedit
 *
 * Copyright (C) 2002-2005 Paolo Maggi 
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, 
 * Boston, MA 02110-1301  USA. 
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "rb-plugin.h"

G_DEFINE_TYPE (RBPlugin, rb_plugin, G_TYPE_OBJECT)

static void
dummy (RBPlugin *plugin, RBShell *shell)
{
	/* Empty */
}

static GtkWidget *
create_configure_dialog	(RBPlugin *plugin)
{
	return NULL;
}

static gboolean
is_configurable (RBPlugin *plugin)
{
	return (RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog !=
		create_configure_dialog);
}

static void 
rb_plugin_class_init (RBPluginClass *klass)
{
	klass->activate = dummy;
	klass->deactivate = dummy;
	
	klass->create_configure_dialog = create_configure_dialog;
	klass->is_configurable = is_configurable;
}

static void
rb_plugin_init (RBPlugin *plugin)
{
	/* Empty */
}

void
rb_plugin_activate (RBPlugin *plugin,
		    RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));
	
	RB_PLUGIN_GET_CLASS (plugin)->activate (plugin, shell);
}

void
rb_plugin_deactivate	(RBPlugin *plugin,
			 RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));

	RB_PLUGIN_GET_CLASS (plugin)->deactivate (plugin, shell);
}
				 
gboolean
rb_plugin_is_configurable (RBPlugin *plugin)
{
	g_return_val_if_fail (RB_IS_PLUGIN (plugin), FALSE);

	return RB_PLUGIN_GET_CLASS (plugin)->is_configurable (plugin);
}

GtkWidget *
rb_plugin_create_configure_dialog (RBPlugin *plugin)
{
	g_return_val_if_fail (RB_IS_PLUGIN (plugin), NULL);
	
	return RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog (plugin);
}
