/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of GtkTreeModel iface containing RBSource objects
 *
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <string.h>

#include <glib/gi18n.h>
#include <gdk/gdk.h>
#include <gtk/gtk.h>

#include "rb-sourcelist-model.h"
#include "rb-tree-dnd.h"
#include "rb-debug.h"
#include "rb-marshal.h"
#include "rb-playlist-source.h"

struct RBSourceListModelPrivate
{
	GtkTreeRowReference *groups[RB_SOURCELIST_GROUP_LAST];
};

#define RB_SOURCELIST_MODEL_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_SOURCELIST_MODEL, RBSourceListModelPrivate))

enum
{
	DROP_RECEIVED,
	LAST_SIGNAL
};

static void rb_sourcelist_model_class_init (RBSourceListModelClass *klass);
static void rb_sourcelist_model_init (RBSourceListModel *model);
static void rb_sourcelist_model_drag_dest_init (RbTreeDragDestIface *iface);
static void rb_sourcelist_model_drag_source_init (RbTreeDragSourceIface *iface);
static void rb_sourcelist_model_finalize (GObject *object);
static gboolean rb_sourcelist_model_is_row_visible (GtkTreeModel *model,
						    GtkTreeIter *iter,
						    RBSourceListModel *sourcelist);
static gboolean rb_sourcelist_model_drag_data_received (RbTreeDragDest *drag_dest,
							GtkTreePath *dest,
							GtkTreeViewDropPosition pos,
							GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_drop_possible (RbTreeDragDest *drag_dest,
						       GtkTreePath *dest,
						       GtkTreeViewDropPosition pos,
						       GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_drop_position (RbTreeDragDest   *drag_dest,
						       GtkTreePath       *dest_path,
						       GList *targets,
						       GtkTreeViewDropPosition *pos);
static GdkAtom rb_sourcelist_model_get_drag_target (RbTreeDragDest *drag_dest,
						    GtkWidget      *widget,
						    GdkDragContext *context,
						    GtkTreePath    *path,
						    GtkTargetList  *target_list);
static gboolean rb_sourcelist_model_drag_data_delete (RbTreeDragSource *drag_source,
						      GList *path_list);
static gboolean rb_sourcelist_model_drag_data_get (RbTreeDragSource *drag_source,
						   GList *path_list,
						   GtkSelectionData *selection_data);
static gboolean rb_sourcelist_model_row_draggable (RbTreeDragSource *drag_source,
						   GList *path_list);
static void rb_sourcelist_model_row_inserted_cb (GtkTreeModel *model,
						 GtkTreePath *path,
						 GtkTreeIter *iter,
						 RBSourceListModel *sourcelist);
static void rb_sourcelist_model_row_deleted_cb (GtkTreeModel *model,
						GtkTreePath *path,
						RBSourceListModel *sourcelist);

static guint rb_sourcelist_model_signals[LAST_SIGNAL] = { 0 };

enum {
	TARGET_PROPERTY,
	TARGET_SOURCE,
	TARGET_URIS,
	TARGET_DELETE
};

static const GtkTargetEntry sourcelist_targets[] = { { "text/x-rhythmbox-album", 0, TARGET_PROPERTY },
						     { "text/x-rhythmbox-artist", 0, TARGET_PROPERTY },
						     { "text/x-rhythmbox-genre", 0, TARGET_PROPERTY },
						     { "application/x-rhythmbox-source", 0, TARGET_SOURCE },
						     { "text/uri-list", 0, TARGET_URIS },
						     { "application/x-delete-me", 0, TARGET_DELETE }};

static GtkTargetList *sourcelist_drag_target_list = NULL;

G_DEFINE_TYPE_EXTENDED (RBSourceListModel,
                        rb_sourcelist_model,
                        GTK_TYPE_TREE_MODEL_FILTER,
                        0,
                        G_IMPLEMENT_INTERFACE (RB_TYPE_TREE_DRAG_SOURCE,
                                               rb_sourcelist_model_drag_source_init)
                        G_IMPLEMENT_INTERFACE (RB_TYPE_TREE_DRAG_DEST,
                                               rb_sourcelist_model_drag_dest_init));

static void
rb_sourcelist_model_class_init (RBSourceListModelClass *class)
{
	GObjectClass   *o_class;
	GtkObjectClass *object_class;

	o_class = (GObjectClass *) class;
	object_class = (GtkObjectClass *) class;

	o_class->finalize = rb_sourcelist_model_finalize;

	rb_sourcelist_model_signals[DROP_RECEIVED] =
		g_signal_new ("drop_received",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceListModelClass, drop_received),
			      NULL, NULL,
			      rb_marshal_VOID__OBJECT_INT_POINTER,
			      G_TYPE_NONE,
			      3,
			      RB_TYPE_SOURCE, G_TYPE_INT, G_TYPE_POINTER);

	if (!sourcelist_drag_target_list)
		sourcelist_drag_target_list =
			gtk_target_list_new (sourcelist_targets,
					     G_N_ELEMENTS (sourcelist_targets));

	g_type_class_add_private (class, sizeof (RBSourceListModelPrivate));
}

static void
rb_sourcelist_model_drag_dest_init (RbTreeDragDestIface *iface)
{
  iface->drag_data_received = rb_sourcelist_model_drag_data_received;
  iface->row_drop_possible = rb_sourcelist_model_row_drop_possible;
  iface->row_drop_position = rb_sourcelist_model_row_drop_position;
  iface->get_drag_target = rb_sourcelist_model_get_drag_target;
}

static void
rb_sourcelist_model_drag_source_init (RbTreeDragSourceIface *iface)
{
  iface->row_draggable = rb_sourcelist_model_row_draggable;
  iface->drag_data_get = rb_sourcelist_model_drag_data_get;
  iface->drag_data_delete = rb_sourcelist_model_drag_data_delete;
}

void
rb_sourcelist_model_set_dnd_targets (RBSourceListModel *sourcelist,
				     GtkTreeView *treeview)
{
	int n_targets = G_N_ELEMENTS (sourcelist_targets);
	g_return_if_fail (RB_IS_SOURCELIST_MODEL (sourcelist));

	rb_tree_dnd_add_drag_dest_support (treeview,
					   (RB_TREE_DEST_EMPTY_VIEW_DROP | RB_TREE_DEST_SELECT_ON_DRAG_TIMEOUT),
					   sourcelist_targets, n_targets,
					   GDK_ACTION_LINK);

	rb_tree_dnd_add_drag_source_support (treeview,
					     GDK_BUTTON1_MASK,
					     sourcelist_targets, n_targets,
					     GDK_ACTION_COPY);
}

static void
rb_sourcelist_model_init (RBSourceListModel *model)
{
	model->priv = RB_SOURCELIST_MODEL_GET_PRIVATE (model);
}

static void
rb_sourcelist_model_finalize (GObject *object)
{
	RBSourceListModel *model;
	int i;

	g_return_if_fail (RB_IS_SOURCELIST_MODEL (object));
	model = RB_SOURCELIST_MODEL (object);

	for (i = 0; i < RB_SOURCELIST_GROUP_LAST; i++) {
		gtk_tree_row_reference_free (model->priv->groups[i]);
	}

	G_OBJECT_CLASS (rb_sourcelist_model_parent_class)->finalize (object);
}

GtkTreeModel *
rb_sourcelist_model_new (void)
{
	RBSourceListModel *model;
	GtkTreeStore *store;
	int i;
 	GType *column_types = g_new (GType, RB_SOURCELIST_MODEL_N_COLUMNS);

	column_types[RB_SOURCELIST_MODEL_COLUMN_PLAYING] = G_TYPE_BOOLEAN;
	column_types[RB_SOURCELIST_MODEL_COLUMN_PIXBUF] = GDK_TYPE_PIXBUF;
	column_types[RB_SOURCELIST_MODEL_COLUMN_NAME] = G_TYPE_STRING;
	column_types[RB_SOURCELIST_MODEL_COLUMN_SOURCE] = G_TYPE_OBJECT;
	column_types[RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES] = PANGO_TYPE_ATTR_LIST;
	column_types[RB_SOURCELIST_MODEL_COLUMN_VISIBILITY] = G_TYPE_BOOLEAN;
	store = gtk_tree_store_newv (RB_SOURCELIST_MODEL_N_COLUMNS,
				     column_types);

 	model = RB_SOURCELIST_MODEL (g_object_new (RB_TYPE_SOURCELIST_MODEL,
						   "child-model", store,
						   "virtual-root", NULL,
						   NULL));
	g_object_unref (store);

	/* create marker rows used to separate source groups */
	for (i = 0; i < RB_SOURCELIST_GROUP_LAST; i++) {
		GtkTreeIter iter;
		GtkTreePath *path;

		gtk_tree_store_append (store, &iter, NULL);
		gtk_tree_store_set (store, &iter,
				    RB_SOURCELIST_MODEL_COLUMN_NAME, "",
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, NULL,
				    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, FALSE,
				    -1);

		path = gtk_tree_model_get_path (GTK_TREE_MODEL (store), &iter);
		model->priv->groups[i] = gtk_tree_row_reference_new (GTK_TREE_MODEL (store), path);
		gtk_tree_path_free (path);
	}

	/* ensure the group markers get updated as sources are added and removed */
	g_signal_connect_object (G_OBJECT (store), "row-inserted",
				 G_CALLBACK (rb_sourcelist_model_row_inserted_cb),
				 model, 0);
	g_signal_connect_object (G_OBJECT (store), "row-deleted",
				 G_CALLBACK (rb_sourcelist_model_row_deleted_cb),
				 model, 0);

	gtk_tree_model_filter_set_visible_func (GTK_TREE_MODEL_FILTER (model),
						(GtkTreeModelFilterVisibleFunc) rb_sourcelist_model_is_row_visible,
						model, NULL);

	g_free (column_types);

	return GTK_TREE_MODEL (model);
}

static gboolean
real_row_is_separator (RBSourceListModel *model, GtkTreeIter *iter)
{
	int i;
	GtkTreePath *path;
	GtkTreeModel *real_model;
	GtkTreePath *group_path;

	real_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));
	path = gtk_tree_model_get_path (real_model, iter);

	/* -1 here because the last group marker is always the last row in
	 * the model, and therefore can never be shown.
	 */
	for (i = 0; i < RB_SOURCELIST_GROUP_LAST-1; i++) {
		group_path = rb_sourcelist_model_get_group_path (model, i);

		if (gtk_tree_path_compare (path, group_path) == 0) {
			/* okay, we know this is a group marker.
			 * if the next row is not a group marker,
			 * then we should show this row as a separator.
			 */
			gboolean separator = FALSE;

			gtk_tree_path_next (path);

			gtk_tree_path_free (group_path);
			group_path = rb_sourcelist_model_get_group_path (model, i+1);
			separator = (gtk_tree_path_compare (path, group_path) != 0);

			gtk_tree_path_free (group_path);
			gtk_tree_path_free (path);
			return separator;
		}
		gtk_tree_path_free (group_path);
	}

	gtk_tree_path_free (path);

	return FALSE;
}

static gboolean
rb_sourcelist_model_is_row_visible (GtkTreeModel *model,
				    GtkTreeIter *iter,
				    RBSourceListModel *sourcelist)
{
	RBSource *source;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

	if (source != NULL) {
		gboolean visible;
		g_object_get (source, "visibility", &visible, NULL);

		g_object_unref (source);

		return visible;
	} else {
		return real_row_is_separator (sourcelist, iter);
	}
}

gboolean
rb_sourcelist_model_row_is_separator (GtkTreeModel *model,
				      GtkTreeIter *iter,
				      RBSourceListModel *sourcelist)
{
	GtkTreeIter real_iter;
	RBSource *source;

	/* rows with actual sources are never separators */
	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);
	if (source != NULL) {
		g_object_unref (source);
		return FALSE;
	}

	gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (sourcelist),
							  &real_iter,
							  iter);
	return real_row_is_separator (sourcelist, &real_iter);
}

static int
get_group_for_path (RBSourceListModel *model, GtkTreePath *path)
{
	GtkTreePath *group_path;
	gboolean found = FALSE;
	int i;

	for (i=0; i < RB_SOURCELIST_GROUP_LAST; i++) {
		group_path = rb_sourcelist_model_get_group_path (model, i);
		g_assert (group_path);

		found = (gtk_tree_path_compare (group_path, path) == 1);
		gtk_tree_path_free (group_path);

		if (found)
			return i;
	}

	g_assert_not_reached ();
}

static gboolean
rb_sourcelist_model_drag_data_received (RbTreeDragDest *drag_dest,
					GtkTreePath *dest,
					GtkTreeViewDropPosition pos,
					GtkSelectionData *selection_data)
{
	RBSourceListModel *model;

	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);

	if (selection_data->type == gdk_atom_intern ("text/uri-list", TRUE)) {
		GtkTreeIter iter;
		RBSource *target = NULL;

		rb_debug ("text/uri-list drag data received");

		if (dest != NULL && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		}

		if (target != NULL) {
			g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
				       0, target, pos, selection_data);
			g_object_unref (target);
		}

		return TRUE;
	}

        /* if artist, album or genre, only allow new playlists */
        if (selection_data->type == gdk_atom_intern ("text/x-rhythmbox-album", TRUE) ||
            selection_data->type == gdk_atom_intern ("text/x-rhythmbox-artist", TRUE) ||
            selection_data->type == gdk_atom_intern ("text/x-rhythmbox-genre", TRUE)) {
                rb_debug ("text/x-rhythmbox-(album|artist|genre) drag data received");
                g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
                               0, NULL, pos, selection_data);
                return TRUE;
        }

	if (selection_data->type == gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) {
		GtkTreePath *path;
		GtkTreePath *real_dest;
		char *path_str;
		GtkTreeIter iter, real_iter;
		GtkTreeIter real_dest_iter;
		GtkTreeModel *real_model;
		RBSource *source;
		RBSourceListGroup group;
		int dest_group;

		if (!dest)
			return FALSE;

		real_model = gtk_tree_model_filter_get_model (GTK_TREE_MODEL_FILTER (model));

		path_str = g_strndup ((char *) selection_data->data, selection_data->length);

		path = gtk_tree_path_new_from_string (path_str);
		gtk_tree_model_get_iter (GTK_TREE_MODEL (model),
					 &iter, path);
		gtk_tree_model_filter_convert_iter_to_child_iter (GTK_TREE_MODEL_FILTER (model),
								  &real_iter, &iter);

		real_dest =
			gtk_tree_model_filter_convert_path_to_child_path (GTK_TREE_MODEL_FILTER (model),
									  dest);

		gtk_tree_model_get (GTK_TREE_MODEL (real_model), &real_iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source,
				    -1);
		g_object_get (source, "sourcelist-group", &group, NULL);
		g_object_unref (source);

		/* restrict sources to within their group */
		dest_group = get_group_for_path (model, real_dest);
		if (dest_group < group) {
			gtk_tree_path_free (real_dest);
			real_dest = rb_sourcelist_model_get_group_path (model, group-1);
			pos = GTK_TREE_VIEW_DROP_AFTER;
		} else if (dest_group > group) {
			gtk_tree_path_free (real_dest);
			real_dest = rb_sourcelist_model_get_group_path (model, group);
			pos = GTK_TREE_VIEW_DROP_BEFORE;
		}

		if (gtk_tree_model_get_iter (GTK_TREE_MODEL (real_model),
					     &real_dest_iter, real_dest)) {

			if (pos == GTK_TREE_VIEW_DROP_AFTER)
				gtk_tree_store_move_after (GTK_TREE_STORE (real_model),
							   &real_iter, &real_dest_iter);
			else
				gtk_tree_store_move_before (GTK_TREE_STORE (real_model),
							    &real_iter, &real_dest_iter);

		} else {
			gtk_tree_store_move_before (GTK_TREE_STORE (real_model),
						    &real_iter, NULL);
		}

		gtk_tree_path_free (real_dest);
		gtk_tree_path_free (path);
		g_free (path_str);
	}

	return FALSE;
}

static gboolean
rb_sourcelist_model_row_drop_possible (RbTreeDragDest *drag_dest,
				       GtkTreePath *dest,
				       GtkTreeViewDropPosition pos,
				       GtkSelectionData *selection_data)
{
	RBSourceListModel *model;

	rb_debug ("row drop possible");
	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);

	if (selection_data->type == gdk_atom_intern ("text/uri-list", TRUE)
	    && !dest)
		return FALSE;

	if (!dest)
		return TRUE;

	/* Call the superclass method */
	return gtk_tree_drag_dest_row_drop_possible (GTK_TREE_DRAG_DEST (GTK_TREE_STORE (model)),
						     dest, selection_data);
}

static gboolean
path_is_droppable (RBSourceListModel *model,
		   GtkTreePath *dest)
{
	GtkTreeIter iter;
	gboolean res;

	res = FALSE;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
		RBSource *source;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

		if (source != NULL) {
			res = rb_source_can_paste (source);
			g_object_unref (source);
		}
	}

	return res;
}

static gboolean
path_is_reorderable (RBSourceListModel *model,
		     GtkTreePath *dest)
{
	GtkTreeIter iter;

	if (gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
		RBSource *source;
		RBSourceListGroup group;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

		if (source == NULL)
			return FALSE;

		g_object_get (source, "sourcelist-group", &group, NULL);
		g_object_unref (source);

		/* fixed and transient sources are not reorderable, everything else is */
		return (group != RB_SOURCELIST_GROUP_FIXED &&
			group != RB_SOURCELIST_GROUP_TRANSIENT);
	}

	return FALSE;
}

static gboolean
rb_sourcelist_model_row_drop_position (RbTreeDragDest   *drag_dest,
				       GtkTreePath       *dest_path,
				       GList *targets,
				       GtkTreeViewDropPosition *pos)
{
	GtkTreeModel *model = GTK_TREE_MODEL (drag_dest);

	if (g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) && dest_path) {
		rb_debug ("application/x-rhythmbox-source type");
		if (!path_is_reorderable (RB_SOURCELIST_MODEL (model), dest_path)) {
			GtkTreePath *test_path = gtk_tree_path_copy (dest_path);
			gtk_tree_path_next (test_path);
			gboolean ret = FALSE;
			if (path_is_reorderable (RB_SOURCELIST_MODEL (model), test_path)) {
				*pos = GTK_TREE_VIEW_DROP_AFTER;
				ret = TRUE;
			}

			gtk_tree_path_free (test_path);
			return ret;
		}

		if (*pos == GTK_TREE_VIEW_DROP_INTO_OR_BEFORE)
			*pos = GTK_TREE_VIEW_DROP_BEFORE;
		else if (*pos == GTK_TREE_VIEW_DROP_INTO_OR_AFTER)
			*pos = GTK_TREE_VIEW_DROP_AFTER;

		return TRUE;
	}

	if (g_list_find (targets, gdk_atom_intern ("text/uri-list", TRUE))) {
		rb_debug ("text/uri-list type");
		if (dest_path && !path_is_droppable (RB_SOURCELIST_MODEL (model), dest_path))
			return FALSE;

		*pos = GTK_TREE_VIEW_DROP_INTO_OR_BEFORE;
		return TRUE;
	}

	if ((g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-artist", TRUE))
	     || g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-album", TRUE))
	     || g_list_find (targets, gdk_atom_intern ("text/x-rhythmbox-genre", TRUE)))
	    && !g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE))) {
		rb_debug ("genre, album, or artist type");
		*pos = GTK_TREE_VIEW_DROP_AFTER;
		return TRUE;
	}

	return FALSE;
}

static GdkAtom
rb_sourcelist_model_get_drag_target (RbTreeDragDest *drag_dest,
				     GtkWidget *widget,
				     GdkDragContext *context,
				     GtkTreePath *path,
				     GtkTargetList *target_list)
{
	if (g_list_find (context->targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE))) {
		/* always accept rb source path if offered */
		return gdk_atom_intern ("application/x-rhythmbox-source", TRUE);
	}

	if (path) {
		/* only accept text/uri-list drops into existing sources */
		return gdk_atom_intern ("text/uri-list", FALSE);
	}

	return gtk_drag_dest_find_target (widget, context,
					  target_list);
}

static gboolean
rb_sourcelist_model_row_draggable (RbTreeDragSource *drag_source,
				   GList *path_list)
{
	GtkTreeIter iter;
	GtkTreePath *path;
	GtkTreeModel *model = GTK_TREE_MODEL (drag_source);

	/* we don't support multi selection */
	g_return_val_if_fail (g_list_length (path_list) == 1, FALSE);

	path = gtk_tree_row_reference_get_path (path_list->data);

	if (path && gtk_tree_model_get_iter (model, &iter, path)) {
		RBSource *source;
		RBSourceListGroup group;

		gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);

		g_object_get (source, "sourcelist-group", &group, NULL);
		g_object_unref (source);

		return (group != RB_SOURCELIST_GROUP_FIXED &&
			group != RB_SOURCELIST_GROUP_TRANSIENT);
	}

	return FALSE;
}

static gboolean
rb_sourcelist_model_drag_data_get (RbTreeDragSource *drag_source,
				   GList *path_list,
				   GtkSelectionData *selection_data)
{
	char *path_str;
	GtkTreePath *path;
	guint target;

	path = gtk_tree_row_reference_get_path (path_list->data);
	if (path == NULL)
		return FALSE;

	if (!gtk_target_list_find (sourcelist_drag_target_list,
				   selection_data->target,
				   &target)) {
		return FALSE;
	}

	if (target == TARGET_SOURCE) {
		rb_debug ("getting drag data as rb source path");
		path_str = gtk_tree_path_to_string (path);
		gtk_selection_data_set (selection_data,
					selection_data->target,
					8, (guchar *) path_str,
					strlen (path_str));
		g_free (path_str);
		gtk_tree_path_free (path);
		return TRUE;
	} else if (target == TARGET_URIS) {
		RBSource *source;
		RhythmDBQueryModel *query_model;
		GtkTreeIter iter;
		GString *data;
		gboolean first = TRUE;

		rb_debug ("getting drag data as uri list");
		if (!gtk_tree_model_get_iter (GTK_TREE_MODEL (drag_source), &iter, path))
			return FALSE;

		data = g_string_new ("");
		gtk_tree_model_get (GTK_TREE_MODEL (drag_source), &iter,
				    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source, -1);
		g_object_get (source, "query-model", &query_model, NULL);
		g_object_unref (source);

		if (!gtk_tree_model_get_iter_first (GTK_TREE_MODEL (query_model), &iter)) {
			g_object_unref (query_model);
			return FALSE;
		}

		do {
			RhythmDBEntry *entry;

			if (first) {
				g_string_append(data, "\r\n");
				first = FALSE;
			}

			entry = rhythmdb_query_model_iter_to_entry (query_model, &iter);
			g_string_append (data, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

			rhythmdb_entry_unref (entry);

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (query_model), &iter));

		g_object_unref (query_model);

		gtk_selection_data_set (selection_data,
					selection_data->target,
					8, (guchar *) data->str,
					data->len);

		g_string_free (data, TRUE);
		return TRUE;

	} else {
		/* unsupported target */
		return FALSE;
	}
}

static gboolean
rb_sourcelist_model_drag_data_delete (RbTreeDragSource *drag_source,
				      GList *paths)
{
	return TRUE;
}

static void
rb_sourcelist_model_row_inserted_cb (GtkTreeModel *model,
				     GtkTreePath *path,
				     GtkTreeIter *iter,
				     RBSourceListModel *sourcelist)
{
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sourcelist));
}

static void
rb_sourcelist_model_row_deleted_cb (GtkTreeModel *model,
			            GtkTreePath *path,
				    RBSourceListModel *sourcelist)
{
	gtk_tree_model_filter_refilter (GTK_TREE_MODEL_FILTER (sourcelist));
}

GtkTreePath *
rb_sourcelist_model_get_group_path (RBSourceListModel *sourcelist,
				    RBSourceListGroup group)
{
	return gtk_tree_row_reference_get_path (sourcelist->priv->groups[group]);
}
