/*
 *  arch-tag: Implementation of spiced up, bug-injected, hacker-hated, gtktreeview hack
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

#include "rb-tree-view.h"
#include "rb-tree-view-column.h"
#include "gtktreeprivate.h"

static void rb_tree_view_class_init (RBTreeViewClass *klass);
static void gtk_tree_view_size_allocate (GtkWidget     *widget,
			                 GtkAllocation *allocation);

static GObjectClass *parent_class = NULL;

GType
rb_tree_view_get_type (void)
{
	static GType rb_tree_view_type = 0;

	if (rb_tree_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTreeViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_tree_view_class_init,
			NULL,
			NULL,
			sizeof (RBTreeView),
			0,
			NULL
		};

		rb_tree_view_type = g_type_register_static (GTK_TYPE_TREE_VIEW,
						         "RBTreeView",
						         &our_info, 0);
	}

	return rb_tree_view_type;
}

static void
rb_tree_view_class_init (RBTreeViewClass *klass)
{
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	widget_class->size_allocate = gtk_tree_view_size_allocate;
}

RBTreeView *
rb_tree_view_new (void)
{
	return RB_TREE_VIEW (g_object_new (RB_TYPE_TREE_VIEW, 
					   "enable-search", FALSE, 
					   NULL));
}

RBTreeView *
rb_tree_view_new_with_model (GtkTreeModel *model)
{
	RBTreeView *tree_view;

	tree_view = RB_TREE_VIEW (g_object_new (RB_TYPE_TREE_VIEW,
						"model", model,
						NULL));

	return tree_view;
}

/* don't try this at home! */
typedef struct _GtkTreeViewChild GtkTreeViewChild;
struct _GtkTreeViewChild
{
  GtkWidget *widget;
  gint x;
  gint y;
  gint width;
  gint height;
};

/* GtkWidget::size_allocate helper */
static void
gtk_tree_view_size_allocate_columns (GtkWidget *widget)
{
  GtkTreeView *tree_view;
  GList *list, *last_column = NULL;
  GtkTreeViewColumn *column;
  GtkAllocation allocation;
  gint width = 0, n_expand_columns = 0, left_over_width = 0, total_requested_width = 0;
  gint min_width, expand_col_num, n_cols = 0;
  gboolean invalidate_all = FALSE;
  GArray *expand_col_widths;

  tree_view = GTK_TREE_VIEW (widget);

  allocation.y = 0;
  allocation.height = tree_view->priv->header_height;

  expand_col_widths = g_array_new (FALSE, FALSE, sizeof (int));

  for (list = tree_view->priv->columns; list != NULL; list = list->next)
    {
      int col_width = 0;
      column = list->data;
      if (!column->visible)
	continue;

      /* We need to handle the dragged button specially.
       */
      if (column == tree_view->priv->drag_column)
	{
	  GtkAllocation drag_allocation;
	  gdk_drawable_get_size (tree_view->priv->drag_window,
				 &(drag_allocation.width),
				 &(drag_allocation.height));
	  drag_allocation.x = 0;
	  drag_allocation.y = 0;
	  gtk_widget_size_allocate (tree_view->priv->drag_column->button,
				    &drag_allocation);
	  total_requested_width += drag_allocation.width;
	  continue;
	}

      if (column->use_resized_width)
	{
          col_width = column->resized_width;
	}
      else if (column->column_type == GTK_TREE_VIEW_COLUMN_FIXED)
	{
	  col_width = column->fixed_width;
	}
      else if (GTK_TREE_VIEW_FLAG_SET (tree_view, GTK_TREE_VIEW_HEADERS_VISIBLE))
	{
	  col_width = MAX (column->requested_width, column->button_request);
	}
      else
	{
          if (column->requested_width > 0)
            col_width = column->requested_width;
	}

      total_requested_width += col_width;

      if (rb_tree_view_column_get_expand (RB_TREE_VIEW_COLUMN (column)) == TRUE)
        {
          n_expand_columns++;

	  g_array_append_val (expand_col_widths, col_width);
        }

      n_cols++;
      last_column = list;
    }

  left_over_width = widget->allocation.width - total_requested_width;

  min_width = widget->allocation.width / n_cols;

  /* gently resize */
  while (left_over_width < 0)
    {
      gboolean did_something = FALSE;
      int i;

      for (i = 0; i < expand_col_widths->len && left_over_width < 0; i++)
        {
	  int size = g_array_index (expand_col_widths, int, i);

	  if (size > min_width)
	    { 
	      size--;
	      left_over_width++;
	      did_something = TRUE;
	    }

	  g_array_index (expand_col_widths, int, i) = size;
	}

      if (did_something == FALSE)
        break;
    }

  expand_col_num = 0;

  for (list = tree_view->priv->columns; list != NULL; list = list->next)
    {
      gint real_requested_width = 0, orig_width;
      column = list->data;
      if (!column->visible)
	continue;

      /* We need to handle the dragged button specially.
       */
      if (column == tree_view->priv->drag_column)
	{
	  GtkAllocation drag_allocation;
	  gdk_drawable_get_size (tree_view->priv->drag_window,
				 &(drag_allocation.width),
				 &(drag_allocation.height));
	  drag_allocation.x = 0;
	  drag_allocation.y = 0;
	  gtk_widget_size_allocate (tree_view->priv->drag_column->button,
				    &drag_allocation);
	  width += drag_allocation.width;
	  continue;
	}

      if (column->use_resized_width)
	{
	  real_requested_width = column->resized_width;
	}
      else if (column->column_type == GTK_TREE_VIEW_COLUMN_FIXED)
	{
	  real_requested_width = column->fixed_width;
	}
      else if (GTK_TREE_VIEW_FLAG_SET (tree_view, GTK_TREE_VIEW_HEADERS_VISIBLE))
	{
	  real_requested_width = MAX (column->requested_width, column->button_request);
	}
      else
	{
	  real_requested_width = column->requested_width;
	  if (real_requested_width < 0)
	    real_requested_width = 0;
	}

      if (column->min_width != -1)
	real_requested_width = MAX (real_requested_width, column->min_width);
      if (column->max_width != -1)
	real_requested_width = MIN (real_requested_width, column->max_width);

      allocation.x = width;
      orig_width = column->width;

      if (list == last_column)
        {
          int newsize;

	  newsize = widget->allocation.width - width;

	  if (real_requested_width < newsize)
            column->width = newsize;
	  else
            column->width = real_requested_width;
        }
      else if (rb_tree_view_column_get_expand (RB_TREE_VIEW_COLUMN (column)) == TRUE)
        {
          if (left_over_width > 0)
            column->width = real_requested_width + (left_over_width / n_expand_columns);
	  else
            column->width = g_array_index (expand_col_widths, int, expand_col_num);
	  expand_col_num++;
        }
      else
        column->width = real_requested_width;

      if (column->width > widget->allocation.width)
        column->width = widget->allocation.width;

      g_object_notify (G_OBJECT (column), "width");
      allocation.width = column->width;
      gtk_widget_size_allocate (column->button, &allocation);
      if (column->window)
	gdk_window_move_resize (column->window,
                                allocation.x + allocation.width - TREE_VIEW_DRAG_WIDTH/2,
				allocation.y,
                                TREE_VIEW_DRAG_WIDTH, allocation.height);

      if ((column->width != orig_width || invalidate_all) && widget->window)
        {
	  /* invalidate */
          GdkRectangle invalid_rect;

	  invalid_rect.x = width;
	  invalid_rect.y = 0;
	  invalid_rect.width = column->width;
	  invalid_rect.height = widget->allocation.height;

	  gdk_window_invalidate_rect (widget->window, &invalid_rect, TRUE);

	  invalidate_all = TRUE;
        }
      width += column->width;
    }

  g_array_free (expand_col_widths, FALSE);
}

static void
gtk_tree_view_size_allocate (GtkWidget     *widget,
			     GtkAllocation *allocation)
{
  GList *tmp_list;
  GtkTreeView *tree_view;

  g_return_if_fail (GTK_IS_TREE_VIEW (widget));

  widget->allocation = *allocation;

  tree_view = GTK_TREE_VIEW (widget);

  tmp_list = tree_view->priv->children;

  while (tmp_list)
    {
      GtkAllocation allocation;

      GtkTreeViewChild *child = tmp_list->data;
      tmp_list = tmp_list->next;

      /* totally ignore our childs requisition */
      allocation.x = child->x;
      allocation.y = child->y;
      allocation.width = child->width;
      allocation.height = child->height;
      gtk_widget_size_allocate (child->widget, &allocation);
    }


  tree_view->priv->hadjustment->page_size = allocation->width;
  tree_view->priv->hadjustment->page_increment = allocation->width * 0.9;
  tree_view->priv->hadjustment->step_increment = allocation->width * 0.1;
  tree_view->priv->hadjustment->lower = 0;
  tree_view->priv->hadjustment->upper = MAX (tree_view->priv->hadjustment->page_size, allocation->width);

  if (tree_view->priv->hadjustment->value + allocation->width > tree_view->priv->width)
    tree_view->priv->hadjustment->value = MAX (tree_view->priv->width - allocation->width, 0);
  gtk_adjustment_changed (tree_view->priv->hadjustment);

  tree_view->priv->vadjustment->page_size = allocation->height - TREE_VIEW_HEADER_HEIGHT (tree_view);
  tree_view->priv->vadjustment->step_increment = tree_view->priv->vadjustment->page_size * 0.1;
  tree_view->priv->vadjustment->page_increment = tree_view->priv->vadjustment->page_size * 0.9;
  tree_view->priv->vadjustment->lower = 0;
  tree_view->priv->vadjustment->upper = MAX (tree_view->priv->vadjustment->page_size, tree_view->priv->height);

  if (tree_view->priv->vadjustment->value + allocation->height - TREE_VIEW_HEADER_HEIGHT (tree_view) > tree_view->priv->height)
    gtk_adjustment_set_value (tree_view->priv->vadjustment,
			      MAX (tree_view->priv->height - tree_view->priv->vadjustment->page_size, 0));
  gtk_adjustment_changed (tree_view->priv->vadjustment);
  
  if (GTK_WIDGET_REALIZED (widget))
    {
      gdk_window_move_resize (widget->window,
			      allocation->x, allocation->y,
			      allocation->width, allocation->height);
      gdk_window_move_resize (tree_view->priv->header_window,
			      - (gint) tree_view->priv->hadjustment->value,
			      0,
			      MAX (tree_view->priv->width, allocation->width),
			      tree_view->priv->header_height);
      gdk_window_move_resize (tree_view->priv->bin_window,
			      - (gint) tree_view->priv->hadjustment->value,
			      TREE_VIEW_HEADER_HEIGHT (tree_view),
			      MAX (tree_view->priv->width, allocation->width),
			      allocation->height - TREE_VIEW_HEADER_HEIGHT (tree_view));
    }

  gtk_tree_view_size_allocate_columns (widget);
}
