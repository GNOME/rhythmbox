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

#ifndef __RB_NODE_H
#define __RB_NODE_H

#include <glib-object.h>

#include <libxml/tree.h>

G_BEGIN_DECLS

#define RB_TYPE_NODE         (rb_node_get_type ())
#define RB_NODE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE, RBNode))
#define RB_NODE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE, RBNodeClass))
#define RB_IS_NODE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE))
#define RB_IS_NODE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE))
#define RB_NODE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE, RBNodeClass))

typedef struct RBNodePrivate RBNodePrivate;

typedef struct
{
	GObject parent;

	RBNodePrivate *priv;
} RBNode;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void (*destroyed)       (RBNode *node);
	void (*restored)        (RBNode *node);

	void (*child_added)     (RBNode *node, RBNode *child);
	void (*child_changed)   (RBNode *node, RBNode *child);
	void (*child_reordered) (RBNode *node, RBNode *child,
			         int old_index, int new_index);
	void (*child_removed)   (RBNode *node, RBNode *child);
} RBNodeClass;

GType       rb_node_get_type              (void);

RBNode     *rb_node_new                   (void);

/* unique node ID */
long        rb_node_get_id                (RBNode *node);

RBNode     *rb_node_get_from_id           (long id);

/* refcounting */
void        rb_node_ref                   (RBNode *node);
void        rb_node_unref                 (RBNode *node);

/* locking */
void        rb_node_freeze                (RBNode *node);
void        rb_node_thaw                  (RBNode *node);

/* property interface */
/* properties */
enum
{
	RB_NODE_PROP_NAME               = 0,
	RB_NODE_PROP_NAME_SORT_KEY      = 1,
	RB_NODE_PROP_GENRE              = 2,
	RB_NODE_PROP_ARTIST             = 3,
	RB_NODE_PROP_ALBUM              = 4,
	RB_NODE_PROP_REAL_GENRE         = 5,
	RB_NODE_PROP_REAL_ARTIST        = 6,
	RB_NODE_PROP_REAL_ALBUM         = 7,
	RB_NODE_PROP_TRACK_NUMBER       = 8,
	RB_NODE_PROP_REAL_TRACK_NUMBER  = 9,
	RB_NODE_PROP_DURATION           = 10,
	RB_NODE_PROP_REAL_DURATION      = 11,
	RB_NODE_PROP_FILE_SIZE          = 12,
	RB_NODE_PROP_LOCATION           = 13,
	RB_NODE_PROP_MTIME              = 14,
	RB_NODE_PROP_RESERVED           = 15,
	RB_NODE_PROP_RATING	        = 16,
	RB_NODE_PROP_NUM_PLAYS          = 17,
	RB_NODE_PROP_LAST_PLAYED        = 18,
	RB_NODE_PROP_LAST_PLAYED_SIMPLE = 19,
	RB_NODE_PROP_ARTIST_SORT_KEY    = 20,
	RB_NODE_PROP_ALBUM_SORT_KEY     = 21,
	RB_NODE_PROP_QUALITY            = 22,
	RB_NODE_PROP_ALT_LOCATIONS      = 23,
	/* 24-99 are reserved */
};

void        rb_node_set_property          (RBNode *node,
				           int property_id,
				           const GValue *value);
gboolean    rb_node_get_property          (RBNode *node,
				           int property_id,
				           GValue *value);

const char *rb_node_get_property_string   (RBNode *node,
					   int property_id);
gboolean    rb_node_get_property_boolean  (RBNode *node,
					   int property_id);
long        rb_node_get_property_long     (RBNode *node,
					   int property_id);
int         rb_node_get_property_int      (RBNode *node,
					   int property_id);
double      rb_node_get_property_double   (RBNode *node,
					   int property_id);
float       rb_node_get_property_float    (RBNode *node,
					   int property_id);
RBNode     *rb_node_get_property_node     (RBNode *node,
					   int property_id);
GObject    *rb_node_get_property_object   (RBNode *node,
					   int property_id);
/* free return value */
char       *rb_node_get_property_time     (RBNode *node,
					   int property_id);

/* xml storage */
void        rb_node_save_to_xml           (RBNode *node,
					   xmlNodePtr parent_xml_node);
RBNode     *rb_node_new_from_xml          (xmlNodePtr xml_node);

/* DAG structure */
void        rb_node_add_child             (RBNode *node,
					   RBNode *child);
void        rb_node_remove_child          (RBNode *node,
					   RBNode *child);
gboolean    rb_node_has_child             (RBNode *node,
					   RBNode *child);

/* Note that rb_node_get_children freezes the node; you'll have to thaw it when done.
 * This is to prevent the data getting changed from another thread. */
GPtrArray  *rb_node_get_children          (RBNode *node);
int         rb_node_get_n_children        (RBNode *node);
RBNode     *rb_node_get_nth_child         (RBNode *node,
					   int n);
int         rb_node_get_child_index       (RBNode *node,
					   RBNode *child);
RBNode     *rb_node_get_next_child        (RBNode *node,
					   RBNode *child);
RBNode     *rb_node_get_previous_child    (RBNode *node,
					   RBNode *child);

/* node cloning (all clones have the same properties, but a different
 * family) */
RBNode     *rb_node_new_clone             (RBNode *node);

RBNode     *rb_node_clone_of              (RBNode *node);

/* node id services */
void        rb_node_system_init           (void);
void        rb_node_system_shutdown       (void);

long        rb_node_new_id                (void);


/* Update 'play' statistics */
void        rb_node_update_play_statistics (RBNode *node);


G_END_DECLS

#endif /* __RB_NODE_H */
