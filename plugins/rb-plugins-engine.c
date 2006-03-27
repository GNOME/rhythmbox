/*
 * Plugin manager for Rhythmbox, based heavily on the code from gedit.
 *
 * Copyright (C) 2002-2005 Paolo Maggi 
 *               2006 James Livingston  <jrl@ids.org.au>
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
 * Foundation, Inc., 59 Temple Place, Suite 330, 
 * Boston, MA 02111-1307, USA. 
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n.h>
#include <glib/gkeyfile.h>
#include <libgnome/gnome-util.h>

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

#define USER_RB_PLUGINS_LOCATION "rhythmbox/plugins/"
#define UNINSTALLED_PLUGINS_LOCATION "plugins"

#define RB_PLUGINS_ENGINE_BASE_KEY CONF_PREFIX "/plugins"
#define RB_PLUGINS_ENGINE_KEY RB_PLUGINS_ENGINE_BASE_KEY "/active-plugins"

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
	guint        notification_id;
};

static void rb_plugin_info_free (RBPluginInfo *info);
static void rb_plugins_engine_plugin_active_cb (GConfClient *client,
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
	if (!g_key_file_load_from_file (plugin_file, file, G_KEY_FILE_NONE, NULL))
	{
		g_warning ("Bad plugin file: %s", file);
		goto error;
	}

	if (!g_key_file_has_key (plugin_file,
			   	 "RB Plugin",
				 "IAge",
				 NULL))
	{
		rb_debug ("IAge key does not exist in file: %s", file);
		goto error;
	}
	
	/* Check IAge=1 */
	if (g_key_file_get_integer (plugin_file,
				    "RB Plugin",
				    "IAge",
				    NULL) != 1)
	{
		rb_debug ("Wrong IAge in file: %s", file);
		goto error;
	}
				    
	/* Get Location */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Module",
				     NULL);
	if (str)
	{
		info->location = str;
	}
	else
	{
		g_warning ("Could not find 'Module' in %s", file);
		goto error;
	}
	
	/* Get the loader for this plugin */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Loader",
				     NULL);
	if (str && strcmp(str, "python") == 0)
	{
		info->lang = RB_PLUGIN_LOADER_PY;
#ifndef ENABLE_PYTHON
		g_warning ("Cannot load python extension '%s', Rhythmbox was not "
					"compiled with python support", file);
		goto error;
#endif
	}
	else
	{
		info->lang = RB_PLUGIN_LOADER_C;
	}
	g_free (str);

	/* Get Name */
	str = g_key_file_get_locale_string (plugin_file,
					    "RB Plugin",
					    "Name",
					    NULL, NULL);
	if (str)
		info->name = str;
	else
	{
		g_warning ("Could not find 'Name' in %s", file);
		goto error;
	}

	/* Get Description */
	str = g_key_file_get_locale_string (plugin_file,
					    "RB Plugin",
					    "Description",
					    NULL, NULL);
	if (str)
		info->desc = str;
	else
		rb_debug ("Could not find 'Description' in %s", file);

	/* Get icon name */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Icon",
				     NULL);
	if (str)
		info->icon_name = str;
	else
		rb_debug ("Could not find 'Description' in %s", file);


	/* Get Authors */
	info->authors = g_key_file_get_string_list (plugin_file,
						    "RB Plugin",
						    "Authors",
						    NULL, NULL);
	if (info->authors == NULL)
		rb_debug ("Could not find 'Author' in %s", file);


	/* Get Copyright */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Copyright",
				     NULL);
	if (str)
		info->copyright = str;
	else
		rb_debug ("Could not find 'Copyright' in %s", file);

	/* Get Copyright */
	str = g_key_file_get_string (plugin_file,
				     "RB Plugin",
				     "Website",
				     NULL);
	if (str)
		info->website = str;
	else
		rb_debug ("Could not find 'Website' in %s", file);
		
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

static void
rb_plugins_engine_load_cb (const char *uri, gpointer userdata)
{
	gchar *plugin_file;
	RBPluginInfo *info;
	char *key_name;
	gboolean activate;
			
	if (!g_str_has_suffix (uri, PLUGIN_EXT))
		return;

	plugin_file = gnome_vfs_get_local_path_from_uri (uri);
	info = rb_plugins_engine_load (plugin_file);
	g_free (plugin_file);

	if (info == NULL)
		return;

	if (g_hash_table_lookup (rb_plugins, info->location)) {
		rb_plugin_info_free (info);
		return;
	}

	g_hash_table_insert (rb_plugins, info->location, info);
	rb_debug ("Plugin %s loaded", info->name);
	
	key_name = g_strdup_printf (CONF_PLUGIN_ACTIVE_KEY, info->location);
	info->notification_id = eel_gconf_notification_add (key_name,
							    (GConfClientNotifyFunc)rb_plugins_engine_plugin_active_cb,
							    info);
	activate = eel_gconf_get_boolean (key_name);
	g_free (key_name);

	if (activate)
		rb_plugins_engine_activate_plugin (info);
}

static void
rb_plugins_engine_load_dir (const gchar *path)
{
	char *uri;

	uri = rb_uri_resolve_relative (path);
	rb_uri_handle_recursively (uri, (GFunc)rb_plugins_engine_load_cb, NULL, NULL);
	g_free (uri);
}

static void
rb_plugins_engine_load_all (void)
{
	gchar *pdir;

	/* load user's plugins */
	pdir = gnome_util_home_file (USER_RB_PLUGINS_LOCATION);
	rb_plugins_engine_load_dir (pdir);
	g_free (pdir);

#ifdef SHARE_UNINSTALLED_DIR
	/* load plugins when running  uninstalled */
	rb_plugins_engine_load_dir (UNINSTALLED_PLUGINS_LOCATION);
#endif
	
	/* load system-wide plugins */
	rb_plugins_engine_load_dir (RB_PLUGIN_DIR);
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

	rb_plugins = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify)rb_plugin_info_free);

	rb_plugins_shell = shell;
	g_object_ref (G_OBJECT (rb_plugins_shell));

	rb_plugins_engine_load_all ();

	garbage_collect_id = g_timeout_add_full (G_PRIORITY_LOW, 20000, garbage_collect_cb, NULL, NULL);

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

	eel_gconf_notification_remove (info->notification_id);

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
	
	switch (info->lang)
	{
		case RB_PLUGIN_LOADER_C:
			dirname = g_path_get_dirname (info->file);	
			g_return_val_if_fail (dirname != NULL, FALSE);

			path = g_module_build_path (dirname, info->location);
#ifdef SHARE_UNINSTALLED_DIR
			if (!g_file_test(path, G_FILE_TEST_EXISTS)) {
				char *temp;

				g_free (path);
				temp = g_build_filename (dirname, ".libs", NULL);
				
				path = g_module_build_path (temp, info->location);
				g_free (temp);
			}
#endif
			
			g_free (dirname);
			g_return_val_if_fail (path != NULL, FALSE);
	
			info->module = G_TYPE_MODULE (rb_module_new (path));
			g_free (path);
			break;
		case RB_PLUGIN_LOADER_PY:
		{
#ifdef ENABLE_PYTHON
			gchar *dir = g_path_get_dirname (info->file);
			
			info->module = G_TYPE_MODULE (
					rb_python_module_new (dir, info->location));
					
			g_free (dir);
#endif
			break;
		}
	}

	
	if (g_type_module_use (info->module) == FALSE)
	{
		switch (info->lang)
		{
			case RB_PLUGIN_LOADER_C:
				g_warning ("Could not load plugin file at %s\n",
				   rb_module_get_path (RB_MODULE (info->module)));
				break;
			case RB_PLUGIN_LOADER_PY:
				g_warning ("Could not load python module %s\n", info->location);
				break;
		}
			   
		g_object_unref (G_OBJECT (info->module));
		info->module = NULL;
		
		return FALSE;
	}
	
	switch (info->lang)
	{
		case RB_PLUGIN_LOADER_C:
			info->plugin = RB_PLUGIN (rb_module_new_object (RB_MODULE (info->module)));
			break;
		case RB_PLUGIN_LOADER_PY:
#ifdef ENABLE_PYTHON
			info->plugin = RB_PLUGIN (rb_python_module_new_object (RB_PYTHON_MODULE (info->module)));
#endif
			break;
	}
	
	g_type_module_unuse (info->module);

	rb_debug ("End");
	
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
	g_return_val_if_fail (info != NULL, FALSE);

	if (info->active)
		return TRUE;

	if (rb_plugins_engine_activate_plugin_real (info, rb_plugins_shell)) {
		char *key_name;

		key_name = g_strdup_printf (CONF_PLUGIN_ACTIVE_KEY, info->location);
		eel_gconf_set_boolean (key_name, TRUE);
		g_free (key_name);
	
		info->active = TRUE;

		return TRUE;
	}

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

