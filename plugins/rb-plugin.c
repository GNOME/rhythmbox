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

#include <glib.h>
#include <libgnome/gnome-util.h>

#include "rb-plugin.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"


G_DEFINE_TYPE (RBPlugin, rb_plugin, G_TYPE_OBJECT)
#define RB_PLUGIN_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_PLUGIN, RBPluginPrivate))

static void rb_plugin_finalise (GObject *o);
static void rb_plugin_set_property (GObject *object,
				    guint prop_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_plugin_get_property (GObject *object,
				    guint prop_id,
				    GValue *value,
				    GParamSpec *pspec);

typedef struct {
	char *name;
} RBPluginPrivate;

enum
{
	PROP_0,
	PROP_NAME,
};



static gboolean
is_configurable (RBPlugin *plugin)
{
	return (RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog != NULL);
}

static void
rb_plugin_class_init (RBPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_plugin_finalise;
	object_class->get_property = rb_plugin_get_property;
	object_class->set_property = rb_plugin_set_property;

	klass->activate = NULL;
	klass->deactivate = NULL;
	klass->create_configure_dialog = NULL;
	klass->is_configurable = is_configurable;

	/* this should be a construction property, but due to the python plugin hack can't be */
	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "name",
							      "name",
							      NULL,
							      G_PARAM_READWRITE /*| G_PARAM_CONSTRUCT_ONLY*/));

	g_type_class_add_private (klass, sizeof (RBPluginPrivate));
}

static void
rb_plugin_init (RBPlugin *plugin)
{
	/* Empty */
}

static void
rb_plugin_finalise (GObject *object)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	g_free (priv->name);

	G_OBJECT_CLASS (rb_plugin_parent_class)->finalize (object);
}

static void
rb_plugin_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_value_dup_string (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_plugin_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

void
rb_plugin_activate (RBPlugin *plugin,
		    RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));

	if (RB_PLUGIN_GET_CLASS (plugin)->activate)
		RB_PLUGIN_GET_CLASS (plugin)->activate (plugin, shell);
}

void
rb_plugin_deactivate	(RBPlugin *plugin,
			 RBShell *shell)
{
	g_return_if_fail (RB_IS_PLUGIN (plugin));
	g_return_if_fail (RB_IS_SHELL (shell));

	if (RB_PLUGIN_GET_CLASS (plugin)->deactivate)
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

	if (RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog)
		return RB_PLUGIN_GET_CLASS (plugin)->create_configure_dialog (plugin);
	else
		return NULL;
}

char*
rb_plugin_find_file (RBPlugin *plugin, const char *file)
{
	RBPluginPrivate *priv = RB_PLUGIN_GET_PRIVATE (plugin);
	char *ret = NULL;

	/* user installed plugins */
	if (ret == NULL && priv->name) {
		char *tmp1, *tmp2;

		tmp1 = gnome_util_home_file (USER_RB_PLUGINS_LOCATION);
		tmp2 = g_build_path (G_DIR_SEPARATOR_S, tmp1, priv->name, file, NULL);
		g_free (tmp1);
		if (g_file_test (tmp2, G_FILE_TEST_EXISTS)) {
			ret = tmp2;
		} else {
			g_free (tmp2);
		}
	}

#ifdef SHARE_UNINSTALLED_DIR
	/* data when running uninstalled */
	if (ret == NULL && priv->name) {
		char *tmp;

		tmp = g_build_path (G_DIR_SEPARATOR_S, UNINSTALLED_PLUGINS_LOCATION, priv->name, file, NULL);
		if (g_file_test (tmp, G_FILE_TEST_EXISTS)) {
			ret = tmp;
		} else {
			g_free (tmp);
		}
	}
#endif

	/* global plugin data */
	if (ret == NULL && priv->name) {
		char *tmp;

		tmp = g_build_path (G_DIR_SEPARATOR_S, SHARE_DIR "/plugins/", priv->name, file, NULL);
		if (g_file_test (tmp, G_FILE_TEST_EXISTS)) {
			ret = tmp;
		} else {
			g_free (tmp);
		}
	}

	/* global data files */
	if (ret == NULL) {
		const char *f;

		f = rb_file (file);
		if (f)
			ret = g_strdup (f);
	}

	rb_debug ("found '%s' when searching for file '%s' for plugin '%s'",
		  ret, file, priv->name);
	return ret;
}

