/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#include <glib.h>
#include <string.h>
#include <stdio.h>

#include "rb-node.h"

struct _RBNodeDetails
{
	GList *parents;
	RBNode *refnode;
	GList *children;
	RBNode *grandparent;

	GHashTable *properties;
	GHashTable *int_properties;

	char *saved_xml_rb_node_name;
};

enum
{
	DESTROYED,
	CHANGED,
	CHILD_CHANGED,
	CHILD_ADDED,
	CHILD_REMOVED,
	LAST_SIGNAL
};

/* object function prototypes */
static void rb_node_class_init (RBNodeClass *klass);
static void rb_node_init (RBNode *t);
static void rb_node_finalize (GObject *object);

/* local function prototypes */
static gboolean free_props (gpointer key, gpointer val, gpointer data);
static void rb_node_changed_cb (RBNode *node, RBNode *parent);

/* globals */
GObjectClass *parent_class = NULL;

static guint rb_node_signals[LAST_SIGNAL] = { 0 };

GType 
rb_node_get_type (void)
{
        static GType rb_node_type = 0;

        if (rb_node_type == 0)
        {
                static const GTypeInfo our_info =
                {
                        sizeof (RBNodeClass),
                        NULL, /* base_init */
                        NULL, /* base_finalize */
                        (GClassInitFunc) rb_node_class_init,
                        NULL,
                        NULL, /* class_data */
                        sizeof (RBNode),
                        0,    /* n_preallocs */
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

	rb_node_signals[DESTROYED] =
		g_signal_new ("destroyed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
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
	rb_node_signals[CHILD_ADDED] =
		g_signal_new ("child_added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, child_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
	rb_node_signals[CHILD_REMOVED] =
		g_signal_new ("child_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (RBNodeClass, child_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
}

static void
rb_node_init (RBNode *t)
{
        t->details = g_new0 (RBNodeDetails, 1);

	t->details->properties     = g_hash_table_new (g_str_hash, g_str_equal);
	t->details->int_properties = g_hash_table_new (g_str_hash, g_str_equal);
	t->details->children       = NULL;
	t->details->parents        = NULL;
	t->details->grandparent    = NULL;
	t->details->refnode        = NULL;
	t->details->saved_xml_rb_node_name = NULL;
}

static void
rb_node_finalize (GObject *object)
{
        RBNode *t;
	GList *l;

        g_return_if_fail (object != NULL);
        g_return_if_fail (RB_IS_NODE (object));
        
        t = RB_NODE (object);

        g_return_if_fail (t->details != NULL);

	for (l = t->details->children; l != NULL; l = g_list_next (l))
	{
		RBNode *tmp = l->data;
		g_list_remove (tmp->details->parents, t);
		g_signal_emit (object, rb_node_signals[CHILD_REMOVED], 0, tmp);
		g_signal_handlers_disconnect_by_func (G_OBJECT (tmp),
						      G_CALLBACK (rb_node_changed_cb),
						      object);
	}

	g_signal_emit (object, rb_node_signals[DESTROYED], 0);

	g_hash_table_foreach_remove (t->details->properties,
				     (GHRFunc) free_props, NULL);
	g_hash_table_destroy (t->details->properties);
	g_hash_table_destroy (t->details->int_properties);

	g_free (t->details->saved_xml_rb_node_name);

        g_free (t->details);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

/* xml_name is optional, and may be set later, but is convenient
   for saving the node to XML */
RBNode *
rb_node_new (const char *xml_name)
{
	RBNode *n;

	n = RB_NODE (g_object_new (RB_TYPE_NODE, NULL));

	if (xml_name != NULL) {
		rb_node_set_xml_name (n, xml_name);
	}

	g_return_val_if_fail (n->details != NULL, NULL);

	return n;
}

RBNode *
rb_node_get_parent (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	return RB_NODE (node->details->parents->data);
}

GList *
rb_node_get_parents (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

        return node->details->parents;
}

RBNode *
rb_node_get_grandparent (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

        return node->details->grandparent;
}

void
rb_node_set_grandparent (RBNode *node, RBNode *granny)
{
	g_return_if_fail (RB_IS_NODE (node));

	node->details->grandparent = granny;
}

GList *
rb_node_get_children (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

        return node->details->children;
}

const gchar *
rb_node_get_string_property (RBNode *node, const gchar *property)
{
	RBNode *n;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	n = node->details->refnode ? node->details->refnode : node;
	g_return_val_if_fail (RB_IS_NODE (n), NULL);
	
	return (gchar*) g_hash_table_lookup (n->details->properties,
					     property);
}

gint
rb_node_get_int_property (RBNode *node, const gchar *property)
{
	RBNode *n;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	n = node->details->refnode ? node->details->refnode : node;
	g_return_val_if_fail (RB_IS_NODE (n), -1);

	return GPOINTER_TO_INT (g_hash_table_lookup (n->details->int_properties,
			                             property));
}

static void
rb_node_changed_cb (RBNode *node, RBNode *parent)
{
	g_signal_emit (G_OBJECT (parent), rb_node_signals[CHILD_CHANGED], 0, node);
}

void 
rb_node_append (RBNode *parent, RBNode *node)
{
	RBNode *p;

	g_return_if_fail (RB_IS_NODE (node));

	if (rb_node_has_child (parent, node) == TRUE) return;

	if (parent != NULL)
	{
		p = parent->details->refnode ? parent->details->refnode : parent;
		g_return_if_fail (RB_IS_NODE (p));

		node->details->parents = g_list_append (node->details->parents, p);

		p->details->children = 
			g_list_append (p->details->children,
				       node);

		g_signal_connect_object (G_OBJECT (node),
				         "changed",
				         G_CALLBACK (rb_node_changed_cb),
				         G_OBJECT (p),
					 0);

		g_signal_emit (G_OBJECT (p), rb_node_signals[CHILD_ADDED], 0, node);
	}

	/* The node with parent == NULL is the toplevel */
}

void 
rb_node_remove (RBNode *node,
	     gint h_level,
	     gint l_level,
	     ActionNotifier func,
	     gpointer data)
{
	GList *c, *parents, *p;
	RBNode *grandparent = rb_node_get_grandparent (node);

	g_return_if_fail (RB_IS_NODE (node));

	if (h_level < l_level) return;

	parents = node->details->parents;

	for (p = parents; p != NULL; p = g_list_next (p))
	{
		RBNode *parent = p->data;
		gboolean found = FALSE;
		gboolean last = FALSE;

		if (parent == NULL) continue;

		parent->details->children = 
			g_list_remove (parent->details->children, node);
		g_signal_emit (G_OBJECT (parent), rb_node_signals[CHILD_REMOVED], 0, node);
		g_signal_handlers_disconnect_by_func (G_OBJECT (node),
						      G_CALLBACK (rb_node_changed_cb),
						      parent);

		/* last node of this grandparent ? */
		if (grandparent != NULL && g_list_length (parent->details->children) > 0)
		{
			GList *s;

			for (s = parent->details->children; s != NULL; s = g_list_next (s))
			{
				if (grandparent ==
				    rb_node_get_grandparent (s->data))
				{
					found = TRUE;
					continue;
				}
			}

			/* no more items of the in this grandparent */
			if (found == FALSE)
			{
				grandparent->details->children =
					g_list_remove (grandparent->details->children, parent);
				g_signal_emit (G_OBJECT (grandparent), rb_node_signals[CHILD_REMOVED], 0, parent);
				g_signal_handlers_disconnect_by_func (G_OBJECT (parent),
								      G_CALLBACK (rb_node_changed_cb),
								      grandparent);
				if (func)
					func (parent, l_level, data);
				if (g_list_length (grandparent->details->children) == 0)
				{
					if (func)
						func (grandparent, l_level + 1, data);
					rb_node_remove (grandparent, h_level, l_level + 2,
						     func, data);
				}
				last = TRUE;
			}
		}
		
		/* last node ? */
		if (last == FALSE && g_list_length (parent->details->children) == 0)
		{
			if (func)
				func (parent, l_level, data);
			rb_node_remove (parent, h_level, l_level + 1, 
				     func, data);
		}
	}

	/* remove children */
	for (c = node->details->children; 
	     c != NULL;
	     c = g_list_next (c))
	{
		if (func)
			func (RB_NODE (c->data), l_level, data);
		rb_node_remove (RB_NODE (c->data), h_level, l_level - 1, func, data);
	}

	g_object_unref (node);
}

void
rb_node_set_string_property (RBNode *node, 
			  const gchar *property,
			  gchar *value)
{
	RBNode *n;
	char *old_value;

	g_return_if_fail (RB_IS_NODE (node));

	n = node->details->refnode ? node->details->refnode : node;
	g_return_if_fail (RB_IS_NODE (n));

	old_value = g_hash_table_lookup (n->details->properties, property);

	if (old_value != NULL)
	{
	        g_free (old_value);
	}

	g_hash_table_insert (n->details->properties,
			     g_strdup (property), value);

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHANGED], 0);
}

void
rb_node_set_int_property (RBNode *node, 
		       const gchar *property,
		       gint value)
{
	RBNode *n;

	g_return_if_fail (RB_IS_NODE (node));

	n = node->details->refnode ? node->details->refnode : node;
	g_return_if_fail (RB_IS_NODE (n));

	g_hash_table_insert (n->details->int_properties,
			     g_strdup (property), GINT_TO_POINTER (value));

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHANGED], 0);
}

static gboolean
free_props (gpointer key, gpointer val, gpointer data)
{
	g_free (key);
	g_free (val);
	return TRUE;
}

gboolean
rb_node_has_child (RBNode *node, RBNode *child)
{
	return (g_list_find (rb_node_get_children (node), child) != NULL);
}

/* Set the name of the XML node to save this node to if/when we save */
void 
rb_node_set_xml_name (RBNode *node, const char *xml_name)
{
	g_free (node->details->saved_xml_rb_node_name);

	node->details->saved_xml_rb_node_name = g_strdup (xml_name);
}

static void
save_string_property (const char *key, const char *value, xmlNodePtr xml_node)
{
	xmlSetProp (xml_node, key, value);	
}

static void
save_int_property (const char *key, gpointer pvalue, xmlNodePtr xml_node)
{
	char *string_value;
	int value = GPOINTER_TO_INT (pvalue);

	string_value = g_strdup_printf ("%d", value);
	xmlSetProp (xml_node, key, string_value);

	g_free (string_value);
}

/* Save this node, but not its children, return the newly created XML node */
xmlNodePtr
rb_node_save_to_xml (RBNode *node, RBNode *grandparent, xmlNodePtr parent, gboolean save_children)
{
	xmlNodePtr xml_node;

	if (grandparent != NULL && 
	    grandparent != node &&
	    g_list_find (grandparent->details->children, node) == NULL)
	{
		if (rb_node_get_grandparent (node) != grandparent) return NULL;
	}

	if (node->details->saved_xml_rb_node_name == NULL)
	{
		g_warning ("Saving unnamed node to XML, using generic name \"RBNode\"");
		node->details->saved_xml_rb_node_name = g_strdup ("RBNode");
	}

	xml_node = xmlNewChild (parent, NULL, node->details->saved_xml_rb_node_name, NULL);

	g_hash_table_foreach (node->details->properties,
			      (GHFunc) save_string_property, xml_node);
	g_hash_table_foreach (node->details->int_properties,
			      (GHFunc) save_int_property, xml_node);

	if (save_children == TRUE)
	{
		GList *l;

		for (l = node->details->children; l != NULL; l = g_list_next (l))
		{
			rb_node_save_to_xml (l->data, grandparent, xml_node, TRUE);
		}
	}

	return xml_node;
}

RBNode *
rb_node_next (RBNode *node)
{
	RBNode *parent = rb_node_get_parent (node);
	GList *children = rb_node_get_children (parent);
	GList *pos = g_list_find (children, node);
	GList *next = g_list_next (pos);

	if (next != NULL)
		return RB_NODE (next->data);
	else
		return NULL;
}

int
rb_node_get_child_index (RBNode *parent, RBNode *node)
{
	GList *children = rb_node_get_children (parent);
	return g_list_index (children, node);
}

RBNode *
rb_node_get_nth_child (RBNode *node, int n)
{
	GList *children = rb_node_get_children (node);
	GList *nth = g_list_nth (children, n);
	
	if (nth != NULL)
		return RB_NODE (nth->data);
	else
		return NULL;
}

int
rb_node_n_children (RBNode *node)
{
	return g_list_length (rb_node_get_children (node));
}
