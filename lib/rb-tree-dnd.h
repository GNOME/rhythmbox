/* rbtreednd.h
 * Copyright (C) 2001  Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#ifndef __RB_TREE_DND_H__
#define __RB_TREE_DND_H__

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_TREE_DRAG_SOURCE            (rb_tree_drag_source_get_type ())
#define RB_TREE_DRAG_SOURCE(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_TREE_DRAG_SOURCE, RbTreeDragSource))
#define RB_IS_TREE_DRAG_SOURCE(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_TREE_DRAG_SOURCE))
#define RB_TREE_DRAG_SOURCE_GET_IFACE(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_TREE_DRAG_SOURCE, RbTreeDragSourceIface))

#define RB_TYPE_TREE_DRAG_DEST              (rb_tree_drag_dest_get_type ())
#define RB_TREE_DRAG_DEST(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_TREE_DRAG_DEST, RbTreeDragDest))
#define RB_IS_TREE_DRAG_DEST(obj)           (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_TREE_DRAG_DEST))
#define RB_TREE_DRAG_DEST_GET_IFACE(obj)    (G_TYPE_INSTANCE_GET_INTERFACE ((obj), RB_TYPE_TREE_DRAG_DEST, RbTreeDragDestIface))


typedef struct _RbTreeDragSource      RbTreeDragSource; /* Dummy typedef */
typedef struct _RbTreeDragSourceIface RbTreeDragSourceIface;


typedef struct _RbTreeDragDest      RbTreeDragDest; /* Dummy typedef */
typedef struct _RbTreeDragDestIface RbTreeDragDestIface;

typedef enum {
	RB_TREE_DEST_EMPTY_VIEW_DROP = 1 << 0,
	RB_TREE_DEST_CAN_DROP_INTO = 1 << 1,
	RB_TREE_DEST_CAN_DROP_BETWEEN = 1 << 2,
	RB_TREE_DEST_SELECT_ON_DRAG_TIMEOUT = 1 << 3
} RbTreeDestFlag;


struct _RbTreeDragSourceIface
{
	GTypeInterface g_iface;

	/* VTable - not signals */
	gboolean     (* rb_row_draggable)        (RbTreeDragSource   *drag_source,
	                              GList              *path_list);

	gboolean     (* rb_drag_data_get)        (RbTreeDragSource   *drag_source,
                                         GList              *path_list,
                                         GtkSelectionData   *selection_data);

	gboolean     (* rb_drag_data_delete)     (RbTreeDragSource *drag_source,
                                         GList            *path_list);
};


struct _RbTreeDragDestIface {

	GTypeInterface g_iface;

	/* VTable - not signals */

	gboolean (* rb_drag_data_received) (RbTreeDragDest   *drag_dest,
					 GtkTreePath       *dest,
					 GtkTreeViewDropPosition pos,
					 GtkSelectionData  *selection_data);

	gboolean (* rb_row_drop_possible)  (RbTreeDragDest   *drag_dest,
					 GtkTreePath       *dest_path,
					 GtkTreeViewDropPosition pos,
					 GtkSelectionData  *selection_data);

	gboolean (* rb_row_drop_position) (RbTreeDragDest   *drag_dest,
					GtkTreePath       *dest_path,
					GList *targets,
					GtkTreeViewDropPosition *pos);

	/* optional */
	GdkAtom  (* rb_get_drag_target)    (RbTreeDragDest   *drag_dest,
					 GtkWidget        *widget,
					 GdkDragContext   *context,
					 GtkTreePath      *dest_path,
					 GtkTargetList    *target_list);
};


GType    rb_tree_drag_source_get_type         (void) G_GNUC_CONST;


/* Returns whether the given row can be dragged */
gboolean rb_tree_drag_source_row_draggable    (RbTreeDragSource *drag_source,
					       GList                  *path_list);

/* Deletes the given row, or returns FALSE if it can't */
gboolean rb_tree_drag_source_drag_data_delete (RbTreeDragSource *drag_source,
					       GList *path_list);

/* Fills in selection_data with type selection_data->target based on the row
 * denoted by path, returns TRUE if it does anything
 */
gboolean rb_tree_drag_source_drag_data_get (RbTreeDragSource *drag_source,
					    GList                  *path_list,
					    GtkSelectionData       *selection_data);


GType    rb_tree_drag_dest_get_type         (void) G_GNUC_CONST;


gboolean rb_tree_drag_dest_drag_data_received (RbTreeDragDest   *drag_dest,
					       GtkTreePath       *dest,
					       GtkTreeViewDropPosition pos,
					       GtkSelectionData  *selection_data);

gboolean rb_tree_drag_dest_row_drop_possible (RbTreeDragDest   *drag_dest,
					      GtkTreePath       *dest_path,
					      GtkTreeViewDropPosition pos,
					      GtkSelectionData  *selection_data);

gboolean rb_tree_drag_dest_row_drop_position (RbTreeDragDest   *drag_dest,
					      GtkTreePath       *dest_path,
					      GList *targets,
					      GtkTreeViewDropPosition *pos);



void rb_tree_dnd_add_drag_dest_support (GtkTreeView *tree_view,
				    RbTreeDestFlag flags,
				    const GtkTargetEntry *targets,
				    gint n_targets,
				    GdkDragAction actions);

void rb_tree_dnd_add_drag_source_support (GtkTreeView *tree_view,
				    GdkModifierType start_button_mask,
				    const GtkTargetEntry *targets,
				    gint n_targets,
				    GdkDragAction actions);


G_END_DECLS

#endif /* __RB_TREE_DND_H__ */
