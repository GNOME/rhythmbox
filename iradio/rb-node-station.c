/*
 *  Copyright (C) 2002 Colin Walters <walters@debian.org>
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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
#include <sys/time.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>
#include <string.h>

#include "rb-node-station.h"
#include "rb-glist-wrapper.h"
#include "rb-string-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"

static void rb_node_station_class_init (RBNodeStationClass *klass);
static void rb_node_station_init (RBNodeStation *node);
static void rb_node_station_finalize (GObject *object);
static void rb_node_station_restored (RBNode *node);

/* static void rb_node_station_sync (RBNodeStation *node, */
/* 				  RBIRadioBackend *backend); */

static GObjectClass *parent_class = NULL;

GType
rb_node_station_get_type (void)
{
	static GType rb_node_station_type = 0;

	if (rb_node_station_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (RBNodeStationClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_station_class_init,
			NULL,
			NULL,
			sizeof (RBNodeStation),
			0,
			(GInstanceInitFunc) rb_node_station_init
		};
		
		rb_node_station_type = g_type_register_static (RB_TYPE_NODE,
							       "RBNodeStation",
							       &our_info, 0);
	}

	return rb_node_station_type;
}

static void
rb_node_station_class_init (RBNodeStationClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBNodeClass *node_class = RB_NODE_CLASS (klass);

	node_class->restored = rb_node_station_restored;

	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_node_station_finalize;
}

static void
rb_node_station_init (RBNodeStation *node)
{
}

static void
rb_node_station_finalize (GObject *object)
{
	RBNodeStation *node;
	RBNode *parent;

	node = RB_NODE_STATION (object);
	
	parent = rb_node_station_get_genre (node);
	if (parent != NULL)
		rb_node_unref (parent);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
set_title_sort_key (RBNode *node)
{
	char *folded, *key;
	GValue titleval = {0,};
	GValue keyvalue = {0,};

	g_assert (rb_node_get_property (node,
					RB_NODE_PROP_NAME,
					&titleval));

	folded = g_utf8_casefold (g_value_get_string (&titleval), -1);
	key = g_utf8_collate_key (folded, -1);
	g_free (folded);
	g_value_init (&keyvalue, G_TYPE_STRING);
	g_value_set_string (&keyvalue, key);
	g_free (key);

	rb_node_set_property (node,
			      RB_NODE_PROP_NAME_SORT_KEY,
			      &keyvalue);

	g_value_unset (&keyvalue);
}

static void
set_genre (RBNodeStation *node,
	   const char *genrename,
	   RBIRadioBackend *backend)
{
	GValue val = { 0, };
	RBNode *genre;

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, genrename);

	genre = rb_iradio_backend_get_genre_by_name (backend,
						     g_value_get_string (&val));
	if (genre == NULL) {
		genre = rb_node_new ();

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME,
				      &val);
		rb_node_set_property (genre,
				      RB_NODE_PROP_GENRE,
				      &val);

		set_title_sort_key (RB_NODE (genre));
		rb_node_add_child (rb_iradio_backend_get_all_genres (backend), genre);
		rb_node_ref (rb_iradio_backend_get_all_genres (backend));
	}
	
	g_value_unset (&val);

	rb_node_add_child (genre, RB_NODE (node));
	rb_node_ref (genre);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_REAL_GENRE,
			      &val);
	g_value_unset (&val);
}

RBNodeStation *
rb_node_station_new (GList *locations, const char *name,
		     const char *genre, const char *source,
		     RBIRadioBackend *iradio_backend)
{
	RBNodeStation *node;
	GValue value = { 0, };
	RBGListWrapper *listwrapper;

	g_return_val_if_fail (locations != NULL, NULL);
	g_return_val_if_fail (RB_IS_IRADIO_BACKEND (iradio_backend), NULL);

	node = RB_NODE_STATION (g_object_new (RB_TYPE_NODE_STATION,
					      "id", rb_node_new_id (),
					      NULL));
	
	g_return_val_if_fail (RB_NODE (node)->priv != NULL, NULL);

	/* Name */
	g_value_init (&value , G_TYPE_STRING);
	g_value_set_string (&value, name);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_NAME,
			      &value);
	g_value_unset (&value);

	set_title_sort_key (RB_NODE (node));

	/* Source */
	g_value_init (&value , G_TYPE_STRING);
	g_value_set_string (&value, source);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_STATION_PROP_SOURCE,
			      &value);
	g_value_unset (&value);

	/* Location */
	{
		GList *first = g_list_first (locations);
		locations = g_list_remove_link (locations, first);
		g_value_init (&value , G_TYPE_STRING);
		g_value_set_string (&value, (char *) first->data);
		rb_node_set_property (RB_NODE (node),
				      RB_NODE_PROP_LOCATION,
				      &value);
		g_value_unset (&value);
	}

	/* Alternate locations */
	listwrapper = rb_glist_wrapper_new (locations);
	g_value_init (&value , G_TYPE_POINTER);
	g_value_set_pointer (&value, listwrapper);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_ALT_LOCATIONS,
			      &value);
	g_value_unset (&value);

	/* Number of plays */
	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "");
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_NUM_PLAYS,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, "");
	/* Last played time */
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_LAST_PLAYED_SIMPLE,
			      &value);
	g_value_unset (&value);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, genre);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_GENRE,
			      &value);
	g_value_unset (&value);

	set_genre (node, genre, iradio_backend);

	rb_node_add_child (rb_iradio_backend_get_all_stations (iradio_backend), RB_NODE (node));
	rb_node_ref (rb_iradio_backend_get_all_stations (iradio_backend));

	return node;
}

/* static void */
/* rb_node_station_sync (RBNodeStation *node, */
/* 		      RBIRadioBackend *backend) */
/* { */
/* 	rb_node_add_child (rb_iradio_backend_get_all_stations (backend), RB_NODE (node)); */
/* 	rb_node_add_child (rb_node_station_get_genre (node), RB_NODE (node)); */
/* } */

RBNode *
rb_node_station_get_genre (RBNodeStation *node)
{
	g_return_val_if_fail (RB_IS_NODE_STATION (node), NULL);
	
	return rb_node_get_property_node (RB_NODE (node),
			                  RB_NODE_PROP_REAL_GENRE);
}

static void
rb_node_station_restored (RBNode *node)
{
	rb_node_ref (rb_node_station_get_genre (RB_NODE_STATION (node)));
}
