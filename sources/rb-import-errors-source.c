/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2006  Jonathan Matthew <jonathan@kaolin.wh9.net>
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

/*
 * This source lists files rhythmbox failed to import.
 */

#include <config.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-entry-view.h"
#include "rb-import-errors-source.h"
#include "rb-util.h"

static void rb_import_errors_source_class_init (RBImportErrorsSourceClass *klass);
static void rb_import_errors_source_init (RBImportErrorsSource *source);
static GObject *rb_import_errors_source_constructor (GType type, guint n_construct_properties,
						     GObjectConstructParam *construct_properties);
static void rb_import_errors_source_dispose (GObject *object);

static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_delete (RBSource *source);
static char *impl_get_status (RBSource *source);

struct RBImportErrorsSourcePrivate
{
	RhythmDB *db;
	RhythmDBQueryModel *model;
	RBEntryView *view;
	RhythmDBEntryType entry_type;
};

G_DEFINE_TYPE (RBImportErrorsSource, rb_import_errors_source, RB_TYPE_SOURCE);

static void
rb_import_errors_source_class_init (RBImportErrorsSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_import_errors_source_dispose;
	object_class->constructor = rb_import_errors_source_constructor;

	source_class->impl_can_browse = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_get_entry_view = impl_get_entry_view;
	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_search = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_delete = impl_delete;

	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_get_status = impl_get_status;
	
	g_type_class_add_private (klass, sizeof (RBImportErrorsSourcePrivate));
}

static void
rb_import_errors_source_init (RBImportErrorsSource *source)
{
	gint size;
	GdkPixbuf *pixbuf;

	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source, RB_TYPE_IMPORT_ERRORS_SOURCE, RBImportErrorsSourcePrivate);
	
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   GTK_STOCK_DIALOG_ERROR,
					   size,
					   0, NULL);
	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}
}

static GObject *
rb_import_errors_source_constructor (GType type, guint n_construct_properties,
				     GObjectConstructParam *construct_properties)
{
	GObject *shell_player;
	RBImportErrorsSource *source;
	RBImportErrorsSourceClass *klass;
	RBShell *shell;
	GPtrArray *query;

	klass = RB_IMPORT_ERRORS_SOURCE_CLASS (g_type_class_peek (RB_TYPE_IMPORT_ERRORS_SOURCE));

	source = RB_IMPORT_ERRORS_SOURCE (G_OBJECT_CLASS (rb_import_errors_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_object_get (G_OBJECT (source), "shell", &shell, NULL);
	g_object_get (G_OBJECT (shell), "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (G_OBJECT (shell));
	
	/* construct real query */
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      	RHYTHMDB_PROP_TYPE,
					RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR,
				      RHYTHMDB_QUERY_END);
	source->priv->model = rhythmdb_query_model_new (source->priv->db, query,
							(GCompareDataFunc) rhythmdb_query_model_string_sort_func,
							RHYTHMDB_PROP_LOCATION, FALSE);
	_rb_source_hide_when_empty (RB_SOURCE (source), source->priv->model);

	g_ptr_array_free (query, TRUE);

	/* set up entry view */
	source->priv->view = rb_entry_view_new (source->priv->db, shell_player, 
						NULL, FALSE, FALSE);

	rb_entry_view_set_model (source->priv->view, source->priv->model);

	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_LOCATION, TRUE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_ERROR, TRUE);
	
	gtk_container_add (GTK_CONTAINER (source), GTK_WIDGET (source->priv->view));
		
	gtk_widget_show_all (GTK_WIDGET (source));

	/* use a fake model for the source's query model property, so we can't try to play
	 * any of the hidden entries.
	 */
	g_object_set (G_OBJECT (source), 
		      "query-model", rhythmdb_query_model_new_empty (source->priv->db), 
		      NULL);

	return G_OBJECT (source);
}

static void
rb_import_errors_source_dispose (GObject *object)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (object);

	if (source->priv->db) {
		g_object_unref (G_OBJECT (source->priv->db));
		source->priv->db = NULL;
	}
	if (source->priv->model) {
		g_object_unref (G_OBJECT (source->priv->model));
		source->priv->model = NULL;
	}

	G_OBJECT_CLASS (rb_import_errors_source_parent_class)->dispose (object);
}


static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (asource);
	return source->priv->view;
}

RBSource *
rb_import_errors_source_new (RBShell *shell,
			     RBLibrarySource *library)
{
	RBSource *source;
	RhythmDBEntryType entry_type;

	g_object_get (G_OBJECT (library), "entry-type", &entry_type, NULL);
	source = RB_SOURCE (g_object_new (RB_TYPE_IMPORT_ERRORS_SOURCE,
					  "name", _("Import Errors"),
					  "shell", shell,
					  "visibility", FALSE,
					  NULL));
	return source;
}

static void
impl_delete (RBSource *asource)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->view);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_delete (source->priv->db, tem->data);
		rhythmdb_commit (source->priv->db);
	}
	g_list_free (sel);
}

static char *
impl_get_status (RBSource *asource)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (asource);
	gint count;
	char *ret;
	
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->model), NULL);
	ret = g_strdup_printf (ngettext ("%d import errors", "%d import errors", count),
			       count);
	return ret;
}

