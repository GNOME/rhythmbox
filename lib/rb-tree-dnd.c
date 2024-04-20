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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 */

#include "config.h"

#include <string.h>
#include <gtk/gtk.h>
#include "rb-tree-dnd.h"

#include "rb-debug.h"

#define RB_TREE_DND_STRING "RbTreeDndString"
/* must be the same value as in gtk_tree_view.c */
#define SCROLL_EDGE_SIZE 15


/**
 * SECTION:rbtreednd
 * @short_description: multi-row drag and drop support for GtkTreeViews
 *
 * Provides support for drag and drop operations to and from GtkTreeView
 * widgets that can include multiple rows.  The model backing the tree view
 * widgets must implement the #RbTreeDragSource and #RbTreeDragDest interfaces.
 */

/**
 * RbTreeDestFlag:
 * @RB_TREE_DEST_EMPTY_VIEW_DROP: If set, drops into empty spaces in the view are accepted
 * @RB_TREE_DEST_CAN_DROP_INTO: If set, drops into existing rows are accepted
 * @RB_TREE_DEST_CAN_DROP_BETWEEN: If set, drops between existing rows are accepted
 * @RB_TREE_DEST_SELECT_ON_DRAG_TIMEOUT: If set, update the drag selection using a timeout
 *
 * Flags controlling drag destination behaviour.
 */

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
  guint drag_motion_handler;
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

  /* Scroll timeout (e.g. during dnd) */
  guint scroll_timeout;

  /* Select on drag timeout */
  GtkTreePath * previous_dest_path;
  guint select_on_drag_timeout;
} RbTreeDndData;

static RbTreeDndData *init_rb_tree_dnd_data (GtkWidget *widget);
static GList * get_context_data (GdkDragContext *context);
static gboolean filter_drop_position (GtkWidget *widget, GdkDragContext *context, GtkTreePath *path, GtkTreeViewDropPosition *pos);
static gint scroll_row_timeout (gpointer data);
static gboolean select_on_drag_timeout (gpointer data);
static void remove_scroll_timeout (GtkTreeView *tree_view);
static void remove_select_on_drag_timeout (GtkTreeView *tree_view);

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
 * @drag_source: a #RbTreeDragSource
 * @path_list: row on which user is initiating a drag
 *
 * Asks the #RbTreeDragSource whether a particular row can be used as
 * the source of a DND operation. If the source doesn't implement
 * this interface, the row is assumed draggable.
 *
 * Return value: %TRUE if the row can be dragged
 **/
gboolean
rb_tree_drag_source_row_draggable (RbTreeDragSource *drag_source,
				   GList            *path_list)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->rb_row_draggable != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);

  if (iface->rb_row_draggable)
    return (* iface->rb_row_draggable) (drag_source, path_list);
  else
    return TRUE;
}


/**
 * rb_tree_drag_source_drag_data_delete:
 * @drag_source: a #RbTreeDragSource
 * @path_list: row that was being dragged
 *
 * Asks the #RbTreeDragSource to delete the row at @path, because
 * it was moved somewhere else via drag-and-drop. Returns %FALSE
 * if the deletion fails because @path no longer exists, or for
 * some model-specific reason. Should robustly handle a @path no
 * longer found in the model!
 *
 * Return value: %TRUE if the row was successfully deleted
 **/
gboolean
rb_tree_drag_source_drag_data_delete (RbTreeDragSource *drag_source,
				      GList            *path_list)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->rb_drag_data_delete != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);

  return (* iface->rb_drag_data_delete) (drag_source, path_list);
}

/**
 * rb_tree_drag_source_drag_data_get:
 * @drag_source: a #RbTreeDragSource
 * @path_list: row that was dragged
 * @selection_data: a #GtkSelectionData to fill with data from the dragged row
 *
 * Asks the #RbTreeDragSource to fill in @selection_data with a
 * representation of the row at @path. @selection_data->target gives
 * the required type of the data.  Should robustly handle a @path no
 * longer found in the model!
 *
 * Return value: %TRUE if data of the required type was provided
 **/
gboolean
rb_tree_drag_source_drag_data_get    (RbTreeDragSource *drag_source,
				      GList            *path_list,
				      GtkSelectionData *selection_data)
{
  RbTreeDragSourceIface *iface = RB_TREE_DRAG_SOURCE_GET_IFACE (drag_source);

  g_return_val_if_fail (RB_IS_TREE_DRAG_SOURCE (drag_source), FALSE);
  g_return_val_if_fail (iface->rb_drag_data_get != NULL, FALSE);
  g_return_val_if_fail (path_list != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->rb_drag_data_get) (drag_source, path_list, selection_data);
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


/**
 * rb_tree_drag_dest_drag_data_received:
 * @drag_dest: a #RbTreeDragDest
 * @dest: the #GtkTreePath on which the data was dropped
 * @pos: the drop position relative to the row identified by @dest
 * @selection_data: a #GtkSelectionData containing the drag data
 *
 * Asks a #RbTreeDragDest to accept some drag and drop data.
 *
 * Return value: %TRUE if the data was accepted, %FALSE otherwise
 */
gboolean
rb_tree_drag_dest_drag_data_received (RbTreeDragDest   *drag_dest,
				      GtkTreePath       *dest,
				      GtkTreeViewDropPosition pos, 
				      GtkSelectionData  *selection_data)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->rb_drag_data_received != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->rb_drag_data_received) (drag_dest, dest, pos, selection_data);
}


/**
 * rb_tree_drag_dest_row_drop_possible:
 * @drag_dest: a #RbTreeDragDest
 * @dest_path: the #GtkTreePath on which the data may be dropped
 * @pos: the drop position relative to the row identified by @dest
 * @selection_data: a #GtkSelectionData containing the drag data
 *
 * Asks the #RbTreeDragDest whether data can be dropped on a particular
 * row.  This should probably check based on the format and the row.
 *
 * Return value: %TRUE if the data can be dropped there
 */
gboolean
rb_tree_drag_dest_row_drop_possible (RbTreeDragDest   *drag_dest,
				     GtkTreePath       *dest_path,
				     GtkTreeViewDropPosition pos,
				     GtkSelectionData  *selection_data)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->rb_row_drop_possible != NULL, FALSE);
  g_return_val_if_fail (selection_data != NULL, FALSE);

  return (* iface->rb_row_drop_possible) (drag_dest, dest_path, pos, selection_data);
}


/**
 * rb_tree_drag_dest_row_drop_position:
 * @drag_dest: a #RbTreeDragDest
 * @dest_path: a #GtkTreePath describing a possible drop row
 * @targets: a #GList containing possible drop target types
 * @pos: returns the #GtkTreeViewDropPosition to use relative to the row
 *
 * Asks the #RbTreeDragDest which drop position to use relative to the specified row.
 * The drag destination should decide which drop position to use based on the 
 * target row and the list of drag targets.
 *
 * Return value: %TRUE if a drop position has been set, %FALSE if a drop should not be
 *   allowed in the specified row
 */
gboolean
rb_tree_drag_dest_row_drop_position (RbTreeDragDest   *drag_dest,
				     GtkTreePath       *dest_path,
				     GList *targets,
				     GtkTreeViewDropPosition *pos)
{
  RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (drag_dest);

  g_return_val_if_fail (RB_IS_TREE_DRAG_DEST (drag_dest), FALSE);
  g_return_val_if_fail (iface->rb_row_drop_position != NULL, FALSE);
  g_return_val_if_fail (targets != NULL, FALSE);
  g_return_val_if_fail (pos != NULL, FALSE);

  return (* iface->rb_row_drop_position) (drag_dest, dest_path, targets, pos);
}

static void
rb_tree_dnd_data_free (gpointer data)
{
  RbTreeDndData *priv_data = data;

  if (priv_data->source_target_list != NULL) {
	  gtk_target_list_unref (priv_data->source_target_list);
  }
  if (priv_data->dest_target_list != NULL) {
	  gtk_target_list_unref (priv_data->dest_target_list);
  }

  g_free (priv_data);
}

static RbTreeDndData *
init_rb_tree_dnd_data (GtkWidget *widget)
{
	RbTreeDndData *priv_data;

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);
	if (priv_data == NULL)
	{
		priv_data = g_new0 (RbTreeDndData, 1);
		priv_data->pending_event = FALSE;
		g_object_set_data_full (G_OBJECT (widget), RB_TREE_DND_STRING, priv_data, rb_tree_dnd_data_free);
		priv_data->drag_motion_handler = 0;
		priv_data->drag_leave_handler = 0;
		priv_data->button_press_event_handler = 0;
		priv_data->scroll_timeout = 0;
		priv_data->previous_dest_path = NULL;
		priv_data->select_on_drag_timeout = 0;
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

static GList *
get_context_data (GdkDragContext *context)
{
	return g_object_get_data (G_OBJECT (context), "rb-tree-view-multi-source-row");
}

static gboolean
filter_drop_position (GtkWidget *widget, GdkDragContext *context, GtkTreePath *path, GtkTreeViewDropPosition *pos)
{
	GtkTreeView *tree_view = GTK_TREE_VIEW (widget);
	GtkTreeModel *model = gtk_tree_view_get_model (tree_view);
	RbTreeDndData *priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);
	gboolean ret;

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

	ret = rb_tree_drag_dest_row_drop_position (RB_TREE_DRAG_DEST (model),
						   path,
						   gdk_drag_context_list_targets (context),
						   pos);
  
	rb_debug ("filtered drop position: %s", ret ? "TRUE" : "FALSE");	
	return ret;
}


/* Scroll function taken/adapted from gtktreeview.c */
static gint
scroll_row_timeout (gpointer data)
{
	GtkTreeView *tree_view = data;
	GdkRectangle visible_rect;
	gint y, x;
	gint offset;
	gfloat value;
	gdouble vadj_value;
	GtkAdjustment* vadj;
	RbTreeDndData *priv_data;
	GdkWindow *window;
	GdkDeviceManager *device_manager;

	priv_data = g_object_get_data (G_OBJECT (tree_view), RB_TREE_DND_STRING);
	g_return_val_if_fail(priv_data != NULL, TRUE);

	window = gtk_tree_view_get_bin_window (tree_view);
	device_manager = gdk_display_get_device_manager (gdk_window_get_display (window));
	gdk_window_get_device_position (window, gdk_device_manager_get_client_pointer (device_manager), &x, &y, NULL);
	gtk_tree_view_convert_widget_to_bin_window_coords (tree_view, x, y, &x, &y);
	gtk_tree_view_convert_bin_window_to_tree_coords (tree_view, x, y, &x, &y);

	gtk_tree_view_get_visible_rect (tree_view, &visible_rect);

	/* see if we are near the edge. */
	if (x < visible_rect.x && x > visible_rect.x + visible_rect.width)
	{
		priv_data->scroll_timeout = 0;
		return FALSE;
	}

	offset = y - (visible_rect.y + 2 * SCROLL_EDGE_SIZE);
	if (offset > 0)
	{
		offset = y - (visible_rect.y + visible_rect.height - 2 * SCROLL_EDGE_SIZE);
		if (offset < 0) 
		{
			priv_data->scroll_timeout = 0;
			return FALSE;
		}
	}

	vadj = gtk_scrollable_get_vadjustment (GTK_SCROLLABLE (tree_view));
	vadj_value = gtk_adjustment_get_value (vadj);
	value = CLAMP (vadj_value + offset,
		       gtk_adjustment_get_lower (vadj),
		       gtk_adjustment_get_upper (vadj) - gtk_adjustment_get_page_size (vadj));
	gtk_adjustment_set_value (vadj, value);

	/* don't remove it if we're on the edge and not scrolling */
	if (ABS (vadj_value - value) > 0.0001)
		remove_select_on_drag_timeout(tree_view);

	return TRUE;
}


static gboolean
select_on_drag_timeout (gpointer data)
{
	GtkTreeView *tree_view = data;
	GtkTreeSelection *selection;
	RbTreeDndData *priv_data;

	priv_data = g_object_get_data (G_OBJECT (tree_view), RB_TREE_DND_STRING);
	g_return_val_if_fail(priv_data != NULL, FALSE);
	g_return_val_if_fail(priv_data->previous_dest_path != NULL, FALSE);

	selection = gtk_tree_view_get_selection(tree_view);
	if (!gtk_tree_selection_path_is_selected(selection,priv_data->previous_dest_path)) {
		rb_debug("Changing selection because of drag timeout");
		gtk_tree_view_set_cursor(tree_view,priv_data->previous_dest_path,NULL,FALSE);
	}

	priv_data->select_on_drag_timeout = 0;
	gtk_tree_path_free(priv_data->previous_dest_path);
	priv_data->previous_dest_path = NULL;
	return FALSE;
}


static void
remove_scroll_timeout (GtkTreeView *tree_view)
 {
	 RbTreeDndData *priv_data;

	 priv_data = g_object_get_data (G_OBJECT (tree_view), RB_TREE_DND_STRING);
	 g_return_if_fail(priv_data != NULL);

	 if (priv_data->scroll_timeout != 0)
	 {
		 g_source_remove (priv_data->scroll_timeout);
		 priv_data->scroll_timeout = 0;
	 }
}


static void
remove_select_on_drag_timeout (GtkTreeView *tree_view)
{
	RbTreeDndData *priv_data;

	priv_data = g_object_get_data (G_OBJECT (tree_view), RB_TREE_DND_STRING);
	g_return_if_fail(priv_data != NULL);

	if (priv_data->select_on_drag_timeout != 0) {
		rb_debug("Removing the select on drag timeout");
		g_source_remove (priv_data->select_on_drag_timeout);
		priv_data->select_on_drag_timeout = 0;
	}
	if (priv_data->previous_dest_path != NULL) {
		gtk_tree_path_free(priv_data->previous_dest_path);
		priv_data->previous_dest_path = NULL;
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
			context = gtk_drag_begin_with_coordinates (widget,
								   priv_data->source_target_list,
								   priv_data->source_actions,
								   priv_data->pressed_button,
								   (GdkEvent*)event,
								   -1, -1);
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
	GtkTreeViewDropPosition pos;
	RbTreeDndData *priv_data;
	GdkDragAction action;

	rb_debug ("drag and drop motion: (%i,%i)", x, y);

  	tree_view = GTK_TREE_VIEW (widget);

	priv_data = g_object_get_data (G_OBJECT (widget), RB_TREE_DND_STRING);

	gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &path, &pos);

	if ((priv_data->previous_dest_path == NULL)
	    || (path == NULL)
	    || gtk_tree_path_compare(path,priv_data->previous_dest_path)) {
				remove_select_on_drag_timeout(tree_view);
	}

	if (path == NULL)
	{
	  	gtk_tree_view_set_drag_dest_row (GTK_TREE_VIEW (widget),
						 NULL,
						 GTK_TREE_VIEW_DROP_BEFORE);

		if (!(priv_data->dest_flags & RB_TREE_DEST_EMPTY_VIEW_DROP)) {
			/* Can't drop here. */
			gdk_drag_status (context, 0, time);

			return TRUE;
		} else if (!filter_drop_position (widget, context, path, &pos)) {
			gdk_drag_status (context, 0, time);
			return TRUE;
		}
	}
	else
	{
		if (!filter_drop_position (widget, context, path, &pos)) {
			gdk_drag_status (context, 0, time);
			return TRUE;
		}

	  if (priv_data->scroll_timeout == 0)
	  {
		  priv_data->scroll_timeout = g_timeout_add (150, scroll_row_timeout, tree_view);
	  }
	}

	if (GTK_WIDGET (tree_view) == gtk_drag_get_source_widget (context) &&
	    gdk_drag_context_get_actions (context) & GDK_ACTION_MOVE)
		action = GDK_ACTION_MOVE;
	else
		action = gdk_drag_context_get_suggested_action (context);

	if (path) {
		gtk_tree_view_set_drag_dest_row (tree_view, path, pos);
		if (priv_data->dest_flags & RB_TREE_DEST_SELECT_ON_DRAG_TIMEOUT) {
			if (priv_data->previous_dest_path != NULL) {
				gtk_tree_path_free (priv_data->previous_dest_path);
			}
			priv_data->previous_dest_path = path;
			if (priv_data->select_on_drag_timeout == 0) {
				rb_debug("Setting up a new select on drag timeout");
				priv_data->select_on_drag_timeout = g_timeout_add_seconds (2, select_on_drag_timeout, tree_view);
			}
		} else {
			gtk_tree_path_free (path);
		}
	}

	gdk_drag_status (context, action, time);

	return TRUE;
}


static gboolean
rb_tree_dnd_drag_leave_cb (GtkWidget        *widget,
                           GdkDragContext   *context,
                           gint              x,
                           gint              y,
                           guint             time)
{
	remove_select_on_drag_timeout(GTK_TREE_VIEW (widget));
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

	remove_scroll_timeout (tree_view);

	/* Unset this thing */
	gtk_tree_view_set_drag_dest_row (tree_view,
               				 NULL,
					 GTK_TREE_VIEW_DROP_BEFORE);

	if (path || priv_data->dest_flags & RB_TREE_DEST_EMPTY_VIEW_DROP) {

		GdkAtom target;
		RbTreeDragDestIface *iface = RB_TREE_DRAG_DEST_GET_IFACE (model);
		if (iface->rb_get_drag_target) {
			RbTreeDragDest *dest = RB_TREE_DRAG_DEST (model);
			target = (* iface->rb_get_drag_target) (dest, widget, 
								context, path, 
								priv_data->dest_target_list);
		} else {
			target = gtk_drag_dest_find_target (widget, context, 
							    priv_data->dest_target_list);
		}

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
	gboolean filtered = TRUE;
	gboolean accepted = FALSE;

	tree_view = GTK_TREE_VIEW (widget);
	model = gtk_tree_view_get_model (tree_view);

	gtk_tree_view_get_dest_row_at_pos (tree_view, x, y, &dest_row, &pos);

	if (dest_row)
		if (!filter_drop_position (widget, context, dest_row, &pos))
			filtered = FALSE;

	if (filtered && (gtk_selection_data_get_length (selection_data) >= 0))
	{
		if (rb_tree_drag_dest_drag_data_received (RB_TREE_DRAG_DEST (model),
                					  dest_row,
							  pos,
                					  selection_data))
        		accepted = TRUE;
	}

	gtk_drag_finish (context,
        		 accepted,
			 (gdk_drag_context_get_selected_action (context) == GDK_ACTION_MOVE),
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
	if (event->window != gtk_tree_view_get_bin_window (tree_view))
		return FALSE;

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

/**
 * rb_tree_dnd_add_drag_source_support:
 * @tree_view: a #GtkTreeView that wants to be a drag source
 * @start_button_mask: a mask describing modifier keys to handle when dragging
 * @targets: an array of #GtkTargetEntry structures describing drag data types
 * @n_targets: the number of elements in @targets
 * @actions: a mask describing drag actions that are allowed from this source
 *
 * Adds event handlers to perform multi-row drag and drop operations from the
 * specified #GtkTreeView widget.  The model backing the #GtkTreeView must
 * implement the #RbTreeDragSource interface.  This should be called immediately
 * after the tree view is created.
 */
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

/**
 * rb_tree_dnd_add_drag_dest_support:
 * @tree_view: a #GtkTreeView that wants to be a drag destination
 * @flags: #RbTreeDestFlags for this drag destination
 * @targets: an array of #GtkTargetEntry structures describing the allowed drag targets
 * @n_targets: the number of elements in @targets
 * @actions: the allowable drag actions for this destination
 *
 * Adds event handlers to perform multi-row drag and drop operations to the specified
 * #GtkTreeView.  The model backing the tree view should implement the #RbTreeDragDest
 * interface.  This should be called immediately after the tree view is created.
 */
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

	if (!priv_data->drag_motion_handler) {

		priv_data->dest_target_list = gtk_target_list_new (targets, n_targets);
		priv_data->dest_actions = actions;
		priv_data->dest_flags = flags;

		gtk_drag_dest_set (GTK_WIDGET (tree_view),
				   0,
				   NULL,
				   0,
				   actions);

		priv_data->drag_motion_handler = g_signal_connect (G_OBJECT (tree_view),
								  "drag_motion",
								  G_CALLBACK (rb_tree_dnd_drag_motion_cb),
								  NULL);
		priv_data->drag_leave_handler = g_signal_connect (G_OBJECT (tree_view),
								  "drag_leave",
								  G_CALLBACK (rb_tree_dnd_drag_leave_cb),
								  NULL);
		priv_data->drag_drop_handler = g_signal_connect (G_OBJECT (tree_view),
								 "drag_drop",
								 G_CALLBACK (rb_tree_dnd_drag_drop_cb),
								 NULL);
	  	priv_data->drag_data_received_handler = g_signal_connect (G_OBJECT (tree_view),
									  "drag_data_received",
									  G_CALLBACK (rb_tree_dnd_drag_data_received_cb),
									  NULL);
	}
}

