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

#include <lib/rb-util.h>
#include <lib/rb-debug.h>
#include <lib/rb-gst-media-types.h>
#include <plugins/rb-plugin-macros.h>
#include <shell/rb-shell.h>
#include <shell/rb-shell-player.h>
#include <sources/rb-display-page-model.h>
#include <sources/rb-playlist-source.h>
#include <sources/rb-device-source.h>
#include <rhythmdb/rhythmdb-property-model.h>

#define RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN	(rb_dbus_media_server_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBDbusMediaServerPlugin, rb_dbus_media_server_plugin, RB, DBUS_MEDIA_SERVER_PLUGIN, PeasExtensionBase)

#include "dbus-media-server-spec.h"

#define RB_MEDIASERVER2_BUS_NAME	MEDIA_SERVER2_BUS_NAME_PREFIX ".Rhythmbox"

#define RB_MEDIASERVER2_PREFIX		"/org/gnome/UPnP/MediaServer2/"
#define RB_MEDIASERVER2_ROOT		RB_MEDIASERVER2_PREFIX "Rhythmbox"
#define RB_MEDIASERVER2_LIBRARY		RB_MEDIASERVER2_PREFIX "Library"
#define RB_MEDIASERVER2_PLAYLISTS	RB_MEDIASERVER2_PREFIX "Playlists"
#define RB_MEDIASERVER2_DEVICES		RB_MEDIASERVER2_PREFIX "Devices"
#define RB_MEDIASERVER2_ENTRY_SUBTREE	RB_MEDIASERVER2_PREFIX "Entry"
#define RB_MEDIASERVER2_ENTRY_PREFIX	RB_MEDIASERVER2_ENTRY_SUBTREE "/"

struct _RBDbusMediaServerPlugin
{
	PeasExtensionBase parent;

	GDBusNodeInfo *node_info;
	guint name_own_id;

	GDBusConnection *connection;

	/* object/subtree registration ids */
	guint root_reg_id[2];
	gboolean root_updated;
	guint entry_reg_id;

	guint emit_updated_id;

	/* source and category registrations */
	GList *sources;
	GList *categories;

	GSettings *settings;
	RhythmDB *db;
	RBDisplayPageModel *display_page_model;
};

struct _RBDbusMediaServerPluginClass
{
	PeasExtensionBaseClass parent_class;
};

typedef struct
{
	char *name;
	guint dbus_reg_id[2];
	gboolean updated;
	char *dbus_path;
	char *parent_dbus_path;

	gboolean (*match_source) (RBSource *source);

	RBDbusMediaServerPlugin *plugin;
} CategoryRegistrationData;

typedef struct
{
	RBSource *source;
	RhythmDBQueryModel *base_query_model;
	guint dbus_reg_id[2];
	gboolean updated;
	char *dbus_path;
	char *parent_dbus_path;

	gboolean flat;
	guint all_tracks_reg_id[2];
	GList *properties;

	RBDbusMediaServerPlugin *plugin;
} SourceRegistrationData;

typedef struct
{
	SourceRegistrationData *source_data;
	char *dbus_path;
	char *display_name;
	guint dbus_object_id[2];
	guint dbus_subtree_id;
	RhythmDBPropType property;
	RhythmDBPropertyModel *model;
	gboolean updated;
	GList *updated_values;
} SourcePropertyRegistrationData;

RB_DEFINE_PLUGIN(RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN, RBDbusMediaServerPlugin, rb_dbus_media_server_plugin,)

G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

static void unregister_source_container (RBDbusMediaServerPlugin *plugin, SourceRegistrationData *source_data, gboolean deactivating);
static void emit_source_tracks_property_updates (RBDbusMediaServerPlugin *plugin, SourceRegistrationData *source_data);
static void emit_property_value_property_updates (RBDbusMediaServerPlugin *plugin, SourcePropertyRegistrationData *source_data, RBRefString *value);
static void emit_category_container_property_updates (RBDbusMediaServerPlugin *plugin, CategoryRegistrationData *category_data);
static void emit_root_property_updates (RBDbusMediaServerPlugin *plugin);

static void
rb_dbus_media_server_plugin_init (RBDbusMediaServerPlugin *plugin)
{
}

static void
register_object (RBDbusMediaServerPlugin *plugin,
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
unregister_object (RBDbusMediaServerPlugin *plugin, guint *ids)
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
	"AlbumArt",
	"TrackNumber"
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
		case RHYTHMDB_PROP_MEDIA_TYPE:
		case RHYTHMDB_PROP_FILE_SIZE:
		case RHYTHMDB_PROP_ALBUM:
		case RHYTHMDB_PROP_ARTIST:
		case RHYTHMDB_PROP_YEAR:
		case RHYTHMDB_PROP_GENRE:
		case RHYTHMDB_PROP_DURATION:
		case RHYTHMDB_PROP_BITRATE:
		case RHYTHMDB_PROP_TRACK_NUMBER:
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
		return g_variant_new_string ("music");
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
		media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
		return g_variant_new_string (rb_gst_media_type_to_mime_type (media_type));
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
	} else if (g_strcmp0 (property_name, "TrackNumber") == 0) {
		return g_variant_new_int32 (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER));
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
		    RBDbusMediaServerPlugin *plugin)
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
			 RBDbusMediaServerPlugin *plugin)
{
	return (char **)g_new0(char *, 1);
}

static GDBusInterfaceInfo **
introspect_entry_subtree (GDBusConnection *connection,
			  const char *sender,
			  const char *object_path,
			  const char *node,
			  RBDbusMediaServerPlugin *plugin)
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
			RBDbusMediaServerPlugin *plugin)
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
emit_container_updated_cb (RBDbusMediaServerPlugin *plugin)
{
	GList *l, *ll, *lll;

	rb_debug ("emitting updates");
	/* source containers */
	for (l = plugin->sources; l != NULL; l = l->next) {
		SourceRegistrationData *source_data = l->data;

		/* property containers */
		for (ll = source_data->properties; ll != NULL; ll = ll->next) {
			SourcePropertyRegistrationData *prop_data = ll->data;

			/* emit value updates */
			for (lll = prop_data->updated_values; lll != NULL; lll = lll->next) {
				RBRefString *value = lll->data;
				emit_property_value_property_updates (plugin, prop_data, value);
			}
			rb_list_destroy_free (prop_data->updated_values, (GDestroyNotify)rb_refstring_unref);
			prop_data->updated_values = NULL;

			if (prop_data->updated) {
				emit_updated (plugin->connection, prop_data->dbus_path);
				prop_data->updated = FALSE;
			}
		}

		if (source_data->updated) {
			emit_source_tracks_property_updates (plugin, source_data);
			if (source_data->flat) {
				emit_updated (plugin->connection, source_data->dbus_path);
			} else {
				char *path;
				path = g_strdup_printf ("%s/all", source_data->dbus_path);
				emit_updated (plugin->connection, path);
				g_free (path);
			}
			source_data->updated = FALSE;
		}

	}

	/* source categories */
	for (l = plugin->categories; l != NULL; l = l->next) {
		CategoryRegistrationData *category_data = l->data;
		if (category_data->updated) {
			emit_category_container_property_updates (plugin, category_data);
			emit_updated (plugin->connection, category_data->dbus_path);
			category_data->updated = FALSE;
		}
	}

	/* root */
	if (plugin->root_updated) {
		emit_root_property_updates (plugin);
		emit_updated (plugin->connection, RB_MEDIASERVER2_ROOT);
		plugin->root_updated = FALSE;
	}

	rb_debug ("done emitting updates");
	plugin->emit_updated_id = 0;
	return FALSE;
}

static void
emit_updated_in_idle (RBDbusMediaServerPlugin *plugin)
{
	if (plugin->emit_updated_id == 0) {
		plugin->emit_updated_id =
			g_idle_add_full (G_PRIORITY_LOW,
					 (GSourceFunc)emit_container_updated_cb,
					 plugin,
					 NULL);
	}
}


/* property value source subcontainers (source/year/1995) */

static char *
encode_property_value (const char *value)
{
	char *encoded;
	const char *hex = "0123456789ABCDEF";
	char *d;
	char c;

	encoded = g_malloc0 (strlen (value) * 3 + 1);
	d = encoded;
	while (*value != '\0') {
		c = *value++;
		if (g_ascii_isalnum (c)) {
			*d++ = c;
		} else {
			guint8 v = (guint8)c;
			*d++ = '_';
			*d++ = hex[(v >> 4) & 0x0f];
			*d++ = hex[v & 0x0f];
		}
	}

	return encoded;
}

#define XDIGIT(c) ((c) <= '9' ? (c) - '0' : ((c) & 0x4F) - 'A' + 10)
#define HEXCHAR(s) ((XDIGIT (s[1]) << 4) + XDIGIT (s[2]))

static char *
decode_property_value (char *encoded)
{
	char *decoded;
	char *e;
	char *d;

	decoded = g_malloc0 (strlen (encoded) + 1);
	d = decoded;
	e = encoded;
	while (*e != '\0') {
		if (*e != '_') {
			*d++ = *e++;
		} else if (e[1] != '\0' && e[2] != '\0') {
			*d++ = HEXCHAR(e);
			e += 3;
		} else {
			/* broken */
			break;
		}
	}

	return decoded;
}

static char *
extract_property_value (RhythmDB *db, const char *object_path)
{
	char **bits;
	char *value;
	int nbits;

	bits = g_strsplit (object_path, "/", 0);
	nbits = g_strv_length (bits);

	value = decode_property_value (bits[nbits-1]);
	g_strfreev (bits);
	return value;
}

static void
property_value_method_call (GDBusConnection *connection,
			    const char *sender,
			    const char *object_path,
			    const char *interface_name,
			    const char *method_name,
			    GVariant *parameters,
			    GDBusMethodInvocation *invocation,
			    SourcePropertyRegistrationData *data)
{
	GVariantBuilder *list;
	RhythmDB *db;
	char *value;

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		return;
	}

	db = data->source_data->plugin->db;
	value = extract_property_value (db, object_path);

	if (g_strcmp0 (method_name, "ListChildren") == 0 ||
	    g_strcmp0 (method_name, "ListItems") == 0) {
		RhythmDBQuery *base;
		RhythmDBQuery *query;
		RhythmDBQueryModel *query_model;
		GtkTreeModel *model;
		GtkTreeIter iter;
		guint list_offset;
		guint list_max;
		char **filter;
		guint count = 0;

		/* consider caching query models? */
		g_object_get (data->source_data->base_query_model, "query", &base, NULL);
		query = rhythmdb_query_copy (base);

		rhythmdb_query_append (db,
				       query,
				       RHYTHMDB_QUERY_PROP_EQUALS, data->property, value,
				       RHYTHMDB_QUERY_END);
		/* maybe use a result list? */
		query_model = rhythmdb_query_model_new_empty (db);
		rhythmdb_do_full_query_parsed (db, RHYTHMDB_QUERY_RESULTS (query_model), query);
		rhythmdb_query_free (query);

		g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));

		if (rb_str_in_strv ("*", (const char **)filter)) {
			g_strfreev (filter);
			filter = g_strdupv ((char **)all_entry_properties);
		}

		model = GTK_TREE_MODEL (query_model);
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			do {
				RhythmDBEntry *entry;
				GVariantBuilder *eb;
				int i;
				if (list_max > 0 && count == list_max) {
					break;
				}

				entry = rhythmdb_query_model_iter_to_entry (query_model, &iter);
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
		g_variant_builder_unref (list);

		g_strfreev (filter);
	} else if (g_strcmp0 (method_name, "ListContainers") == 0) {
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);
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

	g_free (value);
}

static guint
get_property_value_count (SourcePropertyRegistrationData *data, const char *value)
{
	guint entry_count = 0;
	GtkTreeIter iter;

	if (rhythmdb_property_model_iter_from_string (data->model, value, &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (data->model), &iter,
				    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &entry_count,
				    -1);
	}
	return entry_count;
}

static GVariant *
get_property_value_property (GDBusConnection *connection,
			     const char *sender,
			     const char *object_path,
			     const char *interface_name,
			     const char *property_name,
			     GError **error,
			     SourcePropertyRegistrationData *data)
{
	RhythmDB *db;
	GVariant *v = NULL;
	char *value;

	db = data->source_data->plugin->db;
	value = extract_property_value (db, object_path);

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_OBJECT_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "Parent") == 0) {
			v = g_variant_new_object_path (data->dbus_path);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			v = g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			v = g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			v = g_variant_new_string (value);
		}
	} else if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {

		if (g_strcmp0 (property_name, "ChildCount") == 0 ||
		    g_strcmp0 (property_name, "ItemCount") == 0) {
			v = g_variant_new_uint32 (get_property_value_count (data, value));
		} else if (g_strcmp0 (property_name, "ContainerCount") == 0) {
			v = g_variant_new_uint32 (0);
		} else if (g_strcmp0 (property_name, "Searchable") == 0) {
			v = g_variant_new_boolean (FALSE);
		}
	}

	if (v == NULL) {
		g_set_error (error,
			     G_DBUS_ERROR,
			     G_DBUS_ERROR_NOT_SUPPORTED,
			     "Property %s.%s not supported",
			     interface_name,
			     property_name);
	}
	g_free (value);
	return v;
}

static void
emit_property_value_property_updates (RBDbusMediaServerPlugin *plugin, SourcePropertyRegistrationData *data, RBRefString *value)
{
	GError *error = NULL;
	const char *invalidated[] = { NULL };
	GVariantBuilder *properties;
	GVariant *parameters;
	GVariant *v;
	char *encoded;
	char *path;

	rb_debug ("updating properties for %s/%s", data->dbus_path, rb_refstring_get (value));
	properties = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));

	v = g_variant_new_uint32 (get_property_value_count (data, rb_refstring_get (value)));
	g_variant_builder_add (properties, "{sv}", "ItemCount", v);
	g_variant_builder_add (properties, "{sv}", "ChildCount", v);
	g_variant_builder_add (properties, "{sv}", "ContainerCount", g_variant_new_uint32 (0));

	encoded = encode_property_value (rb_refstring_get (value));
	path = g_strdup_printf ("%s/%s", data->dbus_path, encoded);
	g_free (encoded);

	parameters = g_variant_new ("(sa{sv}^as)",
				    MEDIA_SERVER2_CONTAINER_IFACE_NAME,
				    properties,
				    invalidated);
	g_variant_builder_unref (properties);
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       path,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       parameters,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to send property changes for MediaServer2 container %s: %s",
			   path,
			   error->message);
		g_clear_error (&error);
	}

	emit_updated (plugin->connection, path);

	g_free (path);
}

static const GDBusInterfaceVTable property_value_vtable =
{
	(GDBusInterfaceMethodCallFunc) property_value_method_call,
	(GDBusInterfaceGetPropertyFunc) get_property_value_property,
	NULL
};

/* property-based source subcontainers (source/year/) */

static void
property_container_method_call (GDBusConnection *connection,
				const char *sender,
				const char *object_path,
				const char *interface_name,
				const char *method_name,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				SourcePropertyRegistrationData *data)
{
	GVariantBuilder *list;

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		return;
	}

	if (g_strcmp0 (method_name, "ListChildren") == 0 ||
	    g_strcmp0 (method_name, "ListContainers") == 0) {
		GtkTreeModel *model;
		GtkTreeIter iter;
		guint list_offset;
		guint list_max;
		const char **filter;
		guint count = 0;
		gboolean all_props;

		g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));

		all_props = rb_str_in_strv ("*", filter);

		model = GTK_TREE_MODEL (data->model);
		if (gtk_tree_model_get_iter_first (model, &iter)) {
			/* skip 'all' row */
			while (gtk_tree_model_iter_next (model, &iter)) {
				char *value;
				guint value_count;
				GVariantBuilder *eb;
				if (list_max > 0 && count == list_max) {
					break;
				}

				if (list_offset > 0) {
					list_offset--;
					continue;
				}

				gtk_tree_model_get (model, &iter,
						    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &value,
						    RHYTHMDB_PROPERTY_MODEL_COLUMN_NUMBER, &value_count,
						    -1);

				eb = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
				if (all_props || rb_str_in_strv ("Parent", filter)) {
					g_variant_builder_add (eb, "{sv}", "Parent", g_variant_new_object_path (object_path));
				}
				if (all_props || rb_str_in_strv ("Type", filter)) {
					g_variant_builder_add (eb, "{sv}", "Type", g_variant_new_string ("container"));
				}
				if (all_props || rb_str_in_strv ("Path", filter)) {
					char *encoded;
					char *value_path;
					encoded = encode_property_value (value);
					value_path = g_strdup_printf ("%s/%s", object_path, encoded);
					g_variant_builder_add (eb, "{sv}", "Path", g_variant_new_string (value_path));
					g_free (encoded);
					g_free (value_path);
				}
				if (all_props || rb_str_in_strv ("DisplayName", filter)) {
					g_variant_builder_add (eb, "{sv}", "DisplayName", g_variant_new_string (value));
				}
				if (all_props || rb_str_in_strv ("ChildCount", filter)) {
					g_variant_builder_add (eb, "{sv}", "ChildCount", g_variant_new_uint32 (value_count));
				}
				if (all_props || rb_str_in_strv ("ItemCount", filter)) {
					g_variant_builder_add (eb, "{sv}", "ItemCount", g_variant_new_uint32 (value_count));
				}
				if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
					g_variant_builder_add (eb, "{sv}", "ContainerCount", g_variant_new_uint32 (0));
				}
				if (all_props || rb_str_in_strv ("Searchable", filter)) {
					g_variant_builder_add (eb, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
				}

				g_variant_builder_add (list, "a{sv}", eb);
				g_free (value);
				count++;
			}
		}
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);

		g_strfreev ((char **)filter);
	} else if (g_strcmp0 (method_name, "ListItems") == 0) {
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);
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
get_property_container_property (GDBusConnection *connection,
				 const char *sender,
				 const char *object_path,
				 const char *interface_name,
				 const char *property_name,
				 GError **error,
				 SourcePropertyRegistrationData *data)
{
	if (g_strcmp0 (interface_name, MEDIA_SERVER2_OBJECT_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "Parent") == 0) {
			return g_variant_new_object_path (data->source_data->dbus_path);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			return g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			return g_variant_new_string (data->display_name);
		}
	} else if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {

		if (g_strcmp0 (property_name, "ChildCount") == 0 ||
		    g_strcmp0 (property_name, "ContainerCount") == 0) {
			/* don't include the 'all' row */
			guint count;
			count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (data->model), NULL) - 1;
			return g_variant_new_uint32 (count);
		} else if (g_strcmp0 (property_name, "ItemCount") == 0) {
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

static const GDBusInterfaceVTable property_vtable =
{
	(GDBusInterfaceMethodCallFunc) property_container_method_call,
	(GDBusInterfaceGetPropertyFunc) get_property_container_property,
	NULL
};

static void
prop_model_row_inserted_cb (GtkTreeModel *model,
			    GtkTreePath *path,
			    GtkTreeIter *iter,
			    SourcePropertyRegistrationData *prop_data)
{
	prop_data->updated = TRUE;
	emit_updated_in_idle (prop_data->source_data->plugin);
}

static void
prop_model_row_changed_cb (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   SourcePropertyRegistrationData *prop_data)
{
	char *value;
	RBRefString *refstring;
	gboolean is_all;
	GList *l;

	gtk_tree_model_get (model, iter,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_TITLE, &value,
			    RHYTHMDB_PROPERTY_MODEL_COLUMN_PRIORITY, &is_all,
			    -1);
	if (is_all) {
		g_free (value);
		return;
	}

	refstring = rb_refstring_new (value);
	g_free (value);

	for (l = prop_data->updated_values; l != NULL; l = l->next) {
		if (refstring == (RBRefString *)l->data) {
			rb_refstring_unref (refstring);
			return;
		}
	}

	prop_data->updated_values = g_list_prepend (prop_data->updated_values, refstring);
	emit_updated_in_idle (prop_data->source_data->plugin);
}

static void
prop_model_row_deleted_cb (RhythmDBPropertyModel *prop_model,
			   GtkTreePath *path,
			   SourcePropertyRegistrationData *prop_data)
{
	prop_data->updated = TRUE;
	emit_updated_in_idle (prop_data->source_data->plugin);
}

static char **
enumerate_property_value_subtree (GDBusConnection *connection,
				  const char *sender,
				  const char *object_path,
				  SourcePropertyRegistrationData *data)
{
	return (char **)g_new0(char *, 1);
}

static GDBusInterfaceInfo **
introspect_property_value_subtree (GDBusConnection *connection,
				   const char *sender,
				   const char *object_path,
				   const char *node,
				   SourcePropertyRegistrationData *data)
{
	GPtrArray *p;
	GDBusInterfaceInfo *i;

	p = g_ptr_array_new ();

	i = g_dbus_node_info_lookup_interface (data->source_data->plugin->node_info, MEDIA_SERVER2_OBJECT_IFACE_NAME);
	g_ptr_array_add (p, g_dbus_interface_info_ref (i));

	i = g_dbus_node_info_lookup_interface (data->source_data->plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
	g_ptr_array_add (p, g_dbus_interface_info_ref (i));

	g_ptr_array_add (p, NULL);

	return (GDBusInterfaceInfo **)g_ptr_array_free (p, FALSE);
}

static const GDBusInterfaceVTable *
dispatch_property_value_subtree (GDBusConnection *connection,
				 const char *sender,
				 const char *object_path,
				 const char *interface_name,
				 const char *node,
				 gpointer *out_user_data,
				 SourcePropertyRegistrationData *data)
{
	*out_user_data = data;
	return &property_value_vtable;
}

static const GDBusSubtreeVTable property_subtree_vtable =
{
	(GDBusSubtreeEnumerateFunc) enumerate_property_value_subtree,
	(GDBusSubtreeIntrospectFunc) introspect_property_value_subtree,
	(GDBusSubtreeDispatchFunc) dispatch_property_value_subtree
};

static void
register_property_container (GDBusConnection *connection,
			     SourceRegistrationData *source_data,
			     RhythmDBPropType property,
			     const char *display_name)
{
	SourcePropertyRegistrationData *data;
	GDBusInterfaceInfo *iface;

	data = g_new0 (SourcePropertyRegistrationData, 1);
	data->source_data = source_data;
	data->property = property;
	data->display_name = g_strdup (display_name);
	data->dbus_path = g_strdup_printf ("%s/%s",
					   source_data->dbus_path,
					   rhythmdb_nice_elt_name_from_propid (source_data->plugin->db, property));

	data->model = rhythmdb_property_model_new (source_data->plugin->db, property);
	g_object_set (data->model, "query-model", source_data->base_query_model, NULL);
	g_signal_connect (data->model, "row-inserted", G_CALLBACK (prop_model_row_inserted_cb), data);
	g_signal_connect (data->model, "row-changed", G_CALLBACK (prop_model_row_changed_cb), data);
	g_signal_connect (data->model, "row-deleted", G_CALLBACK (prop_model_row_deleted_cb), data);

	data->dbus_subtree_id =
		g_dbus_connection_register_subtree (connection,
						    data->dbus_path,
						    &property_subtree_vtable,
						    G_DBUS_SUBTREE_FLAGS_DISPATCH_TO_UNENUMERATED_NODES,
						    data,
						    NULL,
						    NULL);

	iface = g_dbus_node_info_lookup_interface (source_data->plugin->node_info, MEDIA_SERVER2_OBJECT_IFACE_NAME);
	data->dbus_object_id[0] =
		g_dbus_connection_register_object (connection,
						   data->dbus_path,
						   iface,
						   &property_vtable,
						   data,
						   NULL,
						   NULL);

	iface = g_dbus_node_info_lookup_interface (source_data->plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
	data->dbus_object_id[1] =
		g_dbus_connection_register_object (connection,
						   data->dbus_path,
						   iface,
						   &property_vtable,
						   data,
						   NULL,
						   NULL);

	source_data->properties = g_list_append (source_data->properties, data);
}


/* source containers */

static void
source_tracks_method_call (GDBusConnection *connection,
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
		g_variant_builder_unref (list);

		g_strfreev (filter);
	} else if (g_strcmp0 (method_name, "ListContainers") == 0) {
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);
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
get_source_tracks_property (GDBusConnection *connection,
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
			if (source_data->flat) {
				return g_variant_new_object_path (source_data->parent_dbus_path);
			} else {
				return g_variant_new_object_path (source_data->dbus_path);
			}
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			return g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			if (source_data->flat) {
				g_object_get (source_data->source, "name", &name, NULL);
				v = g_variant_new_string (name);
				g_free (name);
				return v;
			} else {
				return g_variant_new_string (_("All Tracks"));
			}
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

static void
add_source_tracks_property (RBDbusMediaServerPlugin *plugin, GVariantBuilder *properties, const char *iface, const char *property, SourceRegistrationData *source_data)
{
	GVariant *v;
	v = get_source_tracks_property (plugin->connection, NULL, source_data->dbus_path, iface, property, NULL, source_data);
	g_variant_builder_add (properties, "{sv}", property, v);
}

static void
emit_source_tracks_property_updates (RBDbusMediaServerPlugin *plugin, SourceRegistrationData *source_data)
{
	GError *error = NULL;
	const char *invalidated[] = { NULL };
	GVariantBuilder *properties;
	GVariant *parameters;
	char *path;

	rb_debug ("updating properties for source %s", source_data->dbus_path);
	properties = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	add_source_tracks_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ItemCount", source_data);
	add_source_tracks_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ChildCount", source_data);
	add_source_tracks_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ContainerCount", source_data);

	parameters = g_variant_new ("(sa{sv}^as)",
				    MEDIA_SERVER2_CONTAINER_IFACE_NAME,
				    properties,
				    invalidated);
	g_variant_builder_unref (properties);
	if (source_data->flat) {
		path = g_strdup (source_data->dbus_path);
	} else {
		path = g_strdup_printf ("%s/all", source_data->dbus_path);
	}
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       path,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       parameters,
				       &error);
	g_free (path);
	if (error != NULL) {
		g_warning ("Unable to send property changes for MediaServer2 container %s: %s",
			   source_data->dbus_path,
			   error->message);
		g_clear_error (&error);
	}
}

static const GDBusInterfaceVTable source_tracks_vtable =
{
	(GDBusInterfaceMethodCallFunc) source_tracks_method_call,
	(GDBusInterfaceGetPropertyFunc) get_source_tracks_property,
	NULL
};


static void
source_properties_method_call (GDBusConnection *connection,
			       const char *sender,
			       const char *object_path,
			       const char *interface_name,
			       const char *method_name,
			       GVariant *parameters,
			       GDBusMethodInvocation *invocation,
			       SourceRegistrationData *source_data)
{
	GVariantBuilder *list;
	guint value_count;

	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) != 0) {
		rb_debug ("method call on unexpected interface %s", interface_name);
		return;
	}

	if (g_strcmp0 (method_name, "ListChildren") == 0 ||
	    g_strcmp0 (method_name, "ListContainers") == 0) {
		GList *l;
		guint list_offset;
		guint list_max;
		const char **filter;
		guint count = 0;
		gboolean all_props;
		GVariantBuilder *eb;

		g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));

		all_props = rb_str_in_strv ("*", filter);

		/* 'all tracks' container */
		if (list_offset == 0) {
			eb = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
			value_count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source_data->base_query_model), NULL);
			if (all_props || rb_str_in_strv ("Parent", filter)) {
				g_variant_builder_add (eb, "{sv}", "Parent", g_variant_new_object_path (object_path));
			}
			if (all_props || rb_str_in_strv ("Type", filter)) {
				g_variant_builder_add (eb, "{sv}", "Type", g_variant_new_string ("container"));
			}
			if (all_props || rb_str_in_strv ("Path", filter)) {
				char *path;
				path = g_strdup_printf ("%s/all", object_path);
				g_variant_builder_add (eb, "{sv}", "Path", g_variant_new_string (path));
				g_free (path);
			}
			if (all_props || rb_str_in_strv ("DisplayName", filter)) {
				g_variant_builder_add (eb, "{sv}", "DisplayName", g_variant_new_string (_("All Tracks")));
			}
			if (all_props || rb_str_in_strv ("ChildCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ChildCount", g_variant_new_uint32 (value_count));
			}
			if (all_props || rb_str_in_strv ("ItemCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ItemCount", g_variant_new_uint32 (value_count));
			}
			if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ContainerCount", g_variant_new_uint32 (0));
			}
			if (all_props || rb_str_in_strv ("Searchable", filter)) {
				g_variant_builder_add (eb, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
			}

			g_variant_builder_add (list, "a{sv}", eb);
			count++;
		} else {
			list_offset--;
		}

		/* property-based containers */
		for (l = source_data->properties; l != NULL; l = l->next) {
			SourcePropertyRegistrationData *prop_data = l->data;
			if (list_max > 0 && count == list_max) {
				break;
			}

			if (list_offset > 0) {
				list_offset--;
				continue;
			}

			value_count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (prop_data->model), NULL) - 1;

			eb = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
			if (all_props || rb_str_in_strv ("Parent", filter)) {
				g_variant_builder_add (eb, "{sv}", "Parent", g_variant_new_object_path (object_path));
			}
			if (all_props || rb_str_in_strv ("Type", filter)) {
				g_variant_builder_add (eb, "{sv}", "Type", g_variant_new_string ("container"));
			}
			if (all_props || rb_str_in_strv ("Path", filter)) {
				g_variant_builder_add (eb, "{sv}", "Path", g_variant_new_string (prop_data->dbus_path));
			}
			if (all_props || rb_str_in_strv ("DisplayName", filter)) {
				g_variant_builder_add (eb, "{sv}", "DisplayName", g_variant_new_string (prop_data->display_name));
			}
			if (all_props || rb_str_in_strv ("ChildCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ChildCount", g_variant_new_uint32 (value_count));
			}
			if (all_props || rb_str_in_strv ("ItemCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ItemCount", g_variant_new_uint32 (0));
			}
			if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
				g_variant_builder_add (eb, "{sv}", "ContainerCount", g_variant_new_uint32 (value_count));
			}
			if (all_props || rb_str_in_strv ("Searchable", filter)) {
				g_variant_builder_add (eb, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
			}

			g_variant_builder_add (list, "a{sv}", eb);
			count++;
		}

		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);

		g_strfreev ((char **)filter);
	} else if (g_strcmp0 (method_name, "ListItems") == 0) {
		list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
		g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
		g_variant_builder_unref (list);
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
get_source_properties_property (GDBusConnection *connection,
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
		    g_strcmp0 (property_name, "ContainerCount") == 0) {
			return g_variant_new_uint32 (g_list_length (source_data->properties) + 1);
		} else if (g_strcmp0 (property_name, "ItemCount") == 0) {
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

static const GDBusInterfaceVTable source_properties_vtable =
{
	(GDBusInterfaceMethodCallFunc) source_properties_method_call,
	(GDBusInterfaceGetPropertyFunc) get_source_properties_property,
	NULL
};

/* source container registration */

static void
add_source_container (GVariantBuilder *list, SourceRegistrationData *source_data, const char **filter)
{
	GVariantBuilder *i;
	gboolean all_props;

	i = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	all_props = rb_str_in_strv ("*", filter);


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
	if (source_data->flat) {
		int entry_count;
		entry_count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source_data->base_query_model), NULL);
		if (all_props || rb_str_in_strv ("ChildCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ChildCount", g_variant_new_uint32 (entry_count));
		}
		if (all_props || rb_str_in_strv ("ItemCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ItemCount", g_variant_new_uint32 (entry_count));
		}
		if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ContainerCount", g_variant_new_uint32 (0));
		}
	} else {
		int child_count = g_list_length (source_data->properties) + 1;
		if (all_props || rb_str_in_strv ("ChildCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ChildCount", g_variant_new_uint32 (child_count));
		}
		if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ContainerCount", g_variant_new_uint32 (child_count));
		}
		if (all_props || rb_str_in_strv ("ItemCount", filter)) {
			g_variant_builder_add (i, "{sv}", "ItemCount", g_variant_new_uint32 (0));
		}
	}
	if (all_props || rb_str_in_strv ("Searchable", filter)) {
		g_variant_builder_add (i, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
	}

	g_variant_builder_add (list, "a{sv}", i);
}

static int
count_sources_by_parent (RBDbusMediaServerPlugin *plugin, const char *parent_dbus_path)
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
list_sources_by_parent (RBDbusMediaServerPlugin *plugin,
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
find_registration_data (RBDbusMediaServerPlugin *plugin, RBSource *source)
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
source_parent_updated (SourceRegistrationData *source_data)
{
	GList *l;
	for (l = source_data->plugin->categories; l != NULL; l = l->next) {
		CategoryRegistrationData *category_data = l->data;
		if (g_strcmp0 (source_data->parent_dbus_path, category_data->dbus_path) == 0) {
			category_data->updated = TRUE;
			break;
		}
	}
	if (l == NULL) {
		source_data->plugin->root_updated = TRUE;
	}

	emit_updated_in_idle (source_data->plugin);
}

static void
source_updated (SourceRegistrationData *source_data)
{
	source_data->updated = TRUE;
	emit_updated_in_idle (source_data->plugin);
}

/* signal handlers for source container updates */

static void
row_inserted_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, SourceRegistrationData *source_data)
{
	source_updated (source_data);
}

static void
row_deleted_cb (GtkTreeModel *model, GtkTreePath *path, SourceRegistrationData *source_data)
{
	source_updated (source_data);
}

static void
entry_prop_changed_cb (RhythmDBQueryModel *model,
		       RhythmDBEntry *entry,
		       RhythmDBPropType prop,
		       const GValue *old,
		       const GValue *new_value,
		       SourceRegistrationData *source_data)
{
	GList *l;

	if (entry_property_maps (prop) == FALSE) {
		return;
	}

	source_updated (source_data);
	for (l = source_data->properties; l != NULL; l = l->next) {
		SourcePropertyRegistrationData *prop_data = l->data;
		RBRefString *value;

		/* property model signal handlers will take care of this */
		if (prop == prop_data->property)
			continue;

		prop_data->updated = TRUE;
		value = rhythmdb_entry_get_refstring (entry, prop_data->property);
		if (g_list_find (prop_data->updated_values, value) == NULL) {
			prop_data->updated_values =
				g_list_prepend (prop_data->updated_values, value);
		}
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
	GList *l;

	if (source_data->base_query_model != NULL) {
		disconnect_query_model_signals (source_data);
		g_object_unref (source_data->base_query_model);
	}

	g_object_get (source, "base-query-model", &source_data->base_query_model, NULL);
	connect_query_model_signals (source_data);

	for (l = source_data->properties; l != NULL; l = l->next) {
		SourcePropertyRegistrationData *prop_data = l->data;
		g_object_set (prop_data->model, "query-model", source_data->base_query_model, NULL);
	}

	source_updated (source_data);
}

static void
name_updated_cb (RBSource *source, GParamSpec *pspec, SourceRegistrationData *source_data)
{
	source_updated (source_data);
}

static void
source_deleted_cb (RBDisplayPage *page, RBDbusMediaServerPlugin *plugin)
{
	SourceRegistrationData *source_data;

	source_data = find_registration_data (plugin, RB_SOURCE (page));
	if (source_data != NULL) {
		rb_debug ("source for container %s deleted", source_data->dbus_path);
		unregister_source_container (plugin, source_data, FALSE);
	}
}


static SourceRegistrationData *
register_source_container (RBDbusMediaServerPlugin *plugin,
			   RBSource *source,
			   const char *dbus_path,
			   const char *parent_dbus_path,
			   gboolean flat)
{
	SourceRegistrationData *source_data;
	GDBusInterfaceInfo *container_iface;

	source_data = g_new0 (SourceRegistrationData, 1);
	source_data->source = g_object_ref (source);
	source_data->dbus_path = g_strdup (dbus_path);
	source_data->parent_dbus_path = g_strdup (parent_dbus_path);
	source_data->plugin = plugin;
	source_data->flat = flat;

	container_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
	if (flat) {
		register_object (plugin, &source_tracks_vtable, container_iface, dbus_path, source_data, source_data->dbus_reg_id);
	} else {
		char *tracks_path;

		register_object (plugin, &source_properties_vtable, container_iface, dbus_path, source_data, source_data->dbus_reg_id);

		tracks_path = g_strdup_printf ("%s/all", dbus_path);
		register_object (plugin, &source_tracks_vtable, container_iface, tracks_path, source_data, source_data->all_tracks_reg_id);
	}

	g_object_get (source, "base-query-model", &source_data->base_query_model, NULL);
	connect_query_model_signals (source_data);
	g_signal_connect (source, "notify::base-query-model", G_CALLBACK (base_query_model_updated_cb), source_data);
	g_signal_connect (source, "notify::name", G_CALLBACK (name_updated_cb), source_data);
	g_signal_connect (source, "deleted", G_CALLBACK (source_deleted_cb), plugin);

	/* add to registration list */
	plugin->sources = g_list_append (plugin->sources, source_data);

	/* emit 'updated' signal on parent container */
	g_dbus_connection_emit_signal (plugin->connection, NULL, parent_dbus_path, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "Updated", NULL, NULL);

	return source_data;
}

static void
unregister_source_container (RBDbusMediaServerPlugin *plugin, SourceRegistrationData *source_data, gboolean deactivating)
{
	/* if object registration ids exist, unregister the object */
	unregister_object (plugin, source_data->dbus_reg_id);

	/* remove signal handlers */
	disconnect_query_model_signals (source_data);
	g_signal_handlers_disconnect_by_func (source_data->source, G_CALLBACK (base_query_model_updated_cb), source_data);
	g_signal_handlers_disconnect_by_func (source_data->source, G_CALLBACK (name_updated_cb), source_data);

	if (deactivating == FALSE) {
		/* remove from registration list */
		plugin->sources = g_list_remove (plugin->sources, source_data);

		/* emit 'updated' signal on parent container */
		source_parent_updated (source_data);
		destroy_registration_data (source_data);
	}
}

/* source category containers */

static void
add_category_container (GVariantBuilder *list, CategoryRegistrationData *data, const char **filter)
{
	GVariantBuilder *i;
	int source_count;
	gboolean all_props;

	i = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	all_props = rb_str_in_strv ("*", filter);

	source_count = count_sources_by_parent (data->plugin, data->dbus_path);

	if (all_props || rb_str_in_strv ("Parent", filter)) {
		g_variant_builder_add (i, "{sv}", "Parent", g_variant_new_object_path (data->parent_dbus_path));
	}
	if (all_props || rb_str_in_strv ("Type", filter)) {
		g_variant_builder_add (i, "{sv}", "Type", g_variant_new_string ("container"));
	}
	if (all_props || rb_str_in_strv ("Path", filter)) {
		g_variant_builder_add (i, "{sv}", "Path", g_variant_new_string (data->dbus_path));
	}
	if (all_props || rb_str_in_strv ("DisplayName", filter)) {
		g_variant_builder_add (i, "{sv}", "DisplayName", g_variant_new_string (data->name));
	}
	if (all_props || rb_str_in_strv ("ChildCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ChildCount", g_variant_new_uint32 (source_count));
	}
	if (all_props || rb_str_in_strv ("ItemCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ItemCount", g_variant_new_uint32 (0));
	}
	if (all_props || rb_str_in_strv ("ContainerCount", filter)) {
		g_variant_builder_add (i, "{sv}", "ContainerCount", g_variant_new_uint32 (source_count));
	}
	if (all_props || rb_str_in_strv ("Searchable", filter)) {
		g_variant_builder_add (i, "{sv}", "Searchable", g_variant_new_boolean (FALSE));
	}

	g_variant_builder_add (list, "a{sv}", i);
}

static void
list_categories_by_parent (RBDbusMediaServerPlugin *plugin,
			   GVariantBuilder *list,
			   const char *parent_dbus_path,
			   guint *list_offset,
			   guint *list_count,
			   guint list_max,
			   const char **filter)
{
	GList *l;
	for (l = plugin->categories; l != NULL; l = l->next) {
		CategoryRegistrationData *category_data;
		if (list_max > 0 && (*list_count) == list_max) {
			break;
		}

		category_data = l->data;
		if (g_strcmp0 (category_data->parent_dbus_path, parent_dbus_path) != 0) {
			continue;
		}

		if ((*list_offset) > 0) {
			(*list_offset)--;
			continue;
		}

		add_category_container (list, category_data, filter);
		(*list_count)++;
	}
}

static int
count_categories_by_parent (RBDbusMediaServerPlugin *plugin, const char *parent_dbus_path)
{
	GList *l;
	int count = 0;
	for (l = plugin->categories; l != NULL; l = l->next) {
		CategoryRegistrationData *category_data;
		category_data = l->data;
		if (g_strcmp0 (category_data->parent_dbus_path, parent_dbus_path) == 0) {
			count++;
		}
	}
	return count;
}


static void
category_container_method_call (GDBusConnection *connection,
				const char *sender,
				const char *object_path,
				const char *interface_name,
				const char *method_name,
				GVariant *parameters,
				GDBusMethodInvocation *invocation,
				CategoryRegistrationData *data)
{
	if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {
		guint list_offset;
		guint list_max;
		guint list_count = 0;
		const char **filter;
		GVariantBuilder *list;

		if (g_strcmp0 (method_name, "ListChildren") == 0 ||
		    g_strcmp0 (method_name, "ListContainers") == 0) {
			g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
			rb_debug ("listing containers (%s) - offset %d, max %d", method_name, list_offset, list_max);

			list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
			list_sources_by_parent (data->plugin, list, object_path, &list_offset, &list_count, list_max, filter);
			rb_debug ("returned %d containers", list_count);

			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_variant_builder_unref (list);
			g_strfreev ((char **)filter);
		} else if (g_strcmp0 (method_name, "ListItems") == 0) {
			rb_debug ("listing items");
			g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
			list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_variant_builder_unref (list);
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
get_category_container_property (GDBusConnection *connection,
				 const char *sender,
				 const char *object_path,
				 const char *interface_name,
				 const char *property_name,
				 GError **error,
				 CategoryRegistrationData *data)
{
	int count;
	if (g_strcmp0 (interface_name, MEDIA_SERVER2_OBJECT_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "Parent") == 0) {
			return g_variant_new_object_path (data->parent_dbus_path);
		} else if (g_strcmp0 (property_name, "Type") == 0) {
			return g_variant_new_string ("container");
		} else if (g_strcmp0 (property_name, "Path") == 0) {
			return g_variant_new_string (object_path);
		} else if (g_strcmp0 (property_name, "DisplayName") == 0) {
			return g_variant_new_string (data->name);
		}
	} else if (g_strcmp0 (interface_name, MEDIA_SERVER2_CONTAINER_IFACE_NAME) == 0) {
		if (g_strcmp0 (property_name, "ChildCount") == 0 ||
		    g_strcmp0 (property_name, "ContainerCount") == 0) {
			count = count_sources_by_parent (data->plugin, object_path);
			rb_debug ("child/container count %d", count);
			return g_variant_new_uint32 (count);
		} else if (g_strcmp0 (property_name, "ItemCount") == 0) {
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

static void
add_category_container_property (RBDbusMediaServerPlugin *plugin, GVariantBuilder *properties, const char *iface, const char *property, CategoryRegistrationData *category_data)
{
	GVariant *v;
	v = get_category_container_property (plugin->connection, NULL, category_data->dbus_path, iface, property, NULL, category_data);
	g_variant_builder_add (properties, "{sv}", property, v);
}

static void
emit_category_container_property_updates (RBDbusMediaServerPlugin *plugin, CategoryRegistrationData *category_data)
{
	GError *error = NULL;
	const char *invalidated[] = { NULL };
	GVariantBuilder *properties;
	GVariant *parameters;

	rb_debug ("updating properties for category %s", category_data->dbus_path);
	properties = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	add_category_container_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ItemCount", category_data);
	add_category_container_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ChildCount", category_data);
	add_category_container_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ContainerCount", category_data);

	parameters = g_variant_new ("(sa{sv}^as)",
				    MEDIA_SERVER2_CONTAINER_IFACE_NAME,
				    properties,
				    invalidated);
	g_variant_builder_unref (properties);
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       category_data->dbus_path,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       parameters,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to send property changes for MediaServer2 container %s: %s",
			   category_data->dbus_path,
			   error->message);
		g_clear_error (&error);
	}
}


static const GDBusInterfaceVTable category_container_vtable =
{
	(GDBusInterfaceMethodCallFunc) category_container_method_call,
	(GDBusInterfaceGetPropertyFunc) get_category_container_property,
	NULL
};

static void
register_category_container (RBDbusMediaServerPlugin *plugin,
			     const char *name,
			     const char *dbus_path,
			     const char *parent_dbus_path,
			     gboolean (*match_source) (RBSource *source))
{
	CategoryRegistrationData *category_data;
	GDBusInterfaceInfo *container_iface;

	category_data = g_new0 (CategoryRegistrationData, 1);
	category_data->name = g_strdup (name);
	category_data->dbus_path = g_strdup (dbus_path);
	category_data->parent_dbus_path = g_strdup (parent_dbus_path);
	category_data->plugin = plugin;
	category_data->match_source = match_source;

	container_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);
	register_object (plugin, &category_container_vtable, container_iface, dbus_path, category_data, category_data->dbus_reg_id);

	/* add to registration list */
	plugin->categories = g_list_append (plugin->categories, category_data);

	/* emit 'updated' signal on parent container */
	g_dbus_connection_emit_signal (plugin->connection, NULL, parent_dbus_path, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "Updated", NULL, NULL);
}

static void
destroy_category_data (CategoryRegistrationData *category_data)
{
	g_free (category_data->name);
	g_free (category_data->dbus_path);
	g_free (category_data->parent_dbus_path);
	g_free (category_data);
}

static void
unregister_category_container (RBDbusMediaServerPlugin *plugin, CategoryRegistrationData *category_data, gboolean deactivating)
{
	/* if object registration ids exist, unregister the object */
	unregister_object (plugin, category_data->dbus_reg_id);

	if (deactivating == FALSE) {
		/* remove from registration list */
		plugin->categories = g_list_remove (plugin->categories, category_data);

		/* emit 'updated' signal on parent container */
		g_dbus_connection_emit_signal (plugin->connection, NULL, category_data->parent_dbus_path, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "Updated", NULL, NULL);

		destroy_category_data (category_data);
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
		  RBDbusMediaServerPlugin *plugin)
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
			list_sources_by_parent (plugin, list, object_path, &list_offset, &list_count, list_max, filter);
			list_categories_by_parent (plugin, list, object_path, &list_offset, &list_count, list_max, filter);

			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_variant_builder_unref (list);
			g_strfreev ((char **)filter);
		} else if (g_strcmp0 (method_name, "ListItems") == 0) {
			rb_debug ("listing items");
			g_variant_get (parameters, "(uu^as)", &list_offset, &list_max, &filter);
			list = g_variant_builder_new (G_VARIANT_TYPE ("aa{sv}"));
			g_dbus_method_invocation_return_value (invocation, g_variant_new ("(aa{sv})", list));
			g_variant_builder_unref (list);
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
		   RBDbusMediaServerPlugin *plugin)
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
			char *share_name = g_settings_get_string (plugin->settings, "share-name");
			if (share_name == NULL || share_name[0] == '\0') {
				g_free (share_name);
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
			count += count_categories_by_parent (plugin, object_path);
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

static void
add_root_property (RBDbusMediaServerPlugin *plugin, GVariantBuilder *properties, const char *iface, const char *property)
{
	GVariant *v;
	v = get_root_property (plugin->connection, NULL, RB_MEDIASERVER2_ROOT, iface, property, NULL, plugin);
	g_variant_builder_add (properties, "{sv}", property, v);
}

static void
emit_root_property_updates (RBDbusMediaServerPlugin *plugin)
{
	GError *error = NULL;
	const char *invalidated[] = { NULL };
	GVariantBuilder *properties;
	GVariant *parameters;


	properties = g_variant_builder_new (G_VARIANT_TYPE ("a{sv}"));
	add_root_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ItemCount");
	add_root_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ChildCount");
	add_root_property (plugin, properties, MEDIA_SERVER2_CONTAINER_IFACE_NAME, "ContainerCount");

	parameters = g_variant_new ("(sa{sv}^as)",
				    MEDIA_SERVER2_CONTAINER_IFACE_NAME,
				    properties,
				    invalidated);
	g_variant_builder_unref (properties);
	g_dbus_connection_emit_signal (plugin->connection,
				       NULL,
				       RB_MEDIASERVER2_ROOT,
				       "org.freedesktop.DBus.Properties",
				       "PropertiesChanged",
				       parameters,
				       &error);
	if (error != NULL) {
		g_warning ("Unable to send property changes for MediaServer2 root container: %s",
			   error->message);
		g_clear_error (&error);
	}
}

static const GDBusInterfaceVTable root_vtable =
{
	(GDBusInterfaceMethodCallFunc) root_method_call,
	(GDBusInterfaceGetPropertyFunc) get_root_property,
	NULL
};

/* source watching */

static gboolean
is_shareable_playlist (RBSource *source)
{
	gboolean is_local;

	if (RB_IS_PLAYLIST_SOURCE (source) == FALSE) {
		return FALSE;
	}

	g_object_get (source, "is-local", &is_local, NULL);
	return is_local;
}

/*
 * exposing a device source over dbus only makes sense once it's fully populated.
 * we don't currently have a way to determine that, so we don't share devices yet.
 */
/*
static gboolean
is_shareable_device (RBSource *source)
{
	return RB_IS_DEVICE_SOURCE (source);
}
*/

static void
display_page_inserted_cb (RBDisplayPageModel *model, RBDisplayPage *page, GtkTreeIter *iter, RBDbusMediaServerPlugin *plugin)
{
	GList *l;

	if (RB_IS_SOURCE (page)) {
		RBSource *source = RB_SOURCE (page);
		/* figure out if this is a source we can publish */
		for (l = plugin->categories; l != NULL; l = l->next) {
			CategoryRegistrationData *category_data = l->data;

			if (category_data->match_source (source)) {
				char *dbus_path;
				dbus_path = g_strdup_printf ("%s/%" G_GINTPTR_MODIFIER "u",
							     category_data->dbus_path,
							     (gintptr) source);
				rb_debug ("adding new source %s to category %s", dbus_path, category_data->name);
				register_source_container (plugin, source, dbus_path, category_data->dbus_path, TRUE);
				g_free (dbus_path);
			}
		}
	}

}

static gboolean
display_page_foreach_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBDbusMediaServerPlugin *plugin)
{
	RBDisplayPage *page;

	gtk_tree_model_get (model, iter,
			    RB_DISPLAY_PAGE_MODEL_COLUMN_PAGE, &page,
			    -1);
	display_page_inserted_cb (RB_DISPLAY_PAGE_MODEL (model), page, iter, plugin);

	g_object_unref (page);
	return FALSE;
}

/* plugin */

static void
name_acquired_cb (GDBusConnection *connection, const char *name, RBDbusMediaServerPlugin *plugin)
{
	rb_debug ("acquired dbus name %s", name);
}

static void
name_lost_cb (GDBusConnection *connection, const char *name, RBDbusMediaServerPlugin *plugin)
{
	rb_debug ("lost dbus name %s", name);
}

static void
impl_activate (PeasActivatable *bplugin)
{
	RBDbusMediaServerPlugin *plugin;
	GDBusInterfaceInfo *container_iface;
	SourceRegistrationData *source_data;
	RBSource *source;
	GError *error = NULL;
	RBShell *shell;

	rb_debug ("activating DBus MediaServer2 plugin");

	plugin = RB_DBUS_MEDIA_SERVER_PLUGIN (bplugin);
	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "db", &plugin->db,
		      "display-page-model", &plugin->display_page_model,
		      NULL);

	plugin->settings = g_settings_new ("org.gnome.rhythmbox.sharing");

	plugin->node_info = g_dbus_node_info_new_for_xml (media_server2_spec, &error);
	if (error != NULL) {
		g_warning ("Unable to parse MediaServer2 spec: %s", error->message);
		g_clear_error (&error);
		g_object_unref (shell);
		return;
	}

	plugin->connection = g_bus_get_sync (G_BUS_TYPE_SESSION, NULL, &error);
	if (error != NULL) {
		g_warning ("Unable to connect to D-Bus: %s", error->message);
		g_clear_error (&error);
		g_object_unref (shell);
		return;
	}

	container_iface = g_dbus_node_info_lookup_interface (plugin->node_info, MEDIA_SERVER2_CONTAINER_IFACE_NAME);

	/* register root */
	register_object (plugin, &root_vtable, container_iface, RB_MEDIASERVER2_ROOT, G_OBJECT (plugin), plugin->root_reg_id);

	/* register fixed sources (library, podcasts, etc.) */
	g_object_get (shell, "library-source", &source, NULL);
	source_data = register_source_container (plugin, source, RB_MEDIASERVER2_LIBRARY, RB_MEDIASERVER2_ROOT, FALSE);
	register_property_container (plugin->connection, source_data, RHYTHMDB_PROP_ARTIST, _("Artists"));
	register_property_container (plugin->connection, source_data, RHYTHMDB_PROP_ALBUM, _("Albums"));
	register_property_container (plugin->connection, source_data, RHYTHMDB_PROP_GENRE, _("Genres"));
	/* year won't work yet */
	g_object_unref (source);

	/* watch for user-creatable sources (playlists, devices) */
	g_signal_connect_object (plugin->display_page_model, "page-inserted", G_CALLBACK (display_page_inserted_cb), plugin, 0);
	gtk_tree_model_foreach (GTK_TREE_MODEL (plugin->display_page_model),
				(GtkTreeModelForeachFunc) display_page_foreach_cb,
				plugin);
	register_category_container (plugin, _("Playlists"), RB_MEDIASERVER2_PLAYLISTS, RB_MEDIASERVER2_ROOT, is_shareable_playlist);
	/* see comments above */
	/* register_category_container (plugin, _("Devices"), RB_MEDIASERVER2_DEVICES, RB_MEDIASERVER2_ROOT, is_shareable_device); */

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
		g_object_unref (shell);
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
	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBDbusMediaServerPlugin *plugin;
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

	for (l = plugin->categories; l != NULL; l = l->next) {
		unregister_category_container (plugin, l->data, TRUE);
	}
	rb_list_destroy_free (plugin->categories, (GDestroyNotify) destroy_category_data);
	plugin->categories = NULL;

	if (plugin->entry_reg_id != 0) {
		g_dbus_connection_unregister_subtree (plugin->connection, plugin->entry_reg_id);
		plugin->entry_reg_id = 0;
	}

	if (plugin->settings != NULL) {
		g_object_unref (plugin->settings);
		plugin->settings = NULL;
	}

	if (plugin->display_page_model != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->display_page_model,
						      display_page_inserted_cb,
						      plugin);
		g_object_unref (plugin->display_page_model);
		plugin->display_page_model = NULL;
	}

	if (plugin->db != NULL) {
		g_object_unref (plugin->db);
		plugin->db = NULL;
	}

	if (plugin->name_own_id > 0) {
		g_bus_unown_name (plugin->name_own_id);
		plugin->name_own_id = 0;
	}

	if (plugin->node_info != NULL) {
		g_dbus_node_info_unref (plugin->node_info);
		plugin->node_info = NULL;
	}

	if (plugin->connection != NULL) {
		g_object_unref (plugin->connection);
		plugin->connection = NULL;
	}
}


G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_dbus_media_server_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_DBUS_MEDIA_SERVER_PLUGIN);
}
