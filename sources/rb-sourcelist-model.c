/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
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

#include "gseal-gtk-compat.h"

/**
 * SECTION:rb-sourcelist-model
 * @short_description: models backing the source list widget
 *
 * The #RBSourceList widget is backed by a #GtkTreeStore containing
 * the sources and a set of attributes used to structure and display
 * them, and a #GtkTreeModelFilter that hides sources with the
 * visibility property set to FALSE.  This class implements the filter
 * model and also creates the actual model.
 *
 * The source list model supports drag and drop in a variety of formats.
 * The simplest of these are text/uri-list and application/x-rhythmbox-entry,
 * which convey URIs and IDs of existing database entries.  When dragged
 * to an existing source, these just add the URIs or entries to the target
 * source.  When dragged to an empty space in the source list, this results
 * in the creation of a static playlist.
 *
 * text/x-rhythmbox-artist, text/x-rhythmbox-album, and text/x-rhythmbox-genre
 * are used when dragging items from the library browser.  When dragged to
 * the source list, these result in the creation of a new auto playlist with
 * the dragged items as criteria.
 */

struct RBSourceListModelPrivate
{
	gpointer dummy;
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
	TARGET_ENTRIES,
	TARGET_DELETE
};

static const GtkTargetEntry sourcelist_targets[] = { { "text/x-rhythmbox-album", 0, TARGET_PROPERTY },
						     { "text/x-rhythmbox-artist", 0, TARGET_PROPERTY },
						     { "text/x-rhythmbox-genre", 0, TARGET_PROPERTY },
						     { "application/x-rhythmbox-source", 0, TARGET_SOURCE },
						     { "application/x-rhythmbox-entry", 0, TARGET_ENTRIES },
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

	o_class = (GObjectClass *) class;

	o_class->finalize = rb_sourcelist_model_finalize;

	/**
	 * RBSourceListModel::drop-received:
	 * @model: the #RBSourceListModel
	 * @target: the #RBSource receiving the drop
	 * @pos: the drop position
	 * @data: the drop data
	 *
	 * Emitted when a drag and drop operation to the source list completes.
	 */
	rb_sourcelist_model_signals[DROP_RECEIVED] =
		g_signal_new ("drop_received",
			      G_OBJECT_CLASS_TYPE (class),
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
  iface->rb_drag_data_received = rb_sourcelist_model_drag_data_received;
  iface->rb_row_drop_possible = rb_sourcelist_model_row_drop_possible;
  iface->rb_row_drop_position = rb_sourcelist_model_row_drop_position;
  iface->rb_get_drag_target = rb_sourcelist_model_get_drag_target;
}

static void
rb_sourcelist_model_drag_source_init (RbTreeDragSourceIface *iface)
{
  iface->rb_row_draggable = rb_sourcelist_model_row_draggable;
  iface->rb_drag_data_get = rb_sourcelist_model_drag_data_get;
  iface->rb_drag_data_delete = rb_sourcelist_model_drag_data_delete;
}

/**
 * rb_sourcelist_model_set_dnd_targets:
 * @sourcelist: the #RBSourceListModel
 * @treeview: the sourcel ist #GtkTreeView
 *
 * Sets up the drag and drop targets for the source list.
 */
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

	g_return_if_fail (RB_IS_SOURCELIST_MODEL (object));
	model = RB_SOURCELIST_MODEL (object);

	G_OBJECT_CLASS (rb_sourcelist_model_parent_class)->finalize (object);
}

/**
 * rb_sourcelist_model_new:
 *
 * This constructs both the GtkTreeStore holding the source
 * data and the filter model that hides invisible sources.
 * 
 * Return value: the #RBSourceListModel
 */
GtkTreeModel *
rb_sourcelist_model_new (void)
{
	RBSourceListModel *model;
	GtkTreeStore *store;
 	GType *column_types = g_new (GType, RB_SOURCELIST_MODEL_N_COLUMNS);

	column_types[RB_SOURCELIST_MODEL_COLUMN_PLAYING] = G_TYPE_BOOLEAN;
	column_types[RB_SOURCELIST_MODEL_COLUMN_PIXBUF] = GDK_TYPE_PIXBUF;
	column_types[RB_SOURCELIST_MODEL_COLUMN_NAME] = G_TYPE_STRING;
	column_types[RB_SOURCELIST_MODEL_COLUMN_SOURCE] = G_TYPE_OBJECT;
	column_types[RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES] = PANGO_TYPE_ATTR_LIST;
	column_types[RB_SOURCELIST_MODEL_COLUMN_VISIBILITY] = G_TYPE_BOOLEAN;
	column_types[RB_SOURCELIST_MODEL_COLUMN_IS_GROUP] = G_TYPE_BOOLEAN;
	column_types[RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY] = RB_TYPE_SOURCE_GROUP_CATEGORY;
	store = gtk_tree_store_newv (RB_SOURCELIST_MODEL_N_COLUMNS,
				     column_types);

 	model = RB_SOURCELIST_MODEL (g_object_new (RB_TYPE_SOURCELIST_MODEL,
						   "child-model", store,
						   "virtual-root", NULL,
						   NULL));
	g_object_unref (store);

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
rb_sourcelist_model_is_row_visible (GtkTreeModel *model,
				    GtkTreeIter *iter,
				    RBSourceListModel *sourcelist)
{
	RBSource *source;
	gboolean  visibility;

	gtk_tree_model_get (GTK_TREE_MODEL (model), iter,
			    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &source,
			    RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, &visibility,
			    -1);

	if (source != NULL) {
		gboolean visible;
		g_object_get (source, "visibility", &visible, NULL);

		g_object_unref (source);

		return visible;
	} else {
		return visibility;
	}

	return TRUE;
}

static gboolean
rb_sourcelist_model_drag_data_received (RbTreeDragDest *drag_dest,
					GtkTreePath *dest,
					GtkTreeViewDropPosition pos,
					GtkSelectionData *selection_data)
{
	RBSourceListModel *model;
	GdkAtom type;

	g_return_val_if_fail (RB_IS_SOURCELIST_MODEL (drag_dest), FALSE);
	model = RB_SOURCELIST_MODEL (drag_dest);
	type = gtk_selection_data_get_data_type (selection_data);

	if (type == gdk_atom_intern ("text/uri-list", TRUE) ||
	    type == gdk_atom_intern ("application/x-rhythmbox-entry", TRUE)) {
		GtkTreeIter iter;
		RBSource *target = NULL;

		rb_debug ("text/uri-list or application/x-rhythmbox-entry drag data received");

		if (dest != NULL && gtk_tree_model_get_iter (GTK_TREE_MODEL (model), &iter, dest)) {
			gtk_tree_model_get (GTK_TREE_MODEL (model), &iter,
					    RB_SOURCELIST_MODEL_COLUMN_SOURCE, &target, -1);
		}

		g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
			       0, target, pos, selection_data);

		if (target != NULL)
			g_object_unref (target);

		return TRUE;
	}

        /* if artist, album or genre, only allow new playlists */
        if (type == gdk_atom_intern ("text/x-rhythmbox-album", TRUE) ||
            type == gdk_atom_intern ("text/x-rhythmbox-artist", TRUE) ||
            type == gdk_atom_intern ("text/x-rhythmbox-genre", TRUE)) {
                rb_debug ("text/x-rhythmbox-(album|artist|genre) drag data received");
                g_signal_emit (G_OBJECT (model), rb_sourcelist_model_signals[DROP_RECEIVED],
                               0, NULL, pos, selection_data);
                return TRUE;
        }

	if (type == gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) {
		/* don't support dnd of sources */
		return FALSE;
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
rb_sourcelist_model_row_drop_position (RbTreeDragDest   *drag_dest,
				       GtkTreePath       *dest_path,
				       GList *targets,
				       GtkTreeViewDropPosition *pos)
{
	GtkTreeModel *model = GTK_TREE_MODEL (drag_dest);

	if (g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-source", TRUE)) && dest_path) {
		rb_debug ("application/x-rhythmbox-source type");
		return FALSE;
	}

	if (g_list_find (targets, gdk_atom_intern ("text/uri-list", TRUE)) ||
	    g_list_find (targets, gdk_atom_intern ("application/x-rhythmbox-entry", TRUE))) {
		rb_debug ("text/uri-list or application/x-rhythmbox-entry type");
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
	if (g_list_find (gdk_drag_context_list_targets (context),
	    gdk_atom_intern ("application/x-rhythmbox-source", TRUE))) {
		/* always accept rb source path if offered */
		return gdk_atom_intern ("application/x-rhythmbox-source", TRUE);
	}

	if (path) {
		/* only accept text/uri-list or application/x-rhythmbox-entry drops into existing sources */
		GdkAtom entry_atom;

		entry_atom = gdk_atom_intern ("application/x-rhythmbox-entry", FALSE);
		if (g_list_find (gdk_drag_context_list_targets (context), entry_atom))
			return entry_atom;

		return gdk_atom_intern ("text/uri-list", FALSE);
	}

	return gtk_drag_dest_find_target (widget, context,
					  target_list);
}

static gboolean
rb_sourcelist_model_row_draggable (RbTreeDragSource *drag_source,
				   GList *path_list)
{
	return FALSE;
}

static gboolean
rb_sourcelist_model_drag_data_get (RbTreeDragSource *drag_source,
				   GList *path_list,
				   GtkSelectionData *selection_data)
{
	char *path_str;
	GtkTreePath *path;
	GdkAtom selection_data_target;
	guint target;

	selection_data_target = gtk_selection_data_get_target (selection_data);
	path = gtk_tree_row_reference_get_path (path_list->data);
	if (path == NULL)
		return FALSE;

	if (!gtk_target_list_find (sourcelist_drag_target_list,
				   selection_data_target,
				   &target)) {
		return FALSE;
	}

	switch (target) {
	case TARGET_SOURCE:
		rb_debug ("getting drag data as rb source path");
		path_str = gtk_tree_path_to_string (path);
		gtk_selection_data_set (selection_data,
					selection_data_target,
					8, (guchar *) path_str,
					strlen (path_str));
		g_free (path_str);
		gtk_tree_path_free (path);
		return TRUE;
	case TARGET_URIS:
	case TARGET_ENTRIES:
	{
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
			if (target == TARGET_URIS) {
				g_string_append (data, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			} else {
				g_string_append_printf (data,
							"%lu",
							rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID));
			}

			rhythmdb_entry_unref (entry);

		} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (query_model), &iter));

		g_object_unref (query_model);

		gtk_selection_data_set (selection_data,
					selection_data_target,
					8, (guchar *) data->str,
					data->len);

		g_string_free (data, TRUE);
		return TRUE;
	}
	default:
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

/**
 * RBSourceListModelColumn:
 * @RB_SOURCELIST_MODEL_COLUMN_PLAYING: TRUE if the source is playing
 * @RB_SOURCELIST_MODEL_COLUMN_PIXBUF: the source's icon as a pixbuf
 * @RB_SOURCELIST_MODEL_COLUMN_NAME: the source name
 * @RB_SOURCELIST_MODEL_COLUMN_SOURCE: the #RBSource object
 * @RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES: Pango attributes used to render the source name
 * @RB_SOURCELIST_MODEL_COLUMN_VISIBILITY: the source's visibility
 * @RB_SOURCELIST_MODEL_COLUMN_IS_GROUP: whether the row identifies a group or a source
 * @RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY: if the row is a group, the category for the group
 * @RB_SOURCELIST_MODEL_N_COLUMNS: the number of columns
 *
 * Columns present in the source list model.
 */

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_sourcelist_model_column_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_PLAYING, "playing"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_PIXBUF, "pixbuf-icon"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_NAME, "name"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_SOURCE, "source"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_ATTRIBUTES, "attributes"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_VISIBILITY, "visibility"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_IS_GROUP, "is-group"),
			ENUM_ENTRY (RB_SOURCELIST_MODEL_COLUMN_GROUP_CATEGORY, "source-group-category"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourceListModelColumn", values);
	}

	return etype;
}

