/* 
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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

#include "rb-iradio-backend.h"
#include "rb-iradio-yp-iterator.h"
#include "rb-iradio-yp-xmlfile.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-stock-icons.h"
#include "rb-node-station.h"
#include "rb-glist-wrapper.h"
#include "rb-library.h"

static void rb_iradio_backend_class_init (RBIRadioBackendClass *klass);
static void rb_iradio_backend_init (RBIRadioBackend *view);
static void rb_iradio_backend_finalize (GObject *object);

static void rb_iradio_backend_save (RBIRadioBackend *backend);
static void genre_added_cb (RBNode *node, RBNode *child, RBIRadioBackend *backend);
static void genre_removed_cb (RBNode *node, RBNode *child, RBIRadioBackend *backend);
static RBNode * rb_iradio_backend_lookup_station_by_location (RBIRadioBackend *backend,
							      const char *uri);

#define RB_IRADIO_BACKEND_XML_VERSION "1.0"

struct RBIRadioBackendPrivate
{
	RBNode *all_genres;
	RBNode *all_stations;
	
	GHashTable *genre_hash;
	GStaticRWLock *genre_hash_lock;

	char *xml_file;
};

static GObjectClass *parent_class = NULL;

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
	
	object_class->finalize = rb_iradio_backend_finalize;

}

static void
rb_iradio_backend_init (RBIRadioBackend *backend)
{
	GValue value = { 0, };

	/* ensure these types have been registered: */
	rb_node_get_type ();
	rb_glist_wrapper_get_type ();
	rb_node_station_get_type ();

	backend->priv = g_new0(RBIRadioBackendPrivate, 1);
	backend->priv->xml_file = g_build_filename (rb_dot_dir (),
						    "iradio.xml",
						    NULL);

	backend->priv->genre_hash = g_hash_table_new (g_str_hash,
						      g_str_equal);

	backend->priv->genre_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (backend->priv->genre_hash_lock);

	backend->priv->all_genres  = rb_node_new ();
	backend->priv->all_stations = rb_node_new ();
	rb_node_ref (backend->priv->all_genres);
	rb_node_ref (backend->priv->all_stations);

	g_signal_connect_object (G_OBJECT (backend->priv->all_genres),
				 "child_added",
				 G_CALLBACK (genre_added_cb),
				 G_OBJECT (backend),
				 0);
	g_signal_connect_object (G_OBJECT (backend->priv->all_genres),
				 "child_removed",
				 G_CALLBACK (genre_removed_cb),
				 G_OBJECT (backend),
				 0);

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
			      RB_ALL_NODE_PROP_PRIORITY,
			      &value);
	rb_node_set_property (backend->priv->all_stations,
			      RB_ALL_NODE_PROP_PRIORITY,
			      &value);
	g_value_unset (&value);

	rb_node_add_child (backend->priv->all_genres,
			   backend->priv->all_stations);
	fprintf(stderr, "backend: created\n");
}

static void
rb_iradio_backend_finalize (GObject *object)
{
	int i;
	RBIRadioBackend *backend;
	GPtrArray *children;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_IRADIO_BACKEND (object));

	backend = RB_IRADIO_BACKEND (object);

	g_return_if_fail (backend->priv != NULL);

	fprintf (stderr, "backend: finalizing\n");

	fprintf (stderr, "FIXME: iradio saving disabled because it's broken right now\n");
/*  	rb_iradio_backend_save (backend); */

/* 	children = rb_node_get_children (backend->priv->all_stations); */
/* 	rb_node_thaw (backend->priv->all_stations); */
/* 	for (i = children->len - 1; i >= 0; i--) { */
/* 		rb_node_unref (g_ptr_array_index (children, i)); */
/* 	} */

	g_hash_table_destroy (backend->priv->genre_hash);
	g_static_rw_lock_free (backend->priv->genre_hash_lock);
	
	rb_node_unref (backend->priv->all_stations);
	rb_node_unref (backend->priv->all_genres);
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

	fprintf(stderr, "inserting genre %s\n", rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	g_hash_table_insert (backend->priv->genre_hash,
			     (char *) rb_node_get_property_string (child, RB_NODE_PROP_NAME),
			     child);
	
	g_static_rw_lock_writer_unlock (backend->priv->genre_hash_lock);
}

static void
genre_removed_cb (RBNode *node,
		  RBNode *child,
		  RBIRadioBackend *backend)
{
	g_static_rw_lock_writer_lock (backend->priv->genre_hash_lock);
	
	fprintf(stderr, "removing genre %s\n", rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	g_hash_table_remove (backend->priv->genre_hash,
			     rb_node_get_property_string (child, RB_NODE_PROP_NAME));
	
	g_static_rw_lock_writer_unlock (backend->priv->genre_hash_lock);
}
static void
load_initial (RBIRadioBackend *backend)
{
	const char *initial_file = rb_file ("iradio-initial.xml");
	RBIRadioYPIterator *it = RB_IRADIO_YP_ITERATOR (g_object_new (RB_TYPE_IRADIO_YP_XMLFILE,
								      "filename", initial_file,
								      NULL));
	RBIRadioStation *station;

	fprintf(stderr, "loading %s\n", initial_file);
	while ((station = rb_iradio_yp_iterator_get_next_station (it)) != NULL) {
		const char *genre;
		const char *name;
		GList *locations;
		RBNodeStation *nodestation;

		g_assert (RB_IS_IRADIO_STATION (station));

		g_object_get(G_OBJECT(station), "genre", &genre, NULL);
		g_object_get(G_OBJECT(station), "name", &name, NULL);
		g_object_get(G_OBJECT(station), "locations", &locations, NULL);

		fprintf(stderr, "adding station: %s %s\n", genre, name);
		
		nodestation = rb_node_station_new (locations, name, genre, "initial", backend);
	}
	g_free ((char *) initial_file);
	g_object_unref (G_OBJECT (it));
}
	

void rb_iradio_backend_load (RBIRadioBackend *backend)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	char *tmp;

	if (g_file_test (backend->priv->xml_file, G_FILE_TEST_EXISTS) == FALSE)
		goto loadinitial;
	
	doc = xmlParseFile (backend->priv->xml_file);

	if (doc == NULL)
	{
		rb_error_dialog (_("Failed to parse %s\n"), backend->priv->xml_file);
		unlink (backend->priv->xml_file);
		goto loadinitial;
	}

	root = xmlDocGetRootElement (doc);

	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_IRADIO_BACKEND_XML_VERSION) != 0)
	{
		fprintf (stderr, "Invalid version in %s\n", backend->priv->xml_file);
		g_free (tmp);
		unlink (backend->priv->xml_file);
		xmlFreeDoc (doc);
		goto loadinitial;
	}
	g_free (tmp);

	for (child = root->children; child != NULL; child = child->next)
	{
		/* This automagically sets up the tree structure */
		rb_node_new_from_xml (child);

		if (RB_IS_NODE_STATION (child))
		{
/* 			rb_node_station_sync (child, backend); */
		}
	}

	xmlFreeDoc (doc);
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

	fprintf(stderr, "writing %s\n", backend->priv->xml_file);

	/* save nodes to xml */
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "rhythmbox_iradio", NULL);
	xmlSetProp (root, "version", RB_IRADIO_BACKEND_XML_VERSION);
	xmlDocSetRootElement (doc, root);

	children = rb_node_get_children (backend->priv->all_genres);
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		if (kid != backend->priv->all_stations)
		{
			fprintf(stderr, "Saving child %p (id %d) (name: %s) of genre root %p\n", kid, i,
				rb_node_get_property_string (kid, RB_NODE_PROP_NAME), backend->priv->all_genres);
			rb_node_save_to_xml (kid, root);
		}
	}
	rb_node_thaw (backend->priv->all_genres);

	children = rb_node_get_children (backend->priv->all_stations);
	for (i = 0; i < children->len; i++)
	{
		RBNode *kid;

		kid = g_ptr_array_index (children, i);
		
		fprintf(stderr, "Saving child %p (id %d) (name: %s) of stations root %p\n", kid, i,
			rb_node_get_property_string (kid, RB_NODE_PROP_NAME), backend->priv->all_stations);
		rb_node_save_to_xml (kid, root);
	}
	rb_node_thaw (backend->priv->all_stations);

	xmlSaveFormatFile (backend->priv->xml_file, doc, 1);
	fprintf(stderr, "writing %s: done\n", backend->priv->xml_file);
}

void
rb_iradio_backend_remove_node (RBIRadioBackend *backend,
			       RBNode *node)
{
	rb_node_unref (RB_NODE (node));
}

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
		RBGListWrapper *listwrapper = RB_GLIST_WRAPPER (rb_node_get_property_object (kid, RB_NODE_PROP_ALT_LOCATIONS));
		GList *cur, *locations = rb_glist_wrapper_get_list (listwrapper);
		for (cur = locations; cur != NULL; cur = cur->next)
		{
			if (!strcmp (uri, (char *) cur->data))
			{
				retval = kid;
				break;
			}
		}
	}
	rb_node_thaw (backend->priv->all_stations);
	return retval;
}

void rb_iradio_backend_add_station_from_uri (RBIRadioBackend *backend,
					     const char *uri)
{
	RBNode *station = rb_iradio_backend_lookup_station_by_location (backend, uri);
	if (station == NULL)
	{
		GList *locations = g_list_append (NULL, g_strdup (uri));
		station = rb_iradio_backend_add_station_full (backend, locations,
							      _("(Unknown)"), _("(Unknown)"));
	}
	else
	{
		/* FIXME: queue station to be played here? or somewhere else?
		 * like return a handle and have the caller queue it? */
	}
}


RBNode *
rb_iradio_backend_add_station_full (RBIRadioBackend *backend,
					 GList *locations,
					 const char *name,
					 const char *genre)
{
	RBNode *ret = RB_NODE (rb_node_station_new (locations,
						    name ? name : _("(Unknown)"),
						    genre ? genre : _("(Unknown)"),
						    "user", backend));
	/* FIXME: queue station to be played here? or somewhere else?
	 * like return a handle and have the caller queue it? */
	return ret;
}
