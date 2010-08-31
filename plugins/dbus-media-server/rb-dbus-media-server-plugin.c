/*
 * rb-dbus-media-server-plugin.c
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <lib/eel-gconf-extensions.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>
#include <shell/rb-plugin.h>
#include <shell/rb-shell.h>
#include <shell/rb-shell-player.h>

#define RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN	(rb_dbus_media_server_plugin_get_type ())
#define RB_DBUS_MEDIA_SERVER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN, RBMediaServer2Plugin))
#define RB_DBUS_MEDIA_SERVER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN, RBMediaServer2PluginClass))
#define RB_IS_DBUS_MEDIA_SERVER_PLUGIN(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN))
#define RB_IS_DBUS_MEDIA_SERVER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN))
#define RB_DBUS_MEDIA_SERVER_PLUGIN_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN, RBMediaServer2PluginClass))

#include "dbus-media-server-spec.h"

#define RB_MEDIASERVER2_BUS_NAME	MEDIA_SERVER2_BUS_NAME_PREFIX ".Rhythmbox"

#define RB_MEDIASERVER2_PREFIX		"/org/gnome/UPnP/MediaServer2/"
#define RB_MEDIASERVER2_ROOT		RB_MEDIASERVER2_PREFIX "Rhythmbox"
#define RB_MEDIASERVER2_LIBRARY		RB_MEDIASERVER2_PREFIX "Library"
#define RB_MEDIASERVER2_ENTRY_SUBTREE	RB_MEDIASERVER2_PREFIX "Entry"
#define RB_MEDIASERVER2_ENTRY_PREFIX	RB_MEDIASERVER2_ENTRY_SUBTREE "/"

typedef struct
{
	RBPlugin parent;

	GDBusNodeInfo *node_info;
	guint name_own_id;

	GDBusConnection *connection;

	/* object/subtree registration ids */
	guint root_reg_id[2];
	gboolean root_updated;
	guint entry_reg_id;

	guint emit_updated_id;

	/* source registrations */
	GList *sources;

	RBShell *shell;
	RhythmDB *db;
} RBMediaServer2Plugin;

typedef struct
{
	RBPluginClass parent_class;
} RBMediaServer2PluginClass;

typedef struct
{
	RBSource *source;
	RhythmDBQueryModel *base_query_model;
	guint dbus_reg_id[2];
	gboolean updated;
	char *dbus_path;
	char *parent_dbus_path;

	RBMediaServer2Plugin *plugin;
} SourceRegistrationData;


G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_dbus_media_server_plugin_get_type		(void) G_GNUC_CONST;

RB_PLUGIN_REGISTER(RBMediaServer2Plugin, rb_dbus_media_server_plugin)


static void
rb_dbus_media_server_plugin_init (RBMediaServer2Plugin *plugin)
{
}

static void
register_object (RBMediaServer2Plugin *plugin,
		 const GDBusInterfaceVTable *vtable,
		 GDBusInterfaceInfo *iface_info,
		 const char *object_path,
		 gpointer method_data,
		 guint *ids)
{
	GError *error = NULL;
	GDBusInterfaceInfo *object_iface;
	object_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_OBJECT_IFACE_NAME);

	ids[0] = g_dbus_connection_register_object (plugin->connection,
						    object_path,
						    object_iface,
						    vtable,
						    method_data,
						    NULL,
						    &error);
	if (error != NULL) {
		g_warning ("Unable to register MediaServer2 object %s: %s",
			   object_path,
			   error->message);
		g_clear_error (&error);
	}

	ids[1] = g_dbus_connection_register_object (plugin->connection,
						    object_path,
						    iface_info,
						    vtable,
						    method_data,
						    NULL,
						    &error);
	if (error != NULL) {
		g_warning ("Unable to register MediaServer2 object %s: %s",
			   object_path,
			   error->message);
		g_clear_error (&error);
	}
}

static void
unregister_object (RBMediaServer2Plugin *plugin, guint *ids)
{
	if (ids[0] != 0) {
		g_dbus_connection_unregister_object (plugin->connection, ids[0]);
		ids[0] = 0;
	}
	if (ids[1] != 0) {
		g_dbus_connection_unregister_object (plugin->connection, ids[1]);
		ids[1] = 0;
	}
}

/* entry subtree */

char *all_entry_properties[] = {
	"Parent",
	"Type",
	"Path",
	"DisplayName",
	"URLs",
	"MIMEType",
	"Size",
	"Artist",
	"Album",
	"Date",
	"Genre",
	"DLNAProfile",
	"Duration",
	"Bitrate",
	"AlbumArt"
};

/* not used yet, since album art isn't exposed
static gboolean
entry_extra_metadata_maps (const char *extra_metadata)
{
	if (g_strcmp0 (extra_metadata, RHYTHMDB_PROP_COVER_ART_URI) == 0) {
		return TRUE;
	}
	return FALSE;
}
*/

static gboolean
entry_property_maps (RhythmDBPropType prop)
{
	switch (prop) {
		case RHYTHMDB_PROP_TITLE:
		case RHYTHMDB_PROP_MIMETYPE:
		case RHYTHMDB_PROP_FILE_SIZE:
		case RHYTHMDB_PROP_ALBUM:
		case RHYTHMDB_PROP_ARTIST:
		case RHYTHMDB_PROP_YEAR:
		case RHYTHMDB_PROP_GENRE:
		case RHYTHMDB_PROP_DURATION:
		case RHYTHMDB_PROP_BITRATE:
			return TRUE;

		default:
			return FALSE;
	}
}

static GVariant *
get_entry_property_value (RhythmDBEntry *entry, const char *property_name)
{
	GVariant *v;

	if (g_strcmp0 (property_name, "Parent") == 0) {
		return g_variant_new_object_path (RB_MEDIASERVER2_ROOT);
	} else if (g_strcmp0 (property_name, "Type") == 0) {
		return g_variant_new_string ("audio");		/* .music? */
	} else if (g_strcmp0 (property_name, "Path") == 0) {
		char *path;

		path = g_strdup_printf (RB_MEDIASERVER2_ENTRY_PREFIX "%lu",
					rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID));
		v = g_variant_new_string (path);
		g_free (path);
		return v;
	} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
		return g_variant_new_string (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
	} else if (g_strcmp0 (property_name, "URLs") == 0) {
		const char *urls[] = { NULL, NULL };
		char *url;
		url = rhythmdb_entry_get_playback_uri (entry);
		urls[0] = url;
		v = g_variant_new_strv (urls, -1);
		g_free (url);
		return v;

	} else if (g_strcmp0 (property_name, "MIMEType") == 0) {
		const char *media_type;
		/* ugh */
		media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
		if (g_strcmp0 (media_type, "application/x-id3") == 0) {
			media_type = "audio/mpeg";
		}
		return g_variant_new_string (media_type);
	} else if (g_strcmp0 (property_name, "Size") == 0) {
		return g_variant_new_int64 (rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE));
	} else if (g_strcmp0 (property_name, "Artist") == 0) {
		return g_variant_new_string (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	} else if (g_strcmp0 (property_name, "Album") == 0) {
		return g_variant_new_string (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	} else if (g_strcmp0 (property_name, "Date") == 0) {
		char *iso8601;
		iso8601 = g_strdup_printf ("%4d-%02d-%02dT%02d:%02d:%02dZ",
					   (int)rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_YEAR),
					   1, 1, 0, 0, 0);
		v = g_variant_new_string (iso8601);
		g_free (iso8601);
		return v;
	} else if (g_strcmp0 (property_name, "Genre") == 0) {
		return g_variant_new_string (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE));
	} else if (g_strcmp0 (property_name, "Duration") == 0) {
		return g_variant_new_int32 (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION));
	} else if (g_strcmp0 (property_name, "Bitrate") == 0) {
		return g_variant_new_int32 (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_BITRATE));
	}

	/* not yet: DLNAProfile, AlbumArt */

	return NULL;
}

static GVariant *
get_entry_property (GDBusConnection *connection,
		    const char *sender,
		    const char *object_path,
		    const char *interface_name,
		    const char *property_name,
		    GError **error,
		    RBMediaServer2Plugin *plugin)
{
	RhythmDBEntry *entry;

	rb_debug ("entry property %s", property_name);
	if (g_str_has_prefix (object_path, RB_MEDIASERVER2_ENTRY_PREFIX) == FALSE) {
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "no");
		return NULL;
	}

	entry = rhythmdb_entry_lookup_from_string (plugin->db, object_path + strlen (RB_MEDIASERVER2_ENTRY_PREFIX), TRUE);
	if (entry == NULL) {
		rb_debug ("entry for object path %s not found", object_path);
		g_set_error (error, G_DBUS_ERROR, G_DBUS_ERROR_NOT_SUPPORTED, "no");
		return NULL;
	}

	return get_entry_property_value (entry, property_name);
}

static const GDBusInterfaceVTable entry_vtable =
{
	NULL,
	(GDBusInterfaceGetPropertyFunc) get_entry_property,
	NULL
};

static char **
enumerate_entry_subtree (GDBusConnection *connection,
			 const char *sender,
			 const char *object_path,
			 RBMediaServer2Plugin *plugin)
{
	return (char **)g_new0(char *, 1);
}

static GDBusInterfaceInfo **
introspect_entry_subtree (GDBusConnection *connection,
			  const char *sender,
			  const char *object_path,
			  const char *node,
			  RBMediaServer2Plugin *plugin)
{
	GPtrArray *p;
	GDBusInterfaceInfo *i;

	p = g_ptr_array_new ();

	i = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_OBJECT_IFACE_NAME);
	g_ptr_array_add (p, g_dbus_interface_info_ref (i));

	i = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_ITEM_IFACE_NAME);
	g_ptr_array_add (p, g_dbus_interface_info_ref (i));

	g_ptr_array_add (p, NULL);

	return (GDBusInterfaceInfo **)g_ptr_array_free (p, FALSE);
}

static const GDBusInterfaceVTable *
dispatch_entry_subtree (GDBusConnection *connection,
			const char *sender,
			const char *object_path,
			const char *interface_name,
			const char *node,
			gpointer *out_user_data,
			RBMediaServer2Plugin *plugin)
{
	*out_user_data = plugin;
	return &entry_vtable;
}

static GDBusSubtreeVTable entry_subtree_vtable =
{
	(GDBusSubtreeEnumerateFunc) enumerate_entry_subtree,
	(GDBusSubtreeIntrospectFunc) introspect_entry_subtree,
	(GDBusSubtreeDispatchFunc) dispatch_entry_subtree
};

/* containers in general */

static void
emit_updated (GDBusConnection *connection, const char *path)
{
	GError *error = NULL;
	g_dbus_connection_emit_signal (connection,
				       NULL,
				       path,
				       MEDIA_SERVER2_CONTAINER_IFACE_NAME,
				       "Updated",
				       NULL,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to emit Updated signal for MediaServer2 container %s: %s",
			   path,
			   error->message);
		g_clear_error (&error);
	}
}

static gboolean
emit_container_updated_cb (RBMediaServer2Plugin *plugin)
{
	GList *l;

	/* source containers */
	for (l = plugin->sources; l != NULL; l = l->next) {
		SourceRegistrationData *source_data = l->data;
		if (source_data->updated) {
			emit_updated (plugin->connection, source_data->dbus_path);
			source_data->updated = FALSE;
		}
	}

	/* .. subtrees .. */

	/* root */
	if (plugin->root_updated) {
		emit_updated (plugin->connection, RB_MEDIASERVER2_ROOT);
		plugin->root_updated = FALSE;
	}

	plugin->emit_updated_id = 0;
	return FALSE;
}


/* source containers */

static void
source_method_call (GDBusConnection *connection,
		    const char *sender,
		    const char *object_path,
		    const char *interface_name,
		    const char *method_name,
		    GVariant *parameters,
		    GDBusMethodInvocation *invocation,
		    SourceRegistrationData *source_data)
{
	GVariantBuilder *list;

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		return;
	}

	if (g_strcmp0 (method_name, "ListChildren") == 0 ||
	    g_strcmp0 (method_name, "ListItems") == 0) {
		GtkTreeModel *model;
		GtkTreeIter iter;
		guint list_offset;
		guint list_max;
		char **filter;
		guint count = 0;

		g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));

		if (rb_str_in_strv ("*", (const char **)filter)) {
			g_strfreev (filter);
			filter = g_strdupv ((char **)all_entry_properties);
		}

		model = GTK_TREE_MODEL (source_data->base_query_model);
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				RhythmDBEntry *entry;
				GVariantBuilder *eb;
				int i;
				if (list_max > 0 && count == list_max) {
					break;
				}

				entry = rhythmdb_query_model_iter_to_entry (source_data->base_query_model, &iter);
				if (entry == NULL) {
					continue;
				}

				if (list_offset > 0) {
					list_offset--;
					continue;
				}

				eb = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
				for (i = 0; filter[i] != NULL; i++) {
					GVariant *v;
					v = get_entry_property_value (entry, filter[i]);
					if (v != NULL) {
						g_variant_builder_add (eb, "{sv}", filter[i], v);
					}
				}

				g_variant_builder_add (list, "a{sv}", eb);
				count++;

			} while (gtk_tree_model_iter_next (model, &iter));
		}
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));

		g_strfreev (filter);
	} else if (g_strcmp0 (method_name, "ListContainers") == 0) {
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
	} else if (g_strcmp0 (method_name, "SearchObjects") == 0) {
		g_dbus_method_invocation_return_value (invocation, NULL);
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static GVariant *
get_source_property (GDBusConnection *connection,
		     const char *sender,
		     const char *object_path,
		     const char *interface_name,
		     const char *property_name,
		     GError **error,
		     SourceRegistrationData *source_data)
{
	GVariant *v;
	char *name;

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_OBJECT_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "Parent") == 0) {
			return g_variant_new_object_path (source_data->parent_dbus_path);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			return g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			g_object_get (source_data->source, "name", &name, NULL);
			v = g_variant_new_string (name);
			g_free (name);
			return v;
		}
	} else if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {

		if (g_strcmp0 (property_name, "ChildCount") == 0 ||
		    g_strcmp0 (property_name, "ItemCount") == 0) {
			int count;
			count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source_data->base_query_model), NULL);
			return g_variant_new_uint32 (count);
		} else if (g_strcmp0 (property_name, "ContainerCount") == 0) {
			return g_variant_new_uint32 (0);
		} else if (g_strcmp0 (property_name, "Searchable") == 0) {
			return g_variant_new_boolean (FALSE);
		}
	}
	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_NOT_SUPPORTED,
		     "Property %s.%s not supported",
		     interface_name,
		     property_name);
	return NULL;
}

static const GDBusInterfaceVTable source_vtable =
{
	(GDBusInterfaceMethodCallFunc) source_method_call,
	(GDBusInterfaceGetPropertyFunc) get_source_property,
	NULL
};

/* source container registration */

static void
add_source_container (GVariantBuilder *list, SourceRegistrationData *source_data, const char **filter)
{
	GVariantBuilder *i;
	int entry_count;
	gboolean all_props;

	i = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	all_props = rb_str_in_strv ("*", filter);

	entry_count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source_data->base_query_model), NULL);

	if (all_props || rb_str_in_strv ("Parent", filter)) {
		g_variant_builder_add (i, "{sv}", "Parent", g_variant_new_object_path (source_data->parent_dbus_path));
	}
	if (all_props || rb_str_in_strv ("Type", filter)) {
		g_variant_builder_add (i, "{sv}", "Type", g_variant_new_string ("container"));
	}
	if (all_props || rb_str_in_strv ("Path", filter)) {
		g_variant_builder_add (i, "{sv}", "Path", g_variant_new_string (source_data->dbus_path));
	}
	if (all_props || rb_str_in_strv ("DisplayName", filter)) {
		char *name;
		g_object_get (source_data->source, "name", &name, NULL);
		g_variant_builder_add (i, "{sv}", "DisplayName", g_variant_new_string (name));
		g_free (name);
	}
	if (all_props || rb_str_in_strv ("ChildCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ChildCount", g_variant_new_uint32 (entry_count));
	}
	if (all_props || rb_str_in_strv ("ItemCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ItemCount", g_variant_new_uint32 (entry_count));
	}
	if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ContainerCount", g_variant_new_uint32 (0));
	}
	if (all_props || rb_str_in_strv ("Searchable", filter)) {
		g_variant_builder_add (i, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
	}

	g_variant_builder_add (list, "a{sv}", i);
}

static int
count_sources_by_parent (RBMediaServer2Plugin *plugin, const char *parent_dbus_path)
{
	GList *l;
	int count = 0;
	for (l = plugin->sources; l != NULL; l = l->next) {
		SourceRegistrationData *source_data;
		source_data = l->data;
		if (g_strcmp0 (source_data->parent_dbus_path, parent_dbus_path) == 0) {
			count++;
		}
	}
	return count;
}

static void
list_sources_by_parent (RBMediaServer2Plugin *plugin,
			GVariantBuilder *list,
			const char *parent_dbus_path,
			guint *list_offset,
			guint *list_count,
			guint list_max,
			const char **filter)
{
	GList *l;
	for (l = plugin->sources; l != NULL; l = l->next) {
		SourceRegistrationData *source_data;
		if (list_max > 0 && (*list_count) == list_max) {
			break;
		}

		source_data = l->data;
		if (g_strcmp0 (source_data->parent_dbus_path, parent_dbus_path) != 0) {
			continue;
		}

		if ((*list_offset) > 0) {
			(*list_offset)--;
			continue;
		}

		add_source_container (list, source_data, filter);
		(*list_count)++;
	}
}

static SourceRegistrationData *
find_registration_data (RBMediaServer2Plugin *plugin, RBSource *source)
{
	GList *l;
	for (l = plugin->sources; l != NULL; l = l->next) {
		SourceRegistrationData *data = l->data;
		if (data->source == source) {
			return data;
		}
	}
	return NULL;
}

static void
destroy_registration_data (SourceRegistrationData *source_data)
{
	g_free (source_data->dbus_path);
	g_free (source_data->parent_dbus_path);
	g_object_unref (source_data->source);
	g_object_unref (source_data->base_query_model);

	g_free (source_data);
}

static void
source_updated (SourceRegistrationData *source_data, gboolean count_changed)
{
	source_data->updated = TRUE;

	if (count_changed) {
		/* find parent updated flag; for now it's always the root */
		source_data->plugin->root_updated = TRUE;
	}

	if (source_data->plugin->emit_updated_id == 0) {
		source_data->plugin->emit_updated_id =
			g_idle_add ((GSourceFunc)emit_container_updated_cb, source_data->plugin);
	}
}

/* signal handlers for source container updates */

static void
row_inserted_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, SourceRegistrationData *source_data)
{
	source_updated (source_data, TRUE);
}

static void
row_deleted_cb (GtkTreeModel *model, GtkTreePath *path, SourceRegistrationData *source_data)
{
	source_updated (source_data, TRUE);
}

static void
entry_prop_changed_cb (RhythmDBQueryModel *model,
		       RhythmDBPropType prop,
		       const GValue *old,
		       const GValue *new_value,
		       SourceRegistrationData *source_data)
{
	if (entry_property_maps (prop)) {
		source_updated (source_data, FALSE);
	}
}

static void
disconnect_query_model_signals (SourceRegistrationData *source_data)
{
	g_signal_handlers_disconnect_by_func (source_data->base_query_model, row_inserted_cb, source_data);
	g_signal_handlers_disconnect_by_func (source_data->base_query_model, entry_prop_changed_cb, source_data);
	g_signal_handlers_disconnect_by_func (source_data->base_query_model, row_deleted_cb, source_data);
}

static void
connect_query_model_signals (SourceRegistrationData *source_data)
{
	g_signal_connect (source_data->base_query_model, "row-inserted", G_CALLBACK (row_inserted_cb), source_data);
	g_signal_connect (source_data->base_query_model, "entry-prop-changed", G_CALLBACK (entry_prop_changed_cb), source_data);
	g_signal_connect (source_data->base_query_model, "row-deleted", G_CALLBACK (row_deleted_cb), source_data);
}

static void
base_query_model_updated_cb (RBSource *source, GParamSpec *pspec, SourceRegistrationData *source_data)
{
	if (source_data->base_query_model != NULL) {
		disconnect_query_model_signals (source_data);
		g_object_unref (source_data->base_query_model);
	}

	g_object_get (source, "base-query-model", &source_data->base_query_model, NULL);
	connect_query_model_signals (source_data);

	source_updated (source_data, TRUE);
}

static void
name_updated_cb (RBSource *source, GParamSpec *pspec, SourceRegistrationData *source_data)
{
	source_updated (source_data, FALSE);
}



static void
register_source_container (RBMediaServer2Plugin *plugin,
			   RBSource *source,
			   const char *dbus_path,
			   const char *parent_dbus_path,
			   gboolean subtree)
{
	SourceRegistrationData *source_data;

	source_data = g_new0 (SourceRegistrationData, 1);
	source_data->source = g_object_ref (source);
	source_data->dbus_path = g_strdup (dbus_path);
	source_data->parent_dbus_path = g_strdup (parent_dbus_path);
	source_data->plugin = plugin;

	if (subtree == FALSE) {
		GDBusInterfaceInfo *container_iface;

		container_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
		register_object (plugin, &source_vtable, container_iface, dbus_path, source_data, source_data->dbus_reg_id);
	}

	g_object_get (source, "base-query-model", &source_data->base_query_model, NULL);
	connect_query_model_signals (source_data);
	g_signal_connect (source, "notify::base-query-model", G_CALLBACK (base_query_model_updated_cb), source_data);
	g_signal_connect (source, "notify::name", G_CALLBACK (name_updated_cb), source_data);

	/* add to registration list */
	plugin->sources = g_list_append (plugin->sources, source_data);

	/* emit 'updated' signal on parent container */
	g_dbus_connection_emit_signal (plugin->connection, NULL, parent_dbus_path, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "Updated", NULL, NULL);
}

static void
unregister_source_container (RBMediaServer2Plugin *plugin, RBSource *source, gboolean deactivating)
{
	SourceRegistrationData *source_data;

	/* find registration data */
	source_data = find_registration_data (plugin, source);
	if (source_data == NULL) {
		rb_debug ("tried to unregister a source that isn't registered");
		return;
	}

	/* if object registration ids exist, unregister the object */
	unregister_object (plugin, source_data->dbus_reg_id);

	/* remove signal handlers */
	disconnect_query_model_signals (source_data);
	g_signal_handlers_disconnect_by_func (source, G_CALLBACK (base_query_model_updated_cb), source_data);
	g_signal_handlers_disconnect_by_func (source, G_CALLBACK (name_updated_cb), source_data);

	if (deactivating == FALSE) {
		/* remove from registration list */
		plugin->sources = g_list_remove (plugin->sources, source_data);

		/* emit 'updated' signal on parent container */
		g_dbus_connection_emit_signal (plugin->connection, NULL, source_data->parent_dbus_path, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "Updated", NULL, NULL);

		destroy_registration_data (source_data);
	}
}


/* root container */

static void
root_method_call (GDBusConnection *connection,
		  const char *sender,
		  const char *object_path,
		  const char *interface_name,
		  const char *method_name,
		  GVariant *parameters,
		  GDBusMethodInvocation *invocation,
		  RBMediaServer2Plugin *plugin)
{
	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {
		guint list_offset;
		guint list_max;
		guint list_count = 0;
		const char **filter;
		GVariantBuilder *list;

		if (g_strcmp0 (method_name, "ListChildren") == 0 ||
		    g_strcmp0 (method_name, "ListContainers") == 0) {
			rb_debug ("listing containers (%s)", method_name);
			g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);

			list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
			/* add subtrees, when such things exist */
			list_sources_by_parent (plugin, list, object_path, &list_offset, &list_count, list_max, filter);

			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_strfreev ((char **)filter);
		} else if (g_strcmp0 (method_name, "ListItems") == 0) {
			rb_debug ("listing items");
			g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
			list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_strfreev ((char **)filter);
		} else if (g_strcmp0 (method_name, "SearchObjects") == 0) {
			rb_debug ("search not supported");
			g_dbus_method_invocation_return_value (invocation, NULL);
		}
	} else {
		g_dbus_method_invocation_return_error (invocation,
						       G_DBUS_ERROR,
						       G_DBUS_ERROR_NOT_SUPPORTED,
						       "Method %s.%s not supported",
						       interface_name,
						       method_name);
	}
}

static GVariant *
get_root_property (GDBusConnection *connection,
		   const char *sender,
		   const char *object_path,
		   const char *interface_name,
		   const char *property_name,
		   GError **error,
		   RBMediaServer2Plugin *plugin)
{
	GVariant *v;
	int count;
	if (g_strcmp0 (interface_name, MEDIA_SERVER2_OBJECT_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "Parent") == 0) {
			return g_variant_new_object_path (object_path);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			return g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			char *share_name = eel_gconf_get_string (CONF_DAAP_SHARE_NAME);
			if (share_name == NULL || share_name[0] == '\0') {
				share_name = g_strdup ("@REALNAME@'s Rhythmbox on @HOSTNAME@");
			}
			v = g_variant_new_string (share_name);
			g_free (share_name);
			return v;
		}
	} else if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "ChildCount") == 0 ||
		    g_strcmp0 (property_name, "ContainerCount") == 0) {
			count = count_sources_by_parent (plugin, object_path);
			/* include subtrees */
			return g_variant_new_uint32 (count);
		} else if (g_strcmp0 (property_name, "ItemCount") == 0) {
			return g_variant_new_uint32 (0);
		} else if (g_strcmp0 (property_name, "Searchable") == 0) {
			return g_variant_new_boolean (FALSE);
		} else if (g_strcmp0 (property_name, "Icon") == 0) {
			/* XXX implement this, I guess */
		}
	}
	g_set_error (error,
		     G_DBUS_ERROR,
		     G_DBUS_ERROR_NOT_SUPPORTED,
		     "Property %s.%s not supported",
		     interface_name,
		     property_name);
	return NULL;
}

static const GDBusInterfaceVTable root_vtable =
{
	(GDBusInterfaceMethodCallFunc) root_method_call,
	(GDBusInterfaceGetPropertyFunc) get_root_property,
	NULL
};

/* plugin */

static void
name_acquired_cb (GDBusConnection *connection, const char *name, RBMediaServer2Plugin *plugin)
{
	rb_debug ("acquired dbus name %s", name);
}

static void
name_lost_cb (GDBusConnection *connection, const char *name, RBMediaServer2Plugin *plugin)
{
	rb_debug ("lost dbus name %s", name);
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBMediaServer2Plugin *plugin;
	GDBusInterfaceInfo *item_iface;
	GDBusInterfaceInfo *container_iface;
	RBSource *source;
	GError *error = NULL;

	rb_debug ("activating DBus MediaServer2 plugin");

	plugin = RB_DBUS_MEDIA_SERVER_PLUGIN (bplugin);
	g_object_get (shell, "db", &plugin->db, NULL);
	plugin->shell = g_object_ref (shell);

	plugin->node_info = g_dbus_node_info_new_for_xml (media_server2_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to parse MediaServer2 spec: %s", error->message);
		g_clear_error (&error);
		return;
	}

	plugin->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (error != NULL) {
		g_warning ("Unable to connect to D-Bus: %s", error->message);
		g_clear_error (&error);
		return;
	}

	container_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
	item_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_ITEM_IFACE_NAME);

	/* register root */
	register_object (plugin, &root_vtable, container_iface, RB_MEDIASERVER2_ROOT, G_OBJECT (plugin), plugin->root_reg_id);

	/* register fixed sources (library, podcasts, etc.) */
	g_object_get (shell, "library-source", &source, NULL);
	register_source_container (plugin, source, RB_MEDIASERVER2_LIBRARY, RB_MEDIASERVER2_ROOT, FALSE);
	g_object_unref (source);

	/* register source subtrees - playlists, devices */

	/* register entry subtree */
	plugin->entry_reg_id = g_dbus_connection_register_subtree (plugin->connection,
								   RB_MEDIASERVER2_ENTRY_SUBTREE,
								   &entry_subtree_vtable,
								   G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
								   plugin,
								   NULL,
								   &error);
	if (error != NULL) {
		g_warning ("Unable to register MediaServer2 entry subtree: %s", error->message);
		g_clear_error (&error);
		return;
	}

	plugin->name_own_id = g_bus_own_name (G_BUS_TYPE_SESSION,
					      RB_MEDIASERVER2_BUS_NAME,
					      G_BUS_NAME_OWNER_FLAGS_NONE,
					      NULL,
					      (GBusNameAcquiredCallback) name_acquired_cb,
					      (GBusNameLostCallback) name_lost_cb,
					      g_object_ref (plugin),
					      g_object_unref);
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBMediaServer2Plugin *plugin;
	GList *l;

	plugin = RB_DBUS_MEDIA_SERVER_PLUGIN (bplugin);

	if (plugin->emit_updated_id != 0) {
		g_source_remove (plugin->emit_updated_id);
		plugin->emit_updated_id = 0;
	}

	/* unregister objects */
	unregister_object (plugin, plugin->root_reg_id);

	for (l = plugin->sources; l != NULL; l = l->next) {
		unregister_source_container (plugin, l->data, TRUE);
	}
	rb_list_destroy_free (plugin->sources, (GDestroyNotify) destroy_registration_data);
	plugin->sources = NULL;

	/* .. unregister subtrees .. */

	if (plugin->entry_reg_id != 0) {
		g_dbus_connection_unregister_subtree (plugin->connection, plugin->entry_reg_id);
		plugin->entry_reg_id = 0;
	}

	if (plugin->shell != NULL) {
		g_object_unref (plugin->shell);
		plugin->shell = NULL;
	}
	if (plugin->db != NULL) {
		g_object_unref (plugin->db);
		plugin->db = NULL;
	}

	if (plugin->name_own_id > 0) {
		g_bus_unown_name (plugin->name_own_id);
		plugin->name_own_id = 0;
	}

	if (plugin->connection != NULL) {
		g_object_unref (plugin->connection);
		plugin->connection = NULL;
	}
}


static void
rb_dbus_media_server_plugin_class_init (RBMediaServer2PluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}
