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

typedef enum
{
	RB_NODE_TYPE_GENERIC,
	RB_NODE_TYPE_ALL_GENRES,
	RB_NODE_TYPE_GENRE,
	RB_NODE_TYPE_ALL_ARTISTS,
	RB_NODE_TYPE_ARTIST,
	RB_NODE_TYPE_ALL_ALBUMS,
	RB_NODE_TYPE_ALBUM,
	RB_NODE_TYPE_ALL_SONGS,
	RB_NODE_TYPE_SONG
} RBNodeType;

#define RB_TYPE_NODE_TYPE (rb_node_type_get_type ())

GType rb_node_type_get_type (void);

typedef enum
{
	RB_NODE_PROPERTY_NAME,
	RB_NODE_PROPERTY_SONG_TRACK_NUMBER,
	RB_NODE_PROPERTY_SONG_DURATION,
	RB_NODE_PROPERTY_SONG_LOCATION,
	RB_NODE_PROPERTY_SONG_FILE_SIZE,
	RB_NODE_PROPERTY_SONG_MTIME
} RBNodeProperty;

#define RB_TYPE_NODE_PROPERTY (rb_node_property_get_type ())

GType rb_node_property_get_type (void);

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
	void (*changed)         (RBNode *node);

	void (*child_created)   (RBNode *node, RBNode *child);
	void (*child_changed)   (RBNode *node, RBNode *child);
	void (*child_destroyed) (RBNode *node, RBNode *child);
} RBNodeClass;

GType       rb_node_get_type         (void);

RBNode     *rb_node_new              (RBNodeType type);

/* ID */
long        rb_node_get_id           (RBNode *node);

RBNode     *rb_node_from_id          (int id);

/* Node type */
RBNodeType  rb_node_get_node_type    (RBNode *node);

/* property interface */
void        rb_node_set_property     (RBNode *node,
				      RBNodeProperty property,
				      const GValue *value);
void        rb_node_get_property     (RBNode *node,
				      RBNodeProperty property,
				      GValue *value);

/* grandparent */
GList      *rb_node_get_grandparents (RBNode *node);
void        rb_node_add_grandparent  (RBNode *node,
				      RBNode *grandparent);
gboolean    rb_node_has_grandparent  (RBNode *node,
				      RBNode *grandparent);

/* parents */
GList      *rb_node_get_parents      (RBNode *node);
void        rb_node_add_parent       (RBNode *node,
				      RBNode *parent);
gboolean    rb_node_has_parent       (RBNode *node,
				      RBNode *parent);

/* children */
GList      *rb_node_get_children     (RBNode *node);
void        rb_node_add_child        (RBNode *node,
				      RBNode *child);
void        rb_node_remove_child     (RBNode *node,
				      RBNode *child);
gboolean    rb_node_has_child        (RBNode *node,
				      RBNode *child);

RBNode     *rb_node_get_nth_child    (RBNode *node,
				      int n);
int         rb_node_child_index      (RBNode *node,
				      RBNode *child);
int         rb_node_n_children       (RBNode *node);

/* XML */
void        rb_node_save_to_xml      (RBNode *node,
			              xmlNodePtr parent_xml_node);
RBNode     *rb_node_new_from_xml     (xmlNodePtr xml_node);

G_END_DECLS

#endif /* __RB_NODE_H */
