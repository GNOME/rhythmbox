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
static void rb_node_save_property (char *property,
		                   GValue *value,
		                   xmlNodePtr node);

typedef enum
{
	RB_NODE_ACTION_UNREF,
	RB_NODE_ACTION_SIGNAL
} RBNodeActionType;

typedef enum
{
	DESTROYED,
	CHANGED,
	CHILD_CREATED,
	CHILD_CHANGED,
	CHILD_DESTROYED,
	LAST_SIGNAL
} RBNodeSignal;

typedef struct
{
	RBNodeActionType type;
	RBNode *node;
	RBNodeSignal signal;
	gpointer user_data;
} RBNodeAction;

static void rb_node_add_action (RBNode *node,
				RBNodeAction *action);

struct RBNodePrivate
{
	RBNodeType type;
	long id;

	GList *parents;
	GList *children;

	GHashTable *properties;

	GStaticRWLock *lock;
};

enum
{
	PROP_0,
	PROP_TYPE,
	PROP_ID
};

static GObjectClass *parent_class = NULL;

static guint rb_node_signals[LAST_SIGNAL] = { 0 };

static GMutex *id_factory_lock = NULL;
static long id_factory = 0;

static GStaticRWLock *id_to_node_hash_lock = NULL;
static GHashTable *id_to_node_hash = NULL;

static GHashTable *name_to_genre  = NULL;
static GHashTable *name_to_artist = NULL;
static GHashTable *name_to_album  = NULL;
static GHashTable *uri_to_song    = NULL;

static GStaticRWLock *name_to_genre_lock  = NULL;
static GStaticRWLock *name_to_artist_lock = NULL;
static GStaticRWLock *name_to_album_lock  = NULL;
static GStaticRWLock *uri_to_song_lock    = NULL; 

/* action queue */
static GQueue *actions = NULL;
static GStaticRWLock *actions_lock = NULL;
static guint actions_idle_func = 0;
static GMutex *actions_idle_func_lock = NULL;

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

	node->priv->lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (node->priv->lock);

	node->priv->type = RB_NODE_TYPE_GENERIC;
	node->priv->id = -1;

	node->priv->properties = g_hash_table_new_full (g_str_hash, g_str_equal,
							(GDestroyNotify) g_free,
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

	g_list_free (node->priv->children);
	g_list_free (node->priv->parents);

	g_hash_table_destroy (node->priv->properties);

	g_static_rw_lock_free (node->priv->lock);

	g_free (node->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_node_dispose (GObject *object)
{
	RBNode *node = RB_NODE (object);
	GList *l;

	g_signal_emit (object, rb_node_signals[DESTROYED], 0);
	
	g_static_rw_lock_writer_lock (id_to_node_hash_lock);
	if (id_to_node_hash != NULL)
		g_hash_table_remove (id_to_node_hash, node);
	g_static_rw_lock_writer_unlock (id_to_node_hash_lock);

	/* decrement parent refcount */
	for (l = node->priv->parents; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		g_static_rw_lock_writer_lock (node2->priv->lock);
		node2->priv->children = g_list_remove (node2->priv->children, node);
		g_static_rw_lock_writer_unlock (node2->priv->lock);
	}
	for (l = node->priv->children; l != NULL; l = g_list_next (l))
	{
		RBNode *node2 = RB_NODE (l->data);

		g_static_rw_lock_writer_lock (node2->priv->lock);
		node2->priv->parents = g_list_remove (node2->priv->parents, node);
		g_static_rw_lock_writer_unlock (node2->priv->lock);
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

		g_static_rw_lock_writer_lock (id_to_node_hash_lock);
		g_hash_table_insert (id_to_node_hash, GINT_TO_POINTER (node->priv->id), node);
		g_static_rw_lock_writer_unlock (id_to_node_hash_lock);
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

static gboolean
rb_node_action_queue_cb (gpointer node_reference)
{
	RBNodeAction *action;
	gboolean empty;

	g_static_rw_lock_reader_lock (actions_lock);
	empty = g_queue_is_empty (actions);
	g_static_rw_lock_reader_unlock (actions_lock);

	if (empty == TRUE)
	{
		g_mutex_lock (actions_idle_func_lock);
		actions_idle_func = 0;
		g_mutex_unlock (actions_idle_func_lock);
		return FALSE;
	}

	{
		g_static_rw_lock_writer_lock (actions_lock);
		action = g_queue_pop_head (actions);
		g_static_rw_lock_writer_unlock (actions_lock);

		switch (action->type)
		{
		case RB_NODE_ACTION_UNREF:
			g_object_unref (G_OBJECT (action->node));
			break;
		case RB_NODE_ACTION_SIGNAL:
			if (action->user_data != NULL)
			{
				g_signal_emit (G_OBJECT (action->node), rb_node_signals[action->signal], 0,
					       action->user_data);
			}
			else
			{
				g_signal_emit (G_OBJECT (action->node), rb_node_signals[action->signal], 0);
			}
			break;
		default:
			break;
		}

		g_free (action);
	}

	return TRUE;
}

GList *
rb_node_get_children (RBNode *node)
{
	GList *ret;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);
	ret = g_list_copy (node->priv->children);
	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

void
rb_node_add_child (RBNode *node,
		   RBNode *child)
{
	RBNodeAction *action;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	g_static_rw_lock_writer_lock (node->priv->lock);

	if (g_list_find (node->priv->children, child) != NULL)
	{
		g_static_rw_lock_writer_unlock (node->priv->lock);
		return;
	}

	node->priv->children = g_list_append (node->priv->children, child);
	child->priv->parents = g_list_append (child->priv->parents, node);
	
	g_signal_connect (G_OBJECT (child),
			  "destroyed",
			  G_CALLBACK (rb_node_child_destroyed_cb),
			  node);
	g_signal_connect (G_OBJECT (child),
			  "changed",
			  G_CALLBACK (rb_node_child_changed_cb),
			  node);

	action = g_new0 (RBNodeAction, 1);
	action->type = RB_NODE_ACTION_SIGNAL;
	action->node = node;
	action->signal = CHILD_CREATED;
	action->user_data = child;

	rb_node_add_action (node, action);

	g_static_rw_lock_writer_unlock (node->priv->lock);
}

void
rb_node_remove_child (RBNode *node,
		      RBNode *child)
{
	RBNodeAction *action;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	g_static_rw_lock_writer_lock (node->priv->lock);

	if (g_list_find (node->priv->children, child) == NULL)
	{
		g_static_rw_lock_writer_unlock (node->priv->lock);
		return;
	}

	action = g_new0 (RBNodeAction, 1);
	action->type = RB_NODE_ACTION_SIGNAL;
	action->node = node;
	action->signal = CHILD_DESTROYED;
	action->user_data = child;

	rb_node_add_action (node, action);
	
	node->priv->children = g_list_remove (node->priv->children, child);
	child->priv->parents = g_list_remove (child->priv->parents, node);
	
	g_signal_handlers_disconnect_by_func (G_OBJECT (child),
					      G_CALLBACK (rb_node_child_destroyed_cb),
		 			      node);
	g_signal_handlers_disconnect_by_func (G_OBJECT (child),
			  		      G_CALLBACK (rb_node_child_changed_cb),
			  		      node);

	g_static_rw_lock_writer_unlock (node->priv->lock);
}

GList *
rb_node_get_parents (RBNode *node)
{
	GList *ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);
	ret = g_list_copy (node->priv->parents);
	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

void
rb_node_add_parent (RBNode *node,
		    RBNode *parent)
{
	rb_node_add_child (parent, node);
}

void
rb_node_remove_parent (RBNode *node,
		       RBNode *parent)
{
	rb_node_remove_child (parent, node);
}

gboolean
rb_node_has_parent (RBNode *node,
		    RBNode *parent)
{
	gboolean ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (parent), FALSE);

	g_static_rw_lock_reader_lock (node->priv->lock);
	
	ret = (g_list_find (node->priv->parents, parent) != NULL);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

static void
rb_node_child_changed_cb (RBNode *child,
			  RBNode *node)
{
	RBNodeAction *action;
	
	action = g_new0 (RBNodeAction, 1);
	action->type = RB_NODE_ACTION_SIGNAL;
	action->node = node;
	action->signal = CHILD_CHANGED;
	action->user_data = child;

	rb_node_add_action (node, action);
}

static void
rb_node_child_destroyed_cb (RBNode *child,
			    RBNode *node)
{
	RBNodeAction *action;

	action = g_new0 (RBNodeAction, 1);
	action->type = RB_NODE_ACTION_SIGNAL;
	action->node = node;
	action->signal = CHILD_DESTROYED;
	action->user_data = child;

	rb_node_add_action (node, action);
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
		      const char *property,
		      const GValue *value)
{
	GValue *val;
	RBNodeType type;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (value != NULL);

	type = rb_node_get_node_type (node);
	switch (type)
	{
	case RB_NODE_TYPE_GENRE:
		if (strcmp (property, "name") == 0)
		{
			g_static_rw_lock_writer_lock (name_to_genre_lock);
			g_hash_table_replace (name_to_genre,
					      g_strdup (g_value_get_string (value)),
					      node);
			g_static_rw_lock_writer_unlock (name_to_genre_lock);
		}
		break;
	case RB_NODE_TYPE_ARTIST:
		if (strcmp (property, "name") == 0)
		{
			g_static_rw_lock_writer_lock (name_to_artist_lock);
			g_hash_table_replace (name_to_artist,
					      g_strdup (g_value_get_string (value)),
					      node);
			g_static_rw_lock_writer_unlock (name_to_artist_lock);
		}
		break;
	case RB_NODE_TYPE_ALBUM:
		if (strcmp (property, "name") == 0)
		{
			g_static_rw_lock_writer_lock (name_to_album_lock);
			g_hash_table_replace (name_to_album,
					      g_strdup (g_value_get_string (value)),
					      node);
			g_static_rw_lock_writer_unlock (name_to_album_lock);
		}
		break;
	case RB_NODE_TYPE_SONG:
		if (strcmp (property, "location") == 0)
		{
			g_static_rw_lock_writer_lock (uri_to_song_lock);
			g_hash_table_replace (uri_to_song,
					      g_strdup (g_value_get_string (value)),
					      node);
			g_static_rw_lock_writer_unlock (uri_to_song_lock);
		}
		break;
	default:
		break;
	}

	val = g_new0 (GValue, 1);
	g_value_init (val, G_VALUE_TYPE (value));
	g_value_copy (value, val);
	
	g_static_rw_lock_writer_lock (node->priv->lock);

	g_hash_table_replace (node->priv->properties,
			      g_strdup (property),
			      val);

	g_static_rw_lock_writer_unlock (node->priv->lock);

	rb_node_changed (node);
}

void
rb_node_get_property (RBNode *node,
		      const char *property,
		      GValue *value)
{
	GValue *val;
	
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (value != NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	val = g_hash_table_lookup (node->priv->properties,
				   property);
	g_return_if_fail (val != NULL);

	g_value_init (value, G_VALUE_TYPE (val));
	g_value_copy (val, value);

	g_static_rw_lock_reader_unlock (node->priv->lock);
}

RBNode *
rb_node_get_nth_child (RBNode *node,
		       int n)
{
	GList *nth;
	RBNode *ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (n >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	nth = g_list_nth (node->priv->children, n);

	if (nth != NULL)
		ret = RB_NODE (nth->data);
	else
		ret = NULL;

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

int
rb_node_child_index (RBNode *node,
		     RBNode *child)
{
	int ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (RB_IS_NODE (child), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = g_list_index (node->priv->children, child);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

int
rb_node_n_children (RBNode *node)
{
	int ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = g_list_length (node->priv->children);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

gboolean
rb_node_has_child (RBNode *node,
		   RBNode *child)
{
	gboolean ret;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (child), FALSE);
	
	g_static_rw_lock_reader_lock (node->priv->lock);
	
	ret = (g_list_find (node->priv->children, child) != NULL);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

int
rb_node_parent_index (RBNode *node,
		      RBNode *parent)
{
	return rb_node_child_index (parent, node);
}

int
rb_node_n_parents (RBNode *node)
{
	int ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = g_list_length (node->priv->parents);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

RBNode *
rb_node_get_nth_parent (RBNode *node,
		        int n)
{
	GList *nth;
	RBNode *ret;
	
	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (n >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	nth = g_list_nth (node->priv->parents, n);

	if (nth != NULL)
		ret = RB_NODE (nth->data);
	else
		ret = NULL;

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
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

	g_hash_table_foreach (node->priv->properties, (GHFunc) rb_node_save_property, xml_node);

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
		if (strcmp (child->name, "parent") == 0)
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
			char *prop_type;
			GType value_type;
			GValue value = { 0, };
			
			prop_type = xmlGetProp (child, "key");

			tmp = xmlGetProp (child, "value_type");
			value_type = g_type_from_name (tmp);
			g_free (tmp);

			tmp = xmlGetProp (child, "value");
			g_value_init (&value, value_type);
			switch (value_type)
			{
			case G_TYPE_STRING:
				g_value_set_string (&value, tmp);
				break;
			case G_TYPE_INT:
				g_value_set_int (&value, atoi (tmp));
				break;
			case G_TYPE_LONG:
				g_value_set_long (&value, atol (tmp));
				break;
			default:
				g_warning ("Unhandled value type: %s", g_type_name (value_type));
				break;
			}
			g_free (tmp);
			
			rb_node_set_property (node,
					      prop_type,
					      &value);
			g_value_unset (&value);
			g_free (prop_type);
		}
	}

	return node;
}

static void
rb_node_changed (RBNode *node)
{
	RBNodeAction *action;

	g_return_if_fail (RB_IS_NODE (node));

	action = g_new0 (RBNodeAction, 1);
	action->type = RB_NODE_ACTION_SIGNAL;
	action->node = node;
	action->signal = CHANGED;

	rb_node_add_action (node, action);
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

	g_static_rw_lock_reader_lock (id_to_node_hash_lock);
	node = g_hash_table_lookup (id_to_node_hash, GINT_TO_POINTER (id));
	g_static_rw_lock_reader_unlock (id_to_node_hash_lock);

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

static long 
rb_node_id_factory_new_id (void)
{
	g_mutex_lock (id_factory_lock);
	id_factory++;
	g_mutex_unlock (id_factory_lock);

	return id_factory;
}

static void
rb_node_id_factory_set_to (long new_factory_position)
{
	g_mutex_lock (id_factory_lock);
	id_factory = new_factory_position + 1;
	g_mutex_unlock (id_factory_lock);
}

static void
rb_node_property_free (GValue *value)
{
	g_value_unset (value);
	g_free (value);
}

static void
rb_node_save_property (char *property,
		       GValue *value,
		       xmlNodePtr node)
{
	char *value_string;
	xmlNodePtr child_node;

	child_node = xmlNewChild (node, NULL, "property", NULL);

	xmlSetProp (child_node, "key", property);

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

RBNode *
rb_node_ref (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	return RB_NODE (g_object_ref (G_OBJECT (node)));
}

void
rb_node_unref (RBNode *node)
{
	RBNodeType type;
	
	g_return_if_fail (RB_IS_NODE (node));

	type = rb_node_get_node_type (node);

	if (G_OBJECT (node)->ref_count == 2 &&
	    (type == RB_NODE_TYPE_GENRE ||
	     type == RB_NODE_TYPE_ARTIST ||
	     type == RB_NODE_TYPE_ALBUM))
	{
		g_object_unref (G_OBJECT (node));
	}

	if (G_OBJECT (node)->ref_count == 1)
	{
		/* object will be killed, since we dont want zombies
		 * in the UI we'll dispose it from the main thread */
		RBNodeAction *action;

		action = g_new0 (RBNodeAction, 1);
		action->type = RB_NODE_ACTION_UNREF;
		action->node = node;

		rb_node_add_action (node, action);
	}
	else
		g_object_unref (G_OBJECT (node));
}

void
rb_node_system_init (void)
{
	g_return_if_fail (actions_lock == NULL);
	
	actions = g_queue_new ();
	actions_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (actions_lock);

	actions_idle_func_lock = g_mutex_new ();

	id_to_node_hash = g_hash_table_new (NULL, NULL);
	id_to_node_hash_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (id_to_node_hash_lock);

	id_factory_lock = g_mutex_new ();

	name_to_genre  = g_hash_table_new_full (g_str_hash,
					        g_str_equal,
					        (GDestroyNotify) g_free,
					        NULL);
	name_to_artist = g_hash_table_new_full (g_str_hash,
					        g_str_equal,
					        (GDestroyNotify) g_free,
					        NULL);
	name_to_album  = g_hash_table_new_full (g_str_hash,
					        g_str_equal,
					        (GDestroyNotify) g_free,
					        NULL);
	uri_to_song    = g_hash_table_new_full (g_str_hash,
					        g_str_equal,
					        (GDestroyNotify) g_free,
					        NULL);

	name_to_genre_lock  = g_new0 (GStaticRWLock, 1);
	name_to_artist_lock = g_new0 (GStaticRWLock, 1);
	name_to_album_lock  = g_new0 (GStaticRWLock, 1);
	uri_to_song_lock    = g_new0 (GStaticRWLock, 1);
}

void
rb_node_system_shutdown (void)
{
	g_return_if_fail (actions_lock != NULL);

	if (actions_idle_func != 0)
		g_source_remove (actions_idle_func);

	g_mutex_free (actions_idle_func_lock);

	g_static_rw_lock_free (actions_lock);

	while (g_queue_is_empty (actions) == FALSE)
	{
		g_free (g_queue_pop_head (actions));
	}

	g_queue_free (actions);

	g_hash_table_destroy (id_to_node_hash);
	g_static_rw_lock_free (id_to_node_hash_lock);

	g_mutex_free (id_factory_lock);

	g_hash_table_destroy (name_to_genre);
	g_hash_table_destroy (name_to_artist);
	g_hash_table_destroy (name_to_album);
	g_hash_table_destroy (uri_to_song);

	g_static_rw_lock_free (name_to_genre_lock);
	g_static_rw_lock_free (name_to_artist_lock);
	g_static_rw_lock_free (name_to_album_lock);
	g_static_rw_lock_free (uri_to_song_lock);
}

RBNode *
rb_node_get_genre_by_name (const char *name)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (name_to_genre_lock);
	ret = g_hash_table_lookup (name_to_genre, name);
	g_static_rw_lock_reader_unlock (name_to_genre_lock);

	return ret;
}

RBNode *
rb_node_get_artist_by_name (const char *name)
{
	RBNode *ret;

	g_static_rw_lock_reader_lock (name_to_artist_lock);
	ret = g_hash_table_lookup (name_to_artist, name);
	g_static_rw_lock_reader_unlock (name_to_artist_lock);

	return ret;
}

RBNode *
rb_node_get_album_by_name (const char *name)
{
	RBNode *ret;
	
	g_static_rw_lock_reader_lock (name_to_album_lock);
	ret = g_hash_table_lookup (name_to_album, name);
	g_static_rw_lock_reader_unlock (name_to_album_lock);

	return ret;
}

RBNode *
rb_node_get_song_by_uri (const char *uri)
{
	RBNode *ret;

	g_static_rw_lock_reader_lock (uri_to_song_lock);
	ret = g_hash_table_lookup (uri_to_song, uri);
	g_static_rw_lock_reader_unlock (uri_to_song_lock);

	return ret;
}

static void 
rb_node_add_action (RBNode *node,
		    RBNodeAction *action)
{
	g_static_rw_lock_writer_lock (actions_lock);
	g_queue_push_tail (actions, action);
	g_static_rw_lock_writer_unlock (actions_lock);

	/* add the idle function that will emit signals */
	g_mutex_lock (actions_idle_func_lock);
	if (actions_idle_func == 0)
	{
		actions_idle_func = g_idle_add_full (100,
						     (GSourceFunc) rb_node_action_queue_cb, 
						     NULL, 
						     NULL);
	}
	g_mutex_unlock (actions_idle_func_lock);
}
