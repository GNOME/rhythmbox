/*
 *  Copyright (C) 2002,2003 Colin Walters <walters@gnu.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 *  $Id$
 */

#include <config.h>
#include <monkey-media.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libxml/tree.h>
#include <gtk/gtkmain.h>
#include <unistd.h>
#include <string.h>
#include <glib.h>
#include <stdio.h>

#include "rb-node-db.h"
#include "rb-node-common.h"
#include "rb-iradio-backend.h"
#include "rb-playlist.h"
#include "rb-debug.h"
#include "rb-string-helpers.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-stock-icons.h"
#include "rb-library.h"

static void rb_iradio_backend_class_init (RBIRadioBackendClass *klass);
static void rb_iradio_backend_init (RBIRadioBackend *view);
static void rb_iradio_backend_finalize (GObject *object);

static void rb_iradio_backend_save (RBIRadioBackend *backend);
static void genre_added_cb (RBNode *node, RBNode *child, RBIRadioBackend *backend);
static void genre_removed_cb (RBNode *node, RBNode *child, guint last_id, RBIRadioBackend *backend);
static void station_changed_cb (RBNode *node, RBNode *parent, guint propid, RBIRadioBackend *backend);

static RBNode * rb_iradio_backend_lookup_station_by_location (RBIRadioBackend *backend,
							      const char *uri);
static void finalize_node (RBNode *node);
static void restore_node (RBNode *node);
static void sync_sort_keys (RBNode *node);
static void set_genre (RBNode *node, const char *genrename, RBIRadioBackend *backend, gboolean unlocked);

#define RB_IRADIO_BACKEND_XML_VERSION "2.2"

struct RBIRadioBackendPrivate
{
	RBNode *all_genres;
	RBNode *all_stations;

	RBNodeDb *db;
	
	GHashTable *genre_hash;
	GStaticRWLock *genre_hash_lock;

	char *xml_file;

	guint idle_save_id;
};

enum
{
	PROP_0,
};

enum
{
	CHANGED,
	LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;

static guint rb_iradio_backend_signals[LAST_SIGNAL] = { 0 };

GType
rb_iradio_backend_get_type (void)
{
	static GType rb_iradio_backend_type = 0;

	if (rb_iradio_backend_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBIRadioBackendClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_iradio_backend_class_init,
			NULL,
			NULL,
			sizeof (RBIRadioBackend),
			0,
			(GInstanceInitFunc) rb_iradio_backend_init
		};

		rb_iradio_backend_type = g_type_register_static (G_TYPE_OBJECT,
								 "RBIRadioBackend",
								 &our_info, 0);
		
	}

	return rb_iradio_backend_type;
}

static void
rb_iradio_backend_class_init (RBIRadioBackendClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	
	parent_class = g_type_class_peek_parent (klass);
	
	rb_iradio_backend_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBIRadioBackendClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	object_class->finalize = rb_iradio_backend_finalize;

}

static void
rb_iradio_backend_init (RBIRadioBackend *backend)
{
	char *libname = g_strdup_printf ("iradio-%s.xml", RB_IRADIO_BACKEND_XML_VERSION);
	GValue value = { 0, };

	backend->priv = g_new0(RBIRadioBackendPrivate, 1);
	backend->priv->xml_file = g_build_filename (rb_dot_dir (),
						    libname,
						    NULL);

	g_free (libname);

	backend->priv->db = rb_node_db_new (RB_NODE_DB_IRADIO);

	backend->priv->genre_hash = g_hash_table_new (g_str_hash,
						      g_str_equal);

	backend->priv->genre_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (backend->priv->genre_hash_lock);

	backend->priv->all_genres  = rb_node_new_with_id (backend->priv->db, IRADIO_GENRES_NODE_ID);
	backend->priv->all_stations  = rb_node_new_with_id (backend->priv->db, IRADIO_STATIONS_NODE_ID);
	rb_node_ref (backend->priv->all_genres);
	rb_node_ref (backend->priv->all_stations);

	rb_node_signal_connect_object (backend->priv->all_genres,
				       RB_NODE_CHILD_ADDED,
				       (RBNodeCallback) genre_added_cb,
				       G_OBJECT (backend));
	rb_node_signal_connect_object (backend->priv->all_genres,
				       RB_NODE_CHILD_REMOVED,
				       (RBNodeCallback) genre_removed_cb,
				       G_OBJECT (backend));

	rb_node_signal_connect_object (backend->priv->all_stations,
				       RB_NODE_CHILD_CHANGED,
				       (RBNodeCallback) station_changed_cb,
				       G_OBJECT (backend));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, _("All"));
	rb_node_set_property (backend->priv->all_genres,
			      RB_NODE_PROP_NAME,
			      &value);
	rb_node_set_property (backend->priv->all_stations,
			      RB_NODE_PROP_NAME,
			      &value);
	rb_node_set_property (backend->priv->all_genres,
			      RB_NODE_PROP_GENRE,
			      &value);
	rb_node_set_property (backend->priv->all_stations,
			      RB_NODE_PROP_GENRE,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_BOOLEAN);
	g_value_set_boolean (&value, TRUE);
	rb_node_set_property (backend->priv->all_genres,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (backend->priv->all_stations,
			      RB_NODE_PROP_PRIORITY,
			      &value);
	g_value_unset (&value);

	rb_node_add_child (backend->priv->all_genres,
			   backend->priv->all_stations);

}

static void
rb_iradio_backend_finalize (GObject *object)
{
	RBIRadioBackend *backend;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_BACKEND (object));

	backend = RB_IRADIO_BACKEND (object);

	g_return_if_fail (backend->priv != NULL);

	g_source_remove (backend->priv->idle_save_id);

	rb_iradio_backend_save (backend);

	rb_node_unref (backend->priv->all_stations);
	rb_node_unref (backend->priv->all_genres);

	g_object_unref (backend->priv->db);

	g_free (backend->priv->xml_file);
	g_free (backend->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBNode *
rb_iradio_backend_get_genre_by_name (RBIRadioBackend *backend,
				     const char *genre)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (backend->priv->genre_hash_lock);
	
	ret = g_hash_table_lookup (backend->priv->genre_hash,
				   genre);
	
	g_static_rw_lock_reader_unlock (backend->priv->genre_hash_lock);

	return ret;
}

RBNode *
rb_iradio_backend_get_all_genres (RBIRadioBackend *backend)
{
	return backend->priv->all_genres;
}

RBNode *
rb_iradio_backend_get_all_stations (RBIRadioBackend *backend)
{
	return backend->priv->all_stations;
}

int
rb_iradio_backend_get_genre_count (RBIRadioBackend *backend)
{
	/* Subtract "All" */
	return g_hash_table_size (backend->priv->genre_hash) - 1;
}

int 
rb_iradio_backend_get_station_count (RBIRadioBackend *backend)
{
	GPtrArray *children = rb_node_get_children (backend->priv->all_stations);
	int ret;
	ret = children->len;
	rb_node_thaw (backend->priv->all_stations);
	return ret;
}

static void
genre_added_cb (RBNode *node,
		RBNode *child,
		RBIRadioBackend *backend)
{
	g_static_rw_lock_writer_lock (backend->priv->genre_hash_lock);

	g_hash_table_insert (backend->priv->genre_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_NAME),
			     child);
	
	g_static_rw_lock_writer_unlock (backend->priv->genre_hash_lock);
}

static void
genre_removed_cb (RBNode *node,
		  RBNode *child,
		  guint last_id,
		  RBIRadioBackend *backend)
{
	g_static_rw_lock_writer_lock (backend->priv->genre_hash_lock);
	
	g_hash_table_remove (backend->priv->genre_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	
	g_static_rw_lock_writer_unlock (backend->priv->genre_hash_lock);
}

static void
station_changed_cb (RBNode *parent,
		    RBNode *node,
		    guint propid,
		    RBIRadioBackend *backend)
{
	rb_debug ("station %p changed propid %d", node, propid);
	if (propid == RB_NODE_PROP_GENRE) {
		set_genre (node, rb_node_get_property_string (node, RB_NODE_PROP_GENRE),
			   backend, TRUE);
	}
}

static void
handle_playlist_entry_cb (RBPlaylist *playlist, const char *uri, const char *title,
			  const char *genre, RBIRadioBackend *backend)
{
	if (rb_uri_is_iradio (uri) != FALSE)
		rb_iradio_backend_add_station_full (backend, uri, title, genre);
	else
		g_warning ("invalid iradio URI in iradio-initial.pls!");
}

static void
load_initial (RBIRadioBackend *backend)
{
	const char *initial_file = rb_file ("iradio-initial.pls");
	RBPlaylist *playlist;

	if (!initial_file) {
		rb_error_dialog (_("Unable to find file \"iradio-initial.xml\""));
		return;
	}
	
	rb_debug ("iradio-backend: loading initial stations");

	playlist = rb_playlist_new ();
	g_signal_connect (G_OBJECT (playlist), "entry",
			  G_CALLBACK (handle_playlist_entry_cb), backend);
	rb_playlist_parse (playlist, initial_file);

	g_object_unref (G_OBJECT (playlist));
	
	g_signal_emit (G_OBJECT (backend), rb_iradio_backend_signals[CHANGED], 0);
}
	
void rb_iradio_backend_load (RBIRadioBackend *backend)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	char *tmp;

	rb_debug ("iradio-backend: loading");

	if (g_file_test (backend->priv->xml_file, G_FILE_TEST_EXISTS) == FALSE)
		goto loadinitial;
	
	doc = xmlParseFile (backend->priv->xml_file);

	if (doc == NULL) {
		rb_error_dialog (_("Failed to parse %s\n"), backend->priv->xml_file);
		unlink (backend->priv->xml_file);
		goto loadinitial;
	}


	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_IRADIO_BACKEND_XML_VERSION) != 0) {
		fprintf (stderr, "Invalid version in %s\n", backend->priv->xml_file);
		g_free (tmp);
		unlink (backend->priv->xml_file);
		xmlFreeDoc (doc);
		goto loadinitial;
	}
	g_free (tmp);

	for (child = root->children; child != NULL; child = child->next) {
		/* This automagically sets up the tree structure */
		RBNode *node = rb_node_new_from_xml (backend->priv->db, child);
		if (node != NULL) {
			restore_node (node);
		}
		rb_debug ("iradio-backend: loaded node %p", node);
	}

	xmlFreeDoc (doc);
	g_signal_emit (G_OBJECT (backend), rb_iradio_backend_signals[CHANGED], 0);
	return;
 loadinitial:
	load_initial (backend);
}

static void
rb_iradio_backend_save (RBIRadioBackend *backend)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GPtrArray *children;
	int i;
	GString *tmpname = g_string_new (backend->priv->xml_file);

	rb_debug ("iradio backend: saving");

	g_string_append (tmpname, ".tmp");

	/* save nodes to xml */
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_iradio", NULL);
	xmlSetProp (root, "version", RB_IRADIO_BACKEND_XML_VERSION);
	xmlDocSetRootElement (doc, root);

	children = rb_node_get_children (backend->priv->all_genres);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != backend->priv->all_stations) {
			rb_node_save_to_xml (kid, root);
		}
	}

	children = rb_node_get_children (backend->priv->all_stations);
	for (i = 0; i < children->len; i++) {
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		rb_node_save_to_xml (kid, root);
	}

	rb_node_thaw (backend->priv->all_genres);
	rb_node_thaw (backend->priv->all_stations);

	xmlSaveFormatFile (tmpname->str, doc, 1);
	rename (tmpname->str, backend->priv->xml_file);

	g_string_free (tmpname, TRUE);
	xmlFreeDoc (doc);

	rb_debug ("iradio backend: done saving");
}

void
rb_iradio_backend_remove_node (RBIRadioBackend *backend,
			       RBNode *node)
{
	rb_node_unref (node);
}

/* Returns a locked RBNode */
static RBNode *
rb_iradio_backend_lookup_station_by_title (RBIRadioBackend *backend,
					  const char *title)
{
	int i;
	RBNode *retval = NULL;
	GPtrArray *children = rb_node_get_children (backend->priv->all_stations);
	for (i = 0; retval == NULL && i < children->len; i++) {
		RBNode *kid = g_ptr_array_index (children, i);
		if (!strcmp (title, rb_node_get_property_string (kid, RB_NODE_PROP_NAME))) {
			retval = kid;
			break;
		}
	}
	if (retval)
		rb_node_freeze (retval);
	rb_node_thaw (backend->priv->all_stations);
	return retval;
}

/* Returns a locked RBNode */
static RBNode *
rb_iradio_backend_lookup_station_by_location (RBIRadioBackend *backend,
					      const char *uri)
{
	int i;
	RBNode *retval = NULL;
	GPtrArray *children = rb_node_get_children (backend->priv->all_stations);
	for (i = 0; retval == NULL && i < children->len; i++)
	{
		RBNode *kid = g_ptr_array_index (children, i);
		const char *loc = rb_node_get_property_string (kid, RB_NODE_PROP_LOCATION);
		if (loc && !strcmp (loc, uri)) {
			retval = kid;
			break;
		}
	}
	if (retval)
		rb_node_freeze (retval);
	rb_node_thaw (backend->priv->all_stations);
	return retval;
}

void
rb_iradio_backend_add_station_from_uri (RBIRadioBackend *backend, const char *uri)
{
	RBNode *station = rb_iradio_backend_lookup_station_by_location (backend, uri);
	if (station == NULL) {
		rb_iradio_backend_add_station_full (backend, uri, _("(Unknown)"), _("(Unknown)"));
	}
	else
		rb_node_thaw (station);
}


void
rb_iradio_backend_add_station_full (RBIRadioBackend *backend,
				    const char *uri,
				    const char *name,
				    const char *genre)
{
	RBNode *node = rb_iradio_backend_lookup_station_by_title (backend, name);

	if (node == NULL) {
		rb_debug ("iradio-backend: adding station; name: %s genre: %s",
			  name, genre);
		node = rb_iradio_backend_new_station (uri,
						      name ? name : _("(Unknown)"),
						      genre ? genre : _("(Unknown)"),
						      "user", backend);
	}
	else {
		rb_debug ("iradio-backend: station %s already exists", name);
		rb_node_thaw (node);
	}
}

GList *
rb_iradio_backend_get_genre_names (RBIRadioBackend *backend)
{
	GList *genrenames = NULL;
	RBNode *genres = rb_iradio_backend_get_all_genres (backend);
	GPtrArray *children = rb_node_get_children (backend->priv->all_genres);
	int i;
	for (i = 0; i < children->len; i++) {
		RBNode *kid;
		const char *name;
		
		kid = g_ptr_array_index (children, i);
		
		name = rb_node_get_property_string (kid, RB_NODE_PROP_NAME);
		if (strcmp (name, _("All")))
			genrenames = g_list_append (genrenames, g_strdup (name));
	}
	rb_node_thaw (genres);
	return genrenames;
}

static void
finalize_node (RBNode *node)
{
	RBNode *parent;

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE);
	if (G_LIKELY (parent != NULL))
		rb_node_unref_with_locked_child (parent, node);
}

static void
restore_node (RBNode *node)
{
	RBNode *parent;

	sync_sort_keys (node);

	if (rb_node_get_property_string (node, RB_NODE_PROP_LOCATION)) {
		rb_node_ref (node);
		rb_node_signal_connect_object (node, RB_NODE_DESTROY,
					       (RBNodeCallback) finalize_node, NULL);
	}

	parent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE);
	if (G_LIKELY (parent != NULL))
		rb_node_ref (parent);
}

static void
set_sort_key_prop (RBNode *node, guint source, guint dest)
{
	GValue val = { 0, };

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string_take_ownership (&val, rb_get_sort_key (rb_node_get_property_string (node, source)));
	rb_node_set_property (node, dest, &val);
	g_value_unset (&val);
}

static void
sync_sort_keys (RBNode *node)
{
		
	set_sort_key_prop (node, RB_NODE_PROP_NAME, RB_NODE_PROP_NAME_SORT_KEY);
}


static void
set_genre (RBNode *node, const char *genrename, RBIRadioBackend *backend, gboolean unlocked)
{
	GValue val = { 0, };
	RBNode *genre;
	RBNode *oldparent = rb_node_get_property_pointer (node, RB_NODE_PROP_REAL_GENRE);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, genrename);

	genre = rb_iradio_backend_get_genre_by_name (backend, g_value_get_string (&val));

	if (oldparent && oldparent != genre) {
		rb_node_remove_child_unlocked (oldparent, node);
		rb_node_unref (oldparent);
	}

	if (genre == NULL) {
		genre = rb_node_new (backend->priv->db);

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME,
				      &val);
		rb_node_set_property (genre,
				      RB_NODE_PROP_GENRE,
				      &val);

		sync_sort_keys (genre);
		rb_node_add_child (rb_iradio_backend_get_all_genres (backend), genre);
		rb_node_ref (rb_iradio_backend_get_all_genres (backend));
	}
	
	g_value_unset (&val);

	if (unlocked)
		rb_node_add_child_unlocked (genre, node);
	else
		rb_node_add_child (genre, node);
	rb_node_ref (genre);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property_unlocked (node,
				       RB_NODE_PROP_REAL_GENRE,
				       &val);
	g_value_unset (&val);
}


RBNode *
rb_iradio_backend_new_station (const char *uri, const char *name,
			       const char *genre, const char *source,
			       RBIRadioBackend *iradio_backend)
{
	RBNode *node;
	GValue value = { 0, };

	g_return_val_if_fail (uri != NULL, NULL);
	g_return_val_if_fail (RB_IS_IRADIO_BACKEND (iradio_backend), NULL);

	node = rb_node_new (iradio_backend->priv->db);
	
	if (G_UNLIKELY (node == NULL))
		return NULL;

	rb_node_signal_connect_object (node, RB_NODE_DESTROY, (RBNodeCallback) finalize_node, NULL);
	rb_node_ref (node);

	/* Name */
	g_value_init (&value , G_TYPE_STRING);
	g_value_set_string (&value, name);
	rb_node_set_property (node,
			      RB_NODE_PROP_NAME,
			      &value);
	g_value_unset (&value);

	sync_sort_keys (node);

	/* Source */
	g_value_init (&value , G_TYPE_STRING);
	g_value_set_string (&value, source);
	rb_node_set_property (node,
			      RB_NODE_PROP_IRADIO_SOURCE,
			      &value);
	g_value_unset (&value);

	g_value_init (&value , G_TYPE_STRING);
	g_value_set_string (&value, uri);
	rb_node_set_property (node,
			      RB_NODE_PROP_LOCATION,
			      &value);
	g_value_unset (&value);

	/* Number of plays */
	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, 0);
	rb_node_set_property (node,
			      RB_NODE_PROP_PLAY_COUNT,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, 0);
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, _("Never"));
	/* Last played time */
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED_STR,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, genre);
	rb_node_set_property (node,
			      RB_NODE_PROP_GENRE,
			      &value);
	g_value_unset (&value);

	set_genre (node, genre, iradio_backend, FALSE);

	rb_node_add_child (rb_iradio_backend_get_all_stations (iradio_backend), node);
	rb_node_ref (rb_iradio_backend_get_all_stations (iradio_backend));

	return node;
}

