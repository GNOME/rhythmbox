/*
 *  arch-tag: Header for GtkTreeModel wrapper of RBNode objects
 *
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
 */

#ifndef __RB_TREE_MODEL_NODE_H
#define __RB_TREE_MODEL_NODE_H

#include <gtk/gtktreemodel.h>

#include "rb-node.h"
#include "rb-node-filter.h"

G_BEGIN_DECLS

#define RB_TYPE_TREE_MODEL_NODE         (rb_tree_model_node_get_type ())
#define RB_TREE_MODEL_NODE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_TREE_MODEL_NODE, RBTreeModelNode))
#define RB_TREE_MODEL_NODE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_TREE_MODEL_NODE, RBTreeModelNodeClass))
#define RB_IS_TREE_MODEL_NODE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_TREE_MODEL_NODE))
#define RB_IS_TREE_MODEL_NODE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_TREE_MODEL_NODE))
#define RB_TREE_MODEL_NODE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_TREE_MODEL_NODE, RBTreeModelNodeClass))

typedef enum
{
	RB_TREE_MODEL_NODE_COL_PLAYING,
	RB_TREE_MODEL_NODE_COL_TRACK_NUMBER,
	RB_TREE_MODEL_NODE_COL_TRACK_NUMBER_STR,
	RB_TREE_MODEL_NODE_COL_TITLE,
	RB_TREE_MODEL_NODE_COL_TITLE_KEY,
	RB_TREE_MODEL_NODE_COL_TITLE_WEIGHT,
	RB_TREE_MODEL_NODE_COL_ARTIST,
	RB_TREE_MODEL_NODE_COL_ARTIST_KEY,
	RB_TREE_MODEL_NODE_COL_ALBUM,
	RB_TREE_MODEL_NODE_COL_ALBUM_KEY,
	RB_TREE_MODEL_NODE_COL_GENRE,
	RB_TREE_MODEL_NODE_COL_DURATION,
	RB_TREE_MODEL_NODE_COL_DURATION_STR,
	RB_TREE_MODEL_NODE_COL_VISIBLE,
	RB_TREE_MODEL_NODE_COL_PRIORITY,
	RB_TREE_MODEL_NODE_COL_RATING,
	RB_TREE_MODEL_NODE_COL_LAST_PLAYED,
	RB_TREE_MODEL_NODE_COL_LAST_PLAYED_STR,
	RB_TREE_MODEL_NODE_COL_PLAY_COUNT,
	RB_TREE_MODEL_NODE_COL_PLAY_COUNT_STR,
	RB_TREE_MODEL_NODE_COL_QUALITY,
	RB_TREE_MODEL_NODE_COL_LOCATION,
	RB_TREE_MODEL_NODE_COL_DUMMY,
	RB_TREE_MODEL_NODE_NUM_COLUMNS
} RBTreeModelNodeColumn;

GType rb_tree_model_node_column_get_type (void);

#define RB_TYPE_TREE_MODEL_NODE_COLUMN (rb_tree_model_node_column_get_type ())

typedef struct RBTreeModelNodePrivate RBTreeModelNodePrivate;

typedef struct
{
	GObject parent;

	RBTreeModelNodePrivate *priv;

	int stamp;
} RBTreeModelNode;

typedef struct
{
	GObjectClass parent;
} RBTreeModelNodeClass;

GType            rb_tree_model_node_get_type         (void);

RBTreeModelNode *rb_tree_model_node_new              (RBNode *root,
						      RBNodeFilter *filter);

RBNode          *rb_tree_model_node_node_from_iter   (RBTreeModelNode *model,
						      GtkTreeIter *iter);
void             rb_tree_model_node_iter_from_node   (RBTreeModelNode *model,
						      RBNode *node,
						      GtkTreeIter *iter);

void             rb_tree_model_node_set_playing_node (RBTreeModelNode *model,
						      RBNode *node);
RBNode          *rb_tree_model_node_get_playing_node (RBTreeModelNode *model);

void             rb_tree_model_node_set_playing      (RBTreeModelNode *model,
				                      gboolean playing);

G_END_DECLS

#endif /* __RB_TREE_MODEL_NODE_H */
