/* gtktreemodelfilter.h
 * Copyright (C) 2000,2001  Red Hat, Inc., Jonathan Blandford <jrb@redhat.com>
 * Copyright (C) 2001,2002  Kristian Rietveld <kris@gtk.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __GTK_TREE_MODEL_FILTER_H__
#define __GTK_TREE_MODEL_FILTER_H__

#include <gtk/gtktreemodel.h>

G_BEGIN_DECLS

#define GTK_TYPE_TREE_MODEL_FILTER             (gtk_tree_model_filter_get_type ())
#define GTK_TREE_MODEL_FILTER(obj)             (GTK_CHECK_CAST ((obj), GTK_TYPE_TREE_MODEL_FILTER, GtkTreeModelFilter))
#define GTK_TREE_MODEL_FILTER_CLASS(klass)     (GTK_CHECK_CLASS_CAST ((klass), GTK_TYPE_TREE_MODEL_FILTER, GtkTreeModelFilterClass))
#define GTK_IS_TREE_MODEL_FILTER(obj)          (GTK_CHECK_TYPE ((obj), GTK_TYPE_TREE_MODEL_FILTER))
#define GTK_IS_TREE_MODEL_FILTER_CLASS(klass)  (GTK_CHECK_CLASS_TYPE ((obj), GTK_TYPE_TREE_MODEL_FILTER))
#define GTK_TREE_MODEL_FILTER_GET_CLASS(obj)   (GTK_CHECK_GET_CLASS ((obj), GTK_TYPE_TREE_MODEL_FILTER, GtkTreeModelFilterClass))

typedef struct _GtkTreeModelFilter       GtkTreeModelFilter;
typedef struct _GtkTreeModelFilterClass  GtkTreeModelFilterClass;

struct _GtkTreeModelFilter
{
  GObject parent;

  /* < private > */
  gpointer root;
  gint stamp;
  guint child_flags;
  GtkTreeModel *child_model;
  gint zero_ref_count;

  gint filter_column;

  gpointer filter_func;
  gpointer user_data;

  GtkTreePath *virtual_root;

  /* signal ids */
  guint changed_id;
  guint inserted_id;
  guint has_child_toggled_id;
  guint deleted_id;
  guint reordered_id;
};

struct _GtkTreeModelFilterClass
{
  GObjectClass parent_class;
};

GType         gtk_tree_model_filter_get_type                   (void);
GtkTreeModel *gtk_tree_model_filter_new_with_model             (GtkTreeModel       *child_model,
								gint                filter_column,
								GtkTreePath        *virtual_root);
void          gtk_tree_model_filter_clear_cache                (GtkTreeModelFilter *tree_model_filter);

void          gtk_tree_model_filter_convert_child_iter_to_iter (GtkTreeModelFilter *tree_model_filter,
								GtkTreeIter        *filter_iter,
								GtkTreeIter        *child_iter);
void          gtk_tree_model_filter_convert_iter_to_child_iter (GtkTreeModelFilter *tree_model_filter,
								GtkTreeIter        *child_iter,
								GtkTreeIter        *filtered_iter);
GtkTreePath  *gtk_tree_model_filter_convert_child_path_to_path (GtkTreeModelFilter *tree_model_filter,
								GtkTreePath        *child_path);
GtkTreePath  *gtk_tree_model_filter_convert_path_to_child_path (GtkTreeModelFilter *tree_model_filter,
								GtkTreePath        *filtered_path);

G_END_DECLS

#endif /* __GTK_TREE_MODEL_FILTER_H__ */
