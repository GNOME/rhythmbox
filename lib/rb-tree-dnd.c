/* rbtreednd.c
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtksignal.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkmain.h>
#include <gtk/gtktreednd.h>
#include "rb-tree-dnd.h"

#include "rb-debug.h"

#define RB_TREE_DND_STRING "RbTreeDndString"

typedef struct
{
  guint pressed_button;
  gint x;
  gint y;
  guint button_press_event_handler;
  guint motion_notify_handler;
  guint button_release_handler;
  guint drag_data_get_handler;
  guint drag_data_delete_handler;
  guint drag_leave_handler;
  guint drag_drop_handler;
  guint drag_data_received_handler;
  GSList *event_list;
  gboolean pending_event;

  GtkTargetList *dest_target_list;
  GdkDragAction dest_actions;
  RbTreeDestFlag dest_flags;

  GtkTargetList *source_target_list;
  GdkDragAction source_actions;
  GdkModifierType start_button_mask;

} RbTreeDndData;

GType
rb_tree_drag_source_get_type (void)
{
  static GType our_type = 0;

  if (!our_type)
    {
      static const GTypeInfo our_info =
      {
        sizeof (RbTreeDragSourceIface), /* class_size */
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      our_type = g_type_register_static (G_TYPE_INTERFACE, "RbTreeDragSource", &our_info, 0);
    }

  return our_type;
}


/**
 * rb_tree_drag_source_row_draggable:
 * @drag_source: a #EggTreeMultiDragSource
 * @path: row on which user is initiating a drag
 *
 * Asks the #EggTreeMultiDragSource whether a particular row can be used as
 * the source of a DND operation. If the source doesn't implement
 * this interface, the row is assumed draggable.
 *
 * Return value: %TRUE if the row can be dragged
 **/
gboolean
rb_tree_drag_source_row_draggable (RbTreeDragSource *drag_source,
				   GList                  *path_list)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->row_draggable != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);

  if (iface->row_draggable)
    return (* iface->row_draggable) (drag_source, path_list);
  else
    return TRUE;
}


/**
 * rb_tree_drag_source_drag_data_delete:
 * @drag_source: a #EggTreeMultiDragSource
 * @path: row that was being dragged
 *
 * Asks the #EggTreeMultiDragSource to delete the row at @path, because
 * it was moved somewhere else via drag-and-drop. Returns %FALSE
 * if the deletion fails because @path no longer exists, or for
 * some model-specific reason. Should robustly handle a @path no
 * longer found in the model!
 *
 * Return value: %TRUE if the row was successfully deleted
 **/
gboolean
rb_tree_drag_source_drag_data_delete (RbTreeDragSource *drag_source,
					     GList                  *path_list)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->drag_data_delete != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);

  return (* iface->drag_data_delete) (drag_source, path_list);
}

/**
 * rb_tree_drag_source_drag_data_get:
 * @drag_source: a #EggTreeMultiDragSource
 * @path: row that was dragged
 * @selection_data: a #EggSelectionData to fill with data from the dragged row
 *
 * Asks the #EggTreeMultiDragSource to fill in @selection_data with a
 * representation of the row at @path. @selection_data->target gives
 * the required type of the data.  Should robustly handle a @path no
 * longer found in the model!
 *
 * Return value: %TRUE if data of the required type was provided
 **/
gboolean
rb_tree_drag_source_drag_data_get    (RbTreeDragSource *drag_source,
					     GList                  *path_list,
					     GtkSelectionData  *selection_data)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->drag_data_get != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->drag_data_get) (drag_source, path_list, selection_data);
}



GType
rb_tree_drag_dest_get_type (void)
{
  static GType our_type = 0;

  if (!our_type)
    {
      static const GTypeInfo our_info =
      {
        sizeof (RbTreeDragDestIface), /* class_size */
	NULL,		/* base_init */
	NULL,		/* base_finalize */
	NULL,
	NULL,		/* class_finalize */
	NULL,		/* class_data */
	0,
	0,              /* n_preallocs */
	NULL
      };

      our_type = g_type_register_static (G_TYPE_INTERFACE, "RbTreeDragDest", &our_info, 0);
    }

  return our_type;
}



gboolean
rb_tree_drag_dest_drag_data_received (RbTreeDragDest   *drag_dest,
				      GtkTreePath       *dest,
				      GtkTreeViewDropPosition pos, 
				      GtkSelectionData  *selection_data)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->drag_data_received != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->drag_data_received) (drag_dest, dest, pos, selection_data);
}



gboolean
rb_tree_drag_dest_row_drop_possible (RbTreeDragDest   *drag_dest,
				     GtkTreePath       *dest_path,
				     GtkTreeViewDropPosition pos,
				     GtkSelectionData  *selection_data)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->drag_data_received != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->row_drop_possible) (drag_dest, dest_path, pos, selection_data);
}


gboolean
rb_tree_drag_dest_row_drop_position (RbTreeDragDest   *drag_dest,
				     GtkTreePath       *dest_path,
				     GList *targets,
				     GtkTreeViewDropPosition *pos)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->row_drop_position != NULL, FALSE);
  g_return_val_if_fail (targets != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  return (* iface->row_drop_position) (drag_dest, dest_path, targets, pos);
}




RbTreeDndData *
init_rb_tree_dnd_data (GtkWidget *widget)
{
	RbTreeDndData *priv_data;

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);
	if (priv_data == NULL)
	{
		priv_data = g_new0 (RbTreeDndData, 1);
		priv_data->pending_event = FALSE;
		g_object_set_data (G_OBJECT (widget), RB_TREE_DND_STRING, priv_data);
		priv_data->drag_leave_handler = 0;
		priv_data->button_press_event_handler = 0;
	}

	return priv_data;
}



static void
stop_drag_check (GtkWidget *widget)
{
	RbTreeDndData *priv_data;
	GSList *l;

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	for (l = priv_data->event_list; l != NULL; l = l->next)
		gdk_event_free (l->data);

	g_slist_free (priv_data->event_list);
	priv_data->event_list = NULL;
	priv_data->pending_event = FALSE;
	g_signal_handler_disconnect (widget, priv_data->motion_notify_handler);
	g_signal_handler_disconnect (widget, priv_data->button_release_handler);
}


static gboolean
rb_tree_dnd_button_release_event_cb (GtkWidget      *widget,
				   GdkEventButton *event,
				   gpointer        data)
{
	RbTreeDndData *priv_data;
	GSList *l;

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	for (l = priv_data->event_list; l != NULL; l = l->next)
		gtk_propagate_event (widget, l->data);

	stop_drag_check (widget);

	return FALSE;
}


static void
selection_foreach (GtkTreeModel *model,
		   GtkTreePath  *path,
		   GtkTreeIter  *iter,
		   gpointer      data)
{
	GList **list_ptr;

	list_ptr = (GList **) data;
	*list_ptr = g_list_prepend (*list_ptr, gtk_tree_row_reference_new (model, path));
}


static void
path_list_free (GList *path_list)
{
	g_list_foreach (path_list, (GFunc) gtk_tree_row_reference_free, NULL);
	g_list_free (path_list);
}


static void
set_context_data (GdkDragContext *context,
		  GList          *path_list)
{
	g_object_set_data_full (G_OBJECT (context),
				"rb-tree-view-multi-source-row",
				path_list,
				(GDestroyNotify) path_list_free);

	rb_debug ("Setting path_list: index=%i", gtk_tree_path_get_indices(path_list->data)[0]);
}


GList *
get_context_data (GdkDragContext *context)
{
	return g_object_get_data (G_OBJECT (context), "rb-tree-view-multi-source-row");
}


void
filter_drop_position (GtkWidget *widget, GdkDragContext *context, GtkTreePath *path, GtkTreeViewDropPosition *pos)
{
  GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
  GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
  RbTreeDndData *priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

  if (!(priv_data->dest_flags &
	(RB_TREE_DEST_CAN_DROP_INTO | RB_TREE_DEST_CAN_DROP_BETWEEN))) {

    rb_tree_drag_dest_row_drop_position (RB_TREE_DRAG_DEST (model),
					 path,
					 context->targets,
					 pos);

  } else {
    if (!(priv_data->dest_flags & RB_TREE_DEST_CAN_DROP_INTO)) {
      if (*pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)
	*pos = GTK_TREE_VIEW_DROP_BEFORE;
      else if (*pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
	*pos = GTK_TREE_VIEW_DROP_AFTER;

    } else if (!(priv_data->dest_flags & RB_TREE_DEST_CAN_DROP_BETWEEN)) {
      if (*pos == GTK_TREE_VIEW_DROP_BEFORE)
	*pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
      else if (*pos == GTK_TREE_VIEW_DROP_AFTER)
	*pos = GTK_TREE_VIEW_DROP_INTO_OR_AFTER;
    }
  }
}



static void
rb_tree_dnd_drag_data_delete_cb (GtkWidget *widget,
			  GdkDragContext *drag_context,
			  gpointer user_data)
{
	GList *path_list;
	GtkTreeModel *model = gtk_tree_view_get_model (GTK_TREE_VIEW(widget));

	path_list = get_context_data (drag_context);
	rb_tree_drag_source_drag_data_delete (RB_TREE_DRAG_SOURCE (model),
					      path_list);

	g_signal_stop_emission_by_name (widget, "drag_data_delete");
}



static void
rb_tree_dnd_drag_data_get_cb (GtkWidget        *widget,
			    GdkDragContext   *context,
			    GtkSelectionData *selection_data,
			    guint             info,
			    guint             time)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GList *path_list;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	if (model == NULL)
		return;

	path_list = get_context_data (context);

	if (path_list == NULL)
		return;

	/* We can implement the GTK_TREE_MODEL_ROW target generically for
	* any model; for DragSource models there are some other targets
	* we also support.
	*/
	if (RB_IS_TREE_DRAG_SOURCE (model))
	{
		rb_tree_drag_source_drag_data_get (RB_TREE_DRAG_SOURCE (model),
						   path_list,
						   selection_data);
    	}
}


static gboolean
rb_tree_dnd_motion_notify_event_cb (GtkWidget      *widget,
				     GdkEventMotion *event,
                 		     gpointer        data)
{
	RbTreeDndData *priv_data;

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	if (gtk_drag_check_threshold (widget,
				      priv_data->x,
				      priv_data->y,
				      event->x,
				      event->y))
	{
		GList *path_list = NULL;
		GtkTreeSelection *selection;
		GtkTreeModel *model;
		GdkDragContext *context;

		selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (widget));
		stop_drag_check (widget);
		gtk_tree_selection_selected_foreach (selection, selection_foreach, &path_list);
		path_list = g_list_reverse (path_list);
		model = gtk_tree_view_get_model (GTK_TREE_VIEW (widget));

		if (rb_tree_drag_source_row_draggable (RB_TREE_DRAG_SOURCE (model), path_list))
		{
			rb_debug ("drag begin");
			context = gtk_drag_begin (widget,
						  priv_data->source_target_list,
						  priv_data->source_actions,
						  priv_data->pressed_button,
						  (GdkEvent*)event);
	  		set_context_data (context, path_list);
	  		gtk_drag_set_icon_default (context);

		} else {
			path_list_free (path_list);
		}
	}

	return TRUE;
}

static gboolean
rb_tree_dnd_drag_motion_cb (GtkWidget        *widget,
                           GdkDragContext   *context,
                           gint              x,
                           gint              y,
                           guint             time)
{
	GtkTreeView *tree_view;
	GtkTreePath *path = NULL;
	GtkTreeModel *model;
	GtkTreeViewDropPosition pos;
	RbTreeDndData *priv_data;
	GdkDragAction action;

	rb_debug ("drag and drop motion: (%i,%i)", x, y);

  	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, &pos);

	if (path == NULL)
	{
	  	gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (widget),
						 NULL,
						 GTK_TREE_VIEW_DROP_BEFORE);

		if (!(priv_data->dest_flags & RB_TREE_DEST_EMPTY_VIEW_DROP)) {
			/* Can't drop here. */
			gdk_drag_status (context, 0, time);

			return TRUE;
		}
	}
	else
	{
	  filter_drop_position (widget, context, path, &pos);
	}

	if (GTK_WIDGET (tree_view) == gtk_drag_get_source_widget (context) &&
	    context->actions & GDK_ACTION_MOVE)
		action = GDK_ACTION_MOVE;
	else
		action = context->suggested_action;

	if (path) {
	        gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
		gtk_tree_path_free (path);
	}

	gdk_drag_status (context, action, time);

	return TRUE;
}


static gboolean
rb_tree_dnd_drag_drop_cb (GtkWidget        *widget,
                         GdkDragContext   *context,
                         gint              x,
                         gint              y,
                         guint             time)
{
	GtkTreeView *tree_view;
	GtkTreePath *path;
	GtkTreeModel *model;
	GtkTreeViewDropPosition pos;
	RbTreeDndData *priv_data;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, &pos);

	/* Unset this thing */
	gtk_tree_view_set_drag_dest_row (tree_view,
               				 NULL,
					 GTK_TREE_VIEW_DROP_BEFORE);

	if (path || priv_data->dest_flags & RB_TREE_DEST_EMPTY_VIEW_DROP) {
		GdkAtom target = gtk_drag_dest_find_target (widget, context, priv_data->dest_target_list);

		if (path)
			gtk_tree_path_free (path);

		if (target != GDK_NONE) {
			gtk_drag_get_data (widget, context, target, time);
			return TRUE;
		}
	}

	return FALSE;
}


static void
rb_tree_dnd_drag_data_received_cb (GtkWidget        *widget,
                                  GdkDragContext   *context,
                                  gint              x,
                                  gint              y,
                                  GtkSelectionData *selection_data,
                                  guint             info,
                                  guint             time)
{
	GtkTreeView *tree_view;
	GtkTreeModel *model;
	GtkTreePath *dest_row;
	GtkTreeViewDropPosition pos;
	gboolean accepted = FALSE;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &dest_row, &pos);

	if (dest_row)
		filter_drop_position (widget, context, dest_row, &pos);

	if (selection_data->length >= 0)
	{
		if (rb_tree_drag_dest_drag_data_received (RB_TREE_DRAG_DEST (model),
                					  dest_row,
							  pos,
                					  selection_data))
        		accepted = TRUE;
	}

	gtk_drag_finish (context,
        		 accepted,
			 (context->action == GDK_ACTION_MOVE),
			 time);

	if (dest_row)
  		gtk_tree_path_free (dest_row);

	g_signal_stop_emission_by_name (widget, "drag_data_received");

}


static gboolean
rb_tree_dnd_button_press_event_cb (GtkWidget      *widget,
					GdkEventButton *event,
					gpointer        data)
{
	GtkTreeView *tree_view;
	GtkTreePath *path = NULL;
	GtkTreeViewColumn *column = NULL;
	gint cell_x, cell_y;
	GtkTreeSelection *selection;
	RbTreeDndData *priv_data;

	if (event->button == 3)
		return FALSE;

	tree_view = GTK_TREE_VIEW (widget);
	priv_data = g_object_get_data (G_OBJECT (tree_view), RB_TREE_DND_STRING);
	if (priv_data == NULL)
	{
		priv_data = g_new0 (RbTreeDndData, 1);
		priv_data->pending_event = FALSE;
		g_object_set_data (G_OBJECT (tree_view), RB_TREE_DND_STRING, priv_data);
	}

	if (g_slist_find (priv_data->event_list, event))
		return FALSE;

	if (priv_data->pending_event)
	{
		/* save the event to be propagated in order */
		priv_data->event_list = g_slist_append (priv_data->event_list, gdk_event_copy ((GdkEvent*)event));
		return TRUE;
	}

	if (event->type == GDK_2BUTTON_PRESS)
		return FALSE;

	gtk_tree_view_get_path_at_pos (tree_view,
				       event->x, event->y,
				       &path, &column,
				       &cell_x, &cell_y);

	selection = gtk_tree_view_get_selection (tree_view);

	if (path)
	{
		gboolean call_parent = (event->state & (GDK_CONTROL_MASK | GDK_SHIFT_MASK) ||
					!gtk_tree_selection_path_is_selected (selection, path) ||
					event->button != 1);

		if (call_parent)
			(GTK_WIDGET_GET_CLASS (tree_view))->button_press_event (widget, event);

		if (gtk_tree_selection_path_is_selected (selection, path))
		{
			priv_data->pressed_button = event->button;
			priv_data->x = event->x;
			priv_data->y = event->y;

			priv_data->pending_event = TRUE;

			if (!call_parent)
				priv_data->event_list = g_slist_append (priv_data->event_list,
									gdk_event_copy ((GdkEvent*)event));

			priv_data->motion_notify_handler = g_signal_connect (G_OBJECT (tree_view),
									     "motion_notify_event",
									     G_CALLBACK (rb_tree_dnd_motion_notify_event_cb),
									     NULL);
			priv_data->button_release_handler = g_signal_connect (G_OBJECT (tree_view),
									      "button_release_event",
									      G_CALLBACK (rb_tree_dnd_button_release_event_cb),
									      NULL);

		}

		gtk_tree_path_free (path);
		/* We called the default handler so we don't let the default handler run */
		return TRUE;
	}

	return FALSE;
}


void
rb_tree_dnd_add_drag_source_support (GtkTreeView *tree_view,
				 GdkModifierType start_button_mask,
				 const GtkTargetEntry *targets,
				 gint n_targets,
				 GdkDragAction actions)
{
	RbTreeDndData *priv_data = NULL;
 	g_return_if_fail (GTK_IS_TREE_VIEW (tree_view));

	priv_data = init_rb_tree_dnd_data (GTK_WIDGET(tree_view));

	if (!priv_data->button_press_event_handler) {

		priv_data->source_target_list = gtk_target_list_new (targets, n_targets);
		priv_data->source_actions = actions;
		priv_data->start_button_mask = start_button_mask;

		priv_data->button_press_event_handler = g_signal_connect (G_OBJECT (tree_view),
							"button_press_event",
							G_CALLBACK (rb_tree_dnd_button_press_event_cb),
							NULL);
		priv_data->drag_data_get_handler = g_signal_connect (G_OBJECT (tree_view),
								     "drag_data_get",
								     G_CALLBACK (rb_tree_dnd_drag_data_get_cb),
								     NULL);
		priv_data->drag_data_delete_handler = g_signal_connect (G_OBJECT (tree_view),
									"drag_data_delete",
									G_CALLBACK (rb_tree_dnd_drag_data_delete_cb),
									NULL);
	}
}





void
rb_tree_dnd_add_drag_dest_support (GtkTreeView *tree_view,
			       RbTreeDestFlag flags,
			       const GtkTargetEntry *targets,
			       gint n_targets,
			       GdkDragAction actions)
{
	RbTreeDndData *priv_data = NULL;
	g_return_if_fail (GTK_IS_TREE_VIEW (tree_view));

	priv_data = init_rb_tree_dnd_data (GTK_WIDGET(tree_view));

	if (!priv_data->drag_leave_handler) {

		priv_data->dest_target_list = gtk_target_list_new (targets, n_targets);
		priv_data->dest_actions = actions;
		priv_data->dest_flags = flags;

		gtk_drag_dest_set (GTK_WIDGET (tree_view),
				   0,
				   NULL,
				   0,
				   actions);

		priv_data->drag_leave_handler = g_signal_connect (G_OBJECT (tree_view),
								  "drag_motion",
								  G_CALLBACK (rb_tree_dnd_drag_motion_cb),
								  NULL);
		priv_data->drag_drop_handler = g_signal_connect (G_OBJECT (tree_view),
								 "drag_drop",
								 G_CALLBACK (rb_tree_dnd_drag_drop_cb),
								 NULL);
	  	priv_data->drag_data_received_handler = g_signal_connect (G_OBJECT (tree_view),
									  "drag_data_received",
									  G_CALLBACK (rb_tree_dnd_drag_data_received_cb), 										  NULL);
	}

}
