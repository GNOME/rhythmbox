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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <gdk/gdk.h>
#include <time.h>

#include "rb-node.h"
#include "rb-debug.h"
#include "rb-enums.h"
#include "rb-marshal.h"
#include "rb-thread-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-glist-wrapper.h"

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
static inline void id_factory_set_to (long new_factory_pos);
static inline void real_set_property (RBNode *node,
		                      int property_id,
		                      GValue *value);
static inline void real_remove_child (RBNode *node,
		                      RBNode *child,
			              gboolean remove_from_parent,
			              gboolean remove_from_child);
static inline void real_add_child (RBNode *node,
		                   RBNode *child);
static inline void read_lock_to_write_lock (RBNode *node);
static inline void write_lock_to_read_lock (RBNode *node);
static inline void lock_gdk (void);
static inline void unlock_gdk (void);
static inline RBNode *node_from_id_real (long id);
static inline int get_child_index_real (RBNode *node,
		                        RBNode *child);

typedef struct
{
	RBNode *node;
	int index;
} RBNodeParent;

struct RBNodePrivate
{
	GStaticRWLock *lock;

	int ref_count;

	long id;

	GPtrArray *properties;

	GHashTable *parents;
	GPtrArray *children;

	RBNode *clone_of;
	GList *clones;
};

enum
{
	PROP_0,
	PROP_ID
};

enum
{
	DESTROYED,
	RESTORED,
	CHILD_ADDED,
	CHILD_CHANGED,
	CHILD_REMOVED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_node_signals[LAST_SIGNAL] = { 0 };

static GMutex *id_factory_lock = NULL;
static long id_factory = 0;

static GStaticRWLock *id_to_node_lock = NULL;
static GPtrArray *id_to_node;

GType
rb_node_get_type (void)
{
	static GType rb_node_type = 0;

	if (rb_node_type == 0) {
		static const GTypeInfo our_info = {
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

	rb_node_signals[DESTROYED] =
		g_signal_new ("destroyed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, destroyed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_node_signals[RESTORED] =
		g_signal_new ("restored",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, restored),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

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
	rb_node_signals[CHILD_REMOVED] =
		g_signal_new ("child_removed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBNodeClass, child_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1,
			      RB_TYPE_NODE);
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
rb_node_init (RBNode *node)
{
	node->priv = g_new0 (RBNodePrivate, 1);

	node->priv->lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (node->priv->lock);

	node->priv->ref_count = 0;

	node->priv->id = -1;

	node->priv->parents = g_hash_table_new_full (int_hash,
						     int_equal,
						     NULL,
						     g_free);

	node->priv->children = g_ptr_array_new ();

	node->priv->properties = g_ptr_array_new ();
}

static void
rb_node_finalize (GObject *object)
{
	RBNode *node;
	int i;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_NODE (object));

	node = RB_NODE (object);

	g_return_if_fail (node->priv != NULL);

	for (i = 0; i < node->priv->properties->len; i++) {
		GValue *val;

		val = g_ptr_array_index (node->priv->properties, i);

		if (val != NULL) {
			g_value_unset (val);
			g_free (val);
		}
	}
	g_ptr_array_free (node->priv->properties, FALSE);

	g_hash_table_destroy (node->priv->parents);

	g_ptr_array_free (node->priv->children, FALSE);

	g_static_rw_lock_free (node->priv->lock);

	g_free (node->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
remove_child (long id,
	      RBNodeParent *node_info,
	      RBNode *node)
{
	g_static_rw_lock_writer_lock (node_info->node->priv->lock);

	real_remove_child (node_info->node, node, TRUE, FALSE);

	g_static_rw_lock_writer_unlock (node_info->node->priv->lock);
}

static void
rb_node_dispose (GObject *object)
{
	RBNode *node;
	GList *l;
	int i;

	node = RB_NODE (object);

	/* remove from id table */
	g_static_rw_lock_writer_lock (id_to_node_lock);

	g_ptr_array_index (id_to_node, node->priv->id) = NULL;

	g_static_rw_lock_writer_unlock (id_to_node_lock);

	lock_gdk ();

	/* remove from DAG */
	g_hash_table_foreach (node->priv->parents,
			      (GHFunc) remove_child,
			      node);

	for (i = 0; i < node->priv->children->len; i++) {
		RBNode *child;

		child = g_ptr_array_index (node->priv->children, i);

		g_static_rw_lock_writer_lock (child->priv->lock);

		real_remove_child (node, child, FALSE, TRUE);

		g_static_rw_lock_writer_unlock (child->priv->lock);
	}

	if (node->priv->clone_of != NULL) {
		RBNode *peer;

		peer = node->priv->clone_of;

		g_static_rw_lock_writer_lock (peer->priv->lock);
		peer->priv->clones = g_list_remove (peer->priv->clones, node);
		g_static_rw_lock_writer_unlock (peer->priv->lock);
	}

	for (l = node->priv->clones; l != NULL; l = g_list_next (l)) {
		RBNode *peer;

		/* kill of the clones */
		peer = l->data;

		g_static_rw_lock_writer_lock (peer->priv->lock);

		peer->priv->ref_count = 0;

		g_object_unref (G_OBJECT (peer));
	}

	g_static_rw_lock_writer_unlock (node->priv->lock);

	g_signal_emit (G_OBJECT (node), rb_node_signals[DESTROYED], 0);

	unlock_gdk ();

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

		g_static_rw_lock_writer_lock (id_to_node_lock);

		/* resize array if needed */
		if (node->priv->id >= id_to_node->len)
			g_ptr_array_set_size (id_to_node, node->priv->id + 1);

		g_ptr_array_index (id_to_node, node->priv->id) = node;

		g_static_rw_lock_writer_unlock (id_to_node_lock);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBNode *
rb_node_new (void)
{
	RBNode *node;

	node = RB_NODE (g_object_new (RB_TYPE_NODE,
				      "id", rb_node_new_id (),
				      NULL));

	g_return_val_if_fail (node->priv != NULL, NULL);

	return node;
}

long
rb_node_get_id (RBNode *node)
{
	long ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = node->priv->id;

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

static inline RBNode *
node_from_id_real (long id)
{
	RBNode *ret = NULL;

	if (id < id_to_node->len)
		ret = g_ptr_array_index (id_to_node, id);;

	return ret;
}

RBNode *
rb_node_get_from_id (long id)
{
	RBNode *ret = NULL;

	g_return_val_if_fail (id > 0, NULL);

	g_static_rw_lock_reader_lock (id_to_node_lock);

	ret = node_from_id_real (id);

	g_static_rw_lock_reader_unlock (id_to_node_lock);

	return ret;
}

void
rb_node_ref (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_writer_lock (node->priv->lock);

	node->priv->ref_count++;

	g_static_rw_lock_writer_unlock (node->priv->lock);
}

void
rb_node_unref (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_writer_lock (node->priv->lock);

	node->priv->ref_count--;

	if (node->priv->ref_count <= 0) {
		g_object_unref (G_OBJECT (node));
	} else {
		g_static_rw_lock_writer_unlock (node->priv->lock);
	}
}

void
rb_node_freeze (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_reader_lock (node->priv->lock);
}

void
rb_node_thaw (RBNode *node)
{
	g_return_if_fail (RB_IS_NODE (node));

	g_static_rw_lock_reader_unlock (node->priv->lock);
}

static void
child_changed (long id,
	       RBNodeParent *node_info,
	       RBNode *node)
{
	g_static_rw_lock_reader_lock (node_info->node->priv->lock);

	g_signal_emit (G_OBJECT (node_info->node), rb_node_signals[CHILD_CHANGED], 0, node);

	g_static_rw_lock_reader_unlock (node_info->node->priv->lock);
}

static inline void
real_set_property (RBNode *node,
		   int property_id,
		   GValue *value)
{
	GValue *old;

	if (property_id >= node->priv->properties->len) {
		g_ptr_array_set_size (node->priv->properties, property_id + 1);
	}

	old = g_ptr_array_index (node->priv->properties, property_id);
	if (old != NULL) {
		g_value_unset (old);
		g_free (old);
	}

	g_ptr_array_index (node->priv->properties, property_id) = value;
}

void
rb_node_set_property (RBNode *node,
		      int property_id,
		      const GValue *value)
{
	GValue *new;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (property_id >= 0);
	g_return_if_fail (value != NULL);

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->priv->lock);

	new = g_new0 (GValue, 1);
	g_value_init (new, G_VALUE_TYPE (value));
	g_value_copy (value, new);

	real_set_property (node, property_id, new);

	write_lock_to_read_lock (node);

	g_hash_table_foreach (node->priv->parents,
			      (GHFunc) child_changed,
			      node);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	unlock_gdk ();
}

gboolean
rb_node_get_property (RBNode *node,
		      int property_id,
		      GValue *value)
{
	GValue *ret;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (property_id >= 0, FALSE);
	g_return_val_if_fail (value != NULL, FALSE);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return FALSE;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return FALSE;
	}

	g_value_init (value, G_VALUE_TYPE (ret));
	g_value_copy (ret, value);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return TRUE;
}

const char *
rb_node_get_property_string (RBNode *node,
			     int property_id)
{
	GValue *ret;
	const char *retval;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (property_id >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	retval = g_value_get_string (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

gboolean
rb_node_get_property_boolean (RBNode *node,
			      int property_id)
{
	GValue *ret;
	gboolean retval;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (property_id >= 0, FALSE);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return FALSE;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return FALSE;
	}

	retval = g_value_get_boolean (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

long
rb_node_get_property_long (RBNode *node,
			   int property_id)
{
	GValue *ret;
	long retval;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (property_id >= 0, -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	retval = g_value_get_long (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

int
rb_node_get_property_int (RBNode *node,
			  int property_id)
{
	GValue *ret;
	int retval;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (property_id >= 0, -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	retval = g_value_get_int (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

double
rb_node_get_property_double (RBNode *node,
			     int property_id)
{
	GValue *ret;
	double retval;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (property_id >= 0, -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	retval = g_value_get_double (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

float
rb_node_get_property_float (RBNode *node,
			    int property_id)
{
	GValue *ret;
	float retval;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (property_id >= 0, -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return -1;
	}

	retval = g_value_get_float (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

RBNode *
rb_node_get_property_node (RBNode *node,
			   int property_id)
{
	GValue *ret;
	RBNode *retval;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (property_id >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	retval = g_value_get_pointer (ret);

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

GObject *
rb_node_get_property_object (RBNode *node,
			     int property_id)
{
	GValue *ret;
	GObject *retval;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (property_id >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return NULL;
	}

	retval = G_OBJECT (g_value_get_pointer (ret));

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

char *
rb_node_get_property_time (RBNode *node,
			   int property_id)
{
	GValue *ret;
	long mtime;
	char *retval;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (property_id >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (property_id >= node->priv->properties->len) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return g_strdup (_("Never"));
	}

	ret = g_ptr_array_index (node->priv->properties, property_id);
	if (ret == NULL) {
		g_static_rw_lock_reader_unlock (node->priv->lock);
		return g_strdup (_("Never"));
	}

	mtime = g_value_get_long (ret);

	if (retval >= 0) {
		GDate *now, *file_date;
		guint32 file_date_age;
		const char *format = NULL;

		now = g_date_new ();
		g_date_set_time (now, time (NULL));

		file_date = g_date_new ();
		g_date_set_time (file_date, mtime);

		file_date_age = (g_date_get_julian (now) - g_date_get_julian (file_date));

		g_date_free (file_date);
		g_date_free (now);

		if (file_date_age == 0) {
			format = _("Today at %-H:%M");
		} else if (file_date_age == 1) {
			format = _("Yesterday at %-H:%M");
		} else {
			format = _("%A, %B %-d %Y at %-H:%M");
		}

		retval = eel_strdup_strftime (format, localtime (&mtime));
	} else {
		retval = g_strdup (_("Never"));
	}

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return retval;
}

static void
save_parent (long id,
	     RBNodeParent *node_info,
	     xmlNodePtr xml_node)
{
	xmlNodePtr parent_xml_node;
	char *xml;

	parent_xml_node = xmlNewChild (xml_node, NULL, "parent", NULL);

	g_static_rw_lock_reader_lock (node_info->node->priv->lock);

	xml = g_strdup_printf ("%ld", node_info->node->priv->id);
	xmlSetProp (parent_xml_node, "id", xml);
	g_free (xml);

	g_static_rw_lock_reader_unlock (node_info->node->priv->lock);
}

void
rb_node_save_to_xml (RBNode *node,
		     xmlNodePtr parent_xml_node)
{
	xmlNodePtr xml_node;
	char *xml;
	int i;

	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (parent_xml_node != NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	xml_node = xmlNewChild (parent_xml_node, NULL, "node", NULL);

	xml = g_strdup_printf ("%ld", node->priv->id);
	xmlSetProp (xml_node, "id", xml);
	g_free (xml);

	xmlSetProp (xml_node, "type", G_OBJECT_TYPE_NAME (node));

	for (i = 0; i < node->priv->properties->len; i++) {
		GValue *value;
		xmlNodePtr value_xml_node;

		value = g_ptr_array_index (node->priv->properties, i);
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
			xml = g_strdup_printf ("%f", g_value_get_float (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_DOUBLE:
			xml = g_strdup_printf ("%f", g_value_get_double (value));
			xmlNodeSetContent (value_xml_node, xml);
			g_free (xml);
			break;
		case G_TYPE_POINTER:
		{
			GObject *obj = g_value_get_pointer (value);
			GType obj_type = G_OBJECT_TYPE (obj);

			xmlSetProp (value_xml_node, "object_type", g_type_name (obj_type));

			if (obj_type == RB_TYPE_GLIST_WRAPPER) {
				RBGListWrapper *listwrap = RB_GLIST_WRAPPER (obj);
				GList *cur;

				for (cur = rb_glist_wrapper_get_list (listwrap); cur; cur = g_list_next (cur)) {
					xmlNodePtr subnode = xmlNewChild (value_xml_node, NULL, "entry", NULL);
					/* Assume the entries in a list are strings for now */
					xml = xmlEncodeEntitiesReentrant (NULL, (char *) cur->data);
					xmlNodeSetContent (subnode, xml);
					g_free (xml);
				}

				break;
			} else if (obj_type == RB_TYPE_NODE) {
				RBNode *prop_node;

				prop_node = RB_NODE (obj);

				g_assert (prop_node != NULL);

				g_static_rw_lock_reader_lock (prop_node->priv->lock);

				xml = g_strdup_printf ("%ld", prop_node->priv->id);
				xmlNodeSetContent (value_xml_node, xml);
				g_free (xml);

				g_static_rw_lock_reader_unlock (prop_node->priv->lock);

				break;
			} else {
				g_assert_not_reached ();
				break;
			}
		}
		default:
			g_assert_not_reached ();
			break;
		}
	}

	g_hash_table_foreach (node->priv->parents,
			      (GHFunc) save_parent,
			      xml_node);

	g_static_rw_lock_reader_unlock (node->priv->lock);
}

/* this function assumes it's safe to not lock anything while loading,
 * this is at least true for the case where we're loading the library xml file
 * from the main loop */
RBNode *
rb_node_new_from_xml (xmlNodePtr xml_node)
{
	RBNode *node;
	xmlNodePtr xml_child;
	char *xml;
	long id;
	GType type;

	g_return_val_if_fail (xml_node != NULL, NULL);

	xml = xmlGetProp (xml_node, "id");
	if (xml == NULL)
		return NULL;
	id = atol (xml);
	g_free (xml);

	id_factory_set_to (id);

	xml = xmlGetProp (xml_node, "type");
	type = g_type_from_name (xml);
	g_free (xml);

	node = RB_NODE (g_object_new (type,
				      "id", id,
				      NULL));

	g_return_val_if_fail (node->priv != NULL, NULL);

	for (xml_child = xml_node->children; xml_child != NULL; xml_child = xml_child->next) {
		if (strcmp (xml_child->name, "parent") == 0) {
			RBNode *parent;
			long parent_id;

			xml = xmlGetProp (xml_child, "id");
			g_assert (xml != NULL);
			parent_id = atol (xml);
			g_free (xml);

			parent = node_from_id_real (parent_id);

			if (parent != NULL)
			{
				real_add_child (parent, node);

				g_signal_emit (G_OBJECT (parent), rb_node_signals[CHILD_ADDED],
					       0, node);
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
				g_value_set_float (value, atof (xml));
				break;
			case G_TYPE_DOUBLE:
				g_value_set_double (value, atof (xml));
				break;
			case G_TYPE_POINTER:
			{
				char *typestring;
				GType obj_type;

				typestring = xmlGetProp (xml_child, "object_type");
				obj_type = g_type_from_name (typestring);
				g_free (typestring);

				/* Since we don't have method
				 * reflection in GObject, there's no
				 * way to find a general deserialize
				 * method, even if we know the class;
				 * we could do something truly evil
				 * using dlopen(), but let's not think
				 * about that. */
				if (obj_type == RB_TYPE_GLIST_WRAPPER) {
					GList *newlist = NULL;
					xmlNodePtr list_child;
					RBGListWrapper *listwrapper = rb_glist_wrapper_new (NULL);

					/* Free the unneeded string allocated above */
					g_free (xml);

					for (list_child = xml_child; list_child; list_child = list_child->next)
						newlist = g_list_prepend (newlist, xmlNodeGetContent (xml_child));

					rb_glist_wrapper_set_list (listwrapper, newlist);

					g_value_set_pointer (value, listwrapper);

					break;
				} else if (obj_type == RB_TYPE_NODE) {
					RBNode *property_node;

					property_node = node_from_id_real (atol (xml));

					g_value_set_pointer (value, property_node);

					break;
				} else {
					g_assert_not_reached ();
					break;
				}

				break;
			}
			default:
				g_assert_not_reached ();
				break;
			}

			real_set_property (node, property_id, value);

			g_free (xml);
		}
	}

	g_signal_emit (G_OBJECT (node), rb_node_signals[RESTORED], 0);

	return node;
}

static inline void
real_add_child (RBNode *node,
		RBNode *child)
{
	RBNodeParent *node_info;

	if (g_hash_table_lookup (child->priv->parents,
				 GINT_TO_POINTER (node->priv->id)) != NULL) {
		return;
	}

	g_ptr_array_add (node->priv->children, child);

	node_info = g_new0 (RBNodeParent, 1);
	node_info->node  = node;
	node_info->index = node->priv->children->len - 1;

	g_hash_table_insert (child->priv->parents,
			     GINT_TO_POINTER (node->priv->id),
			     node_info);
}

void
rb_node_add_child (RBNode *node,
		   RBNode *child)
{
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->priv->lock);
	g_static_rw_lock_writer_lock (child->priv->lock);

	real_add_child (node, child);

	write_lock_to_read_lock (node);
	write_lock_to_read_lock (child);

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_ADDED], 0, child);

	g_static_rw_lock_reader_unlock (node->priv->lock);
	g_static_rw_lock_reader_unlock (child->priv->lock);

	unlock_gdk ();
}

static inline void
real_remove_child (RBNode *node,
		   RBNode *child,
		   gboolean remove_from_parent,
		   gboolean remove_from_child)
{
	RBNodeParent *node_info;

	write_lock_to_read_lock (node);
	write_lock_to_read_lock (child);

	g_signal_emit (G_OBJECT (node), rb_node_signals[CHILD_REMOVED], 0, child);

	read_lock_to_write_lock (node);
	read_lock_to_write_lock (child);

	node_info = g_hash_table_lookup (child->priv->parents,
			                 GINT_TO_POINTER (node->priv->id));

	if (remove_from_parent) {
		int i;

		g_ptr_array_remove_index (node->priv->children,
					  node_info->index);

		/* correct indices on kids */
		for (i = node_info->index; i < node->priv->children->len; i++) {
			RBNode *borked_node;
			RBNodeParent *borked_node_info;

			borked_node = g_ptr_array_index (node->priv->children, i);

			g_static_rw_lock_writer_lock (borked_node->priv->lock);

			borked_node_info = g_hash_table_lookup (borked_node->priv->parents,
						                GINT_TO_POINTER (node->priv->id));
			borked_node_info->index--;

			g_static_rw_lock_writer_unlock (borked_node->priv->lock);
		}
	}

	if (remove_from_child) {
		g_hash_table_remove (child->priv->parents,
				     GINT_TO_POINTER (node->priv->id));
	}
}

void
rb_node_remove_child (RBNode *node,
		      RBNode *child)
{
	g_return_if_fail (RB_IS_NODE (node));
	g_return_if_fail (RB_IS_NODE (child));

	lock_gdk ();

	g_static_rw_lock_writer_lock (node->priv->lock);
	g_static_rw_lock_writer_lock (child->priv->lock);

	real_remove_child (node, child, TRUE, TRUE);

	g_static_rw_lock_writer_unlock (node->priv->lock);
	g_static_rw_lock_writer_unlock (child->priv->lock);

	unlock_gdk ();
}

gboolean
rb_node_has_child (RBNode *node,
		   RBNode *child)
{
	gboolean ret;

	g_return_val_if_fail (RB_IS_NODE (node), FALSE);
	g_return_val_if_fail (RB_IS_NODE (child), FALSE);

	g_static_rw_lock_reader_lock (node->priv->lock);
	g_static_rw_lock_reader_lock (child->priv->lock);

	ret = (g_hash_table_lookup (child->priv->parents,
				    GINT_TO_POINTER (node->priv->id)) != NULL);

	g_static_rw_lock_reader_unlock (node->priv->lock);
	g_static_rw_lock_reader_unlock (child->priv->lock);

	return ret;
}

GPtrArray *
rb_node_get_children (RBNode *node)
{
	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	return node->priv->children;
}

int
rb_node_get_n_children (RBNode *node)
{
	int ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = node->priv->children->len;

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

RBNode *
rb_node_get_nth_child (RBNode *node,
		       int n)
{
	RBNode *ret;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (n >= 0, NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	if (n < node->priv->children->len) {
		ret = g_ptr_array_index (node->priv->children, n);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

static inline int
get_child_index_real (RBNode *node,
		      RBNode *child)
{
	RBNodeParent *node_info;

	node_info = g_hash_table_lookup (child->priv->parents,
					 GINT_TO_POINTER (node->priv->id));

	if (node_info == NULL)
		return -1;

	return node_info->index;
}

int
rb_node_get_child_index (RBNode *node,
			 RBNode *child)
{
	RBNodeParent *node_info;
	int ret;

	g_return_val_if_fail (RB_IS_NODE (node), -1);
	g_return_val_if_fail (RB_IS_NODE (child), -1);

	g_static_rw_lock_reader_lock (node->priv->lock);
	g_static_rw_lock_reader_lock (child->priv->lock);

	node_info = g_hash_table_lookup (child->priv->parents,
					 GINT_TO_POINTER (node->priv->id));

	if (node_info == NULL)
		return -1;

	ret = node_info->index;

	g_static_rw_lock_reader_unlock (node->priv->lock);
	g_static_rw_lock_reader_unlock (child->priv->lock);

	return ret;
}

RBNode *
rb_node_get_next_child (RBNode *node,
			RBNode *child)
{
	RBNode *ret;
	int idx;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);
	g_return_val_if_fail (RB_IS_NODE (child), NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);
	g_static_rw_lock_reader_lock (child->priv->lock);

	idx = get_child_index_real (node, child);

	if ((idx + 1) < node->priv->children->len) {
		ret = g_ptr_array_index (node->priv->children, idx + 1);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->priv->lock);
	g_static_rw_lock_reader_unlock (child->priv->lock);

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

	g_static_rw_lock_reader_lock (node->priv->lock);
	g_static_rw_lock_reader_lock (child->priv->lock);

	idx = get_child_index_real (node, child);

	if ((idx - 1) >= 0) {
		ret = g_ptr_array_index (node->priv->children, idx - 1);
	} else {
		ret = NULL;
	}

	g_static_rw_lock_reader_unlock (node->priv->lock);
	g_static_rw_lock_reader_unlock (child->priv->lock);

	return ret;
}

/* mwuahahah the attack of the clones! */
RBNode *
rb_node_new_clone (RBNode *peer)
{
	RBNode *node;

	node = RB_NODE (g_object_new (RB_TYPE_NODE,
				      "id", rb_node_new_id (),
				      NULL));

	g_return_val_if_fail (node->priv != NULL, NULL);

	g_ptr_array_free (node->priv->properties, FALSE);
	node->priv->properties = peer->priv->properties;

	node->priv->clone_of = peer;

	g_static_rw_lock_writer_lock (peer->priv->lock);
	peer->priv->clones = g_list_append (peer->priv->clones, node);
	g_static_rw_lock_writer_unlock (peer->priv->lock);

	return node;
}

RBNode *
rb_node_clone_of (RBNode *node)
{
	RBNode *ret;

	g_return_val_if_fail (RB_IS_NODE (node), NULL);

	g_static_rw_lock_reader_lock (node->priv->lock);

	ret = node->priv->clone_of;

	g_static_rw_lock_reader_unlock (node->priv->lock);

	return ret;
}

void
rb_node_system_init (void)
{
	/* id to node */
	id_to_node = g_ptr_array_new ();

	id_to_node_lock = g_new0 (GStaticRWLock, 1);
	g_static_rw_lock_init (id_to_node_lock);

	/* id factory */
	id_factory = 0;
	id_factory_lock = g_mutex_new ();
}

void
rb_node_system_shutdown (void)
{
	g_ptr_array_free (id_to_node, FALSE);

	g_static_rw_lock_free (id_to_node_lock);

	g_mutex_free (id_factory_lock);
}

long
rb_node_new_id (void)
{
	long ret;

	g_mutex_lock (id_factory_lock);

	id_factory++;

	ret = id_factory;

	g_mutex_unlock (id_factory_lock);

	return ret;
}

static inline void
id_factory_set_to (long new_factory_pos)
{
	id_factory = new_factory_pos + 1;
}

/* evillish hacks to temporarily readlock->writelock and v.v. */
static inline void
write_lock_to_read_lock (RBNode *node)
{
	g_static_mutex_lock (&node->priv->lock->mutex);
	node->priv->lock->read_counter++;
	g_static_mutex_unlock (&node->priv->lock->mutex);

	g_static_rw_lock_writer_unlock (node->priv->lock);
}

static inline void
read_lock_to_write_lock (RBNode *node)
{
	g_static_mutex_lock (&node->priv->lock->mutex);
	node->priv->lock->read_counter--;
	g_static_mutex_unlock (&node->priv->lock->mutex);

	g_static_rw_lock_writer_lock (node->priv->lock);
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

void
rb_node_update_play_statistics (RBNode *node)
{
	char *play_count, *time_string;
	time_t now;
	GValue value = { 0, };

	g_return_if_fail (RB_IS_NODE (node));

	/* Increment current play count */
	play_count = (char *) rb_node_get_property_string (RB_NODE (node),
				                           RB_NODE_PROP_NUM_PLAYS);

	if (play_count != NULL)
		play_count = g_strdup_printf ("%ld", atol (play_count) + 1);
	else
		play_count = g_strdup ("1");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, play_count);
	g_free (play_count);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_NUM_PLAYS,
			      &value);
	g_value_unset (&value);

	/* Reset the last played time */
	time (&now);

	g_value_init (&value, G_TYPE_LONG);
	g_value_set_long (&value, now);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_LAST_PLAYED,
			      &value);
	g_value_unset (&value);

	time_string = eel_strdup_strftime (_("%Y-%m-%d %H:%M"), localtime (&now));

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, time_string);
	g_free (time_string);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_PROP_LAST_PLAYED_SIMPLE,
			      &value);
	g_value_unset (&value);
}
