/*
 *  arch-tag: Implementation of widget to display RhythmDB entries
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtktreeview.h>

#ifdef USE_GTK_TREE_MODEL_SORT_WORKAROUND
	#include "gtktreemodelsort.h"
#else
	#include <gtk/gtktreemodelsort.h>
#endif

#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkiconfactory.h>
#include <gtk/gtktooltips.h>
#include <gdk/gdkkeysyms.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <string.h>
#include <libxml/tree.h>
#include <stdlib.h>

#include "eggtreemultidnd.h"
#include "rb-tree-model-sort.h"
#include "rb-tree-view-column.h"
#include "rb-entry-view.h"
#include "rb-dialog.h"
#include "rb-library.h"
#include "rb-debug.h"
#include "rhythmdb.h"
#include "rhythmdb-query-model.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-cell-renderer-rating.h"
#include "rb-string-helpers.h"
#include "rb-library-dnd-types.h"
#include "rb-stock-icons.h"
#include "rb-tree-view.h"
#include "eel-gconf-extensions.h"

static void rb_entry_view_class_init (RBEntryViewClass *klass);
static void rb_entry_view_init (RBEntryView *view);
static void rb_entry_view_finalize (GObject *object);
static void rb_entry_view_set_property (GObject *object,
				       guint prop_id,
				       const GValue *value,
				       GParamSpec *pspec);
static void rb_entry_view_get_property (GObject *object,
				       guint prop_id,
				       GValue *value,
				       GParamSpec *pspec);
static void rb_entry_view_construct (RBEntryView *view);
static int rb_entry_view_sort_func (GtkTreeModel *model,
			           GtkTreeIter *a,
			           GtkTreeIter *b,
			           gpointer user_data);
static void rb_entry_view_selection_changed_cb (GtkTreeSelection *selection,
				               RBEntryView *view);
static void rb_entry_view_row_activated_cb (GtkTreeView *treeview,
			                   GtkTreePath *path,
			                   GtkTreeViewColumn *column,
			                   RBEntryView *view);
static void gtk_tree_model_sort_row_inserted_cb (GtkTreeModel *model,
				                 GtkTreePath *path,
				                 GtkTreeIter *iter,
				                 RBEntryView *view);
static void gtk_tree_model_sort_row_deleted_cb (GtkTreeModel *model,
				                GtkTreePath *path,
			                        RBEntryView *view);
static void gtk_tree_model_sort_row_changed_cb (GtkTreeModel *model,
				                GtkTreePath *path,
				                GtkTreeIter *iter,
			                        RBEntryView *view);
static void gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					              RBEntryView *view);
static gboolean emit_entry_changed (RBEntryView *view);
static void rb_entry_view_columns_parse (RBEntryView *view,
					const char *config);
static void rb_entry_view_columns_config_changed_cb (GConfClient* client,
						    guint cnxn_id,
						    GConfEntry *entry,
						    gpointer user_data);
static void rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
				   const char *path,
				   int rating,
				   RBEntryView *view);
static gboolean rb_entry_view_button_press_cb (GtkTreeView *treeview,
					      GdkEventButton *event,
					      RBEntryView *view);
static GList * parse_columns_as_glist (const char *str);
static gboolean rb_entry_view_entry_is_visible (RBEntryView *view, RhythmDBEntry *entry,
						GtkTreeIter *iter);
static void rb_entry_view_scroll_to_iter (RBEntryView *view,
					  GtkTreeIter *iter);
static gboolean set_sort_column_id (RBEntryView *view);
static gboolean poll_model (RBEntryView *view);


struct RBEntryViewPrivate
{
	RhythmDB *db;
	
	RhythmDBQueryModel *query_model;
	GtkTreeModel *sortmodel;

	GtkWidget *treeview;
	GtkTreeSelection *selection;

	gboolean playing;
	RhythmDBEntry *playing_entry;

	GdkPixbuf *playing_pixbuf;
	GdkPixbuf *paused_pixbuf;

	char *view_desc_file;

	gboolean have_selection;
	GList *entry_selection;

	gboolean keep_selection;

	RhythmDBEntry *selected_entry;

	gboolean change_sig_queued;
	guint change_sig_id;

	gboolean selection_lock;

	char *columns_key;
	guint gconf_notification_id;
	GHashTable *columns;
	GHashTable *allowed_columns;

	int saved_sort_column_id;
	GtkSortType saved_sort_type;

	RhythmDBPropType default_sort_column_id;

	GList *search_columns;

	gboolean idle;

	guint model_poll_id;

#ifdef USE_GTK_TREE_VIEW_WORKAROUND	
	gboolean use_column_sizing_hack;
	guint freeze_count;
#endif
};

typedef struct {
	RBEntryView *view;
	RBTreeViewColumn *column;
} RBEntryViewSortData;

enum
{
	ENTRY_ADDED,
	ENTRY_SELECTED,
	ENTRY_ACTIVATED,
	CHANGED,
	SHOW_POPUP,
	PLAYING_ENTRY_DELETED,
	HAVE_SEL_CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB,
	PROP_QUERY_MODEL,
	PROP_PLAYING_ENTRY,
	PROP_VIEW_DESC_FILE,
};

static GObjectClass *parent_class = NULL;

static guint rb_entry_view_signals[LAST_SIGNAL] = { 0 };

GType
rb_entry_view_get_type (void)
{
	static GType rb_entry_view_type = 0;

	if (rb_entry_view_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBEntryViewClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_entry_view_class_init,
			NULL,
			NULL,
			sizeof (RBEntryView),
			0,
			(GInstanceInitFunc) rb_entry_view_init
		};

		rb_entry_view_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW,
							    "RBEntryView",
							    &our_info, 0);
	}

	return rb_entry_view_type;
}

static void
rb_entry_view_class_init (RBEntryViewClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_entry_view_finalize;

	object_class->set_property = rb_entry_view_set_property;
	object_class->get_property = rb_entry_view_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_QUERY_MODEL,
					 g_param_spec_object ("entry-model",
							      "GtkTreeModel",
							      "GtkTreeModel with RhythmDBEntrys",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_PLAYING_ENTRY,
					 g_param_spec_pointer ("playing-entry",
							       "Playing entry",
							       "Playing entry",
							       G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_VIEW_DESC_FILE,
					 g_param_spec_string ("view-desc-file",
							      "View description",
							      "View description",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_entry_view_signals[ENTRY_ADDED] =
		g_signal_new ("entry-added",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_added),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[ENTRY_ACTIVATED] =
		g_signal_new ("entry-activated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_activated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[ENTRY_SELECTED] =
		g_signal_new ("entry-selected",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, entry_selected),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_POINTER);
	rb_entry_view_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_entry_view_signals[SHOW_POPUP] =
		g_signal_new ("show_popup",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, show_popup),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_entry_view_signals[PLAYING_ENTRY_DELETED] =
		g_signal_new ("playing_entry_deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, playing_entry_removed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__POINTER,
			      G_TYPE_NONE,
			      1, G_TYPE_POINTER);
	rb_entry_view_signals[HAVE_SEL_CHANGED] =
		g_signal_new ("have_selection_changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBEntryViewClass, have_selection_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__BOOLEAN,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_BOOLEAN);
}

static void
rb_entry_view_init (RBEntryView *view)
{
	GtkWidget *dummy;
	
	view->priv = g_new0 (RBEntryViewPrivate, 1);

	dummy = gtk_tree_view_new ();
	view->priv->playing_pixbuf = gtk_widget_render_icon (dummy,
							     RB_STOCK_PLAYING,
							     GTK_ICON_SIZE_MENU,
							     NULL);

	view->priv->paused_pixbuf = gtk_widget_render_icon (dummy,
							    RB_STOCK_PAUSED,
							    GTK_ICON_SIZE_MENU,
							    NULL);
	gtk_widget_destroy (dummy);
}

static void
rb_entry_view_finalize (GObject *object)
{
	RBEntryView *view;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_ENTRY_VIEW (object));

	view = RB_ENTRY_VIEW (object);

	g_return_if_fail (view->priv != NULL);

	if (view->priv->change_sig_queued)
		g_source_remove (view->priv->change_sig_id);

	g_source_remove (view->priv->model_poll_id);

	g_list_free (view->priv->search_columns);

	g_list_free (view->priv->entry_selection);

	g_free (view->priv->view_desc_file);

	g_free (view->priv->columns_key);

	if (view->priv->gconf_notification_id > 0)
		eel_gconf_notification_remove (view->priv->gconf_notification_id);

	g_hash_table_destroy (view->priv->columns);

	g_object_unref (G_OBJECT (view->priv->sortmodel));
	g_object_unref (G_OBJECT (view->priv->query_model));

	g_object_unref (G_OBJECT (view->priv->playing_pixbuf));
	g_object_unref (G_OBJECT (view->priv->paused_pixbuf));

	g_free (view->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}


/* FIXME!
 * This is a gross hack to work around a GTK+ bug.  See
 * http://bugzilla.gnome.org/show_bug.cgi?id=119797 for more
 * information.
 */
void
rb_entry_view_thaw (RBEntryView *view)
{
#if 0
#ifdef USE_GTK_TREE_VIEW_WORKAROUND
	GList *columns;
	int i;

	if (!view->priv->use_column_sizing_hack)
		return;

	g_return_if_fail (view->priv->freeze_count > 0);

	view->priv->freeze_count--;
	if (view->priv->freeze_count > 0)
		return;

	rb_debug ("thawing entry view");

	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));

	for (i = 0; i < RB_TREE_MODEL_NODE_NUM_COLUMNS && columns != NULL; i++, columns = g_list_next (columns))
		switch (i)
		{
		case RB_TREE_MODEL_NODE_COL_PLAYING:
			break;
		case RB_TREE_MODEL_NODE_COL_RATING:
			break;
		default:
			gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (columns->data),
							 GTK_TREE_VIEW_COLUMN_AUTOSIZE);

			break;
		}
	g_list_free (columns);
#endif
#endif
}

void
rb_entry_view_freeze (RBEntryView *view)
{
#if 0
#ifdef USE_GTK_TREE_VIEW_WORKAROUND
	GList *columns;
	int i;

	if (!view->priv->use_column_sizing_hack)
		return;

	view->priv->freeze_count++;

	rb_debug ("freezing entry view");
	
	columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
	
	for (i = 0; i < RB_TREE_MODEL_NODE_NUM_COLUMNS && columns != NULL; i++, columns = g_list_next (columns))
		switch (i)
		{
		case RB_TREE_MODEL_NODE_COL_PLAYING:
			break;
		case RB_TREE_MODEL_NODE_COL_RATING:
			break;
		default:
			gtk_tree_view_column_set_sizing (GTK_TREE_VIEW_COLUMN (columns->data),
							 GTK_TREE_VIEW_COLUMN_FIXED);

			break;
		}

	g_list_free (columns);
#endif
#endif
}

static void
rb_entry_view_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBEntryView *view = RB_ENTRY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		view->priv->db = g_value_get_object (value);
		break;
	case PROP_QUERY_MODEL:
	{
		RhythmDBQueryModel *new_model;
		GtkTreeModel *new_sort_model;
		GList *columns;
		gint colnum;
		
		if (view->priv->query_model)
			rhythmdb_query_model_cancel (view->priv->query_model);
		new_model = g_value_get_object (value);
		new_sort_model = gtk_tree_model_sort_new_with_model (GTK_TREE_MODEL (new_model));

		/* RHYTHMDB FIXME */
/* 	g_signal_connect_object (G_OBJECT (view->priv->sortmodel), */
/* 				 "node_from_iter", */
/* 				 G_CALLBACK (node_from_sort_iter_cb), */
/* 				 view, */
/* 				 0); */
		g_signal_connect_object (G_OBJECT (new_sort_model),
					 "row_inserted",
					 G_CALLBACK (gtk_tree_model_sort_row_inserted_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (new_sort_model),
					 "row_deleted",
					 G_CALLBACK (gtk_tree_model_sort_row_deleted_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (new_sort_model),
					 "row_changed",
					 G_CALLBACK (gtk_tree_model_sort_row_changed_cb),
					 view,
					 0);
		g_signal_connect_object (G_OBJECT (new_sort_model),
					 "sort_column_changed",
					 G_CALLBACK (gtk_tree_sortable_sort_column_changed_cb),
					 view,
					 0);

		gtk_tree_view_set_model (GTK_TREE_VIEW (view->priv->treeview),
					 GTK_TREE_MODEL (new_sort_model));
		view->priv->sortmodel = new_sort_model;
		view->priv->query_model = new_model;

		for (colnum = 0, columns = gtk_tree_view_get_columns (GTK_TREE_VIEW (view->priv->treeview));
		     columns; colnum++, columns = columns->next) {
			RBEntryViewSortData *data = g_new0 (RBEntryViewSortData, 1);
			data->view = view;
			data->column = columns->data;

			gtk_tree_sortable_set_sort_func (GTK_TREE_SORTABLE (new_sort_model),
							 colnum,
							 rb_entry_view_sort_func,
							 data,
							 g_free);
		}

		if (view->priv->model_poll_id == 0) {
			view->priv->model_poll_id =
				g_idle_add ((GSourceFunc) poll_model, view);
		}

		/* do this in an idle, so that it gets set when done with initializing the rest --
		 * will speed up xml loading a lot */
		g_idle_add ((GSourceFunc) set_sort_column_id, view);

		break;
	}
	case PROP_PLAYING_ENTRY:
	{
		GtkTreeIter iter;
		rb_entry_view_freeze (view);

		view->priv->playing_entry = g_value_get_pointer (value);

		rb_entry_view_thaw (view);

		if (view->priv->playing_entry
		    && !rb_entry_view_entry_is_visible (view, view->priv->playing_entry, &iter))
			rb_entry_view_scroll_to_iter (view, &iter);
	}
	break;
	case PROP_VIEW_DESC_FILE:
		g_free (view->priv->view_desc_file);
		view->priv->view_desc_file = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_entry_view_get_property (GObject *object,
			   guint prop_id,
			   GValue *value,
			   GParamSpec *pspec)
{
	RBEntryView *view = RB_ENTRY_VIEW (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, view->priv->db);
		break;
	case PROP_PLAYING_ENTRY:
		g_value_set_pointer (value, view->priv->playing_entry);
		break;
	case PROP_VIEW_DESC_FILE:
		g_value_set_string (value, view->priv->view_desc_file);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBEntryView *
rb_entry_view_new (RhythmDB *db,
		   const char *view_desc_file)
{
	RBEntryView *view;

	g_assert (view_desc_file != NULL);
	g_assert (g_file_test (view_desc_file, G_FILE_TEST_EXISTS) == TRUE);

	view = RB_ENTRY_VIEW (g_object_new (RB_TYPE_ENTRY_VIEW,
					   "hadjustment", NULL,
					   "vadjustment", NULL,
					   "hscrollbar_policy", GTK_POLICY_AUTOMATIC,
					   "vscrollbar_policy", GTK_POLICY_ALWAYS,
					   "shadow_type", GTK_SHADOW_IN,
					   "view-desc-file", view_desc_file,
					    "db", db,
					   NULL));

	rb_entry_view_construct (view);

	g_return_val_if_fail (view->priv != NULL, NULL);

	return view;
}

void
rb_entry_view_set_query_model (RBEntryView *view, RhythmDBQueryModel *model)
{
	g_object_set (G_OBJECT (view), "entry-model", model, NULL);
}


static RhythmDBEntry *
entry_from_tree_path (RBEntryView *view, GtkTreePath *path)
{
	GtkTreeIter sort_iter, entry_iter;
	RhythmDBEntry *entry;

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel), &sort_iter, path);
	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&entry_iter, &sort_iter);
	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model), &entry_iter, 0,
			    &entry, -1);
	return entry;
}

RhythmDBQueryModel *
rb_entry_view_get_model (RBEntryView *view)
{
	g_return_val_if_fail (RB_IS_ENTRY_VIEW (view), NULL);
	return view->priv->query_model;
}

/* RHYTHMDB FIXME */
/* static void */
/* node_from_sort_iter_cb (RBTreeModelSort *model, */
/* 		        GtkTreeIter *iter, */
/* 		        void **node, */
/* 		        RBEntryView *view) */
/* { */
/* 	GtkTreeIter filter_iter, node_iter; */

/* 	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (model), */
/* 							&filter_iter, iter); */
/* 	egg_tree_model_filter_convert_iter_to_child_iter (EGG_TREE_MODEL_FILTER (view->priv->filtermodel), */
/* 							  &node_iter, &filter_iter); */
/* 	*node = rb_tree_model_node_node_from_iter (RB_TREE_MODEL_NODE (view->priv->nodemodel), &node_iter); */
/* 	if (rb_node_get_property_boolean (*node, RB_NODE_PROP_PRIORITY) == TRUE) */
/* 		*node = NULL; */
/* } */

static gboolean
bool_to_int (const char *boolean)
{
	return (strcmp (boolean, "true") == 0);
}

static gboolean
set_sort_column_id (RBEntryView *view)
{
	gdk_threads_enter ();
	
	if (view->priv->sortmodel)
		gtk_tree_sortable_set_sort_column_id (GTK_TREE_SORTABLE (view->priv->sortmodel),
						      view->priv->default_sort_column_id, GTK_SORT_ASCENDING);

	gdk_threads_leave ();

	return FALSE;
}

static gboolean
rb_entry_view_search_equal (GtkTreeModel *model,
                           gint column,
                           const gchar *key,
                           GtkTreeIter *iter,
                           RBEntryView *view)
{
	gboolean retval = TRUE;
	GList *l;
	char *foldkey;

	foldkey = g_utf8_casefold (key, -1);

	for (l = view->priv->search_columns; l != NULL && retval == TRUE; l = g_list_next (l)) {
		GValue a_value = { 0, };
		GType type;
		const char *stra;
		RhythmDBQueryType col;

		col = GPOINTER_TO_INT (l->data);

		gtk_tree_model_get_value (model, iter, col, &a_value);
		type = gtk_tree_model_get_column_type (model, col);

		switch (G_TYPE_FUNDAMENTAL (type))
		{
		case G_TYPE_STRING:
			stra = g_value_get_string (&a_value);

			if (stra != NULL) {
				char *folda = g_utf8_casefold (stra, -1);

				if (strstr (folda, foldkey) != NULL)
					retval = FALSE;

				g_free (folda);
			}

			break;
		case G_TYPE_INT:
			if (g_value_get_int (&a_value) == atoi (key))
				retval = FALSE;

			break;
		case G_TYPE_LONG:
			if (g_value_get_long (&a_value) == atol (key))
				retval = FALSE;

			break;
		}

		g_value_unset (&a_value);
	}

	g_free (foldkey);

	return retval;
}

static int
column_number_from_name (const char *name)
{
	GEnumClass *prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
	GEnumClass *unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);
	GEnumValue *ev;
	int ret;

	ev = g_enum_get_value_by_name (prop_class, name);
	if (!ev)
		ev = g_enum_get_value_by_name (unsaved_prop_class, name);

	if (ev)
		ret = ev->value+1;  /* Offset since the first column is the playing display */
	else if (!strcmp (name, "playing"))
		ret = 0;
	else
		ret = -1;
	return ret;
}

static GList *
parse_columns_as_glist (const char *str)
{
	GList *ret = NULL;
	char **parts = g_strsplit (str, ",", 0);
	int i;

	for (i = 0; parts != NULL && parts[i] != NULL; i++)
		ret = g_list_append (ret, GINT_TO_POINTER (column_number_from_name (parts[i])));

	g_strfreev (parts);

	return ret;
}

/* Sweet name, eh? */
struct RBEntryViewCellDataFuncData {
	RBEntryView *view;
	RhythmDBPropType propid;
};

static void
rb_entry_view_playing_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				      GtkTreeModel *tree_model, GtkTreeIter *iter,
				      RBEntryView *view)
{
	RhythmDBEntry *entry;
	GdkPixbuf *pixbuf;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &entry, -1);

	if (entry == view->priv->playing_entry && view->priv->playing)
		pixbuf = view->priv->playing_pixbuf;
	else if (entry == view->priv->playing_entry)
		pixbuf = view->priv->paused_pixbuf;
	else
		pixbuf = NULL;

	g_object_set (G_OBJECT (renderer), "pixbuf", pixbuf, NULL);
}

static void
rb_entry_view_rating_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model, GtkTreeIter *iter,
				     RBEntryView *view)
{
	RhythmDBEntry *entry;
	guint rating;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &entry, -1);

	rhythmdb_read_lock (view->priv->db);

	rating = rhythmdb_entry_get_int (view->priv->db, entry, RHYTHMDB_PROP_RATING);

	rhythmdb_read_unlock (view->priv->db);

	g_object_set (G_OBJECT (renderer), "rating", rating, NULL);
}

static void
rb_entry_view_intstr_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				  GtkTreeModel *tree_model, GtkTreeIter *iter,
				  struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	char *str;
	int val;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &entry, -1);

	rhythmdb_read_lock (data->view->priv->db);

	val = rhythmdb_entry_get_int (data->view->priv->db, entry, data->propid);
	rhythmdb_read_unlock (data->view->priv->db);

	if (val >= 0)
		str = g_strdup_printf ("%d", val);
	else
		str = g_strdup ("");

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	g_free (str);
}

static void
rb_entry_view_play_count_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
					 GtkTreeModel *tree_model, GtkTreeIter *iter,
					 struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	int i;
	char *str;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &entry, -1);

	rhythmdb_read_lock (data->view->priv->db);

	i = rhythmdb_entry_get_int (data->view->priv->db, entry, data->propid);
	if (i == 0)
		str = _("Never");
	else
		str = g_strdup_printf ("%d", i);

	rhythmdb_read_unlock (data->view->priv->db);

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
	if (i != 0)
		g_free (str);
}

static void
rb_entry_view_string_cell_data_func (GtkTreeViewColumn *column, GtkCellRenderer *renderer,
				     GtkTreeModel *tree_model, GtkTreeIter *iter,
				     struct RBEntryViewCellDataFuncData *data)
{
	RhythmDBEntry *entry;
	const char *str;

	gtk_tree_model_get (GTK_TREE_MODEL (tree_model), iter, 0, &entry, -1);

	rhythmdb_read_lock (data->view->priv->db);

	str = rhythmdb_entry_get_string (data->view->priv->db, entry, data->propid);

	rhythmdb_read_unlock (data->view->priv->db);

	g_object_set (G_OBJECT (renderer), "text", str, NULL);
}

static void
rb_entry_view_construct (RBEntryView *view)
{
	xmlDocPtr doc;
	xmlNodePtr child;
	char *tmp;
	char *tips;
	GtkTooltips *tool_tips;
	gboolean tooltip_active = FALSE;
	RhythmDBQueryModel *query_model;
	GtkTreeViewColumn *gcolumn;
	GtkCellRenderer *renderer;
	int width;

	tool_tips = gtk_tooltips_new ();
	gtk_tooltips_enable (tool_tips);

	view->priv->columns = g_hash_table_new (NULL, NULL);
	view->priv->allowed_columns = g_hash_table_new (NULL, NULL);


	view->priv->treeview = GTK_WIDGET (gtk_tree_view_new ());

	query_model = rhythmdb_query_model_new_empty (view->priv->db);
	rb_entry_view_set_query_model (view, query_model);
	g_object_unref (G_OBJECT (query_model));
	
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "button_press_event",
			         G_CALLBACK (rb_entry_view_button_press_cb),
			         view,
				 0);
	g_signal_connect_object (G_OBJECT (view->priv->treeview),
			         "row_activated",
			         G_CALLBACK (rb_entry_view_row_activated_cb),
			         view,
				 0);
	view->priv->selection = gtk_tree_view_get_selection (GTK_TREE_VIEW (view->priv->treeview));
	g_signal_connect_object (G_OBJECT (view->priv->selection),
			         "changed",
			         G_CALLBACK (rb_entry_view_selection_changed_cb),
			         view,
				 0);

	gtk_container_add (GTK_CONTAINER (view), view->priv->treeview);

	/* load layout */
	rb_debug ("loading layout from %s", view->priv->view_desc_file);
	doc = xmlParseFile (view->priv->view_desc_file);

	if (doc == NULL) {
		rb_error_dialog (_("Failed to parse %s as EntryView layout file"),
				 view->priv->view_desc_file);
		return;
	}

	child = doc->children; 	
	while (child && child->type != XML_ELEMENT_NODE) {
		child = child->next;
	}

	tmp = xmlGetProp (child, "rules-hint");
	if (tmp != NULL)
		gtk_tree_view_set_rules_hint (GTK_TREE_VIEW (view->priv->treeview), bool_to_int (tmp));
	g_free (tmp);

	tmp = xmlGetProp (child, "headers-visible");
	if (tmp != NULL)
		gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (view->priv->treeview), bool_to_int (tmp));
	g_free (tmp);

#ifdef USE_GTK_TREE_VIEW_WORKAROUND
	tmp = xmlGetProp (child, "use-column-sizing-hack");
	if (tmp != NULL)
		view->priv->use_column_sizing_hack = TRUE;
	g_free (tmp);
#endif

	tmp = xmlGetProp (child, "selection-mode");
	if (tmp != NULL) {
		GEnumClass *class = g_type_class_ref (GTK_TYPE_SELECTION_MODE);
		GEnumValue *ev = g_enum_get_value_by_name (class, tmp);
		gtk_tree_selection_set_mode (view->priv->selection, ev->value);
		g_type_class_unref (class);
	}
	g_free (tmp);

	tmp = xmlGetProp (child, "search-order");
	if (tmp != NULL)
		view->priv->search_columns = parse_columns_as_glist (tmp);
	g_free (tmp);

	tmp = xmlGetProp (child, "allowed-columns");
	if (tmp != NULL) {
		GList *l = parse_columns_as_glist (tmp);
		for (; l != NULL; l = g_list_next (l)) {
			g_hash_table_insert (view->priv->allowed_columns,
					     l->data, GINT_TO_POINTER (1));
		}
		g_list_free (l);
	}
	rb_debug ("allowed columns: %s", tmp);
	g_free (tmp);

	tmp = xmlGetProp (child, "keep-selection");
	if (tmp != NULL)
		view->priv->keep_selection = bool_to_int (tmp);
	g_free (tmp);

	tmp = xmlGetProp (child, "searchable");
	if (tmp != NULL && bool_to_int (tmp)) {
                gtk_tree_view_set_enable_search (GTK_TREE_VIEW (view->priv->treeview), TRUE);
                gtk_tree_view_set_search_equal_func (GTK_TREE_VIEW (view->priv->treeview),
						     (GtkTreeViewSearchEqualFunc) rb_entry_view_search_equal, view, NULL);
	}
	g_free (tmp);

	view->priv->columns_key = xmlGetProp (child, "column-visibility-pref");

        /* Playing icon column */
	gcolumn = GTK_TREE_VIEW_COLUMN (rb_tree_view_column_new ());
	renderer = rb_cell_renderer_pixbuf_new ();
	gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
	gtk_tree_view_column_set_cell_data_func (gcolumn, renderer,
						 (GtkTreeCellDataFunc)
						 rb_entry_view_playing_cell_data_func,
						 view,
						 NULL);
	gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_FIXED);
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
	gtk_tree_view_column_set_fixed_width (gcolumn, width + 5);
	gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview), gcolumn);


	for (child = child->children; child != NULL; child = child->next) {
		char *propname;
		RhythmDBPropType proptype;
		char *title = NULL;
		gboolean reorderable = FALSE, resizable = FALSE, clickable = TRUE;
		gboolean default_sort_column = FALSE, expand = FALSE;
		GList *sort_order = NULL;
		int column;
		GEnumClass *prop_class = g_type_class_ref (RHYTHMDB_TYPE_PROP);
		GEnumClass *unsaved_prop_class = g_type_class_ref (RHYTHMDB_TYPE_UNSAVED_PROP);

		/* get props from the xml file */
		propname = xmlGetProp (child, "column");
		if (propname == NULL)
			continue;
		else {
			column = column_number_from_name (propname);
			proptype = column-1;
			if (column < 0) {
				fprintf (stderr, "Unknown column %s!\n", propname);
				continue;
			}
		}

		title = xmlGetProp (child, "_title");

		tmp = xmlGetProp (child, "reorderable");
		if (tmp != NULL)
			reorderable = bool_to_int (tmp);
		g_free (tmp);

		tips = xmlGetProp (child, "tooltip");
		if (tips != NULL)
		  tooltip_active = TRUE;

		tmp = xmlGetProp (child, "resizable");
		if (tmp != NULL)
			resizable = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "clickable");
		if (tmp != NULL)
			clickable = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "default-sort-column");
		if (tmp != NULL)
			default_sort_column = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "default-search-column");
		if (tmp != NULL && bool_to_int (tmp))
			gtk_tree_view_set_search_column (GTK_TREE_VIEW (view->priv->treeview), column);
		g_free (tmp);

		tmp = xmlGetProp (child, "expand");
		if (tmp != NULL)
			expand = bool_to_int (tmp);
		g_free (tmp);

		tmp = xmlGetProp (child, "sort-order");
		if (tmp != NULL) {
			char **parts = g_strsplit (tmp, " ", 0);
			int i;

			for (i = 0; parts != NULL && parts[i] != NULL; i++) {
				RhythmDBPropType prop = column_number_from_name (parts[i])-1;
				g_assert (prop >= 0);
				sort_order = g_list_append (sort_order, GINT_TO_POINTER (prop));
			}

			g_strfreev (parts);
		}
		g_free (tmp);

		g_type_class_unref (prop_class);
		g_type_class_unref (unsaved_prop_class);

		/* so we got all info, now we can actually build the column */
		gcolumn = GTK_TREE_VIEW_COLUMN (rb_tree_view_column_new ());
		switch (column)
		{
		case 0:  /* Playing icon column is handled above */
			g_assert_not_reached ();
			break;
		case RHYTHMDB_PROP_RATING+1:
			renderer = rb_cell_renderer_rating_new ();
			gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
			gtk_tree_view_column_set_cell_data_func (gcolumn, renderer,
								 (GtkTreeCellDataFunc)
								 rb_entry_view_rating_cell_data_func,
								 view,
								 NULL);
			gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_FIXED);
			gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &width, NULL);
			gtk_tree_view_column_set_fixed_width (gcolumn, width * 5 + 5);

			g_signal_connect_object (renderer,
						 "rated",
					         G_CALLBACK (rb_entry_view_rated_cb),
						 G_OBJECT (view),
						 0);

			break;
		default:
		{
			struct RBEntryViewCellDataFuncData *data =
				g_new0 (struct RBEntryViewCellDataFuncData, 1);
			GtkTreeCellDataFunc func;
			
			data->view = view;
			data->propid = column-1;

			if (data->propid == RHYTHMDB_PROP_TRACK_NUMBER)
				func = (GtkTreeCellDataFunc) rb_entry_view_intstr_cell_data_func;
			else if (data->propid == RHYTHMDB_PROP_PLAY_COUNT)
				func = (GtkTreeCellDataFunc) rb_entry_view_play_count_cell_data_func;
			else
				func = (GtkTreeCellDataFunc) rb_entry_view_string_cell_data_func;				

			renderer = gtk_cell_renderer_text_new ();
			gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
			gtk_tree_view_column_set_cell_data_func (gcolumn, renderer,
								 func, data, g_free);
			gtk_tree_view_column_set_sizing (gcolumn, GTK_TREE_VIEW_COLUMN_AUTOSIZE);

			break;
		}
		}

		rb_tree_view_column_set_sort_order (RB_TREE_VIEW_COLUMN (gcolumn),
						    sort_order);
							    
		gtk_tree_view_column_set_resizable (gcolumn, resizable);

		if (title != NULL) {
			gtk_tree_view_column_set_title (gcolumn, _(title));
			g_free (title);
		}

		gtk_tree_view_column_set_reorderable (gcolumn, reorderable);

		if (clickable == TRUE) {
			if (sort_order != NULL)
				gtk_tree_view_column_set_sort_column_id (gcolumn, column);
		}

		gtk_tree_view_column_set_clickable (gcolumn, clickable);

		if (default_sort_column == TRUE) {
			view->priv->default_sort_column_id = column;
		}

		rb_debug ("appending column; %s", xmlGetProp (child, "column"));
		gtk_tree_view_append_column (GTK_TREE_VIEW (view->priv->treeview),
					     gcolumn);

		g_hash_table_insert (view->priv->columns,
				     GINT_TO_POINTER (column),
				     gcolumn);

		if (tooltip_active) {
		  gtk_tooltips_set_tip (GTK_TOOLTIPS(tool_tips), 
					GTK_WIDGET (gcolumn->button), 
					tips, NULL);
		  tooltip_active = FALSE;
		  g_free (tips);
		}

	}

	if (view->priv->columns_key != NULL)
	{
		char *config = eel_gconf_get_string (view->priv->columns_key);

		if (config != NULL) {
			rb_entry_view_columns_parse (view, config);

			view->priv->gconf_notification_id = 
				eel_gconf_notification_add (view->priv->columns_key,
							    rb_entry_view_columns_config_changed_cb,
							    view);

			g_free (config);
		}
	}

	xmlFreeDoc (doc);
}

static void
rb_entry_view_rated_cb (RBCellRendererRating *cellrating,
		       const char *path_string,
		       int rating,
		       RBEntryView *view)
{
	GtkTreePath *path;
	RhythmDBEntry *entry;
	GValue value = { 0, };

	g_return_if_fail (rating >= 1 && rating <= 5 );
	g_return_if_fail (path_string != NULL);

	path = gtk_tree_path_new_from_string (path_string);
	entry = entry_from_tree_path (view, path);
	gtk_tree_path_free (path);

	rb_entry_view_freeze (view);

	rhythmdb_write_lock (view->priv->db);

	g_value_init (&value, G_TYPE_INT);
	g_value_set_int (&value, rating);
	rhythmdb_entry_set (view->priv->db, entry, RHYTHMDB_PROP_RATING,
			    &value);
	g_value_unset (&value);

	rhythmdb_write_unlock (view->priv->db);

	rb_entry_view_thaw (view);
}

void
rb_entry_view_set_playing_entry (RBEntryView *view,
				 RhythmDBEntry *entry)
{
	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	g_object_set (G_OBJECT (view), "playing-entry", entry, NULL);
}

RhythmDBEntry *
rb_entry_view_get_playing_entry (RBEntryView *view)
{
	g_return_val_if_fail (RB_IS_ENTRY_VIEW (view), NULL);

	return view->priv->playing_entry;
}

RhythmDBEntry *
rb_entry_view_get_first_entry (RBEntryView *view)
{
	GtkTreeIter iter;
	RhythmDBEntry *entry;

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (view->priv->query_model), &iter)) {
		gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model),
				    &iter, 0, &entry, -1);
		return entry;
	}
	return NULL;
}

RhythmDBEntry *
rb_entry_view_get_next_entry (RBEntryView *view)
{
	GtkTreeIter entry_iter, sort_iter;
	RhythmDBEntry *entry;

	if (view->priv->playing_entry == NULL)
		return NULL;

	if (!rhythmdb_query_model_iter_from_entry (view->priv->query_model,
						   view->priv->playing_entry,
						   &entry_iter))
		return NULL;
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&sort_iter, &entry_iter);
	
	if (gtk_tree_model_iter_next (GTK_TREE_MODEL (view->priv->sortmodel),
				      &sort_iter)) {
		gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
								&entry_iter, &sort_iter);
		gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model),
				    &entry_iter, 0, &entry, -1);
		return entry;
	}
	return NULL;
}

RhythmDBEntry *
rb_entry_view_get_previous_entry (RBEntryView *view)
{
	GtkTreeIter entry_iter, sort_iter;
	GtkTreePath *path;
	RhythmDBEntry *entry;


	if (view->priv->playing_entry == NULL)
		return NULL;
	
	if (!rhythmdb_query_model_iter_from_entry (view->priv->query_model,
						   view->priv->playing_entry,
						   &entry_iter))
		return NULL;
	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&sort_iter, &entry_iter);

	path = gtk_tree_model_get_path (GTK_TREE_MODEL (view->priv->sortmodel), &sort_iter);
	g_assert (path);
	if (!gtk_tree_path_prev (path)) {
		gtk_tree_path_free (path);
		return NULL;
	}

	g_assert (gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel), &sort_iter,
					   path));
	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&entry_iter, &sort_iter);

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model),
			    &entry_iter, 0, &entry, -1);
	return entry;
}

static gboolean
harvest_entries (GtkTreeModel *model,
		 GtkTreePath *path,
		 GtkTreeIter *iter,
		 void **data)
{
	GtkTreeModelSort *sortmodel = GTK_TREE_MODEL_SORT (model);
	RhythmDBQueryModel *query_model = RHYTHMDB_QUERY_MODEL (sortmodel->child_model);
	GList **list = (GList **) data;
	GtkTreeIter iter2;
	RhythmDBEntry *entry;

	gtk_tree_model_sort_convert_iter_to_child_iter (sortmodel, &iter2, iter);

	gtk_tree_model_get (GTK_TREE_MODEL (query_model), &iter2, 0, &entry, -1);

	*list = g_list_append (*list, entry);

	return FALSE;
}

GList *
rb_entry_view_get_selected_entries (RBEntryView *view)
{
	GList *list = NULL;

	gtk_tree_selection_selected_foreach (view->priv->selection,
					     (GtkTreeSelectionForeachFunc) harvest_entries,
					     (gpointer) &list);
	g_list_free (view->priv->entry_selection);
	view->priv->entry_selection = g_list_copy (list);

	return list;
}

RhythmDBEntry *
rb_entry_view_get_random_entry (RBEntryView *view)
{
	RhythmDBEntry *entry;
	GtkTreePath *path;
	GtkTreeIter iter, iter2;
	char *path_str;
	int index, n_rows;

	n_rows = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (view->priv->query_model), NULL);
	if (n_rows == 0)
		return NULL;
	else if ((n_rows - 1) > 0)
		index = g_random_int_range (0, n_rows);
	else
		index = 0;

	path_str = g_strdup_printf ("%d", index);
	path = gtk_tree_path_new_from_string (path_str);
	g_free (path_str);

	gtk_tree_model_get_iter (GTK_TREE_MODEL (view->priv->sortmodel),
				 &iter, path);

	gtk_tree_path_free (path);

	gtk_tree_model_sort_convert_iter_to_child_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&iter2, &iter);

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model),
			    &iter, 0, &iter2, -1);

	return entry;
}

static int
rb_entry_view_sort_func (GtkTreeModel *model,
			 GtkTreeIter *a,
			 GtkTreeIter *b,
			 gpointer user_data)
{
	RBEntryViewSortData *data;
	GList *order;
	GList *l;
	int retval = 0;
	RBTreeViewColumn *gcolumn;
	RBEntryView *view;

	data = user_data;
	view = RB_ENTRY_VIEW (data->view);
	gcolumn = RB_TREE_VIEW_COLUMN (data->column);
	order = rb_tree_view_column_get_sort_order (gcolumn);

	for (l = order; l != NULL && retval == 0; l = g_list_next (l)) {
		RhythmDBPropType prop = GPOINTER_TO_INT (l->data);
		GType type;
		GValue a_entry = {0, };
		GValue b_entry = {0, };
		GValue a_value = {0, };
		GValue b_value = {0, };
		const char *stra, *strb;
		char *folda, *foldb;

		/* +1 offset for playing column */
		gtk_tree_model_get_value (model, a, prop+1, &a_entry);
		gtk_tree_model_get_value (model, b, prop+1, &b_entry);

		type = rhythmdb_get_property_type (view->priv->db, prop);
		g_value_init (&a_value, type);
		g_value_init (&b_value, type);

		rhythmdb_read_lock (view->priv->db);
		rhythmdb_entry_get (view->priv->db,
				    g_value_get_pointer (&a_entry),
				    prop, &a_value);
		rhythmdb_entry_get (view->priv->db,
				    g_value_get_pointer (&b_entry),
				    prop, &b_value);
		rhythmdb_read_unlock (view->priv->db);

		switch (G_TYPE_FUNDAMENTAL (type))
		{
		case G_TYPE_BOOLEAN:
			if (g_value_get_boolean (&a_value) < g_value_get_boolean (&b_value))
				retval = -1;
			else if (g_value_get_boolean (&a_value) == g_value_get_boolean (&b_value))
				retval = 0;
			else
				retval = 1;
			break;
		case G_TYPE_STRING:
			stra = g_value_get_string (&a_value);
			strb = g_value_get_string (&b_value);
			if (stra == NULL) stra = "";
			if (strb == NULL) strb = "";

			if (prop == RHYTHMDB_PROP_ARTIST_SORT_KEY ||
			    prop == RHYTHMDB_PROP_ALBUM_SORT_KEY ||
			    prop == RHYTHMDB_PROP_TITLE_SORT_KEY)
				retval = strcmp (stra, strb);
			else {
				folda = g_utf8_casefold (stra, g_utf8_strlen (stra, -1));
				foldb = g_utf8_casefold (strb, g_utf8_strlen (strb, -1));
				retval = g_utf8_collate (folda, foldb);
				g_free (folda);
				g_free (foldb);
			}
			break;
		case G_TYPE_INT:
			if (g_value_get_int (&a_value) < g_value_get_int (&b_value))
				retval = -1;
			else if (g_value_get_int (&a_value) == g_value_get_int (&b_value))
				retval = 0;
			else
				retval = 1;
			break;
		case G_TYPE_LONG:
			if (g_value_get_long (&a_value) < g_value_get_long (&b_value))
				retval = -1;
			else if (g_value_get_long (&a_value) == g_value_get_long (&b_value))
				retval = 0;
			else
				retval = 1;
			break;
		case G_TYPE_OBJECT:
			if ((g_value_get_object (&a_value) == NULL) && (g_value_get_object (&b_value) != NULL))
				retval = -1;
			else if ((g_value_get_object (&a_value) != NULL) && (g_value_get_object (&b_value) == NULL))
				retval = 1;
			else
				retval = 0;
			break;
		default:
			g_warning ("Attempting to sort on invalid type %s\n", g_type_name (type));
			break;
		}

		g_value_unset (&a_value);
		g_value_unset (&b_value);
		g_value_unset (&a_entry);
		g_value_unset (&b_entry);
	}

	return retval;
}

static gboolean
rb_entry_view_button_press_cb (GtkTreeView *treeview,
			      GdkEventButton *event,
			      RBEntryView *view)
{
	if (event->button == 3) {
		GtkTreePath *path;
		RhythmDBEntry *entry;

		gtk_tree_view_get_path_at_pos (treeview, event->x, event->y, &path, NULL, NULL, NULL);
		if (path != NULL) {
			entry = entry_from_tree_path (view, path);
			
			/* FIXME - don't require a popup to select an entry */
			rb_entry_view_select_entry (view, entry);
		
			g_signal_emit (G_OBJECT (view), rb_entry_view_signals[SHOW_POPUP], 0);
		}
		return view->priv->have_selection;
	}

	return FALSE;
}

static void
queue_changed_sig (RBEntryView *view)
{
	if (!view->priv->change_sig_queued) {
		rb_debug ("queueing changed signal");
		view->priv->change_sig_id = g_timeout_add (250, (GSourceFunc) emit_entry_changed, view);
	}
	view->priv->change_sig_queued = TRUE;
}

static void
rb_entry_view_selection_changed_cb (GtkTreeSelection *selection,
				   RBEntryView *view)
{
	gboolean available;
	RhythmDBEntry *selected_entry = NULL;
	GList *sel;

	if (view->priv->selection_lock == TRUE)
		return;

	sel = rb_entry_view_get_selected_entries (view);
	available = (sel != NULL);
	if (sel != NULL)
		selected_entry = (g_list_first (sel))->data;

	if (available != view->priv->have_selection) {
		queue_changed_sig (view);
		view->priv->have_selection = available;

		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[HAVE_SEL_CHANGED], 0, available);
	}

	if (selected_entry != NULL && selected_entry != view->priv->selected_entry)
		g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_SELECTED], 0, selected_entry);

	view->priv->selected_entry = selected_entry;

	g_list_free (sel);
}

gboolean
rb_entry_view_have_selection (RBEntryView *view)
{
	return view->priv->have_selection;
}

static void
rb_entry_view_row_activated_cb (GtkTreeView *treeview,
			       GtkTreePath *path,
			       GtkTreeViewColumn *column,
			       RBEntryView *view)
{
	GtkTreeIter iter, iter2;
	RhythmDBEntry *entry;

	rb_debug ("row activated");
	gtk_tree_model_get_iter (view->priv->sortmodel, &iter, path);
	gtk_tree_model_sort_convert_iter_to_child_iter
		(GTK_TREE_MODEL_SORT (view->priv->sortmodel), &iter2, &iter);

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model), &iter2, 0,
			    &entry, -1);

	rb_debug ("emitting entry activated");
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_ACTIVATED], 0, entry);
}

static void
gtk_tree_model_sort_row_inserted_cb (GtkTreeModel *model,
				     GtkTreePath *path,
				     GtkTreeIter *iter,
				     RBEntryView *view)
{
	RhythmDBEntry *entry;
	gtk_tree_model_get (model, iter, 0, &entry, -1);
	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[ENTRY_ADDED], 0, entry);
	queue_changed_sig (view);
}

static void
gtk_tree_model_sort_row_deleted_cb (GtkTreeModel *model,
				    GtkTreePath *path,
			            RBEntryView *view)
{
	queue_changed_sig (view);
}

static void
gtk_tree_model_sort_row_changed_cb (GtkTreeModel *model,
				    GtkTreePath *path,
				    GtkTreeIter *iter,
			            RBEntryView *view)
{
	queue_changed_sig (view);
}

static void
gtk_tree_sortable_sort_column_changed_cb (GtkTreeSortable *sortable,
					  RBEntryView *view)
{
	queue_changed_sig (view);
}

void
rb_entry_view_select_all (RBEntryView *view)
{
	gtk_tree_selection_select_all (view->priv->selection);
}

void
rb_entry_view_select_none (RBEntryView *view)
{
	view->priv->selection_lock = TRUE;

	gtk_tree_selection_unselect_all (view->priv->selection);

	view->priv->selected_entry = NULL;

	view->priv->selection_lock = FALSE;
}

void
rb_entry_view_select_entry (RBEntryView *view,
			    RhythmDBEntry *entry)
{
	GtkTreeIter entry_iter, sort_iter;

	if (entry == NULL)
		return;

	view->priv->selection_lock = TRUE;

	rb_entry_view_select_none (view);

	rhythmdb_query_model_iter_from_entry (view->priv->query_model,
					      entry, &entry_iter);

	gtk_tree_model_get (GTK_TREE_MODEL (view->priv->query_model), &entry_iter, 0,
			    &entry, -1);

	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&sort_iter, &entry_iter);

	gtk_tree_selection_select_iter (view->priv->selection, &sort_iter);

	view->priv->selection_lock = FALSE;
}

void
rb_entry_view_scroll_to_entry (RBEntryView *view,
			       RhythmDBEntry *entry)
{
	GtkTreeIter iter;
	
	rhythmdb_query_model_iter_from_entry (view->priv->query_model,
					      entry, &iter);
	
	rb_entry_view_scroll_to_iter (view, &iter);
}

static void
rb_entry_view_scroll_to_iter (RBEntryView *view,
			      GtkTreeIter *iter)
{
	GtkTreePath *path;
	GtkTreeIter sort_iter;

	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							&sort_iter, iter);

	path = gtk_tree_model_get_path (view->priv->sortmodel, &sort_iter);
	gtk_tree_view_scroll_to_cell (GTK_TREE_VIEW (view->priv->treeview), path,
				      gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				      TRUE, 0.5, 0.0);
	gtk_tree_view_set_cursor (GTK_TREE_VIEW (view->priv->treeview), path,
				  gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0), FALSE);

	gtk_tree_path_free (path);
}

static gboolean
rb_entry_view_entry_is_visible (RBEntryView *view,
				RhythmDBEntry *entry,
				GtkTreeIter *iter)
{
	GtkTreeIter iter2;
	GtkTreePath *path;
	GdkRectangle rect;

	g_return_val_if_fail (entry != NULL, FALSE);

	if (!GTK_WIDGET_REALIZED (view))
		return FALSE;

	rhythmdb_query_model_iter_from_entry (view->priv->query_model,
					      entry, &iter2);

	gtk_tree_model_sort_convert_child_iter_to_iter (GTK_TREE_MODEL_SORT (view->priv->sortmodel),
							iter, &iter2);

	path = gtk_tree_model_get_path (view->priv->sortmodel, iter);
	gtk_tree_view_get_cell_area (GTK_TREE_VIEW (view->priv->treeview),
				     path,
				     gtk_tree_view_get_column (GTK_TREE_VIEW (view->priv->treeview), 0),
				     &rect);

	gtk_tree_path_free (path);

	return rect.y != 0 && rect.height != 0;
}

static gboolean
emit_entry_changed (RBEntryView *view)
{
	GDK_THREADS_ENTER ();

	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[CHANGED], 0);

	view->priv->change_sig_queued = FALSE;

	GDK_THREADS_LEAVE ();

	return FALSE;
}

/* static void */
/* playing_entry_deleted_cb (RhythmDB *db, RBEntryView *view) */
/* { */
/* 	rb_debug ("emitting playing entry destroyed"); */
/* 	g_signal_emit (G_OBJECT (view), rb_entry_view_signals[PLAYING_ENTRY_DELETED], */
/* 		       0, view->priv->playing_entry); */
/* } */

void
rb_entry_view_enable_drag_source (RBEntryView *view,
				 const GtkTargetEntry *targets,
				 int n_targets)
{
	g_return_if_fail (view != NULL);

	egg_tree_multi_drag_add_drag_support (GTK_TREE_VIEW (view->priv->treeview));

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (view->priv->treeview),
						GDK_BUTTON1_MASK | GDK_BUTTON3_MASK,
						targets, n_targets, GDK_ACTION_COPY);
}

static void
rb_entry_view_columns_config_changed_cb (GConfClient* client,
					guint cnxn_id,
					GConfEntry *entry,
					gpointer user_data)
{
	RBEntryView *view = user_data;
	GConfValue *value;
	const char *str;

	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	value = gconf_entry_get_value (entry);
	str = gconf_value_get_string (value);

	rb_entry_view_columns_parse (view, str);
}

static void
rb_entry_view_columns_parse (RBEntryView *view,
			    const char *config)
{
	int i;
	char **items;
	GList *visible_columns = NULL;
	GtkTreeViewColumn *column = NULL;

	g_return_if_fail (view != NULL);
	g_return_if_fail (config != NULL);

	/* the list of visible columns */
	items = g_strsplit (config, ",", 0);
	if (items != NULL) {
		for (i = 0; items[i] != NULL; i++) {
			int value = column_number_from_name (items[i]);

			if ((value >= 0)
			    && (value < RHYTHMDB_NUM_PROPERTIES)) {
				if (g_hash_table_lookup (view->priv->allowed_columns,
							 GINT_TO_POINTER (value)) == NULL) {
					rb_debug ("column %s is not allowed", items[i]);
					continue;
				}
				visible_columns = g_list_append (visible_columns,
								 GINT_TO_POINTER (value));
			}
		}

		g_strfreev (items);
	}

	/* set the visibility for all columns */
	for (i = 0; i < RHYTHMDB_NUM_PROPERTIES+1; i++) {
		switch (i)
		{
		case 0: /* Playing */
		case RHYTHMDB_PROP_TITLE+1:
			break;
			
		default:
			column = g_hash_table_lookup (view->priv->columns,
						      GINT_TO_POINTER (i));
			if (column != NULL) {
				/* RHYTHMDB FIXME */
 				/* gboolean visible = g_list_find (visible_columns, GINT_TO_POINTER (i)) != NULL; */
				/* gtk_tree_view_column_set_visible (column, visible); */

				gtk_tree_view_column_set_visible (column, TRUE);
			}
			break;

		}
	}

	if (visible_columns != NULL)
		g_list_free (visible_columns);
}

void
rb_entry_view_set_playing (RBEntryView *view,
			   gboolean playing)
{
	g_return_if_fail (RB_IS_ENTRY_VIEW (view));

	rb_entry_view_freeze (view);

	view->priv->playing = TRUE;

	rb_entry_view_thaw (view);
}

static gboolean
poll_model (RBEntryView *view)
{
	GTimeVal timeout;
	gboolean did_sync;
	GDK_THREADS_ENTER ();

	g_get_current_time (&timeout);
	g_time_val_add (&timeout, 500);

	did_sync = rhythmdb_query_model_sync (view->priv->query_model, &timeout);

	GDK_THREADS_LEAVE ();

	if (did_sync)
		g_idle_add ((GSourceFunc) poll_model, view);
	else
		g_timeout_add (300, (GSourceFunc) poll_model, view);
	return FALSE;
}
