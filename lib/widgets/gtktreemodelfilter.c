/* gtktreemodelfilter.c
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

#include "gtktreemodelfilter.h"
#include <gtk/gtksignal.h>
#include <string.h>

//#define VERBOSE 1

/* ITER FORMAT:
 *
 * iter->stamp = tree_model_filter->stamp
 * iter->user_data = FilterLevel
 * iter->user_data2 = FilterElt
 */

typedef struct _FilterElt FilterElt;
typedef struct _FilterLevel FilterLevel;

struct _FilterElt
{
  GtkTreeIter iter;
  FilterLevel *children;
  gint offset;
  gint ref_count;
  gint zero_ref_count;
  gboolean visible;
};

struct _FilterLevel
{
  GArray *array;
  gint ref_count;
  FilterElt *parent_elt;
  FilterLevel *parent_level;
};

/* properties */
enum {
  PROP_0,
  /* construct args */
  PROP_MODEL,
  PROP_FILTER_COLUMN,
  PROP_VROOT
};

#define GTK_TREE_MODEL_FILTER_CACHE_CHILD_ITERS(tree_model_filter) \
        (((GtkTreeModelFilter *)tree_model_filter)->child_flags & GTK_TREE_MODEL_ITERS_PERSIST)
#define FILTER_ELT(filter_elt) ((FilterElt *)filter_elt)
#define FILTER_LEVEL(filter_level) ((FilterLevel *)filter_level)

#define GET_CHILD_ITER(tree_model_filter,child_iter,filter_iter) gtk_tree_model_filter_convert_iter_to_child_iter(GTK_TREE_MODEL_FILTER (tree_model_filter), child_iter, filter_iter);

/* general code (object/interface init, properties, etc) */
static void gtk_tree_model_filter_init            (GtkTreeModelFilter      *tree_model_filter);
static void gtk_tree_model_filter_class_init      (GtkTreeModelFilterClass *tree_model_filter_class);
static void gtk_tree_model_filter_tree_model_init (GtkTreeModelIface       *iface);
static void gtk_tree_model_filter_finalize        (GObject                 *object);
static void gtk_tree_model_filter_set_property    (GObject                 *object,
						   guint                    prop_id,
						   const GValue            *value,
						   GParamSpec              *pspec);
static void gtk_tree_model_filter_get_property    (GObject                 *object,
						   guint                    prop_id,
						   GValue                  *value,
						   GParamSpec              *pspec);

/* signal handlers */
static void gtk_tree_model_filter_row_changed           (GtkTreeModel          *model,
							 GtkTreePath           *start_path,
							 GtkTreeIter           *start_iter,
							 gpointer               data);
static void gtk_tree_model_filter_row_inserted          (GtkTreeModel          *model,
							 GtkTreePath           *path,
							 GtkTreeIter           *iter,
							 gpointer               data);
static void gtk_tree_model_filter_row_has_child_toggled (GtkTreeModel          *model,
							 GtkTreePath           *path,
							 GtkTreeIter           *iter,
							 gpointer               data);
static void gtk_tree_model_filter_row_deleted           (GtkTreeModel          *model,
							 GtkTreePath           *path,
							 gpointer               data);
static void gtk_tree_model_filter_rows_reordered        (GtkTreeModel          *s_model,
							 GtkTreePath           *s_path,
							 GtkTreeIter           *s_iter,
							 gint                  *new_order,
							 gpointer               data);

/* TreeModel interface */
static guint        gtk_tree_model_filter_get_flags          (GtkTreeModel          *tree_model);
static gint         gtk_tree_model_filter_get_n_columns      (GtkTreeModel          *tree_model);
static GType        gtk_tree_model_filter_get_column_type    (GtkTreeModel          *tree_model,
							      gint                   index);
static gboolean     gtk_tree_model_filter_get_iter           (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      GtkTreePath           *path);
static GtkTreePath *gtk_tree_model_filter_get_path           (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static void         gtk_tree_model_filter_get_value          (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      gint                   column,
							      GValue                *value);
static gboolean     gtk_tree_model_filter_iter_next          (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static gboolean     gtk_tree_model_filter_iter_children      (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      GtkTreeIter           *parent);
static gboolean     gtk_tree_model_filter_iter_has_child     (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static gint         gtk_tree_model_filter_iter_n_children    (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static gboolean     gtk_tree_model_filter_iter_nth_child     (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      GtkTreeIter           *parent,
							      gint                   n);
static gboolean     gtk_tree_model_filter_iter_parent        (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      GtkTreeIter           *child);
static void         gtk_tree_model_filter_ref_node           (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static void         gtk_tree_model_filter_unref_node         (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter);
static void         gtk_tree_model_filter_real_unref_node    (GtkTreeModel          *tree_model,
							      GtkTreeIter           *iter,
							      gboolean               propagate_unref);

/* Private functions */
static void         gtk_tree_model_filter_build_level       (GtkTreeModelFilter *tree_model_filter,
							     FilterLevel        *parent_level,
							     FilterElt          *parent_elt);
static void         gtk_tree_model_filter_free_level        (GtkTreeModelFilter *tree_model_filter,
							     FilterLevel        *filter_level);
static void         gtk_tree_model_filter_increment_stamp   (GtkTreeModelFilter *tree_model_filter);
static gboolean     gtk_tree_model_filter_visible           (GtkTreeModelFilter *tree_model_filter,
							     GtkTreeIter        *child_iter);
static GtkTreePath *gtk_tree_model_filter_elt_get_path      (FilterLevel        *level,
							     FilterElt          *elt,
							     GtkTreePath        *root);
GtkTreeModel       *gtk_tree_model_filter_get_model         (GtkTreeModelFilter *tree_model);
static void         gtk_tree_model_filter_set_model         (GtkTreeModelFilter *tree_model_filter,
                                                             GtkTreeModel       *child_model);
static void         gtk_tree_model_filter_set_filter_column (GtkTreeModelFilter *tree_model_filter,
                                                             gint                filter_column);
static void         gtk_tree_model_filter_set_root          (GtkTreeModelFilter *filter,
                                                             GtkTreePath        *root);

static GtkTreePath *gtk_real_tree_model_filter_convert_child_path_to_path (GtkTreeModelFilter *tree_model_filter,
									   GtkTreePath        *child_path,
									   gboolean            build_levels,
									   gboolean            fetch_childs);
static void         gtk_tree_model_filter_remove                          (GtkTreeModelFilter *filter,
									   GtkTreeIter        *iter,
									   gboolean            emit_signal);
static gboolean     gtk_tree_model_filter_fetch_child                     (GtkTreeModelFilter *filter,
									   FilterLevel        *level,
									   gint                offset);
static void         gtk_tree_model_filter_row_back_child_updater          (GtkTreeModelFilter *filter,
									   FilterLevel        *level,
									   FilterElt          *elt);
static void         gtk_tree_path_add                                     (GtkTreePath        *path1,
									   GtkTreePath        *path2);
static GtkTreePath *gtk_tree_path_apply_root                              (GtkTreePath        *path,
									   GtkTreePath        *root);

static GObjectClass *parent_class = NULL;

GType
gtk_tree_model_filter_get_type (void)
{
  static GType tree_model_filter_type = 0;

  if (!tree_model_filter_type)
    {
      static const GTypeInfo tree_model_filter_info =
      {
        sizeof (GtkTreeModelFilterClass),
        NULL,           /* base_init */
        NULL,           /* base_finalize */
        (GClassInitFunc) gtk_tree_model_filter_class_init,
        NULL,           /* class_finalize */
        NULL,           /* class_data */
        sizeof (GtkTreeModelFilter),
        0,              /* n_preallocs */
        (GInstanceInitFunc) gtk_tree_model_filter_init
      };

      static const GInterfaceInfo tree_model_info =
      {
        (GInterfaceInitFunc) gtk_tree_model_filter_tree_model_init,
        NULL,
        NULL
      };

      tree_model_filter_type = g_type_register_static (G_TYPE_OBJECT,
                                                       "GtkTreeModelFilter",
                                                       &tree_model_filter_info, 0);

      g_type_add_interface_static (tree_model_filter_type,
                                   GTK_TYPE_TREE_MODEL,
                                   &tree_model_info);
    }

  return tree_model_filter_type;
}

static void
gtk_tree_model_filter_init (GtkTreeModelFilter *tree_model_filter)
{
  tree_model_filter->filter_column = -1;
  tree_model_filter->filter_func = NULL;
  tree_model_filter->user_data = NULL;
  tree_model_filter->stamp = 0;
  tree_model_filter->zero_ref_count = 0;
  tree_model_filter->root = NULL;
}

static void
gtk_tree_model_filter_class_init (GtkTreeModelFilterClass *tree_model_filter_class)
{
  GObjectClass *object_class;

  object_class = (GObjectClass *) tree_model_filter_class;
  parent_class = g_type_class_peek_parent (tree_model_filter_class);

  object_class->set_property = gtk_tree_model_filter_set_property;
  object_class->get_property = gtk_tree_model_filter_get_property;

  object_class->finalize = gtk_tree_model_filter_finalize;

  /* Properties */
  g_object_class_install_property (object_class,
                                   PROP_MODEL,
                                   g_param_spec_object ("model",
							/* following 2 strings need translation? */
                                                        "TreeModelFilter model",
                                                        "The model for the TreeModelFilter to filter",
                                                        GTK_TYPE_TREE_MODEL,
                                                        G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
		  		   PROP_FILTER_COLUMN,
				   g_param_spec_int ("filter_column",
						     /* ditto */
						     "TreeModelFilter filter column",
						     "Column in the child model which should be used for the filtering information. This column should store Boolean values",
						     0, INT_MAX,
						     G_TYPE_INT,
						     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
  g_object_class_install_property (object_class,
		  		   PROP_VROOT,
				   g_param_spec_boxed ("virtual_root",
						       /* ditto */
						       "TreeModelFilter vroot path",
						       "Path which points to the parent of the virtual root",
						       GTK_TYPE_TREE_PATH,
						       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
gtk_tree_model_filter_tree_model_init (GtkTreeModelIface *iface)
{
  iface->get_flags = gtk_tree_model_filter_get_flags;
  iface->get_n_columns = gtk_tree_model_filter_get_n_columns;
  iface->get_column_type = gtk_tree_model_filter_get_column_type;
  iface->get_iter = gtk_tree_model_filter_get_iter;
  iface->get_path = gtk_tree_model_filter_get_path;
  iface->get_value = gtk_tree_model_filter_get_value;
  iface->iter_next = gtk_tree_model_filter_iter_next;
  iface->iter_children = gtk_tree_model_filter_iter_children;
  iface->iter_has_child = gtk_tree_model_filter_iter_has_child;
  iface->iter_n_children = gtk_tree_model_filter_iter_n_children;
  iface->iter_nth_child = gtk_tree_model_filter_iter_nth_child;
  iface->iter_parent = gtk_tree_model_filter_iter_parent;
  iface->ref_node = gtk_tree_model_filter_ref_node;
  iface->unref_node = gtk_tree_model_filter_unref_node;
}

/**
 * gtk_tree_model_filter_new_with_model:
 * @child_model: A #GtkTreeModel
 * @filter_column: column in the child model which contains filter information
 * @root: the virtual root, or %NULL for the regular root.
 *
 * Creates a new #GtkTreeModel, with @child_model as the child_model.
 *
 * Return value: A new #GtkTreeModel.
 */
GtkTreeModel *
gtk_tree_model_filter_new_with_model (GtkTreeModel *child_model,
                                      gint          filter_column,
				      GtkTreePath  *root)
{
  GtkTreeModel *retval;

  g_return_val_if_fail (GTK_IS_TREE_MODEL (child_model), NULL);

  retval = GTK_TREE_MODEL (g_object_new (gtk_tree_model_filter_get_type (), NULL));

  gtk_tree_model_filter_set_model (GTK_TREE_MODEL_FILTER (retval), child_model);
  gtk_tree_model_filter_set_filter_column (GTK_TREE_MODEL_FILTER (retval), filter_column);
  gtk_tree_model_filter_set_root (GTK_TREE_MODEL_FILTER (retval), root);

  return retval;
}

/* GObject callbacks */
static void
gtk_tree_model_filter_finalize (GObject *object)
{
  GtkTreeModelFilter *tree_model_filter = (GtkTreeModelFilter *) object;

  gtk_tree_model_filter_set_model (tree_model_filter, NULL);

  if (tree_model_filter->virtual_root)
    gtk_tree_path_free (tree_model_filter->virtual_root);

  if (tree_model_filter->root)
    gtk_tree_model_filter_free_level (tree_model_filter,
                                      tree_model_filter->root);

  /* must chain up */
  parent_class->finalize (object);
}

static void
gtk_tree_model_filter_set_property (GObject      *object,
                                    guint         prop_id,
                                    const GValue *value,
                                    GParamSpec   *pspec)
{
  GtkTreeModelFilter *tree_model_filter = GTK_TREE_MODEL_FILTER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      gtk_tree_model_filter_set_model (tree_model_filter, g_value_get_object (value));
      break;
    case PROP_FILTER_COLUMN:
      gtk_tree_model_filter_set_filter_column (tree_model_filter, g_value_get_int (value));
      break;
    case PROP_VROOT:
      gtk_tree_model_filter_set_root (tree_model_filter, g_value_get_boxed (value));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

static void
gtk_tree_model_filter_get_property (GObject    *object,
                                    guint       prop_id,
                                    GValue     *value,
                                    GParamSpec *pspec)
{
  GtkTreeModelFilter *tree_model_filter = GTK_TREE_MODEL_FILTER (object);

  switch (prop_id)
    {
    case PROP_MODEL:
      g_value_set_object (value, gtk_tree_model_filter_get_model(tree_model_filter));
      break;
    case PROP_FILTER_COLUMN:
      g_value_set_int (value, tree_model_filter->filter_column);
      break;
    case PROP_VROOT:
      g_value_set_boxed (value, tree_model_filter->virtual_root);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    }
}

/* helpers */
static void
gtk_tree_model_filter_row_back_child_updater (GtkTreeModelFilter *filter,
                                              FilterLevel        *level,
                                              FilterElt          *elt)
{
  int i, len;
  GArray *a = NULL;
  GtkTreeIter childs;

#ifdef VERBOSE
  g_print ("row back child updater v1.0\n");

  if (!elt->visible)
    g_print ("elt not visible, bailing out\n");
#endif

  if (!elt->visible)
    return;

  if (elt->children)
    a = elt->children->array;

  {
    GtkTreeIter iter;
    GtkTreeIter foo;

    iter.stamp = filter->stamp;
    iter.user_data = level;
    iter.user_data2 = elt;

    gtk_tree_model_filter_convert_iter_to_child_iter (filter, &foo, &iter);

    len = gtk_tree_model_iter_n_children (filter->child_model, &foo);
    gtk_tree_model_iter_children (filter->child_model, &childs, &foo);
  }

  for (i = 0; i < len; i++)
    {
      gint j, index = -1;
      GValue val = {0,};

      if (a)
        {
          for (j = 0; j < a->len; j++)
            if (g_array_index (a, FilterElt, j).offset == i)
              {
                index = i;
                break;
              }
        }
      
      gtk_tree_model_get_value (filter->child_model, &childs,
				filter->filter_column, &val);
      
      if (g_value_get_boolean (&val))
        {
          /* insert row */
          if (index == -1)
            {
              FilterElt *e;
              GtkTreePath *path;
              GtkTreeIter it, tmp;

              /* bring it back in */
#ifdef VERBOSE
              g_print ("need to bring row back in...\n");
#endif
              if (!a && !elt->children)
                {
                  FilterLevel *new_level = NULL;

                  new_level = g_new (FilterLevel, 1);
                  new_level->array = g_array_sized_new (FALSE, FALSE, sizeof (FilterElt), 0);
                  new_level->ref_count = 0;
                  new_level->parent_elt = elt;
                  new_level->parent_level = level;

                  elt->children = new_level;
                  a = elt->children->array;
                }

              gtk_tree_model_filter_fetch_child (filter,
                  elt->children, i);

              for (j = 0; j < a->len; j++)
                if (g_array_index (a, FilterElt, j).offset == i)
                  {
                    index = j;
                    break;
                  }

	      if (index == -1)
	        {
		  g_warning ("gtk_tree_model_filter_fetch_child failed!\n");
		  return;
		}

              e = &g_array_index (a, FilterElt, index);
              e->visible = FALSE;

	      it.stamp = filter->stamp;
	      it.user_data = elt->children;
	      it.user_data2 = e;

	      gtk_tree_model_ref_node (GTK_TREE_MODEL (filter), &it);
	      gtk_tree_model_filter_increment_stamp (filter);
	      it.stamp = filter->stamp;
	      gtk_tree_model_unref_node (GTK_TREE_MODEL (filter), &it);

	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &it);

	      e->visible = TRUE;
              gtk_tree_model_row_inserted (GTK_TREE_MODEL (filter), path, &it);
              gtk_tree_path_free (path);

              if (gtk_tree_model_iter_children (GTK_TREE_MODEL (filter), &tmp, &it))
                gtk_tree_model_filter_row_back_child_updater (filter,
                  elt->children, e);

	      it.user_data = level;
	      it.user_data2 = elt;
	      path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &it);
	      gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (filter), path, &it);
	      gtk_tree_path_free (path);
            }
          else
            {
              FilterElt *e = &g_array_index (a, FilterElt, index);
              GtkTreePath *path;
              GtkTreeIter i, tmp;

	      if (!e->visible)
	        {
		  e->visible = FALSE;

#ifdef VERBOSE
		  g_print ("-> row back\n");
#endif

	          gtk_tree_model_filter_increment_stamp (filter);

	          i.stamp = filter->stamp;
	          i.user_data = elt->children;
	          i.user_data2 = e;

	          path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &i);
	          e->visible = TRUE;
	          gtk_tree_model_row_inserted (GTK_TREE_MODEL (filter), path, &i);
	          gtk_tree_path_free (path);

                  if (gtk_tree_model_iter_children (GTK_TREE_MODEL (filter), &tmp, &i))
                    gtk_tree_model_filter_row_back_child_updater (filter,
								  elt->children, e);

		  i.user_data = level;
		  i.user_data2 = elt;
		  path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &i);
		  gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (filter), path, &i);
		  gtk_tree_path_free (path);
		}
            }
        }
      else
        {
          /* remove the iter */
          if (index != -1)
            {
              FilterElt *e = &g_array_index (a, FilterElt, index);
              GtkTreeIter i;

#ifdef VERBOSE
              g_print ("removing iter in silence\n");
#endif

	      i.stamp = filter->stamp;
	      i.user_data = elt->children;
	      i.user_data2 = e;

              gtk_tree_model_filter_remove (filter, &i, FALSE);
            }
        }

      gtk_tree_model_iter_next (filter->child_model, &childs);
    }
}

static void
gtk_tree_model_filter_remove (GtkTreeModelFilter *filter,
                              GtkTreeIter        *iter,
                              gboolean            emit_signal)
{
  FilterElt *elt, *parent;
  FilterLevel *level, *parent_level;
  gint offset, i, length;

  level = FILTER_LEVEL (iter->user_data);
  elt = FILTER_ELT (iter->user_data2);
  parent = level->parent_elt;
  parent_level = level->parent_level;
  length = level->array->len;

  offset = elt->offset;

#ifdef VERBOSE
  g_print ("------- remove \n");
#endif

  /* ref couting */
  while (elt->ref_count > 0)
    gtk_tree_model_filter_real_unref_node (GTK_TREE_MODEL (filter), iter, FALSE);

  if (emit_signal)
    {
      GtkTreePath *path;

      path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), iter);
      gtk_tree_model_filter_increment_stamp (filter);
      gtk_tree_model_row_deleted (GTK_TREE_MODEL (filter), path);
      gtk_tree_path_free (path);
    }

  if (length == 1 && emit_signal && iter->user_data != filter->root)
    {
      /* the code above destroyed the level */
      goto emit_has_child_toggled;
    }

  for (i = 0; i < level->array->len; i++)
    if (elt->offset == g_array_index (level->array, FilterElt, i).offset)
      break;

  g_array_remove_index (level->array, i);

  for (i = 0; i < level->array->len; i++)
    {
      elt = &g_array_index (level->array, FilterElt, i);
      if (elt->children)
        elt->children->parent_elt = elt;
    }

emit_has_child_toggled:
  /* children are being handled first, so we can check it this way */
  if ((parent && parent->children && parent->children->array->len <= 1)
      || (length == 1 && emit_signal && iter->user_data != filter->root))
    {
      /* we're about to remove the latest child */
      GtkTreeIter piter;
      GtkTreePath *ppath;

      piter.stamp = filter->stamp;
      piter.user_data = parent_level;
      piter.user_data2 = parent;

      ppath = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &piter);

#ifdef VERBOSE
      g_print ("emit has_child_toggled (in filter_remove)\n");
#endif
      gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (filter),
          ppath, &piter);
      gtk_tree_path_free (ppath);
    }
}

/* TreeModel signals */
static void
gtk_tree_model_filter_row_changed           (GtkTreeModel          *s_model,
                                             GtkTreePath           *start_path,
                                             GtkTreeIter           *start_iter,
                                             gpointer               data)
{
  GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER (data);
  GtkTreeIter iter;
  GtkTreeIter siter;
  GtkTreePath *path;

  FilterElt *elt;
  FilterLevel *level;
  gint offset;

  gboolean new;
  gboolean free_start_path = FALSE;

  g_return_if_fail (start_path != NULL || start_iter != NULL);

  if (!start_path)
    {
      start_path = gtk_tree_model_get_path (s_model, start_iter);
      free_start_path = TRUE;
    }

  if (!start_iter)
    gtk_tree_model_get_iter (s_model, &siter, start_path);
  else
    siter = *start_iter;

  if (!filter->root)
    {
      /* build root level and set visibility to FALSE */
      gtk_tree_model_filter_build_level (filter, NULL, NULL);

      if (filter->root)
	{
	  gint i;
	  GArray *a;

	  a = ((FilterLevel *)filter->root)->array;

	  for (i = 0; i < a->len; i++)
	    g_array_index (a, FilterElt, i).visible = FALSE;
	}
    }

  path = gtk_real_tree_model_filter_convert_child_path_to_path (filter, start_path, FALSE, TRUE);
  if (!path)
    {
      if (free_start_path)
        gtk_tree_path_free (start_path);
#ifdef VERBOSE
      g_print ("couldn't convert path, bailing out\n");
#endif
      return;
    }

#ifdef VERBOSE
  g_print ("_row_changed handling %s\n", gtk_tree_path_to_string (path));
#endif

  gtk_tree_model_get_iter (GTK_TREE_MODEL (filter), &iter, path);
  gtk_tree_path_free (path);
  if (free_start_path)
    gtk_tree_path_free (start_path);

  {
    GValue val = {0, };

    gtk_tree_model_get_value (s_model, &siter, filter->filter_column, &val);
    new = g_value_get_boolean (&val);
    g_value_unset (&val);
  }

  level = FILTER_LEVEL (iter.user_data);
  elt = FILTER_ELT (iter.user_data2);
  offset = elt->offset;

  if (elt->visible == TRUE && new == FALSE)
    {
#ifdef VERBOSE
      g_print ("visible to false -> delete row\n");
#endif
      gtk_tree_model_filter_remove (filter, &iter, TRUE);
    }
  else if (elt->visible == FALSE && new == TRUE)
    {
      GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &iter);
      GtkTreeIter tmp;

#ifdef VERBOSE
      g_print ("visible to true -> insert row\n");
#endif
      elt->visible = TRUE;

      gtk_tree_model_filter_increment_stamp (filter);
      gtk_tree_model_get_iter (GTK_TREE_MODEL (filter), &iter, path);
      gtk_tree_model_row_inserted (GTK_TREE_MODEL (filter), path, &iter);

      if (gtk_tree_model_iter_children (s_model, &tmp, start_iter))
        gtk_tree_model_filter_row_back_child_updater (filter, level, elt);

      gtk_tree_path_free (path);
    }
  else if (elt->visible == FALSE && new == FALSE)
    {
#ifdef VERBOSE
      g_print ("remove iter in silence\n");
#endif
      gtk_tree_model_filter_remove (filter, &iter, FALSE);
    }
  else
    {
      GtkTreePath *path = gtk_tree_model_get_path (GTK_TREE_MODEL (filter), &iter);
      GtkTreeIter tmp;
#ifdef VERBOSE
      g_print ("visible didn't change (== %d) -- pass row_changed\n",
	       elt->visible);
#endif
      gtk_tree_model_row_changed (GTK_TREE_MODEL (filter), path, &iter);

      if (gtk_tree_model_iter_children (s_model, &tmp, start_iter)
	  && elt->visible)
	gtk_tree_model_filter_row_back_child_updater (filter, level, elt);

      gtk_tree_path_free (path);
    }
}

static void
gtk_tree_model_filter_row_inserted          (GtkTreeModel          *s_model,
                                             GtkTreePath           *s_path,
                                             GtkTreeIter           *s_iter,
                                             gpointer               data)
{
  GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER (data);
  GtkTreePath *path;
  GtkTreePath *real_path;
  GtkTreeIter iter;

  GtkTreeIter real_s_iter;

  FilterElt *elt;
  FilterLevel *level;
  FilterLevel *parent_level;

  gint i = 0, offset, index = -1;

  gboolean free_s_path = FALSE;

  g_return_if_fail (s_path != NULL || s_iter != NULL);

  if (!s_path)
    {
      s_path = gtk_tree_model_get_path (s_model, s_iter);
      free_s_path = TRUE;
    }

  if (s_iter)
    real_s_iter = *s_iter;
  else
    gtk_tree_model_get_iter (s_model, &real_s_iter, s_path);

  /* only insert the row if it's visible */
  if (!gtk_tree_model_filter_visible (filter, s_iter))
    goto done;

  if (!filter->root)
    {
      gtk_tree_model_filter_build_level (filter, NULL, NULL);
      /* that already put the inserted iter in the level */

      goto done_and_submit;
    }

  parent_level = level = FILTER_LEVEL (filter->root);

  /* subtract virtual root if needed */
  if (filter->virtual_root)
    {
      real_path = gtk_tree_path_apply_root (s_path, filter->virtual_root);
      /* not our root */
      if (!real_path)
	goto done;
    }
  else
    real_path = gtk_tree_path_copy (s_path);

  if (gtk_tree_path_get_depth (real_path) - 1 >= 1)
    {
      /* find the parent level */
      while (i < gtk_tree_path_get_depth (real_path) - 1)
	{
	  gint j;
	  
	  if (!level)
	    /* we don't cover this signal */
	    goto done;
	  
	  if (level->array->len < gtk_tree_path_get_indices (real_path)[i])
	    {
	      g_warning ("A node was inserted with a parent that's not in the tree.\nThis possibly means that a GtkTreeModel inserted a child node before the parent was insterted.");
	      goto done;
	    }
	  
	  elt = NULL;
	  for (j = 0; j < level->array->len; j++)
	    if (g_array_index (level->array, FilterElt, j).offset == gtk_tree_path_get_indices (s_path)[i])
	      {
		elt = &g_array_index (level->array, FilterElt, j);
		break;
	      }
	  
	  if (!elt)
	    /* parent was probably filtered out */
	    goto done;
	  
	  if (!elt->children)
	    {
	      GtkTreePath *tmppath;
	      GtkTreeIter  tmpiter;
	      
	      tmpiter.stamp = filter->stamp;
	      tmpiter.user_data = level;
	      tmpiter.user_data2 = elt;
	      
	      tmppath = gtk_tree_model_get_path (GTK_TREE_MODEL (data),
						 &tmpiter);
	      
	      if (tmppath)
		{
#ifdef VERBOSE
		  g_print ("has_child_toggled in row_inserted\n");
#endif
		  gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (data),
							tmppath, &tmpiter);
		  gtk_tree_path_free (tmppath);
		}
	      
	      /* not covering this signal */
	      goto done;
	    }
	  
	  level = elt->children;
	  parent_level = level;
	  i++;
	}
    }
  
  if (!parent_level)
    goto done;
  
  /* let's try to insert the value */
  i = 0;
  offset = gtk_tree_path_get_indices (real_path)[gtk_tree_path_get_depth (real_path) - 1];
  
 if (gtk_tree_model_filter_visible (filter, &real_s_iter))
    {
      FilterElt felt;

      if (GTK_TREE_MODEL_FILTER_CACHE_CHILD_ITERS (filter))
        felt.iter = real_s_iter;
      felt.offset = offset;
      felt.zero_ref_count = 0;
      felt.ref_count = 0;
      felt.visible = FALSE;
      felt.children = NULL;

      for (i = 0; i < level->array->len; i++)
        if (g_array_index (level->array, FilterElt, i).offset > offset)
          break;

      g_array_insert_val (level->array, i, felt);
      index = i;
    }

  for (i = 0; i < level->array->len; i++)
    {
      FilterElt *e = &g_array_index (level->array, FilterElt, i);
      if ((e->offset >= offset) && i != index)
        e->offset++;
      if (e->children)
        e->children->parent_elt = e;
    }

  if (!gtk_tree_model_filter_visible (filter, &real_s_iter))
    goto done;

 done_and_submit:
  /* s_path here! AND NOT real_path, because this function does
   * root subtraction */
  path = gtk_real_tree_model_filter_convert_child_path_to_path (filter,
								s_path,
								FALSE, TRUE);
  if (!path)
    return;

  gtk_tree_model_filter_increment_stamp (filter);
  
  gtk_tree_model_get_iter (GTK_TREE_MODEL (data), &iter, path);
  gtk_tree_model_row_inserted (GTK_TREE_MODEL (data), path, &iter);

 done:
  if (free_s_path)
    gtk_tree_path_free (s_path);

  return;
}

static void
gtk_tree_model_filter_row_has_child_toggled (GtkTreeModel          *s_model,
                                             GtkTreePath           *s_path,
                                             GtkTreeIter           *s_iter,
                                             gpointer               data)
{
  GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER (data);
  GtkTreePath *path;
  GtkTreeIter iter;

  g_return_if_fail (s_path != NULL && s_iter != NULL);

  /* FIXME: this code hasn't been tested and looks broken ... I need
   * to investigate it */

  if (!gtk_tree_model_filter_visible (filter, s_iter))
    return;

  path = gtk_real_tree_model_filter_convert_child_path_to_path (filter, s_path, FALSE, TRUE);
  if (!path)
    return;

  gtk_tree_model_get_iter (GTK_TREE_MODEL (data), &iter, path);
#ifdef VERBOSE
  if (!FILTER_ELT (iter.user_data2)->visible)
    g_warning ("visible flag mismatch\n");
  g_print ("has child toggled pass on\n");
#endif
  gtk_tree_model_row_has_child_toggled (GTK_TREE_MODEL (data), path, &iter);

  gtk_tree_path_free (path);
}

static void
gtk_tree_model_filter_row_deleted           (GtkTreeModel          *s_model,
                                             GtkTreePath           *s_path,
                                             gpointer               data)
{
  GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER (data);
  GtkTreePath *path;
  GtkTreeIter iter;
  FilterElt *elt;
  FilterLevel *level;
  gint offset;
  gboolean visible;
  gint i;

  g_return_if_fail (s_path != NULL);

  path = gtk_real_tree_model_filter_convert_child_path_to_path (filter, s_path, FALSE, TRUE);
  if (!path)
    return;

  gtk_tree_model_get_iter (GTK_TREE_MODEL (data), &iter, path);

  level = FILTER_LEVEL (iter.user_data);
  elt = FILTER_ELT (iter.user_data2);
  offset = elt->offset;
  visible = elt->visible;

  if (visible)
    {
      if (level->ref_count == 0 && level != filter->root)
        {
          gtk_tree_model_filter_increment_stamp (filter);
          gtk_tree_model_row_deleted (GTK_TREE_MODEL (data), path);

          gtk_tree_path_free (path);
          return;
        }

      gtk_tree_model_filter_increment_stamp (filter);
      gtk_tree_model_row_deleted (GTK_TREE_MODEL (data), path);
      iter.stamp = filter->stamp;

      while (elt->ref_count > 0)
        gtk_tree_model_filter_real_unref_node (GTK_TREE_MODEL (data), &iter, FALSE);
    }

  /* remove the row */
  for (i = 0; i < level->array->len; i++)
    if (elt->offset == g_array_index (level->array, FilterElt, i).offset)
      break;

  offset = g_array_index (level->array, FilterElt, i).offset;
  g_array_remove_index (level->array, i);

  for (i = 0; i < level->array->len; i++)
    {
      elt = &g_array_index (level->array, FilterElt, i);
      if (elt->offset > offset)
        elt->offset--;
      if (elt->children)
        elt->children->parent_elt = elt;
    }

  gtk_tree_path_free (path);
}

static void
gtk_tree_model_filter_rows_reordered        (GtkTreeModel          *s_model,
                                             GtkTreePath           *s_path,
                                             GtkTreeIter           *s_iter,
                                             gint                  *new_order,
                                             gpointer               data)
{
  FilterElt *elt;
  FilterLevel *level;
  GtkTreeModelFilter *filter = GTK_TREE_MODEL_FILTER (data);

  GtkTreePath *path;
  GtkTreeIter iter;

  gint *tmp_array;
  gint i, j, elt_count;
  gint length;

  GArray *new_array;

  g_return_if_fail (new_order != NULL);

  if (s_path == NULL || gtk_tree_path_get_indices (s_path) == NULL)
    {
      if (filter->root == NULL)
	return;

      length = gtk_tree_model_iter_n_children (s_model, NULL);

      if (filter->virtual_root)
        {
	  gint new_pos = -1;

	  /* reorder root level of path */
	  for (i = 0; i < length; i++)
	    if (new_order[i] == gtk_tree_path_get_indices (filter->virtual_root)[0])
	      new_pos = i;

	  if (new_pos < 0)
	    return;

	  gtk_tree_path_get_indices (filter->virtual_root)[0] = new_pos;
	  return;
	}

      path = gtk_tree_path_new ();
      level = FILTER_LEVEL (filter->root);
    }
  else
    {
      GtkTreeIter child_iter;

      /* virtual root anchor reordering */
      if (filter->virtual_root &&
	  gtk_tree_path_get_depth (s_path) <
	  gtk_tree_path_get_depth (filter->virtual_root))
        {
	  gint new_pos = -1;
	  gint length;
	  gint level;
	  GtkTreeIter real_s_iter;

	  level = gtk_tree_path_get_depth (s_path);

	  if (s_iter)
	    real_s_iter = *s_iter;
	  else
	    gtk_tree_model_get_iter (s_model, &real_s_iter,
		s_path);

	  length = gtk_tree_model_iter_n_children (s_model, &real_s_iter);

	  for (i = 0; i < length; i++)
	    if (new_order[i] == gtk_tree_path_get_indices (filter->virtual_root)[level])
	      new_pos = i;

	  if (new_pos < 0)
	    return;

	  gtk_tree_path_get_indices (filter->virtual_root)[level] = new_pos;
	  return;
	}

      path = gtk_real_tree_model_filter_convert_child_path_to_path (filter, s_path, FALSE, FALSE);
      if (!path && filter->virtual_root &&
	  gtk_tree_path_compare (s_path, filter->virtual_root))
	return;

      if (!path && !filter->virtual_root)
	return;

      if (!path)
        {
	  /* root level mode */
	  if (!s_iter)
	    gtk_tree_model_get_iter (s_model, s_iter, s_path);
	  length = gtk_tree_model_iter_n_children (s_model, s_iter);
	  path = gtk_tree_path_new ();
	  level = FILTER_LEVEL (filter->root);
	}
      else
        {
          gtk_tree_model_get_iter (GTK_TREE_MODEL (data), &iter, path);

          level = FILTER_LEVEL (iter.user_data);
          elt = FILTER_ELT (iter.user_data2);

          if (!elt->children)
            {
              gtk_tree_path_free (path);
              return;
            }

          level = elt->children;

          GET_CHILD_ITER (filter, &child_iter, &iter);
          length = gtk_tree_model_iter_n_children (s_model, &child_iter);
        }
    }

  if (level->array->len < 1)
    return;

  /* note: we do not bail out here if level->array->len < 2, like
   * the GtkTreeModelSort does. This because we do some special tricky
   * reordering
   */

  /* construct a new array */
  new_array = g_array_sized_new (FALSE, FALSE, sizeof (FilterElt), level->array->len);
  tmp_array = g_new (gint, level->array->len);

#ifdef VERBOSE
  g_print ("reorder: running with length = %d\n", length);
#endif

  for (i = 0, elt_count = 0; i < length; i++)
    {
      FilterElt *e = NULL;
      gint old_offset = -1;

      for (j = 0; j < level->array->len; j++)
	if (g_array_index (level->array, FilterElt, j).offset == new_order[i])
	  {
	    e = &g_array_index (level->array, FilterElt, j);
	    old_offset = j;
	    break;
	  }

      if (!e)
	continue;

      tmp_array[elt_count] = old_offset;
      g_array_append_val (new_array, *e);
      g_array_index (new_array, FilterElt, elt_count).offset = i;
      elt_count++;
    }

#ifdef VERBOSE
  g_print ("length: old %d, new %d\n", level->array->len,
      new_array->len);
#endif

  g_array_free (level->array, TRUE);
  level->array = new_array;

#ifdef VERBOSE
  for (j = 0; j < length; j++)
    g_print ("%3d", new_order[j]);
  g_print ("\n");

  for (j = 0; j < level->array->len; j++)
    g_print ("%3d", tmp_array[j]);
  g_print ("\n");

  for (j = 0; j < level->array->len; j++)
    g_print ("%3d", g_array_index (level->array, FilterElt, j).offset);
  g_print ("\n");
#endif

  /* fix up stuff */
  for (i = 0; i < level->array->len; i++)
    {
      FilterElt *e = &g_array_index (level->array, FilterElt, i);
      if (e->children)
	e->children->parent_elt = e;
    }

  /* emit rows_reordered */
  if (!gtk_tree_path_get_indices (path))
    gtk_tree_model_rows_reordered (GTK_TREE_MODEL (data), path, NULL,
	tmp_array);
  else
    gtk_tree_model_rows_reordered (GTK_TREE_MODEL (data), path, &iter,
        tmp_array);

  /* done */
  g_free (tmp_array);
  gtk_tree_path_free (path);
}

/* TreeModelIface implemtation */
static guint
gtk_tree_model_filter_get_flags (GtkTreeModel *tree_model)
{
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), 0);

  return 0;
}

static gint
gtk_tree_model_filter_get_n_columns (GtkTreeModel *tree_model)
{
  GtkTreeModelFilter *tree_model_filter = (GtkTreeModelFilter *) tree_model;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), 0);

  if (tree_model_filter->child_model == 0)
    return 0;

  return gtk_tree_model_get_n_columns (tree_model_filter->child_model);
}

static GType
gtk_tree_model_filter_get_column_type (GtkTreeModel *tree_model,
				       gint          index)
{
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), G_TYPE_INVALID);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, G_TYPE_INVALID);

  return gtk_tree_model_get_column_type (GTK_TREE_MODEL_FILTER (tree_model)->child_model, index);
}

static gboolean
gtk_tree_model_filter_get_iter (GtkTreeModel *tree_model,
				GtkTreeIter  *iter,
				GtkTreePath  *path)
{
  GtkTreeModelFilter *tree_model_filter;
  gint *indices;
  FilterLevel *level;
  gint depth, i;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, FALSE);

  tree_model_filter = (GtkTreeModelFilter *) tree_model;
  indices = gtk_tree_path_get_indices (path);

  if (tree_model_filter->root == NULL)
    gtk_tree_model_filter_build_level (tree_model_filter, NULL, NULL);
  level = FILTER_LEVEL (tree_model_filter->root);

  depth = gtk_tree_path_get_depth (path);
  if (depth == 0)
    return FALSE;

  for (i = 0; i < depth - 1; i++)
    {
      if ((level == NULL) ||
          (level->array->len < indices[i]))
        return FALSE;

      if (g_array_index (level->array, FilterElt, indices[i]).children == NULL)
        gtk_tree_model_filter_build_level (tree_model_filter, level, &g_array_index (level->array, FilterElt, indices[i]));
      level = g_array_index (level->array, FilterElt, indices[i]).children;
    }

  if (level == NULL || level->array->len <= 0)
    return FALSE;
  iter->stamp = tree_model_filter->stamp;
  iter->user_data = level;
  iter->user_data2 = &g_array_index (level->array, FilterElt, indices[depth - 1]);
  return TRUE;
}

static GtkTreePath *
gtk_tree_model_filter_get_path (GtkTreeModel *tree_model,
				GtkTreeIter  *iter)
{
  GtkTreePath *retval;
  FilterLevel *level;
  FilterElt *elt;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), NULL);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, NULL);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp, NULL);

  retval = gtk_tree_path_new ();
  level = iter->user_data;
  elt = iter->user_data2;
  while (level != NULL)
    {
      gtk_tree_path_prepend_index (retval, elt - (FilterElt *)level->array->data);
      elt = level->parent_elt;
      level = level->parent_level;
    }

  return retval;
}

static void
gtk_tree_model_filter_get_value (GtkTreeModel *tree_model,
				 GtkTreeIter  *iter,
				 gint          column,
				 GValue       *value)
{
  GtkTreeIter child_iter;

  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model));
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL);
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp);

  GET_CHILD_ITER (tree_model, &child_iter, iter);
  gtk_tree_model_get_value (GTK_TREE_MODEL_FILTER (tree_model)->child_model,
                            &child_iter, column, value);
}

static gboolean
gtk_tree_model_filter_iter_next (GtkTreeModel *tree_model,
				 GtkTreeIter  *iter)
{
  FilterLevel *level;
  FilterElt *elt;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp, FALSE);

  level = iter->user_data;
  elt = iter->user_data2;

  if (elt - (FilterElt *)level->array->data >= level->array->len - 1)
    {
      iter->stamp = 0;
      return FALSE;
    }
  iter->user_data2 = elt + 1;

  return TRUE;
}

static gboolean
gtk_tree_model_filter_iter_children (GtkTreeModel *tree_model,
				     GtkTreeIter  *iter,
				     GtkTreeIter  *parent)
{
  GtkTreeModelFilter *tree_model_filter = (GtkTreeModelFilter *) tree_model;
  FilterLevel *level;

  iter->stamp = 0;
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  g_return_val_if_fail (tree_model_filter->child_model != NULL, FALSE);
  if (parent) g_return_val_if_fail (tree_model_filter->stamp == parent->stamp, FALSE);

  if (parent == NULL)
    {
      if (tree_model_filter->root == NULL)
        gtk_tree_model_filter_build_level (tree_model_filter, NULL, NULL);
      if (tree_model_filter->root == NULL)
        return FALSE;

      level = tree_model_filter->root;
      iter->stamp = tree_model_filter->stamp;
      iter->user_data = level;
      iter->user_data2 = level->array->data;
    }
  else
    {
      if (((FilterElt *)parent->user_data2)->children == NULL)
        gtk_tree_model_filter_build_level (tree_model_filter,
                                         (FilterLevel *)parent->user_data,
                                         (FilterElt *)parent->user_data2);
      if (((FilterElt *)parent->user_data2)->children == NULL)
        return FALSE;
      iter->stamp = tree_model_filter->stamp;
      iter->user_data = ((FilterElt *)parent->user_data2)->children;
      iter->user_data2 = ((FilterLevel *)iter->user_data)->array->data;
    }
  
  return TRUE;
}

static gboolean
gtk_tree_model_filter_iter_has_child (GtkTreeModel *tree_model,
				      GtkTreeIter  *iter)
{
  GtkTreeIter child_iter;
  GtkTreeModelFilter *filter;
  FilterElt *elt;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp, FALSE);

  filter = GTK_TREE_MODEL_FILTER (tree_model);

  GET_CHILD_ITER (tree_model, &child_iter, iter);
  elt = FILTER_ELT (iter->user_data2);

  if (!elt->children
      && gtk_tree_model_iter_has_child (filter->child_model, &child_iter))
    gtk_tree_model_filter_build_level (filter, FILTER_LEVEL (iter->user_data),
        elt);

  if (elt->children && elt->children->array->len > 0)
    return TRUE;

  return FALSE;
}


static gint
gtk_tree_model_filter_iter_n_children (GtkTreeModel *tree_model,
				       GtkTreeIter  *iter)
{
  GtkTreeIter child_iter;
  GtkTreeModelFilter *filter;
  FilterElt *elt;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), 0);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, 0);
  if (iter)
    g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp, 0);
  
  filter = GTK_TREE_MODEL_FILTER (tree_model);

  if (!iter)
    {
      if (!GTK_TREE_MODEL_FILTER (tree_model)->root)
        gtk_tree_model_filter_build_level (GTK_TREE_MODEL_FILTER (tree_model), NULL, NULL);

#ifdef VERBOSE
  g_print ("- length %d\n", FILTER_LEVEL (GTK_TREE_MODEL_FILTER (tree_model)->root)->array->len);
#endif
  
      return FILTER_LEVEL (GTK_TREE_MODEL_FILTER (tree_model)->root)->array->len;
    }

  elt = FILTER_ELT (iter->user_data2);
  GET_CHILD_ITER (tree_model, &child_iter, iter);

  if (!elt->children &&
      gtk_tree_model_iter_has_child (filter->child_model, &child_iter))
    gtk_tree_model_filter_build_level (filter, FILTER_LEVEL (iter->user_data),
        elt);

  if (elt->children && elt->children->array->len)
    return elt->children->array->len;


  return 0;
}

static gboolean
gtk_tree_model_filter_iter_nth_child (GtkTreeModel *tree_model,
				      GtkTreeIter  *iter,
				      GtkTreeIter  *parent,
				      gint          n)
{
  FilterLevel *level;
  /* We have this for the iter == parent case */
  GtkTreeIter children;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  if (parent) g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == parent->stamp, FALSE);

  /* Use this instead of has_child to force us to build the level, if needed */
  if (gtk_tree_model_filter_iter_children (tree_model, &children, parent) == FALSE)
    {
      iter->stamp = 0;
      return FALSE;
    }

  level = children.user_data;
  if (n >= level->array->len)
    {
      iter->stamp = 0;
      return FALSE;
    }

  iter->stamp = GTK_TREE_MODEL_FILTER (tree_model)->stamp;
  iter->user_data = level;
  iter->user_data2 = &g_array_index (level->array, FilterElt, n);

  return TRUE;
}

static gboolean
gtk_tree_model_filter_iter_parent (GtkTreeModel *tree_model,
				   GtkTreeIter  *iter,
				   GtkTreeIter  *child)
{
  FilterLevel *level;

  iter->stamp = 0;
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL, FALSE);
  g_return_val_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == child->stamp, FALSE);

  level = child->user_data;

  if (level->parent_level)
    {
      iter->stamp = GTK_TREE_MODEL_FILTER (tree_model)->stamp;
      iter->user_data = level->parent_level;
      iter->user_data2 = level->parent_elt;

      return TRUE;
    }
  return FALSE;
}

static void
gtk_tree_model_filter_ref_node (GtkTreeModel *tree_model,
				GtkTreeIter  *iter)
{
  GtkTreeModelFilter *tree_model_filter = (GtkTreeModelFilter *) tree_model;
  GtkTreeIter child_iter;
  FilterLevel *level;
  FilterElt *elt;

  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model));
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL);
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp);

  GET_CHILD_ITER (tree_model, &child_iter, iter);

  gtk_tree_model_ref_node (tree_model_filter->child_model, &child_iter);

  level = iter->user_data;
  elt = iter->user_data2;

  elt->ref_count++;
  level->ref_count++;
  if (level->ref_count == 1)
    {
      FilterLevel *parent_level = level->parent_level;
      FilterElt *parent_elt = level->parent_elt;
      /* We were at zero -- time to decrement the zero_ref_count val */
      do
        {
          if (parent_elt)
            parent_elt->zero_ref_count--;
          else
            tree_model_filter->zero_ref_count--;
          
          if (parent_level)
            {
              parent_elt = parent_level->parent_elt;
              parent_level = parent_level->parent_level;
            }
        }
      while (parent_level);
    }
}

static void
gtk_tree_model_filter_unref_node (GtkTreeModel *tree_model,
				  GtkTreeIter  *iter)
{
  gtk_tree_model_filter_real_unref_node (tree_model, iter, TRUE);
}

static void
gtk_tree_model_filter_real_unref_node (GtkTreeModel *tree_model,
				       GtkTreeIter  *iter,
				       gboolean      propagate_unref)
{
  GtkTreeModelFilter *tree_model_filter = (GtkTreeModelFilter *) tree_model;
  FilterLevel *level;
  FilterElt *elt;

  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model));
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->child_model != NULL);
  g_return_if_fail (GTK_TREE_MODEL_FILTER (tree_model)->stamp == iter->stamp);

  if (propagate_unref)
    {
      GtkTreeIter child_iter;
      GET_CHILD_ITER (tree_model, &child_iter, iter);
      gtk_tree_model_unref_node (tree_model_filter->child_model, &child_iter);
    }

  level = iter->user_data;
  elt = iter->user_data2;

  g_return_if_fail (elt->ref_count > 0);

  elt->ref_count--;
  level->ref_count--;
  if (level->ref_count == 0)
    {
      FilterLevel *parent_level = level->parent_level;
      FilterElt *parent_elt = level->parent_elt;

      /* We are at zero -- time to increment the zero_ref_count val */
      while (parent_level)
        {
          parent_elt->zero_ref_count++;
          
          parent_elt = parent_level->parent_elt;
          parent_level = parent_level->parent_level;
        }
      tree_model_filter->zero_ref_count++;
    }
}

/* ModelFilter API */

/**
 * gtk_tree_model_filter_set_model:
 * @tree_model_filter: The #GtkTreeModelFilter.
 * @child_model: A #GtkTreeModel, or NULL.
 *
 * Sets the model of @tree_model_filter to be @model.  If @model is NULL, then the
 * old model is unset.  The filter function is unset as a result of this call.
 * The model will be in an unfiltered state until a filter function is set.
 **/
static void
gtk_tree_model_filter_set_model (GtkTreeModelFilter *tree_model_filter,
				 GtkTreeModel       *child_model)
{
  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter));

  if (child_model)
    g_object_ref (G_OBJECT (child_model));

  if (tree_model_filter->child_model)
    {
      g_signal_handler_disconnect (G_OBJECT (tree_model_filter->child_model),
                                   tree_model_filter->changed_id);
      g_signal_handler_disconnect (G_OBJECT (tree_model_filter->child_model),
                                   tree_model_filter->inserted_id);
      g_signal_handler_disconnect (G_OBJECT (tree_model_filter->child_model),
                                   tree_model_filter->has_child_toggled_id);
      g_signal_handler_disconnect (G_OBJECT (tree_model_filter->child_model),
                                   tree_model_filter->deleted_id);
      g_signal_handler_disconnect (G_OBJECT (tree_model_filter->child_model),
                                   tree_model_filter->reordered_id);

      /* reset our state */
      if (tree_model_filter->root)
	gtk_tree_model_filter_free_level (tree_model_filter, tree_model_filter->root);
      tree_model_filter->root = NULL;
      g_object_unref (G_OBJECT (tree_model_filter->child_model));
      tree_model_filter->filter_column = -1;
    }

  tree_model_filter->child_model = child_model;

  if (child_model)
    {
      gint n_columns;

      g_object_ref (tree_model_filter->child_model);
      tree_model_filter->changed_id =
        g_signal_connect (child_model, "row_changed",
                          G_CALLBACK (gtk_tree_model_filter_row_changed),
                          tree_model_filter);
      tree_model_filter->inserted_id =
        g_signal_connect (child_model, "row_inserted",
                          G_CALLBACK (gtk_tree_model_filter_row_inserted),
                          tree_model_filter);
      tree_model_filter->has_child_toggled_id =
        g_signal_connect (child_model, "row_has_child_toggled",
                          G_CALLBACK (gtk_tree_model_filter_row_has_child_toggled),
                          tree_model_filter);
      tree_model_filter->deleted_id =
        g_signal_connect (child_model, "row_deleted",
                          G_CALLBACK (gtk_tree_model_filter_row_deleted),
                          tree_model_filter);
      tree_model_filter->reordered_id =
        g_signal_connect (child_model, "rows_reordered",
                          G_CALLBACK (gtk_tree_model_filter_rows_reordered),
                          tree_model_filter);

      tree_model_filter->child_flags = gtk_tree_model_get_flags (child_model);
      n_columns = gtk_tree_model_get_n_columns (child_model);

      tree_model_filter->stamp = g_random_int ();
    }
}

static void
gtk_tree_model_filter_set_filter_column (GtkTreeModelFilter *tree_model_filter,
                                         gint                filter_column)
{
  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter));
  g_return_if_fail (filter_column >= 0);

  tree_model_filter->filter_column = filter_column;
}

static void
gtk_tree_model_filter_set_root (GtkTreeModelFilter *filter,
                                GtkTreePath        *root)
{
  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (filter));

  if (!root)
    filter->virtual_root = NULL;
  else
    filter->virtual_root = gtk_tree_path_copy (root);
}

static GtkTreePath *
gtk_tree_model_filter_elt_get_path (FilterLevel  *level,
                                    FilterElt    *elt,
				    GtkTreePath  *root)
{
  FilterLevel *walker = level;
  FilterElt *walker2 = elt;
  GtkTreePath *path;
  GtkTreePath *real_path;
  
  g_return_val_if_fail (level != NULL, NULL);
  g_return_val_if_fail (elt != NULL, NULL);

  path = gtk_tree_path_new ();

  while (walker)
    {
      gtk_tree_path_prepend_index (path, walker2->offset);

      walker2 = walker->parent_elt;
      walker = walker->parent_level;
    }

  if (root)
    {
      real_path = gtk_tree_path_copy (root);

      gtk_tree_path_add (real_path, path);
      gtk_tree_path_free (path);
      return real_path;
    }
  
  return path;
}

/**
 * gtk_tree_model_filter_get_model:
 * @tree_model: a #GtkTreeModelFilter
 *
 * Returns the model the #GtkTreeModelFilter is filtering.
 *
 * Return value: the "child model" being filtered
 **/
GtkTreeModel *
gtk_tree_model_filter_get_model (GtkTreeModelFilter  *tree_model)
{
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model), NULL);

  return tree_model->child_model;
}

static gboolean
gtk_tree_model_filter_fetch_child (GtkTreeModelFilter *filter,
                                   FilterLevel        *level,
                                   gint                offset)
{
  int i = 0;
  int len;
  GtkTreePath *s_path = NULL;
  GtkTreeIter  s_iter;
  FilterElt elt;

#ifdef VERBOSE
  g_print ("_fetch_child for offset %d\n", offset);
#endif

  if (level->parent_level)
    {
      s_path = gtk_tree_model_filter_elt_get_path (level->parent_level, level->parent_elt, filter->virtual_root);
      if (!s_path)
        return FALSE;
      gtk_tree_model_get_iter (filter->child_model, &s_iter, s_path);

      len = gtk_tree_model_iter_n_children (filter->child_model, &s_iter);
    }
  else
    len = gtk_tree_model_iter_n_children (filter->child_model, NULL);

  if (offset >= len)
    {
      if (s_path)
        gtk_tree_path_free (s_path);
      return FALSE;
    }

  /* add child */
  elt.offset = offset;
  elt.zero_ref_count = 0;
  elt.ref_count = 0;
  elt.children = NULL;
  elt.visible = FALSE;
  if (GTK_TREE_MODEL_FILTER_CACHE_CHILD_ITERS (filter))
    {
      if (!s_path)
        {
	  if (filter->virtual_root)
	    s_path = gtk_tree_path_copy (filter->virtual_root);
	  else
            s_path = gtk_tree_path_new ();
        }
      gtk_tree_path_append_index (s_path, offset);
#ifdef VERBOSE
      g_print ("fetching %s...\n", gtk_tree_path_to_string (s_path));
#endif

      gtk_tree_model_get_iter (filter->child_model, &elt.iter, s_path);
#ifdef VERBOSE
      g_print ("Fetch child, got iter c%p\n", elt.iter.user_data);
#endif
    }

  if (s_path)
    gtk_tree_path_free (s_path);

  /* find index */
  for (i = 0; i < level->array->len; i++)
    if (g_array_index (level->array, FilterElt, i).offset > offset)
      break;

  g_array_insert_val (level->array, i, elt);

  for (i = 0; i < level->array->len; i++)
    {
      FilterElt *e = &(g_array_index (level->array, FilterElt, i));
      if (e->children)
        e->children->parent_elt = e;
    }

  return TRUE;
}

static GtkTreePath *
gtk_real_tree_model_filter_convert_child_path_to_path (GtkTreeModelFilter *tree_model_filter,
						       GtkTreePath        *child_path,
						       gboolean            build_levels,
						       gboolean            fetch_childs)
{
  gint *child_indices;
  GtkTreePath *retval;
  GtkTreePath *real_path;
  FilterLevel *level;
  gint i;

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter), NULL);
  g_return_val_if_fail (tree_model_filter->child_model != NULL, NULL);
  g_return_val_if_fail (child_path != NULL, NULL);

  if (!tree_model_filter->virtual_root)
    real_path = gtk_tree_path_copy (child_path);
  else
    real_path = gtk_tree_path_apply_root (child_path, tree_model_filter->virtual_root);

  if (!real_path)
    return NULL;

  retval = gtk_tree_path_new ();
  child_indices = gtk_tree_path_get_indices (real_path);

  if (tree_model_filter->root == NULL && build_levels)
    gtk_tree_model_filter_build_level (tree_model_filter, NULL, NULL);
  level = FILTER_LEVEL (tree_model_filter->root);

  for (i = 0; i < gtk_tree_path_get_depth (real_path); i++)
    {
      gint j;
      gboolean found_child = FALSE;

      if (!level)
        {
	  gtk_tree_path_free (real_path);
          gtk_tree_path_free (retval);
          return NULL;
        }

      for (j = 0; j < level->array->len; j++)
        {
          if ((g_array_index (level->array, FilterElt, j)).offset == child_indices[i])
            {
              gtk_tree_path_append_index (retval, j);
              if (g_array_index (level->array, FilterElt, j).children == NULL && build_levels)
                gtk_tree_model_filter_build_level (tree_model_filter, level, &g_array_index (level->array, FilterElt, j));
              level = g_array_index (level->array, FilterElt, j).children;
              found_child = TRUE;
              break;
            }
        }
      if (! found_child && fetch_childs)
        {
          /* hrm child not found, try to bring it back */
          if (!gtk_tree_model_filter_fetch_child (tree_model_filter, level, child_indices[i]))
            {
	      gtk_tree_path_free (real_path);
              gtk_tree_path_free (retval);
              return NULL;
            }

          for (j = 0; j < level->array->len; j++)
            {
              if ((g_array_index (level->array, FilterElt, j)).offset == child_indices[i])
                {
                  gtk_tree_path_append_index (retval, j);
                  if (g_array_index (level->array, FilterElt, j).children == NULL && build_levels)
                    gtk_tree_model_filter_build_level (tree_model_filter, level, &g_array_index (level->array, FilterElt, j));
                  level = g_array_index (level->array, FilterElt, j).children;
                  found_child = TRUE;
                  break;
                }
            }

          if (!found_child)
            {
              /* our happy fun fetch attempt failed ?!?!? */
	      gtk_tree_path_free (real_path);
              gtk_tree_path_free (retval);
              return NULL;
            }
        }
      else if (!found_child && !fetch_childs)
        {
	  /* no path */
	  gtk_tree_path_free (real_path);
	  gtk_tree_path_free (retval);
	  return NULL;
	}
    }

  gtk_tree_path_free (real_path);
  return retval;
}

/**
 * gtk_tree_model_filter_convert_child_path_to_path:
 * @tree_model_filter: A #GtkTreeModelFilter
 * @child_path: A #GtkTreePath to convert
 * 
 * Converts @child_path to a path relative to @tree_model_filter.  That is,
 * @child_path points to a path in the child model.  The returned path will
 * point to the same row in the filtered model.  If @child_path isn't a valid path
 * on the child model, then %NULL is returned.
 * 
 * Return value: A newly allocated #GtkTreePath, or %NULL
 **/
GtkTreePath *
gtk_tree_model_filter_convert_child_path_to_path (GtkTreeModelFilter *tree_model_filter,
						  GtkTreePath        *child_path)
{
  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter), NULL);
  g_return_val_if_fail (tree_model_filter->child_model != NULL, NULL);
  g_return_val_if_fail (child_path != NULL, NULL);

  return gtk_real_tree_model_filter_convert_child_path_to_path (tree_model_filter, child_path, TRUE, TRUE);
}

/**
 * gtk_tree_model_filter_convert_child_iter_to_iter:
 * @tree_model_filter: A #GtkTreeModelFilter
 * @filter_iter: An uninitialized #GtkTreeIter.
 * @child_iter: A valid #GtkTreeIter pointing to a row on the child model
 * 
 * Sets @filter_iter to point to the row in @tree_model_filter that corresponds to
 * the row pointed at by @child_iter.
 **/
void
gtk_tree_model_filter_convert_child_iter_to_iter (GtkTreeModelFilter *tree_model_filter,
						  GtkTreeIter        *filter_iter,
						  GtkTreeIter        *child_iter)
{
  GtkTreePath *child_path, *path;

  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter));
  g_return_if_fail (filter_iter != NULL);
  g_return_if_fail (child_iter != NULL);

  filter_iter->stamp = 0;

  child_path = gtk_tree_model_get_path (tree_model_filter->child_model, child_iter);
  g_return_if_fail (child_path != NULL);

  path = gtk_tree_model_filter_convert_child_path_to_path (tree_model_filter, child_path);
  gtk_tree_path_free (child_path);
  g_return_if_fail (path != NULL);

  gtk_tree_model_get_iter (GTK_TREE_MODEL (tree_model_filter), filter_iter, path);
  gtk_tree_path_free (path);
}

/**
 * gtk_tree_model_filter_convert_path_to_child_path:
 * @tree_model_filter: A #GtkTreeModelFilter
 * @filtered_path: A #GtkTreePath to convert
 * 
 * Converts @filter_path to a path on the child model of @tree_model_filter.  That
 * is, @filter_path points ot a location in @tree_model_filter.  The returned path
 * will point to the same location in the model not being filtered.  If @path does not point to a 
 * 
 * Return value: A newly allocated #GtkTreePath, or %NULLL
 **/
GtkTreePath *
gtk_tree_model_filter_convert_path_to_child_path (GtkTreeModelFilter *tree_model_filter,
						  GtkTreePath        *filtered_path)
{
  gint *filtered_indices;
  GtkTreePath *retval;
  FilterLevel *level;
  gint i;
  /* FIXME: THIS IS BROKEN!!!!!!!!!!!!!!!!!!!! */

  /* FIXME: this needs virtual root support */

  g_return_val_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter), NULL);
  g_return_val_if_fail (tree_model_filter->child_model != NULL, NULL);
  g_return_val_if_fail (filtered_path != NULL, NULL);

  g_warning ("_convert_path_to_child_path: THIS FUNCTION IS BROKEN!\n");

  retval = gtk_tree_path_new ();
  filtered_indices = gtk_tree_path_get_indices (filtered_path);
  if (tree_model_filter->root == NULL)
    gtk_tree_model_filter_build_level (tree_model_filter, NULL, NULL);
  level = FILTER_LEVEL (tree_model_filter->root);

  for (i = 0; i < gtk_tree_path_get_depth (filtered_path); i++)
    {
      if ((level == NULL) ||
          (level->array->len <= filtered_indices[i]))
        {
          gtk_tree_path_free (retval);
          return NULL;
        }
      if (g_array_index (level->array, FilterElt, filtered_indices[i]).children == NULL)
        gtk_tree_model_filter_build_level (tree_model_filter, level, &g_array_index (level->array, FilterElt, filtered_indices[i]));

      if (level == NULL)
	break;

      gtk_tree_path_append_index (retval, g_array_index (level->array, FilterElt, i).offset);
    }
  
  return retval;
}

/**
 * gtk_tree_model_filter_convert_iter_to_child_iter:
 * @tree_model_filter: A #GtkTreeModelFilter
 * @child_iter: An uninitialized #GtkTreeIter
 * @filtered_iter: A valid #GtkTreeIter pointing to a row on @tree_model_filter.
 * 
 * Sets @child_iter to point to the row pointed to by *filtered_iter.
 **/
void
gtk_tree_model_filter_convert_iter_to_child_iter (GtkTreeModelFilter *tree_model_filter,
						  GtkTreeIter        *child_iter,
						  GtkTreeIter        *filtered_iter)
{
  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter));
  g_return_if_fail (tree_model_filter->child_model != NULL);
  g_return_if_fail (child_iter != NULL);
  g_return_if_fail (filtered_iter != NULL);
  g_return_if_fail (filtered_iter->stamp == tree_model_filter->stamp);

  if (GTK_TREE_MODEL_FILTER_CACHE_CHILD_ITERS (tree_model_filter))
    {
      *child_iter = FILTER_ELT (filtered_iter->user_data2)->iter;
    }
  else
    {
      GtkTreePath *path;
      FilterElt *elt;
      FilterLevel *level;

      elt = FILTER_ELT (filtered_iter->user_data2);
      level = FILTER_LEVEL (filtered_iter->user_data);

      path = gtk_tree_model_filter_elt_get_path (level, elt,
	  tree_model_filter->virtual_root);

      gtk_tree_model_get_iter (tree_model_filter->child_model, child_iter, path);
      gtk_tree_path_free (path);
    }
}

static void
gtk_tree_model_filter_build_level (GtkTreeModelFilter *tree_model_filter,
				   FilterLevel        *parent_level,
				   FilterElt          *parent_elt)
{
  GtkTreeIter iter;
  GtkTreeIter root;
  FilterLevel *new_level;
  gint length = 0;
  gint i;

  g_assert (tree_model_filter->child_model != NULL);

  if (parent_level == NULL)
    {
      if (tree_model_filter->virtual_root)
        {
	  if (gtk_tree_model_get_iter (tree_model_filter->child_model, &root, tree_model_filter->virtual_root) == FALSE)
	      return;
          length = gtk_tree_model_iter_n_children (tree_model_filter->child_model, &root);
#ifdef VERBOSE
	  g_print ("--- vroot %d children\n", length);
#endif

	  if (gtk_tree_model_iter_children (tree_model_filter->child_model, &iter, &root) == FALSE)
	    return;
	}
      else
        {
          if (gtk_tree_model_get_iter_first (tree_model_filter->child_model, &iter) == FALSE)
            return;
          length = gtk_tree_model_iter_n_children (tree_model_filter->child_model, NULL);
	}
    }
  else
    {
      GtkTreeIter parent_iter;
      GtkTreeIter child_parent_iter;

      parent_iter.stamp = tree_model_filter->stamp;
      parent_iter.user_data = parent_level;
      parent_iter.user_data2 = parent_elt;

      gtk_tree_model_filter_convert_iter_to_child_iter (tree_model_filter,
                                                      &child_parent_iter,
                                                      &parent_iter);
      if (gtk_tree_model_iter_children (tree_model_filter->child_model,
                                        &iter,
                                        &child_parent_iter) == FALSE)
        return;

      /* stamp may have changed */
      gtk_tree_model_filter_convert_iter_to_child_iter (tree_model_filter,
							&child_parent_iter,
							&parent_iter);

      length = gtk_tree_model_iter_n_children (tree_model_filter->child_model, &child_parent_iter);
    }

  g_return_if_fail (length > 0);

  new_level = g_new (FilterLevel, 1);
  new_level->array = g_array_sized_new (FALSE, FALSE, sizeof (FilterElt), length);  new_level->ref_count = 0;
  new_level->parent_elt = parent_elt;
  new_level->parent_level = parent_level;

  if (parent_elt)
    parent_elt->children = new_level;
  else
    tree_model_filter->root = new_level;

  /* increase the count of zero ref_counts.*/
  while (parent_level)
    {
      parent_elt->zero_ref_count++;

      parent_elt = parent_level->parent_elt;
      parent_level = parent_level->parent_level;
    }
  if (new_level != tree_model_filter->root)
    tree_model_filter->zero_ref_count++;
  
  i = 0;
  do
  {
    if (gtk_tree_model_filter_visible (tree_model_filter, &iter))
      {
        FilterElt filter_elt;

        filter_elt.offset = i;
        filter_elt.zero_ref_count = 0;
        filter_elt.ref_count = 0;
        filter_elt.children = NULL;
        filter_elt.visible = TRUE;

        if (GTK_TREE_MODEL_FILTER_CACHE_CHILD_ITERS (tree_model_filter))
          filter_elt.iter = iter;

        g_array_append_val (new_level->array, filter_elt);
      }
    i++;
  } while (gtk_tree_model_iter_next (tree_model_filter->child_model, &iter));
}

static void
gtk_tree_model_filter_free_level (GtkTreeModelFilter *tree_model_filter,
				  FilterLevel        *filter_level)
{
  gint i;

#ifdef VERBOSE
  g_print ("freeing level: %p, %p (ref = %d)\n", filter_level, 
           filter_level->array, filter_level->ref_count);
  g_print ("-- parents - elt: %p, level %p\n",
           filter_level->parent_elt, filter_level->parent_level);
#endif

  g_assert (filter_level);
  
#ifdef VERBOSE
  g_print ("-- freeing current level (ref = %d)\n",filter_level->ref_count);
#endif

  if (filter_level->ref_count == 0)
    {
      FilterLevel *parent_level = filter_level->parent_level;
      FilterElt *parent_elt = filter_level->parent_elt;

      do
        {
          if (parent_elt)
            parent_elt->zero_ref_count--;
          else
            tree_model_filter->zero_ref_count--;
          
          if (parent_level)
            {
              parent_elt = parent_level->parent_elt;
              parent_level = parent_level->parent_level;
            }
        }
      while (parent_level);
    }
  
#ifdef VERBOSE
  g_print ("-- freeing children\n");
#endif
  
  for (i = 0; i < filter_level->array->len; i++)
    {
      if (g_array_index (filter_level->array, FilterElt, i).children)
        gtk_tree_model_filter_free_level (tree_model_filter, 
                                        FILTER_LEVEL(g_array_index (filter_level->array, FilterElt, i).children));
    }
  
  if (filter_level->parent_elt)
    {
      filter_level->parent_elt->children = NULL;
    }
  else
    {
      tree_model_filter->root = NULL;
    }
  
#ifdef VERBOSE
  g_print ("free %p\n", filter_level->array);
#endif

  g_array_free (filter_level->array, TRUE);
  filter_level->array = NULL;

#ifdef VERBOSE
  g_print ("free %p\n", filter_level);
#endif

  g_free (filter_level);
  filter_level = NULL;

#ifdef VERBOSE
  g_print ("-------- done ---------\n");
#endif
}

static void
gtk_tree_model_filter_increment_stamp (GtkTreeModelFilter *tree_model_filter)
{
  do
    {
      tree_model_filter->stamp++;
    }
  while (tree_model_filter->stamp == 0);

  gtk_tree_model_filter_clear_cache (tree_model_filter);
}

static gboolean
gtk_tree_model_filter_visible (GtkTreeModelFilter *tree_model_filter,
                               GtkTreeIter        *child_iter)
{
  GValue val = {0, };

  gtk_tree_model_get_value (tree_model_filter->child_model,
                            child_iter, tree_model_filter->filter_column,
                            &val);

  if (g_value_get_boolean (&val))
    {
      g_value_unset (&val);
      return TRUE;
    }

  g_value_unset (&val);
  return FALSE;
}

static void
gtk_tree_path_add (GtkTreePath *path1,
		   GtkTreePath *path2)
{
  int i;
  gint depth;
  gint *indices;

  g_return_if_fail (path1 != NULL);
  g_return_if_fail (path2 != NULL);

  depth = gtk_tree_path_get_depth (path2);
  indices = gtk_tree_path_get_indices (path2);

  for (i = 0; i < depth; i++)
    gtk_tree_path_append_index (path1, indices[i]);
}

static GtkTreePath *
gtk_tree_path_apply_root (GtkTreePath *path,
			  GtkTreePath *root)
{
  GtkTreePath *retval;
  int i;
  gint depth;
  gint *indices;

  if (gtk_tree_path_get_depth (path) <= gtk_tree_path_get_depth (root))
    return NULL;

  depth = gtk_tree_path_get_depth (path);
  indices = gtk_tree_path_get_indices (path);

  for (i = 0; i < gtk_tree_path_get_depth (root); i++)
    if (indices[i] != gtk_tree_path_get_indices (root)[i])
      return NULL;

  retval = gtk_tree_path_new ();

  for (; i < depth; i++)
    gtk_tree_path_append_index (retval, indices[i]);

  return retval;
}

static void
gtk_tree_model_filter_clear_cache_helper (GtkTreeModelFilter *tree_model_filter,
					  FilterLevel        *level)
{
  gint i;

  g_assert (level != NULL);

  for (i = 0; i < level->array->len; i++)
    {
      if (g_array_index (level->array, FilterElt, i).zero_ref_count > 0)
        gtk_tree_model_filter_clear_cache_helper (tree_model_filter, g_array_index (level->array, FilterElt, i).children);
    }

  if (level->ref_count == 0 && level != tree_model_filter->root)
    {
      gtk_tree_model_filter_free_level (tree_model_filter, level);
      return;
    }
}

/**
 * gtk_tree_model_filter_clear_cache:
 * @tree_model_filter: A #GtkTreeModelFilter
 * 
 * This function should almost never be called.  It clears the @tree_model_filter
 * of any cached iterators that haven't been reffed with
 * gtk_tree_model_ref_node().  This might be useful if the child model being
 * filtered is static (and doesn't change often) and there has been a lot of
 * unreffed access to nodes.  As a side effect of this function, all unreffed
 * iters will be invalid.
 **/
void
gtk_tree_model_filter_clear_cache (GtkTreeModelFilter *tree_model_filter)
{
  g_return_if_fail (GTK_IS_TREE_MODEL_FILTER (tree_model_filter));

  if (tree_model_filter->zero_ref_count)
    gtk_tree_model_filter_clear_cache_helper (tree_model_filter, (FilterLevel *)tree_model_filter->root);
}
