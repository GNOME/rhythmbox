/*
 * rb-ipod-helpers.c
 *
 * Copyright (C) 2002-2005 Jorg Schuler <jcsjcs at users sourceforge net>
 * Copyright (C) 2006 James "Doc" Livingston
 * Copyright (C) 2008 Christophe Fergeau <teuf@gnome.org>
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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>
#include <glade/glade.h>
#include <gpod/itdb.h>
#ifdef HAVE_HAL
#include <libhal.h>
#include <dbus/dbus.h>
#endif

#include "rb-ipod-helpers.h"

#include "rb-debug.h"
#include "rb-dialog.h"


enum {
      COL_INFO = 0,
};

static gchar *ipod_info_to_string (const Itdb_IpodInfo *info)
{
	if (info->capacity >= 1) {   /* size in GB */
		return g_strdup_printf ("%2.0f GB %s", info->capacity,
					itdb_info_get_ipod_model_name_string (info->ipod_model));
	} else if (info->capacity > 0) {   /* size in MB */
		return g_strdup_printf ("%3.0f MB %s", info->capacity * 1024,
					itdb_info_get_ipod_model_name_string (info->ipod_model));
	} else {   /* no capacity information available */
		return g_strdup_printf ("%s",
					itdb_info_get_ipod_model_name_string (info->ipod_model));
	}
}

static double 
get_rounded_ipod_capacity (const char *mountpoint)
{
        guint64 capacity;

        capacity = rb_ipod_helpers_get_capacity (mountpoint);
        capacity += (1000*1000*500 - 1);
        capacity -= (capacity % (1000*1000*500));
        return (double)capacity/(1000.0*1000*1000);
}

static void
set_cell (GtkCellLayout   *cell_layout,
	  GtkCellRenderer *cell,
	  GtkTreeModel    *tree_model,
	  GtkTreeIter     *iter,
	  gpointer         data)
{
	gboolean header;
	gchar *text;
	Itdb_IpodInfo *info;

	gtk_tree_model_get (tree_model, iter, COL_INFO, &info, -1);
	g_return_if_fail (info);

	header = gtk_tree_model_iter_has_child (tree_model, iter);

	if (header) {
		text = g_strdup (
				 itdb_info_get_ipod_generation_string (info->ipod_generation));
	} else {
		text = ipod_info_to_string (info);
	}

	g_object_set (cell,
		      "sensitive", !header,
		      "text", text,
		      NULL);
	g_free (text);
}

static gboolean
model_equals (gconstpointer a, gconstpointer b)
{
	const Itdb_IpodInfo *lhs = (const Itdb_IpodInfo *)a;
	const Itdb_IpodInfo *rhs = (const Itdb_IpodInfo *)b;

	return !((lhs->capacity == rhs->capacity) 
		 && (lhs->ipod_model == rhs->ipod_model) 
		 && (lhs->ipod_generation == rhs->ipod_generation));
}

static GHashTable *
build_model_table (const char *mount_path)
{
	const Itdb_IpodInfo *table;
	const Itdb_IpodInfo *model_info;
	GHashTable *models;
	gdouble ipod_capacity;

	ipod_capacity = get_rounded_ipod_capacity (mount_path);
	models = g_hash_table_new_full (g_int_hash, g_int_equal,
					NULL, (GDestroyNotify)g_list_free);
	table = itdb_info_get_ipod_info_table ();
	for (model_info = table;
	     model_info->model_number != NULL;
	     model_info++) {
		GList *infos;

		infos = g_hash_table_lookup (models,
					     &model_info->ipod_generation);
		if (g_list_find_custom (infos, model_info, model_equals)) {
			continue;
		}
		if (model_info->capacity == ipod_capacity) {
			/* Steal the key from the hash table since we don't 
			 * want 'infos' to be g_list_freed by the hash
			 * table destroy notify function
			 */
			g_hash_table_steal (models,
					    &model_info->ipod_generation);
			infos = g_list_prepend (infos, (gpointer)model_info);

			g_hash_table_insert (models, 
					     (gpointer)&model_info->ipod_generation, 
					     infos);
		}
	}

	return models;
}

struct FillModelContext {
	GtkWidget *combo;
	GtkTreeStore *store;
	const Itdb_IpodInfo *ipod_info;
};

static void 
fill_one_generation (gpointer key, gpointer value, gpointer data)
{
	GList *infos;
	GList *it;
	Itdb_IpodGeneration generation;
	gboolean first;
	GtkTreeIter iter;
	struct FillModelContext *ctx;

	ctx = (struct FillModelContext *)data;
	infos = (GList *)value;
	generation = *(Itdb_IpodGeneration*)key;

	first = TRUE;
	for (it = infos; it != NULL; it = it->next) {
		const Itdb_IpodInfo *info;
		GtkTreeIter iter_child;

		info = (const Itdb_IpodInfo *)it->data;
		g_assert (info->ipod_generation == generation);

		if (first) {
			gtk_tree_store_append (ctx->store, &iter, NULL);
			gtk_tree_store_set (ctx->store, &iter, 
					    COL_INFO, info, -1);
			first = FALSE;
		}
		gtk_tree_store_append (ctx->store, &iter_child, &iter);
		gtk_tree_store_set (ctx->store, &iter_child, 
				    COL_INFO, info, -1);
		if (info == ctx->ipod_info) {
			gtk_combo_box_set_active_iter (GTK_COMBO_BOX (ctx->combo), 
						       &iter_child);
		}
	}
}

static void
fill_model_combo (GtkWidget *combo, const char *mount_path)
{
	GHashTable *models;
	Itdb_Device *device;
	GtkTreeStore *store;
	const Itdb_IpodInfo *ipod_info;
	GtkCellRenderer *renderer;
	struct FillModelContext ctx;

	device = itdb_device_new ();
	itdb_device_set_mountpoint (device, mount_path);
	itdb_device_read_sysinfo (device);
	ipod_info = itdb_device_get_ipod_info (device);
	itdb_device_free (device);

	store = gtk_tree_store_new (1, G_TYPE_POINTER);
	gtk_combo_box_set_model (GTK_COMBO_BOX (combo), GTK_TREE_MODEL (store));

	ctx.combo = combo;
	ctx.store = store;
	ctx.ipod_info = ipod_info;
	models = build_model_table (mount_path);
	g_hash_table_foreach (models, fill_one_generation, &ctx);
	g_hash_table_destroy (models);
	g_object_unref (store);

	gtk_cell_layout_clear (GTK_CELL_LAYOUT (combo));

	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (combo), renderer, FALSE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (combo),
					    renderer,
					    set_cell,
					    NULL, NULL);
}

gboolean
rb_ipod_helpers_show_first_time_dialog (GMount *mount, const char *glade_file)
{
	/* could be an uninitialised iPod, ask the user */
	GladeXML *xml;
	GtkWidget *dialog;
	GtkWidget *widget;
	GtkTreeModel *tree_model;
	GtkTreeIter iter;
	int response;
	char *mountpoint;
	const Itdb_IpodInfo *info;
	char *ipod_name;
	GFile *root;
	GError *error = NULL;

	root = g_mount_get_root (mount);
	if (root == NULL) {
		return FALSE;      
	}
	mountpoint = g_file_get_path (root);
	g_object_unref (G_OBJECT (root));

	if (mountpoint == NULL) {
		return FALSE;
	}

	/* create message dialog with model-number combo box
	 * and asking whether they want to initialise the iPod
	 */
	xml = glade_xml_new (glade_file, NULL, NULL);
	dialog = glade_xml_get_widget (xml, "ipod_init");
	widget = glade_xml_get_widget (xml, "model_combo");
	fill_model_combo (widget, mountpoint);

	rb_debug ("showing init dialog for ipod mount on '%s'", mountpoint);

	response = gtk_dialog_run (GTK_DIALOG (dialog));
	if (response != GTK_RESPONSE_ACCEPT) {
		gtk_widget_destroy (dialog);
		g_free (mountpoint);
		return FALSE;
	}

	/* get model number and name */
	tree_model = gtk_combo_box_get_model (GTK_COMBO_BOX (widget));
	gtk_combo_box_get_active_iter (GTK_COMBO_BOX (widget), &iter);
	gtk_tree_model_get (tree_model, &iter, COL_INFO, &info, -1);
	widget = glade_xml_get_widget (xml, "name_entry");
	ipod_name = g_strdup (gtk_entry_get_text (GTK_ENTRY (widget)));

	gtk_widget_destroy (dialog);

	rb_debug ("attempting to init ipod on '%s', with model '%s' and name '%s'",
		  mountpoint, info->model_number, ipod_name);
	if (!itdb_init_ipod (mountpoint, info->model_number, ipod_name, &error)) {
		rb_error_dialog (NULL, _("Unable to initialise new iPod"), "%s", error->message);

		g_free (mountpoint);
		g_free (ipod_name);
		g_error_free (error);
		return FALSE;
	}

	g_free (mountpoint);
	g_free (ipod_name);

	return TRUE;
}

static gchar *
rb_ipod_helpers_get_itunesdb_path (GMount *mount)
{
        GFile *root;
        gchar *mount_point;
        gchar *result = NULL;

        root = g_mount_get_root (mount);
        if (root != NULL) {
                mount_point = g_file_get_path (root);
                if (mount_point != NULL) {
                        result = itdb_get_itunesdb_path (mount_point);
                }

                g_free (mount_point);
                g_object_unref (root);
        }

        return result;
}

static guint64 get_fs_property (const char *mountpoint, const char *attr)
{
        GFile *root;
        GFileInfo *info;
        guint64 value;

        root = g_file_new_for_path (mountpoint);
        info = g_file_query_filesystem_info (root, attr, NULL, NULL);
        g_object_unref (G_OBJECT (root));
        if (info == NULL) {
                return 0;
        }
        if (!g_file_info_has_attribute (info, attr)) {
                g_object_unref (G_OBJECT (info));
                return 0;
        }
        value = g_file_info_get_attribute_uint64 (info, attr);
        g_object_unref (G_OBJECT (info));

        return value;
}

guint64
rb_ipod_helpers_get_capacity (const char *mountpoint)
{
        return  get_fs_property (mountpoint, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
}

guint64
rb_ipod_helpers_get_free_space (const char *mountpoint)
{
        return get_fs_property (mountpoint, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
}

char *
rb_ipod_helpers_get_device (RBSource *source)
{
        GMount *mount;
        GVolume *volume;
        char *device;

        g_object_get (RB_SOURCE (source), "mount", &mount, NULL);
        volume = g_mount_get_volume (mount);
        device = g_volume_get_identifier (volume,
                                          G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);
        g_object_unref (G_OBJECT (volume));
        g_object_unref (G_OBJECT (mount));

        return device;
}

static gboolean
rb_ipod_helpers_mount_has_ipod_db (GMount *mount)
{
        char *itunesdb_path;
        gboolean result;

        itunesdb_path = rb_ipod_helpers_get_itunesdb_path (mount);

        if (itunesdb_path != NULL) {
                result = g_file_test (itunesdb_path, G_FILE_TEST_EXISTS);
        } else {
                result = FALSE;
        }
        g_free (itunesdb_path);

        return result;
}

#ifdef HAVE_HAL
static gboolean
hal_udi_is_ipod (const char *udi)
{
	LibHalContext *ctx;
	DBusConnection *conn;
	char *parent_udi;
	char **methods_list;
	guint i;
	gboolean result;
	DBusError error;
	gboolean inited = FALSE;

	result = FALSE;
	dbus_error_init (&error);

	conn = NULL;
	parent_udi = NULL;
	methods_list = NULL;

	ctx = libhal_ctx_new ();
	if (ctx == NULL) {
		/* FIXME: should we return an error somehow so that we can
		 * fall back to a check for iTunesDB presence instead ?
		 */
		rb_debug ("cannot connect to HAL");
		goto end;
	}
	conn = dbus_bus_get (DBUS_BUS_SYSTEM, &error);
	if (conn == NULL || dbus_error_is_set (&error))
		goto end;

	libhal_ctx_set_dbus_connection (ctx, conn);
	if (!libhal_ctx_init (ctx, &error) || dbus_error_is_set (&error))
		goto end;

	inited = TRUE;
	parent_udi = libhal_device_get_property_string (ctx, udi,
							"info.parent", &error);
	if (parent_udi == NULL || dbus_error_is_set (&error))
		goto end;
	methods_list = libhal_device_get_property_strlist (ctx, parent_udi,
							   "portable_audio_player.access_method.protocols", &error);
	if (methods_list == NULL || dbus_error_is_set (&error))
		goto end;
	for (i = 0; methods_list[i] != NULL; i++) {
		if (strcmp ("ipod", methods_list[i]) == 0) {
			result = TRUE;
			break;
		}
	}

end:
	g_free (parent_udi);
	libhal_free_string_array (methods_list);

	if (dbus_error_is_set (&error)) {
		rb_debug ("Error: %s\n", error.message);
		dbus_error_free (&error);
		dbus_error_init (&error);
	}

	if (ctx) {
		if (inited)
			libhal_ctx_shutdown (ctx, &error);
		libhal_ctx_free(ctx);
	}

	dbus_error_free (&error);

	return result;
}

gboolean
rb_ipod_helpers_is_ipod (GMount *mount)
{
	gchar *udi;
        gboolean result;
	GVolume *volume;

	volume = g_mount_get_volume (mount);
	if (volume == NULL)
		return FALSE;

	udi = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_HAL_UDI);
	g_object_unref (volume);

        if (udi == NULL) {
                return FALSE;
        }

        result = hal_udi_is_ipod (udi);
        g_free (udi);

	return result;
}

#else

gboolean
rb_ipod_helpers_is_ipod (GMount *mount)
{
	GFile *root;
	gchar *mount_point;
	gboolean result = FALSE;

	root = g_mount_get_root (mount);
	if (root != NULL) {
		gchar *device_dir;

		mount_point = g_file_get_path (root);
		if (mount_point != NULL) {
			device_dir = itdb_get_device_dir (mount_point);
			if (device_dir != NULL)  {
				result = g_file_test (device_dir,
						      G_FILE_TEST_IS_DIR);
				g_free (device_dir);
			}
		}

		g_free (mount_point);
		g_object_unref (root);
	}

	return result;
}
#endif

gboolean rb_ipod_helpers_needs_init (GMount *mount)
{
	/* This function is a useless one-liner for now, but it should check
	 * for the existence of the firsttime file on the ipod to tell if
	 * the ipod is new or not
	 */
	return (!rb_ipod_helpers_mount_has_ipod_db (mount));
}
