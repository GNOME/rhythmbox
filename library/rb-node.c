/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#include <stdlib.h>
#include <string.h>

#include "rb-node.h"

static void rb_node_class_init (RBNodeClass *klass);
static void rb_node_init (RBNode *node);
static void rb_node_finalize (GObject *object);
static void rb_node_dispose (GObject *object);
static void rb_node_set_object_property (GObject *object,
			                 guint prop_id,
			                 const GValue *value,
			                 GParamSpec *pspec);
static void rb_node_get_object_property (GObject *object,
			                 guint prop_id,
			                 GValue *value,
			                 GParamSpec *pspec);
static void rb_node_changed (RBNode *node);
static long rb_node_id_factory_new_id (void);
static void rb_node_id_factory_set_to (long new_factory_position);
static void rb_node_property_free (GValue *value);
static void rb_node_child_changed_cb (RBNode *child,
			              RBNode *node);
static void rb_node_child_destroyed_cb (RBNode *child,
			                RBNode *node);
static void rb_node_save_property (gpointer property,
		                   GValue *value,
		                   xmlNodePtr node);

struct RBNodePrivate
{
	RBNodeType type;
	long id;

	GList *grandparents;
	GList *grandchildren;
	GList *parents;
	GList *children;

	GHashTable *properties;
};

enum
{
	PROP_0,
	PROP_TYPE,
	PROP_ID
};

enum
{
	DESTROYED,
	CHANGED,
	CHILD_CREATED,
	CHILD_CHANGED,
	CHILD_DESTROYED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_node_signals[LAST_SIGNAL] = { 0 };

static long id_factory = 0;

static GHashTable *id_to_node_hash = NULL;

GType
rb_node_get_type (void)
{
	static GType rb_node_type = 0;

	if (rb_node_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBNodeClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_class_init,
			NULL,
			NULL,
			sizeof (RBNode),
			0,
			(GInstanceInitFunc) rb_node_init
		};

		rb_node_type = g_type_register_static (G_TYPE_OBJECT,
						       "RBNode",
						       &our_info, 0);
	}

	return rb_node_type;
}

static void
rb_node_class_init (RBNodeClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_node_finalize;
	object_class->dispose  = rb_node_dispose;

	object_class->set_property = rb_node_set_object_property;
	object_class->get_property = rb_node_get_object_property;

	g_object_class_install_property (object_class,
					 PROP_ID,
					 g_param_spec_long ("id",
							    "Node ID",
							    "Node ID",
							    0, G_MAXLONG, 0,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_TYPE,
					 g_param_spec_enum ("type",
							    "Node type",
							    "Node type",
							    RB_TYPE_NODE_TYPE, RB_NODE_TYPE_GENERIC,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_node_signals[DESTROYED] =
		g_signal_new ("destroyed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, destroyed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_node_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	/* The Babysitting Features[tm] */
	rb_node_signals[CHILD_CREATED] =
		g_signal_new ("child_created",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, child_created),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
	rb_node_signals[CHILD_CHANGED] =
		g_signal_new ("child_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, child_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
	rb_node_signals[CHILD_DESTROYED] =
		g_signal_new ("child_destroyed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, child_destroyed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
}

static void
rb_node_init (RBNode *node)
{
	node->priv = g_new0 (RBNodePrivate, 1);

	node->priv->type = RB_NODE_TYPE_GENERIC;
	node->priv->id = -1;

	node->priv->properties = g_hash_table_new_full (NULL, NULL, NULL,
							(GDestroyNotify) rb_node_property_free);
}

static void
rb_node_finalize (GObject *object)
{
	RBNode *node;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE (object));

	node = RB_NODE (object);

	g_return_if_fail (node->priv != NULL);

	g_list_free (node->priv->grandchildren);
	g_list_free (node->priv->grandparents);
	g_list_free (node->priv->children);
	g_list_free (node->priv->parents);

	g_hash_table_destroy (node->priv->properties);

	g_free (node->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_node_dispose (GObject *object)
{
	RBNode *node = RB_NODE (object);
	GList *l;
	RBNodeType type;

	g_signal_emit (object, rb_node_signals[DESTROYED], 0);
	
	type = rb_node_get_node_type (node);

	if (id_to_node_hash != NULL)
		g_hash_table_remove (id_to_node_hash, node);

	/* decrement parent refcount */
	for (l = node->priv->parents; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		node2->priv->children = g_list_remove (node2->priv->children, node);

		if ((type != RB_NODE_TYPE_ALL_GENRES) &&
		    (type != RB_NODE_TYPE_ALL_ARTISTS) &&
		    (type != RB_NODE_TYPE_ALL_ALBUMS) &&
		    (type != RB_NODE_TYPE_ALL_SONGS))
		{
			g_object_unref (G_OBJECT (node2));
			
			/* initial refcount .. */
			if (G_OBJECT (node2)->ref_count == 1)
			{
				RBNodeType type2 = rb_node_get_node_type (node2);

				if ((type2 != RB_NODE_TYPE_ALL_GENRES) &&
				    (type2 != RB_NODE_TYPE_ALL_ARTISTS) &&
				    (type2 != RB_NODE_TYPE_ALL_ALBUMS) &&
				    (type2 != RB_NODE_TYPE_ALL_SONGS))
				{
					g_object_unref (G_OBJECT (node2));
				}
			}
		}
	}
	for (l = node->priv->grandparents; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		node2->priv->grandchildren = g_list_remove (node2->priv->grandchildren, node);
	}
	for (l = node->priv->grandchildren; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		node2->priv->grandparents = g_list_remove (node2->priv->grandparents, node);
	}
	for (l = node->priv->children; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		node2->priv->parents = g_list_remove (node2->priv->parents, node);
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rb_node_set_object_property (GObject *object,
		             guint prop_id,
		             const GValue *value,
		             GParamSpec *pspec)
{
	RBNode *node = RB_NODE (object);

	switch (prop_id)
	{
	case PROP_ID:
		node->priv->id = g_value_get_long (value);

		if (id_to_node_hash == NULL)
			id_to_node_hash = g_hash_table_new (NULL, NULL);
		g_hash_table_insert (id_to_node_hash, GINT_TO_POINTER (node->priv->id), node);
		break;
	case PROP_TYPE:
		node->priv->type = g_value_get_enum (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_node_get_object_property (GObject *object,
		             guint prop_id,
		             GValue *value,
		             GParamSpec *pspec)
{
	RBNode *node = RB_NODE (object);

	switch (prop_id)
	{
	case PROP_ID:
		g_value_set_long (value, node->priv->id);
		break;
	case PROP_TYPE:
		g_value_set_enum (value, node->priv->type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

GList *
rb_node_get_children (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	return node->priv->children;
}

void
rb_node_add_child (RBNode *node,
		   RBNode *child)
{
	RBNodeType type;
	
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	if (g_list_find (node->priv->children, child) != NULL)
		return;
	
	node->priv->children = g_list_append (node->priv->children, child);
	child->priv->parents = g_list_append (child->priv->parents, node);
	
	/* dont increase the refcount when we add the child to an all node */
	type = rb_node_get_node_type (child);
	if ((type != RB_NODE_TYPE_ALL_GENRES) &&
	    (type != RB_NODE_TYPE_ALL_ARTISTS) &&
	    (type != RB_NODE_TYPE_ALL_ALBUMS) &&
	    (type != RB_NODE_TYPE_ALL_SONGS))
	{
		g_object_ref (G_OBJECT (node));
	}

	g_signal_connect (G_OBJECT (child),
			  "destroyed",
			  G_CALLBACK (rb_node_child_destroyed_cb),
			  node);
	g_signal_connect (G_OBJECT (child),
			  "changed",
			  G_CALLBACK (rb_node_child_changed_cb),
			  node);

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_CREATED], 0, child);
}

void
rb_node_remove_child (RBNode *node,
		      RBNode *child)
{
	RBNodeType type;
	
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	if (g_list_find (node->priv->children, child) == NULL)
		return;

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_DESTROYED], 0, child);
	
	node->priv->children = g_list_remove (node->priv->children, child);
	child->priv->parents = g_list_remove (child->priv->parents, node);
	
	/* dont increase the refcount when we add the child to an all node */
	type = rb_node_get_node_type (child);
	if ((type != RB_NODE_TYPE_ALL_GENRES) &&
	    (type != RB_NODE_TYPE_ALL_ARTISTS) &&
	    (type != RB_NODE_TYPE_ALL_ALBUMS) &&
	    (type != RB_NODE_TYPE_ALL_SONGS))
	{
		g_object_unref (G_OBJECT (node));
	}

	g_signal_handlers_disconnect_by_func (G_OBJECT (child),
					      G_CALLBACK (rb_node_child_destroyed_cb),
		 			      node);
	g_signal_handlers_disconnect_by_func (G_OBJECT (child),
			  		      G_CALLBACK (rb_node_child_changed_cb),
			  		      node);
}

GList *
rb_node_get_parents (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	return node->priv->parents;
}

void
rb_node_add_parent (RBNode *node,
		    RBNode *parent)
{
	rb_node_add_child (parent, node);
}

gboolean
rb_node_has_parent (RBNode *node,
		    RBNode *parent)
{
	GList *parents;
	
	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (parent), FALSE);

	parents = rb_node_get_grandparents (node);
	
	return (g_list_find (parents, parent) != NULL);
}

static void
rb_node_child_changed_cb (RBNode *child,
			  RBNode *node)
{
	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_CHANGED], 0, child);
}

static void
rb_node_child_destroyed_cb (RBNode *child,
			    RBNode *node)
{
	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_DESTROYED], 0, child);
}

long
rb_node_get_id (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), -1);

	return node->priv->id;
}

RBNodeType
rb_node_get_node_type (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), RB_NODE_TYPE_GENERIC);

	return node->priv->type;
}

void
rb_node_set_property (RBNode *node,
		      RBNodeProperty property,
		      const GValue *value)
{
	GValue *val;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (value != NULL);

	val = g_new0 (GValue, 1);
	g_value_init (val, G_VALUE_TYPE (value));
	g_value_copy (value, val);

	g_hash_table_replace (node->priv->properties,
			      GINT_TO_POINTER (property),
			      val);

	rb_node_changed (node);
}

void
rb_node_get_property (RBNode *node,
		      RBNodeProperty property,
		      GValue *value)
{
	GValue *val;
	
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (value != NULL);

	val = g_hash_table_lookup (node->priv->properties,
				   GINT_TO_POINTER (property));
	g_return_if_fail (val != NULL);

	g_value_init (value, G_VALUE_TYPE (val));
	g_value_copy (val, value);
}

GList *
rb_node_get_grandparents (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	return node->priv->grandparents;
}

void
rb_node_add_grandparent (RBNode *node,
			 RBNode *grandparent)
{
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (grandparent));

	if (g_list_find (node->priv->grandparents, grandparent) != NULL)
		return;
	
	node->priv->grandparents = g_list_append (node->priv->grandparents,
						  grandparent);
	grandparent->priv->grandchildren = g_list_append (grandparent->priv->grandchildren,
							  node);
}

gboolean
rb_node_has_grandparent (RBNode *node,
			 RBNode *grandparent)
{
	GList *grandparents;
	
	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (grandparent), FALSE);

	grandparents = rb_node_get_grandparents (node);

	if (grandparents == NULL)
		return TRUE;
	
	return (g_list_find (grandparents, grandparent) != NULL);
}

RBNode *
rb_node_get_nth_child (RBNode *node,
		       int n)
{
	GList *children, *nth;
	
	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (n >= 0, NULL);

	children = rb_node_get_children (node);
	nth = g_list_nth (children, n);

	return RB_NODE (nth->data);
}

int
rb_node_child_index (RBNode *node,
		     RBNode *child)
{
	GList *children;
	
	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (RB_IS_NODE (child), -1);

	children = rb_node_get_children (node);

	return g_list_index (children, child);
}

int
rb_node_n_children (RBNode *node)
{
	GList *children;
	
	g_return_val_if_fail (RB_IS_NODE (node), -1);

	children = rb_node_get_children (node);

	return g_list_length (children);
}

gboolean
rb_node_has_child (RBNode *node,
		   RBNode *child)
{
	GList *children;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (child), FALSE);
	
	children = rb_node_get_children (node);

	return (g_list_find (children, child) != NULL);
}

void
rb_node_save_to_xml (RBNode *node,
		     xmlNodePtr parent_xml_node)
{
	xmlNodePtr xml_node;
	GEnumClass *class;
	GEnumValue *ev;
	GList *l;
	char *tmp;
	long id;
	
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (parent_xml_node != NULL);

	xml_node = xmlNewChild (parent_xml_node, NULL, "RBNode", NULL);

	id = rb_node_get_id (node);

	tmp = g_strdup_printf ("%ld", id);
	xmlSetProp (xml_node, "id", tmp);
	g_free (tmp);

	class = g_type_class_ref (RB_TYPE_NODE_TYPE);

	ev = g_enum_get_value (class, rb_node_get_node_type (node));

	xmlSetProp (xml_node, "type", ev->value_name);

	g_type_class_unref (class);

	for (l = node->priv->parents; l != NULL; l = g_list_next (l))
	{
		RBNode *parent = RB_NODE (l->data);
		xmlNodePtr parent_xml_node;

		g_assert (RB_IS_NODE (parent));

		id = rb_node_get_id (parent);

		parent_xml_node = xmlNewChild (xml_node, NULL, "parent", NULL);
		
		tmp = g_strdup_printf ("%ld", id);
		xmlSetProp (parent_xml_node, "id", tmp);
		g_free (tmp);
	}

	for (l = node->priv->grandparents; l != NULL; l = g_list_next (l))
	{
		RBNode *grandparent = RB_NODE (l->data);
		xmlNodePtr grandparent_xml_node;

		g_assert (RB_IS_NODE (grandparent));

		id = rb_node_get_id (grandparent);

		grandparent_xml_node = xmlNewChild (xml_node, NULL, "grandparent", NULL);

		tmp = g_strdup_printf ("%ld", id);
		xmlSetProp (grandparent_xml_node, "id", tmp);
		g_free (tmp);
	}

	g_hash_table_foreach (node->priv->properties, (GHFunc) rb_node_save_property, xml_node);
}

RBNode *
rb_node_new_from_xml (xmlNodePtr xml_node)
{
	RBNode *node;
	RBNodeType type;
	GEnumClass *class;
	GEnumValue *ev;
	long id;
	char *tmp;
	xmlNodePtr child;
	
	g_return_val_if_fail (xml_node != NULL, NULL);

	tmp = xmlGetProp (xml_node, "id");
	if (tmp == NULL)
		return NULL;
	id = atol (tmp);
	g_free (tmp);

	rb_node_id_factory_set_to (id);

	tmp = xmlGetProp (xml_node, "type");
	class = g_type_class_ref (RB_TYPE_NODE_TYPE);
	ev = g_enum_get_value_by_name (class, tmp);
	type = ev->value;
	g_type_class_unref (class);
	g_free (tmp);

	node = RB_NODE (g_object_new (RB_TYPE_NODE,
				      "type", type,
				      "id", id,
				      NULL));
		
	g_return_val_if_fail (node->priv != NULL, NULL);

	for (child = xml_node->children; child != NULL; child = child->next)
	{
		if (strcmp (child->name, "grandparent") == 0)
		{
			RBNode *grandparent;
			
			tmp = xmlGetProp (child, "id");
			g_assert (tmp != NULL);
			id = atol (tmp);
			g_free (tmp);

			grandparent = rb_node_from_id (id);
			g_assert (grandparent != NULL);

			rb_node_add_grandparent (node, grandparent);
		}
		else if (strcmp (child->name, "parent") == 0)
		{
			RBNode *parent;

			tmp = xmlGetProp (child, "id");
			g_assert (tmp != NULL);
			id = atol (tmp);
			g_free (tmp);

			parent = rb_node_from_id (id);
			
			if (parent != NULL)
				rb_node_add_child (parent, node);
		}
		else if (strcmp (child->name, "property") == 0)
		{
			RBNodeProperty prop_type;
			GType value_type;
			GValue *value;
			
			tmp = xmlGetProp (child, "type");
			class = g_type_class_ref (RB_TYPE_NODE_PROPERTY);
			ev = g_enum_get_value_by_name (class, tmp);
			prop_type = ev->value;
			g_type_class_unref (class);
			g_free (tmp);

			tmp = xmlGetProp (child, "value_type");
			value_type = g_type_from_name (tmp);
			g_free (tmp);

			tmp = xmlGetProp (child, "value");
			value = g_new0 (GValue, 1);
			g_value_init (value, value_type);
			switch (value_type)
			{
			case G_TYPE_STRING:
				g_value_set_string (value, tmp);
				break;
			case G_TYPE_INT:
				g_value_set_int (value, atoi (tmp));
				break;
			case G_TYPE_LONG:
				g_value_set_long (value, atol (tmp));
				break;
			default:
				g_warning ("Unhandled value type: %s", g_type_name (value_type));
				break;
			}
			g_free (tmp);
			
			g_hash_table_replace (node->priv->properties,
					      GINT_TO_POINTER (prop_type),
					      value);
		}
	}

	return node;
}

static void
rb_node_changed (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHANGED], 0);
}

RBNode *
rb_node_new (RBNodeType type)
{
	RBNode *node;

	node = RB_NODE (g_object_new (RB_TYPE_NODE,
				      "type", type,
				      "id", rb_node_id_factory_new_id (),
				      NULL));

	g_return_val_if_fail (node->priv != NULL, NULL);

	return node;
}

RBNode *
rb_node_from_id (int id)
{
	RBNode *node;

	g_return_val_if_fail (id >= 0, NULL);

	node = g_hash_table_lookup (id_to_node_hash, GINT_TO_POINTER (id));

	return node;
}

GType
rb_node_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ RB_NODE_TYPE_GENERIC,     "RB_NODE_TYPE_GENERIC",     "generic"     },
			{ RB_NODE_TYPE_ALL_GENRES,  "RB_NODE_TYPE_ALL_GENRES",  "all_genres"  },
			{ RB_NODE_TYPE_GENRE,       "RB_NODE_TYPE_GENRE",       "genre"       },
			{ RB_NODE_TYPE_ALL_ARTISTS, "RB_NODE_TYPE_ALL_ARTISTS", "all_artists" },
			{ RB_NODE_TYPE_ARTIST,      "RB_NODE_TYPE_ARTIST",      "artist"      },
			{ RB_NODE_TYPE_ALL_ALBUMS,  "RB_NODE_TYPE_ALL_ALBUMS",  "all_albums"  },
			{ RB_NODE_TYPE_ALBUM,       "RB_NODE_TYPE_ALBUM",       "album"       },
			{ RB_NODE_TYPE_ALL_SONGS,   "RB_NODE_TYPE_ALL_SONGS",   "all_songs"   },
			{ RB_NODE_TYPE_SONG,        "RB_NODE_TYPE_SONG",        "song"        },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBNodeType", values);
	}
	
	return etype;
}

GType
rb_node_property_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)
	{
		static const GEnumValue values[] =
		{
			{ RB_NODE_PROPERTY_NAME,              "RB_NODE_PROPERTY_NAME",              "name" },
			{ RB_NODE_PROPERTY_SONG_TRACK_NUMBER, "RB_NODE_PROPERTY_SONG_TRACK_NUMBER", "song_track_number" },
			{ RB_NODE_PROPERTY_SONG_DURATION,     "RB_NODE_PROPERTY_SONG_DURATION",     "song_duration" },
			{ RB_NODE_PROPERTY_SONG_LOCATION,     "RB_NODE_PROPERTY_SONG_LOCATION",     "song_location" },
			{ RB_NODE_PROPERTY_SONG_FILE_SIZE,    "RB_NODE_PROPERTY_SONG_FILE_SIZE",    "file_size" },
			{ RB_NODE_PROPERTY_SONG_MTIME,        "RB_NODE_PROPERTY_SONG_MTIME",        "mtime" },
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBNodeProperty", values);
	}

	return etype;
}

static long 
rb_node_id_factory_new_id (void)
{
	id_factory++;

	return id_factory;
}

static void
rb_node_id_factory_set_to (long new_factory_position)
{
	id_factory = new_factory_position + 1;
}

static void
rb_node_property_free (GValue *value)
{
	g_value_unset (value);
	g_free (value);
}

static void
rb_node_save_property (gpointer property,
		       GValue *value,
		       xmlNodePtr node)
{
	char *value_string;
	GEnumClass *class;
	GEnumValue *ev;
	xmlNodePtr child_node;

	child_node = xmlNewChild (node, NULL, "property", NULL);

	class = g_type_class_ref (RB_TYPE_NODE_PROPERTY);

	ev = g_enum_get_value (class, GPOINTER_TO_INT (property));

	xmlSetProp (child_node, "type", ev->value_name);

	g_type_class_unref (class);

	switch (G_VALUE_TYPE (value))
	{
	case G_TYPE_STRING:
		value_string = g_value_dup_string (value);
		break;
	case G_TYPE_INT:
		value_string = g_strdup_printf ("%d", g_value_get_int (value));
		break;
	case G_TYPE_LONG:
		value_string = g_strdup_printf ("%ld", g_value_get_long (value));
		break;
	default:
		g_warning ("Unhandled value type");
		value_string = NULL;
		break;
	};

	xmlSetProp (child_node, "value_type", g_type_name (G_VALUE_TYPE (value)));
	xmlSetProp (child_node, "value", value_string);
	g_free (value_string);
}
