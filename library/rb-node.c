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

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <glib.h>
#include <bonobo/bonobo-i18n.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gdk/gdk.h>
#include <time.h>

#include "rb-node.h"
// #include "rb-string.h"
#include "rb-glist-wrapper.h"
#include "rb-thread-helpers.h"
#include "rb-cut-and-paste-code.h"

typedef struct
{
	RBNode *node;
	int id;
	RBNodeCallback callback;
	RBNodeSignalType type;
	gpointer data;
} RBNodeSignalData;

typedef struct
{
	RBNode *node;
	guint index;
} RBNodeParent;

struct RBNode
{
	GStaticRWLock *lock;

	int ref_count;

	gulong id;

	GPtrArray *properties;

	GHashTable *parents;
	GPtrArray *children;

	GHashTable *signals;
	int signal_id;

	RBNodeDb *db;
};

typedef struct
{
	RBNodeSignalType type;
	va_list valist;
} ENESCData;

/* evillish hacks to temporarily readlock->writelock and v.v. */
static inline void
write_lock_to_read_lock (RBNode *node)
{
	g_static_mutex_lock (&node->lock->mutex);
	node->lock->read_counter++;
	g_static_mutex_unlock (&node->lock->mutex);

	g_static_rw_lock_writer_unlock (node->lock);
}

static inline void
read_lock_to_write_lock (RBNode *node)
{
	g_static_mutex_lock (&node->lock->mutex);
	node->lock->read_counter--;
	g_static_mutex_unlock (&node->lock->mutex);

	g_static_rw_lock_writer_lock (node->lock);
}

static inline void
lock_gdk (void)
{
	if (rb_thread_helpers_in_main_thread () == FALSE)
		GDK_THREADS_ENTER ();
}

static inline void
unlock_gdk (void)
{
	if (rb_thread_helpers_in_main_thread () == FALSE)
		GDK_THREADS_LEAVE ();
}

static gboolean
int_equal (gconstpointer a,
	   gconstpointer b)
{
	return GPOINTER_TO_INT (a) == GPOINTER_TO_INT (b);
}

static guint
int_hash (gconstpointer a)
{
	return GPOINTER_TO_INT (a);
}

static void
callback (long id, RBNodeSignalData *data, gpointer *dummy)
{
	ENESCData *user_data;
	va_list valist;

	user_data = (ENESCData *) dummy;

	G_VA_COPY(valist, user_data->valist);

	if (data->type != user_data->type) return;

	switch (data->type)
	{
		case RB_NODE_DESTROY:
		case RB_NODE_RESTORED:
			data->callback (data->node, data->data);
		break;

		case RB_NODE_CHILD_ADDED:
		case RB_NODE_CHILD_CHANGED:
			data->callback (data->node, va_arg (valist, RBNode *), data->data);
		break;

		case RB_NODE_CHILD_REMOVED:
		{
			RBNode *node;
			guint last_index;

			node = va_arg (valist, RBNode *);
			last_index = va_arg (valist, guint);

			data->callback (data->node, node, last_index, data->data);
		}
		break;

		case RB_NODE_CHILDREN_REORDERED:
			data->callback (data->node, va_arg (valist, int *), data->data);
		break;
	}

        va_end(valist);
}

static void
rb_node_emit_signal (RBNode *node, RBNodeSignalType type, ...)
{
	ENESCData data;

	va_start (data.valist, type);

	data.type = type;

	g_hash_table_foreach (node->signals,
			      (GHFunc) callback,
			      &data);

	va_end (data.valist);
}

static void
rb_node_finalize (RBNode *node)
{
	guint i;

	g_hash_table_destroy (node->signals);
	node->signals = NULL;

	for (i = 0; i < node->properties->len; i++) {
		GValue *val;

		val = g_ptr_array_index (node->properties, i);

		if (val != NULL) {
			g_value_unset (val);
			g_free (val);
		}
	}
	g_ptr_array_free (node->properties, TRUE);

	g_hash_table_destroy (node->parents);

	g_ptr_array_free (node->children, TRUE);

	g_static_rw_lock_free (node->lock);
	g_free (node->lock);

	g_free (node);
}

static inline void
real_remove_child (RBNode *node,
		   RBNode *child,
		   gboolean remove_from_parent,
		   gboolean remove_from_child)
{
	RBNodeParent *node_info;

	node_info = g_hash_table_lookup (child->parents,
			                 GINT_TO_POINTER (node->id));

	if (remove_from_parent) {
		guint i;
		guint old_index;

		old_index = node_info->index;

		g_ptr_array_remove_index (node->children,
					  node_info->index);

		/* correct indices on kids */
		for (i = node_info->index; i < node->children->len; i++) {
			RBNode *borked_node;
			RBNodeParent *borked_node_info;

			borked_node = g_ptr_array_index (node->children, i);

			g_static_rw_lock_writer_lock (borked_node->lock);

			borked_node_info = g_hash_table_lookup (borked_node->parents,
						                GINT_TO_POINTER (node->id));
			borked_node_info->index--;

			g_static_rw_lock_writer_unlock (borked_node->lock);
		}

		write_lock_to_read_lock (node);
		write_lock_to_read_lock (child);

		rb_node_emit_signal (node, RB_NODE_CHILD_REMOVED, child, old_index);

		read_lock_to_write_lock (node);
		read_lock_to_write_lock (child);
	}

	if (remove_from_child) {
		g_hash_table_remove (child->parents,
				     GINT_TO_POINTER (node->id));
	}
}

static void
remove_child (long id,
	      RBNodeParent *node_info,
	      RBNode *node)
{
	g_static_rw_lock_writer_lock (node_info->node->lock);

	real_remove_child (node_info->node, node, TRUE, FALSE);

	g_static_rw_lock_writer_unlock (node_info->node->lock);
}

static void
signal_object_weak_notify (RBNodeSignalData *signal_data,
                           GObject *where_the_object_was)
{
	rb_node_signal_disconnect (signal_data->node, signal_data->id);
}

static void
unref_signal_objects (long id,
	              RBNodeSignalData *signal_data,
	              RBNode *node)
{
	if (signal_data->data)
	{
		g_object_weak_unref (G_OBJECT (signal_data->data),
				     (GWeakNotify)signal_object_weak_notify,
				     signal_data);
	}
}

static void
rb_node_dispose (RBNode *node)
{
	guint i;

	write_lock_to_read_lock (node);

	rb_node_emit_signal (node, RB_NODE_DESTROY);

	read_lock_to_write_lock (node);

	lock_gdk ();

	/* remove from DAG */
	g_hash_table_foreach (node->parents,
			      (GHFunc) remove_child,
			      node);

	for (i = 0; i < node->children->len; i++) {
		RBNode *child;

		child = g_ptr_array_index (node->children, i);

		g_static_rw_lock_writer_lock (child->lock);

		real_remove_child (node, child, FALSE, TRUE);

		g_static_rw_lock_writer_unlock (child->lock);
	}

	g_static_rw_lock_writer_unlock (node->lock);

	g_hash_table_foreach (node->signals,
			      (GHFunc) unref_signal_objects,
			      node);

	_rb_node_db_remove_id (node->db, node->id);

	unlock_gdk ();
}

RBNode *
rb_node_new (RBNodeDb *db)
{
	long id;

	g_return_val_if_fail (RB_IS_NODE_DB (db), NULL);

	id = _rb_node_db_new_id (db);

	return rb_node_new_with_id (db, id);
}

RBNode *
rb_node_new_with_id (RBNodeDb *db, gulong reserved_id)
{
	RBNode *node;

	g_return_val_if_fail (RB_IS_NODE_DB (db), NULL);

	node = g_new0 (RBNode, 1);

	node->lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (node->lock);

	node->ref_count = 0;

	node->id = reserved_id;

	node->db = db;

	node->properties = g_ptr_array_new ();

	node->children = g_ptr_array_new ();

	node->parents = g_hash_table_new_full (int_hash,
					       int_equal,
					       NULL,
					       g_free);

	node->signals = g_hash_table_new_full (int_hash,
					       int_equal,
					       NULL,
					       g_free);
	node->signal_id = 0;

	_rb_node_db_add_id (db, reserved_id, node);

	return node;
}

RBNodeDb *
rb_node_get_db (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	
	return node->db;
}

long
rb_node_get_id (RBNode *node)
{
	long ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->lock);

	ret = node->id;

	g_static_rw_lock_reader_unlock (node->lock);

	return ret;
}

void
rb_node_ref (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_writer_lock (node->lock);

	node->ref_count++;

	g_static_rw_lock_writer_unlock (node->lock);
}

void
rb_node_unref (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_writer_lock (node->lock);

	node->ref_count--;

	if (node->ref_count <= 0) {
		rb_node_dispose (node);
		rb_node_finalize (node);
	} else {
		g_static_rw_lock_writer_unlock (node->lock);
	}
}

void
rb_node_freeze (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_reader_lock (node->lock);
}

void
rb_node_thaw (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_reader_unlock (node->lock);
}

static void
child_changed (gulong id,
	       RBNodeParent *node_info,
	       RBNode *node)
{
	g_static_rw_lock_reader_lock (node_info->node->lock);

	rb_node_emit_signal (node_info->node, RB_NODE_CHILD_CHANGED, node);

	g_static_rw_lock_reader_unlock (node_info->node->lock);
}

static inline void
real_set_property (RBNode *node,
		   guint property_id,
		   GValue *value)
{
	GValue *old;

	if (property_id >= node->properties->len) {
		g_ptr_array_set_size (node->properties, property_id + 1);
	}

	old = g_ptr_array_index (node->properties, property_id);
	if (old != NULL) {
		g_value_unset (old);
		g_free (old);
	}

	g_ptr_array_index (node->properties, property_id) = value;
}

void
rb_node_set_property (RBNode *node,
		        guint property_id,
		        const GValue *value)
{
	GValue *new;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (property_id >= 0);
	g_return_if_fail (value != NULL);

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->lock);

	new = g_new0 (GValue, 1);
	g_value_init (new, G_VALUE_TYPE (value));
	g_value_copy (value, new);

	real_set_property (node, property_id, new);

	write_lock_to_read_lock (node);

	g_hash_table_foreach (node->parents,
			      (GHFunc) child_changed,
			      node);

	g_static_rw_lock_reader_unlock (node->lock);

	unlock_gdk ();
}

gboolean
rb_node_get_property (RBNode *node,
		        guint property_id,
		        GValue *value)
{
	GValue *ret;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (property_id >= 0, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	g_static_rw_lock_reader_lock (node->lock);

	if (property_id >= node->properties->len) {
		g_static_rw_lock_reader_unlock (node->lock);
		return FALSE;
	}

	ret = g_ptr_array_index (node->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->lock);
		return FALSE;
	}

	g_value_init (value, G_VALUE_TYPE (ret));
	g_value_copy (ret, value);

	g_static_rw_lock_reader_unlock (node->lock);

	return TRUE;
}

#define DEFINE_GETTER(NAME, TYPE, DEFAULT)				     \
TYPE rb_node_get_property_ ## NAME (RBNode *node, guint property_id) \
{ \
	GValue *ret; \
	TYPE retval; \
	g_return_val_if_fail (RB_IS_NODE (node), DEFAULT); \
	g_return_val_if_fail (property_id >= 0, DEFAULT); \
	g_static_rw_lock_reader_lock (node->lock); \
	if (property_id >= node->properties->len) { \
		g_static_rw_lock_reader_unlock (node->lock); \
		return DEFAULT; \
	} \
	ret = g_ptr_array_index (node->properties, property_id); \
	if (ret == NULL) { \
		g_static_rw_lock_reader_unlock (node->lock); \
		return DEFAULT; \
	} \
	retval = g_value_get_ ## NAME (ret); \
	g_static_rw_lock_reader_unlock (node->lock); \
	return retval; \
}

DEFINE_GETTER(string, const char *, NULL);
DEFINE_GETTER(boolean, gboolean, FALSE);
DEFINE_GETTER(pointer, gpointer, NULL);
DEFINE_GETTER(long, long, 0);
DEFINE_GETTER(int, int, 0);
DEFINE_GETTER(double, double, 0);
DEFINE_GETTER(float, float, 0);

static void
save_parent (gulong id,
	     RBNodeParent *node_info,
	     xmlNodePtr xml_node)
{
	xmlNodePtr parent_xml_node;
	char *xml;

	parent_xml_node = xmlNewChild (xml_node, NULL, "parent", NULL);

	g_static_rw_lock_reader_lock (node_info->node->lock);

	xml = g_strdup_printf ("%ld", node_info->node->id);
	xmlSetProp (parent_xml_node, "id", xml);
	g_free (xml);

	g_static_rw_lock_reader_unlock (node_info->node->lock);
}

void
rb_node_save_to_xml (RBNode *node,
		       xmlNodePtr parent_xml_node)
{
	xmlNodePtr xml_node;
	char *xml;
	char xml_buf [G_ASCII_DTOSTR_BUF_SIZE];
	guint i;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (parent_xml_node != NULL);

	g_static_rw_lock_reader_lock (node->lock);

	xml_node = xmlNewChild (parent_xml_node, NULL, "node", NULL);

	xml = g_strdup_printf ("%ld", node->id);
	xmlSetProp (xml_node, "id", xml);
	g_free (xml);

	for (i = 0; i < node->properties->len; i++) {
		GValue *value;
		xmlNodePtr value_xml_node;

		value = g_ptr_array_index (node->properties, i);
		if (value == NULL)
			continue;

		value_xml_node = xmlNewChild (xml_node, NULL, "property", NULL);

		xml = g_strdup_printf ("%d", i);
		xmlSetProp (value_xml_node, "id", xml);
		g_free (xml);

		xmlSetProp (value_xml_node, "value_type", g_type_name (G_VALUE_TYPE (value)));

		switch (G_VALUE_TYPE (value))
		{
		case G_TYPE_STRING:
			xml = xmlEncodeEntitiesReentrant (NULL,
							  g_value_get_string (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_BOOLEAN:
			xml = g_strdup_printf ("%d", g_value_get_boolean (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_INT:
			xml = g_strdup_printf ("%d", g_value_get_int (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_LONG:
			xml = g_strdup_printf ("%ld", g_value_get_long (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_FLOAT:
			g_ascii_dtostr (xml_buf, sizeof (xml_buf), 
					g_value_get_float (value));
			xmlNodeSetContent (value_xml_node, xml_buf);
			break;
		case G_TYPE_DOUBLE:
			g_ascii_dtostr (xml_buf, sizeof (xml_buf),
					g_value_get_double (value));
			xmlNodeSetContent (value_xml_node, xml_buf);
			break;
		case G_TYPE_POINTER:
		{
			gpointer obj = g_value_get_pointer (value);

			if (RB_IS_GLIST_WRAPPER (obj)) {
				RBGListWrapper *listwrap = RB_GLIST_WRAPPER (obj);
				GType obj_type = G_OBJECT_TYPE (obj);
				GList *cur;
				
				xmlSetProp (value_xml_node, "object_type", g_type_name (obj_type));

				for (cur = rb_glist_wrapper_get_list (listwrap); cur; cur = g_list_next (cur)) {
					xmlNodePtr subnode = xmlNewChild (value_xml_node, NULL, "entry", NULL);
					/* Assume the entries in a list are strings for now */
					xml = xmlEncodeEntitiesReentrant (NULL, (char *) cur->data);
					xmlNodeSetContent (subnode, xml);
					g_free (xml);
				}
			} else {
				/* Assume it's a node.  */
				RBNode *prop_node;

				prop_node = obj;

				g_assert (prop_node != NULL);

				g_static_rw_lock_reader_lock (prop_node->lock);

				xml = g_strdup_printf ("%ld", prop_node->id);
				xmlNodeSetContent (value_xml_node, xml);
				g_free (xml);

				g_static_rw_lock_reader_unlock (prop_node->lock);
			}
		}
		break;
		default:
			g_assert_not_reached ();
			break;
		}
	}

	g_hash_table_foreach (node->parents,
			      (GHFunc) save_parent,
			      xml_node);

	g_static_rw_lock_reader_unlock (node->lock);
}

static inline void
real_add_child (RBNode *node,
		RBNode *child)
{
	RBNodeParent *node_info;

	if (g_hash_table_lookup (child->parents,
				 GINT_TO_POINTER (node->id)) != NULL) {
		return;
	}

	g_ptr_array_add (node->children, child);

	node_info = g_new0 (RBNodeParent, 1);
	node_info->node  = node;
	node_info->index = node->children->len - 1;

	g_hash_table_insert (child->parents,
			     GINT_TO_POINTER (node->id),
			     node_info);
}

/* this function assumes it's safe to not lock anything while loading,
 * this is at least true for the case where we're loading the library xml file
 * from the main loop */
RBNode *
rb_node_new_from_xml (RBNodeDb *db, xmlNodePtr xml_node)
{
	RBNode *node;
	xmlNodePtr xml_child;
	char *xml;
	long id;

	g_return_val_if_fail (RB_IS_NODE_DB (db), NULL);
	g_return_val_if_fail (xml_node != NULL, NULL);

	xml = xmlGetProp (xml_node, "id");
	if (xml == NULL)
		return NULL;
	id = atol (xml);
	g_free (xml);

	node = rb_node_new_with_id (db, id);

	for (xml_child = xml_node->children; xml_child != NULL; xml_child = xml_child->next) {
		if (strcmp (xml_child->name, "parent") == 0) {
			RBNode *parent;
			long parent_id;

			xml = xmlGetProp (xml_child, "id");
			g_assert (xml != NULL);
			parent_id = atol (xml);
			g_free (xml);

			parent = rb_node_db_get_node_from_id (db, parent_id);

			if (parent != NULL)
			{
				real_add_child (parent, node);

				rb_node_emit_signal (parent, RB_NODE_CHILD_ADDED, node);
			}
		} else if (strcmp (xml_child->name, "property") == 0) {
			GType value_type;
			GValue *value;
			int property_id;

			xml = xmlGetProp (xml_child, "id");
			property_id = atoi (xml);
			g_free (xml);

			xml = xmlGetProp (xml_child, "value_type");
			value_type = g_type_from_name (xml);
			g_free (xml);

			xml = xmlNodeGetContent (xml_child);
			value = g_new0 (GValue, 1);
			g_value_init (value, value_type);

			switch (value_type)
			{
			case G_TYPE_STRING:
				g_value_set_string (value, xml);
				break;
			case G_TYPE_INT:
				g_value_set_int (value, atoi (xml));
				break;
			case G_TYPE_BOOLEAN:
				g_value_set_boolean (value, atoi (xml));
				break;
			case G_TYPE_LONG:
				g_value_set_long (value, atol (xml));
				break;
			case G_TYPE_FLOAT:
				g_value_set_float (value, g_ascii_strtod (xml, NULL));
				break;
			case G_TYPE_DOUBLE:
				g_value_set_double (value, g_ascii_strtod (xml, NULL));
				break;
			case G_TYPE_POINTER:
			{
				char *typestring;
				GType obj_type;

				typestring = xmlGetProp (xml_child, "object_type");
				if (typestring)
					obj_type = g_type_from_name (typestring);

				/* Since we don't have method
				 * reflection in GObject, there's no
				 * way to find a general deserialize
				 * method, even if we know the class;
				 * we could do something truly evil
				 * using dlopen(), but let's not think
				 * about that. */
				if (typestring && g_type_is_a (obj_type, RB_TYPE_GLIST_WRAPPER)) {
					GList *newlist = NULL;
					xmlNodePtr list_child;
					RBGListWrapper *listwrapper = rb_glist_wrapper_new (NULL);

					for (list_child = xml_child->children; list_child; list_child = list_child->next) {
						if (xmlNodeIsText(list_child))
							continue;
						newlist = g_list_prepend (newlist, xmlNodeGetContent (list_child));
					}

					rb_glist_wrapper_set_list (listwrapper, newlist);

					g_value_set_pointer (value, listwrapper);

				} else {
					/* Assume it's a node. */
					RBNode *property_node;

					property_node = rb_node_db_get_node_from_id (db, atol (xml));

					g_value_set_pointer (value, property_node);

				}
				g_free (typestring);
			}
			break;
			default:
				g_assert_not_reached ();
				break;
			}

			real_set_property (node, property_id, value);

			g_free (xml);
		}
	}

	rb_node_emit_signal (node, RB_NODE_RESTORED);

	return node;
}

void
rb_node_add_child (RBNode *node,
		   RBNode *child)
{
	lock_gdk ();

	g_return_if_fail (RB_IS_NODE (node));
	
	g_static_rw_lock_writer_lock (node->lock);
	g_static_rw_lock_writer_lock (child->lock);

	real_add_child (node, child);

	write_lock_to_read_lock (node);
	write_lock_to_read_lock (child);

	rb_node_emit_signal (node, RB_NODE_CHILD_ADDED, child);

	g_static_rw_lock_reader_unlock (node->lock);
	g_static_rw_lock_reader_unlock (child->lock);

	unlock_gdk ();
}

void
rb_node_remove_child (RBNode *node,
		        RBNode *child)
{
	lock_gdk ();

	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_writer_lock (node->lock);
	g_static_rw_lock_writer_lock (child->lock);

	real_remove_child (node, child, TRUE, TRUE);

	g_static_rw_lock_writer_unlock (node->lock);
	g_static_rw_lock_writer_unlock (child->lock);

	unlock_gdk ();
}

gboolean
rb_node_has_child (RBNode *node,
		     RBNode *child)
{
	gboolean ret;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	
	g_static_rw_lock_reader_lock (node->lock);
	g_static_rw_lock_reader_lock (child->lock);

	ret = (g_hash_table_lookup (child->parents,
				    GINT_TO_POINTER (node->id)) != NULL);

	g_static_rw_lock_reader_unlock (node->lock);
	g_static_rw_lock_reader_unlock (child->lock);

	return ret;
}

static int
rb_node_real_get_child_index (RBNode *node,
			   RBNode *child)
{
	RBNodeParent *node_info;
	int ret;

	node_info = g_hash_table_lookup (child->parents,
					 GINT_TO_POINTER (node->id));

	if (node_info == NULL)
		return -1;

	ret = node_info->index;

	return ret;
}

void
rb_node_sort_children (RBNode *node,
			 GCompareFunc compare_func)
{
	GPtrArray *newkids;
	int i, *new_order;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (compare_func != NULL);

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->lock);

	newkids = g_ptr_array_new ();
	g_ptr_array_set_size (newkids, node->children->len);

	/* dup the array */
	for (i = 0; i < node->children->len; i++)
	{
		g_ptr_array_index (newkids, i) = g_ptr_array_index (node->children, i);
	}

	g_ptr_array_sort (newkids, compare_func);

	new_order = g_new (int, newkids->len);
	memset (new_order, -1, sizeof (int) * newkids->len);

	for (i = 0; i < newkids->len; i++)
	{
		RBNodeParent *node_info;
		RBNode *child;

		child = g_ptr_array_index (newkids, i);
		new_order[rb_node_real_get_child_index (node, child)] = i;
		node_info = g_hash_table_lookup (child->parents,
					         GINT_TO_POINTER (node->id));
		node_info->index = i;
	}

	g_ptr_array_free (node->children, FALSE);
	node->children = newkids;

	write_lock_to_read_lock (node);

	rb_node_emit_signal (node, RB_NODE_CHILDREN_REORDERED, new_order);

	g_free (new_order);

	g_static_rw_lock_reader_unlock (node->lock);

	unlock_gdk ();
}

void
rb_node_reorder_children (RBNode *node,
			    int *new_order)
{
	GPtrArray *newkids;
	int i;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (new_order != NULL);

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->lock);

	newkids = g_ptr_array_new ();
	g_ptr_array_set_size (newkids, node->children->len);

	for (i = 0; i < node->children->len; i++) {
		RBNode *child;
		RBNodeParent *node_info;

		child = g_ptr_array_index (node->children, i);

		g_ptr_array_index (newkids, new_order[i]) = child;

		node_info = g_hash_table_lookup (child->parents,
					         GINT_TO_POINTER (node->id));
		node_info->index = new_order[i];
	}

	g_ptr_array_free (node->children, FALSE);
	node->children = newkids;

	write_lock_to_read_lock (node);

	rb_node_emit_signal (node, RB_NODE_CHILDREN_REORDERED, new_order);

	g_static_rw_lock_reader_unlock (node->lock);

	unlock_gdk ();
}

GPtrArray *
rb_node_get_children (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	g_static_rw_lock_reader_lock (node->lock);

	return node->children;
}

int
rb_node_get_n_children (RBNode *node)
{
	int ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->lock);

	ret = node->children->len;

	g_static_rw_lock_reader_unlock (node->lock);

	return ret;
}

RBNode *
rb_node_get_nth_child (RBNode *node,
		         guint n)
{
	RBNode *ret;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (n >= 0, NULL);

	g_static_rw_lock_reader_lock (node->lock);

	if (n < node->children->len) {
		ret = g_ptr_array_index (node->children, n);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->lock);

	return ret;
}

static inline int
get_child_index_real (RBNode *node,
		      RBNode *child)
{
	RBNodeParent *node_info;

	node_info = g_hash_table_lookup (child->parents,
					 GINT_TO_POINTER (node->id));

	if (node_info == NULL)
		return -1;

	return node_info->index;
}


int
rb_node_get_child_index (RBNode *node,
			   RBNode *child)
{
	int ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (RB_IS_NODE (child), -1);

	g_static_rw_lock_reader_lock (node->lock);
	g_static_rw_lock_reader_lock (child->lock);

	ret = rb_node_real_get_child_index (node, child);

	g_static_rw_lock_reader_unlock (node->lock);
	g_static_rw_lock_reader_unlock (child->lock);

	return ret;
}

RBNode *
rb_node_get_next_child (RBNode *node,
			  RBNode *child)
{
	RBNode *ret;
	guint idx;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (RB_IS_NODE (child), NULL);
	
	g_static_rw_lock_reader_lock (node->lock);
	g_static_rw_lock_reader_lock (child->lock);

	idx = get_child_index_real (node, child);

	if ((idx + 1) < node->children->len) {
		ret = g_ptr_array_index (node->children, idx + 1);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->lock);
	g_static_rw_lock_reader_unlock (child->lock);

	return ret;
}

RBNode *
rb_node_get_previous_child (RBNode *node,
			      RBNode *child)
{
	RBNode *ret;
	int idx;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (RB_IS_NODE (child), NULL);
	
	g_static_rw_lock_reader_lock (node->lock);
	g_static_rw_lock_reader_lock (child->lock);

	idx = get_child_index_real (node, child);

	if ((idx - 1) >= 0) {
		ret = g_ptr_array_index (node->children, idx - 1);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->lock);
	g_static_rw_lock_reader_unlock (child->lock);

	return ret;
}

int
rb_node_signal_connect_object (RBNode *node,
				 RBNodeSignalType type,
				 RBNodeCallback callback,
				 GObject *object)
{
	RBNodeSignalData *signal_data;
	int ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	signal_data = g_new0 (RBNodeSignalData, 1);
	signal_data->node = node;
	signal_data->id = node->signal_id;
	signal_data->callback = callback;
	signal_data->type = type;
	signal_data->data = object;

	g_hash_table_insert (node->signals,
			     GINT_TO_POINTER (node->signal_id),
			     signal_data);
	if (object)
	{
		g_object_weak_ref (object,
				   (GWeakNotify)signal_object_weak_notify,
				   signal_data);
	}

	ret = node->signal_id;
	node->signal_id++;

	return ret;
}

void
rb_node_signal_disconnect (RBNode *node,
			     int signal_id)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_hash_table_remove (node->signals,
			     GINT_TO_POINTER (signal_id));
}

void
rb_node_update_play_statistics (RBNode *node)
{
	char *time_string;
	time_t now;
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, rb_node_get_property_int (node,
							   RB_NODE_PROP_PLAY_COUNT) + 1);

	/* Increment current play count */
	rb_node_set_property (node,
			      RB_NODE_PROP_PLAY_COUNT,
			      &value);
	g_value_unset (&value);
	
	/* Reset the last played time */
	time (&now);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, now);
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED,
			      &value);
	g_value_unset (&value);

	time_string = eel_strdup_strftime (_("%Y-%m-%d %H:%M"), localtime (&now));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, time_string);
	g_free (time_string);
	rb_node_set_property (node,
			      RB_NODE_PROP_LAST_PLAYED_STR,
			      &value);
	g_value_unset (&value);
}
