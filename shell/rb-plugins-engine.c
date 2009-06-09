/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Plugin manager for Rhythmbox, based heavily on the code from gedit.
 *
 * Copyright (C) 2002-2005 Paolo Maggi
 *               2006 James Livingston  <doclivingston@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "eel-gconf-extensions.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "rb-util.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-dialog.h"

#include "rb-module.h"
#ifdef ENABLE_PYTHON
#include "rb-python-module.h"
#endif

#include "rb-plugins-engine.h"

#define PLUGIN_EXT	".rb-plugin"

typedef enum
{
	RB_PLUGIN_LOADER_C,
	RB_PLUGIN_LOADER_PY,
} RBPluginLang;

struct _RBPluginInfo
{
	gchar        *file;

	gchar        *location;
	RBPluginLang lang;
	GTypeModule  *module;

	gchar        *name;
	gchar        *desc;
	gchar        **authors;
	gchar        *copyright;
	gchar        *website;

	gchar        *icon_name;
	GdkPixbuf    *icon_pixbuf;

	RBPlugin     *plugin;

	gboolean     active;
	gboolean     visible;
	guint        active_notification_id;
	guint        visible_notification_id;
};

static void rb_plugin_info_free (RBPluginInfo *info);
static void rb_plugins_engine_plugin_active_cb (GConfClient *client,
						guint cnxn_id,
						GConfEntry *entry,
						RBPluginInfo *info);
static void rb_plugins_engine_plugin_visible_cb (GConfClient *client,
						 guint cnxn_id,
						 GConfEntry *entry,
						 RBPluginInfo *info);
static gboolean rb_plugins_engine_activate_plugin_real (RBPluginInfo *info,
							RBShell *shell);
static void rb_plugins_engine_deactivate_plugin_real (RBPluginInfo *info,
						      RBShell *shell);

static GHashTable *rb_plugins = NULL;
guint garbage_collect_id = 0;
RBShell *rb_plugins_shell = NULL;

static RBPluginInfo *
rb_plugins_engine_load (const gchar *file)
{
	RBPluginInfo *info;
	GKeyFile *plugin_file = NULL;
	gchar *str;

	g_return_val_if_fail (file != NULL, NULL);

	rb_debug ("Loading plugin: %s", file);

	info = g_new0 (RBPluginInfo, 1);
	info->file = g_strdup (file);

	plugin_file = g_key_file_new ();
	if (!g_key_file_load_from_file (plugin_file, file, G_KEY_FILE_NONE, NULL)) {
		g_warning ("Bad plugin file: %s", file);
		goto error;
	}

	if (!g_key_file_has_key (plugin_file,
				 "RB Plugin",
				 "IAge",
				 NULL))	{
		rb_debug ("IAge key does not exist in file: %s", file);
		goto error;
	}

	/* Check IAge=1 */
	if (g_key_file_get_integer (plugin_file,
				    "RB Plugin",
				    "IAge",
				    NULL) != 1)	{
		rb_debug ("Wrong IAge in file: %s", file);
		goto error;
	}

	/* Get Location */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Module",
				     NULL);
	if (str) {
		info->location = str;
	} else {
		g_warning ("Could not find 'Module' in %s", file);
		goto error;
	}

	/* Get the loader for this plugin */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Loader",
				     NULL);
	if (str && strcmp(str, "python") == 0) {
		info->lang = RB_PLUGIN_LOADER_PY;
#ifndef ENABLE_PYTHON
		rb_debug ("Cannot load python extension '%s', Rhythmbox was not "
					"compiled with python support", file);
		g_free (str);
		goto error;
#endif
	} else {
		info->lang = RB_PLUGIN_LOADER_C;
	}
	g_free (str);

	/* Get Name */
	str = g_key_file_get_locale_string (plugin_file,
					    "RB Plugin",
					    "Name",
					    NULL, NULL);
	if (str) {
		info->name = str;
	} else {
		g_warning ("Could not find 'Name' in %s", file);
		goto error;
	}

	/* Get Description */
	str = g_key_file_get_locale_string (plugin_file,
					    "RB Plugin",
					    "Description",
					    NULL, NULL);
	if (str) {
		info->desc = str;
	} else {
		rb_debug ("Could not find 'Description' in %s", file);
		info->desc = g_strdup ("");
	}

	/* Get icon name */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Icon",
				     NULL);
	if (str) {
		info->icon_name = str;
	} else {
		rb_debug ("Could not find 'Icon' in %s", file);
		info->icon_name = g_strdup ("");
	}

	/* Get Authors */
	info->authors = g_key_file_get_string_list (plugin_file,
						    "RB Plugin",
						    "Authors",
						    NULL, NULL);
	if (info->authors == NULL)
		rb_debug ("Could not find 'Authors' in %s", file);

	/* Get Copyright */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Copyright",
				     NULL);
	if (str) {
		info->copyright = str;
	} else {
		rb_debug ("Could not find 'Copyright' in %s", file);
		info->copyright = g_strdup ("");
	}

	/* Get Copyright */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Website",
				     NULL);
	if (str) {
		info->website = str;
	} else {
		rb_debug ("Could not find 'Website' in %s", file);
		info->website = g_strdup ("");
	}

	g_key_file_free (plugin_file);

	return info;

error:
	g_free (info->file);
	g_free (info->location);
	g_free (info->name);
	g_free (info);
	g_key_file_free (plugin_file);

	return NULL;
}

static gboolean
rb_plugins_engine_load_cb (GFile *file, gboolean dir, gpointer userdata)
{
	char *plugin_path;
	RBPluginInfo *info;
	char *key_name;
	gboolean activate;
	const char *sep;

	plugin_path = g_file_get_path (file);

	sep = strrchr (plugin_path, G_DIR_SEPARATOR);
	if (sep == NULL)
		sep = plugin_path;
	else
		sep += 1;

	/* don't look inside version control system directories.
	 * most are already covered by excluding hidden files/directories.
	 */
	if (dir && (g_str_has_prefix (sep, "_darcs") || g_str_has_prefix (sep, "CVS"))) {
		rb_debug ("not loading plugin from hidden/VCS directory %s", plugin_path);
		g_free (plugin_path);
		return FALSE;
	}

	if (dir || !g_str_has_suffix (plugin_path, PLUGIN_EXT)) {
		g_free (plugin_path);
		return TRUE;
	}

	info = rb_plugins_engine_load (plugin_path);
	g_free (plugin_path);

	if (info == NULL)
		return TRUE;

	if (g_hash_table_lookup (rb_plugins, info->location)) {
		rb_plugin_info_free (info);
		return TRUE;
	}

	g_hash_table_insert (rb_plugins, info->location, info);
	rb_debug ("Plugin %s loaded", info->name);

	key_name = g_strdup_printf (CONF_PLUGIN_ACTIVE_KEY, info->location);
	info->active_notification_id = eel_gconf_notification_add (key_name,
								   (GConfClientNotifyFunc)rb_plugins_engine_plugin_active_cb,
								   info);
	activate = eel_gconf_get_boolean (key_name);
	g_free (key_name);

	key_name = g_strdup_printf (CONF_PLUGIN_HIDDEN_KEY, info->location);
	info->visible_notification_id = eel_gconf_notification_add (key_name,
								    (GConfClientNotifyFunc)rb_plugins_engine_plugin_visible_cb,
								    info);
	info->visible = !eel_gconf_get_boolean (key_name);
	g_free (key_name);

	if (activate)
		rb_plugins_engine_activate_plugin (info);
	return TRUE;
}

static void
rb_plugins_engine_load_dir (const char *path)
{
	GFile *plugindir;
	char *plugin_uri;

	plugindir = g_file_new_for_commandline_arg (path);
	plugin_uri = g_file_get_uri (plugindir);

	rb_uri_handle_recursively (plugin_uri, NULL, (RBUriRecurseFunc) rb_plugins_engine_load_cb, NULL);

	g_object_unref (plugindir);
	g_free (plugin_uri);
}

static void
rb_plugins_engine_load_all (void)
{
	GList *paths;

	paths = rb_get_plugin_paths ();
	while (paths != NULL) {
		rb_plugins_engine_load_dir (paths->data);
		g_free (paths->data);
		paths = g_list_delete_link (paths, paths);
	}
}

static gboolean
garbage_collect_cb (gpointer data)
{
	rb_plugins_engine_garbage_collect ();
	return TRUE;
}

gboolean
rb_plugins_engine_init (RBShell *shell)
{
	g_return_val_if_fail (rb_plugins == NULL, FALSE);

	if (!g_module_supported ())
	{
		g_warning ("rb is not able to initialize the plugins engine.");
		return FALSE;
	}
	rb_profile_start ("plugins engine init");

	rb_plugins = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)rb_plugin_info_free);

	rb_plugins_shell = shell;
	g_object_ref (G_OBJECT (rb_plugins_shell));
#ifdef ENABLE_PYTHON
	rb_python_module_init_python ();
#endif

	rb_plugins_engine_load_all ();

	garbage_collect_id = g_timeout_add_seconds_full (G_PRIORITY_LOW, 20, garbage_collect_cb, NULL, NULL);

	rb_profile_end ("plugins engine init");

	return TRUE;
}

void
rb_plugins_engine_garbage_collect (void)
{
#ifdef ENABLE_PYTHON
	rb_python_garbage_collect ();
#endif
}

static void
rb_plugin_info_free (RBPluginInfo *info)
{
	if (info->active)
		rb_plugins_engine_deactivate_plugin_real (info, rb_plugins_shell);

	if (info->plugin != NULL) {
		rb_debug ("Unref plugin %s", info->name);

		g_object_unref (info->plugin);

		/* info->module must not be unref since it is not possible to finalize
		 * a type module */
	}

	eel_gconf_notification_remove (info->active_notification_id);
	eel_gconf_notification_remove (info->visible_notification_id);

	g_free (info->file);
	g_free (info->location);
	g_free (info->name);
	g_free (info->desc);
	g_free (info->website);
	g_free (info->copyright);
	g_free (info->icon_name);

	if (info->icon_pixbuf)
		g_object_unref (info->icon_pixbuf);
	g_strfreev (info->authors);

	g_free (info);
}

void
rb_plugins_engine_shutdown (void)
{
	g_hash_table_destroy (rb_plugins);
	rb_plugins = NULL;

	g_object_unref (rb_plugins_shell);
	rb_plugins_shell = NULL;

	g_source_remove (garbage_collect_id);
	rb_plugins_engine_garbage_collect ();

#ifdef ENABLE_PYTHON
	rb_python_shutdown ();
#endif
}

GList *
rb_plugins_engine_get_plugins_list (void)
{
	return rb_collate_hash_table_values (rb_plugins);
}

static gboolean
load_plugin_module (RBPluginInfo *info)
{
	gchar *path;
	gchar *dirname;

	g_return_val_if_fail (info != NULL, FALSE);
	g_return_val_if_fail (info->file != NULL, FALSE);
	g_return_val_if_fail (info->location != NULL, FALSE);
	g_return_val_if_fail (info->plugin == NULL, FALSE);

	switch (info->lang) {
		case RB_PLUGIN_LOADER_C:
			dirname = g_path_get_dirname (info->file);
			g_return_val_if_fail (dirname != NULL, FALSE);

			path = g_module_build_path (dirname, info->location);
#ifdef USE_UNINSTALLED_DIRS
			if (!g_file_test (path, G_FILE_TEST_EXISTS)) {
				char *temp;

				g_free (path);
				temp = g_build_filename (dirname, ".libs", NULL);

				path = g_module_build_path (temp, info->location);
				g_free (temp);
			}
#endif

			g_free (dirname);
			g_return_val_if_fail (path != NULL, FALSE);

			info->module = G_TYPE_MODULE (rb_module_new (path, info->location));
			g_free (path);
			break;
		case RB_PLUGIN_LOADER_PY:
#ifdef ENABLE_PYTHON
			info->module = G_TYPE_MODULE (rb_python_module_new (info->file, info->location));
#else
			rb_debug ("cannot load plugin %s, python plugin support is disabled", info->location);
#endif
			break;
	}

	if (g_type_module_use (info->module) == FALSE) {
		g_warning ("Could not load plugin %s\n", info->location);

		g_object_unref (G_OBJECT (info->module));
		info->module = NULL;

		return FALSE;
	}

	switch (info->lang) {
		case RB_PLUGIN_LOADER_C:
			info->plugin = RB_PLUGIN (rb_module_new_object (RB_MODULE (info->module)));
			break;
		case RB_PLUGIN_LOADER_PY:
#ifdef ENABLE_PYTHON
			info->plugin = RB_PLUGIN (rb_python_module_new_object (RB_PYTHON_MODULE (info->module)));
#endif
			break;
	}

	return TRUE;
}

static gboolean
rb_plugins_engine_activate_plugin_real (RBPluginInfo *info, RBShell *shell)
{
	gboolean res = TRUE;

	if (info->plugin == NULL)
		res = load_plugin_module (info);

	if (res)
		rb_plugin_activate (info->plugin, shell);
	else
		g_warning ("Error, impossible to activate plugin '%s'", info->name);

	return res;
}

gboolean
rb_plugins_engine_activate_plugin (RBPluginInfo *info)
{
	gboolean ret;

	g_return_val_if_fail (info != NULL, FALSE);

	if (info->active)
		return TRUE;

	ret = rb_plugins_engine_activate_plugin_real (info, rb_plugins_shell);

	if (info->visible != FALSE || ret != FALSE) {
		char *key_name;

		key_name = g_strdup_printf (CONF_PLUGIN_ACTIVE_KEY, info->location);
		eel_gconf_set_boolean (key_name, ret);
		g_free (key_name);
	}
        info->active = ret;

        if (ret != FALSE)
                return TRUE;


	rb_error_dialog (NULL, _("Plugin Error"), _("Unable to activate plugin %s"), info->name);

	return FALSE;
}

static void
rb_plugins_engine_deactivate_plugin_real (RBPluginInfo *info, RBShell *shell)
{
	rb_plugin_deactivate (info->plugin, rb_plugins_shell);
}

gboolean
rb_plugins_engine_deactivate_plugin (RBPluginInfo *info)
{
	char *key_name;

	g_return_val_if_fail (info != NULL, FALSE);

	if (!info->active)
		return TRUE;

	rb_plugins_engine_deactivate_plugin_real (info, rb_plugins_shell);

	/* Update plugin state */
	info->active = FALSE;

	key_name = g_strdup_printf (CONF_PLUGIN_ACTIVE_KEY, info->location);
	eel_gconf_set_boolean (key_name, FALSE);
	g_free (key_name);

	return TRUE;
}

gboolean
rb_plugins_engine_plugin_is_active (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, FALSE);

	return info->active;
}

gboolean
rb_plugins_engine_plugin_is_visible (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, FALSE);

	return info->visible;
}

gboolean
rb_plugins_engine_plugin_is_configurable (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, FALSE);

	if ((info->plugin == NULL) || !info->active)
		return FALSE;

	return rb_plugin_is_configurable (info->plugin);
}

void
rb_plugins_engine_configure_plugin (RBPluginInfo *info,
				       GtkWindow       *parent)
{
	GtkWidget *conf_dlg;

	GtkWindowGroup *wg;

	g_return_if_fail (info != NULL);

	conf_dlg = rb_plugin_create_configure_dialog (info->plugin);
	g_return_if_fail (conf_dlg != NULL);
	gtk_window_set_transient_for (GTK_WINDOW (conf_dlg),
				      parent);

	wg = parent->group;
	if (wg == NULL)
	{
		wg = gtk_window_group_new ();
		gtk_window_group_add_window (wg, parent);
	}

	gtk_window_group_add_window (wg,
				     GTK_WINDOW (conf_dlg));

	gtk_window_set_modal (GTK_WINDOW (conf_dlg), TRUE);
	gtk_widget_show (conf_dlg);
}

static void
rb_plugins_engine_plugin_active_cb (GConfClient *client,
				    guint cnxn_id,
				    GConfEntry *entry,
				    RBPluginInfo *info)
{
	if (gconf_value_get_bool (entry->value)) {
		rb_plugins_engine_activate_plugin (info);
	} else {
		rb_plugins_engine_deactivate_plugin (info);
	}
}

static void
rb_plugins_engine_plugin_visible_cb (GConfClient *client,
				     guint cnxn_id,
				     GConfEntry *entry,
				     RBPluginInfo *info)
{
	info->visible = !gconf_value_get_bool (entry->value);
}

const gchar *
rb_plugins_engine_get_plugin_name (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->name;
}

const gchar *
rb_plugins_engine_get_plugin_description (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->desc;
}

const gchar **
rb_plugins_engine_get_plugin_authors (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, (const gchar **)NULL);

	return (const gchar **)info->authors;
}

const gchar *
rb_plugins_engine_get_plugin_website (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->website;
}

const gchar *
rb_plugins_engine_get_plugin_copyright (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	return info->copyright;
}

GdkPixbuf *
rb_plugins_engine_get_plugin_icon (RBPluginInfo *info)
{
	g_return_val_if_fail (info != NULL, NULL);

	if (info->icon_name == NULL)
		return NULL;

	if (info->icon_pixbuf == NULL) {
		char *filename = NULL;
		char *dirname;

		dirname = g_path_get_dirname (info->file);
		filename = g_build_filename (dirname, info->icon_name, NULL);
		g_free (dirname);

		info->icon_pixbuf = gdk_pixbuf_new_from_file (filename, NULL);
		g_free (filename);
	}

	return info->icon_pixbuf;
}
