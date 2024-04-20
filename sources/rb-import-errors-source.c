/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2006-2010  Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>
#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-entry-view.h"
#include "rb-import-errors-source.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-missing-plugins.h"
#include "rb-builder-helpers.h"

static void rb_import_errors_source_class_init (RBImportErrorsSourceClass *klass);
static void rb_import_errors_source_init (RBImportErrorsSource *source);
static void rb_import_errors_source_constructed (GObject *object);
static void rb_import_errors_source_dispose (GObject *object);

static void impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);
static void impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec);

static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_delete_selected (RBSource *source);
static void impl_get_status (RBDisplayPage *page, char **text, gboolean *busy);

static void rb_import_errors_source_songs_show_popup_cb (RBEntryView *view,
							 gboolean over_entry,
							 RBImportErrorsSource *source);
static void infobar_response_cb (GtkInfoBar *bar, gint response, RBImportErrorsSource *source);

static void missing_plugin_row_inserted_cb (GtkTreeModel *model,
					    GtkTreePath *path,
					    GtkTreeIter *iter,
					    RBImportErrorsSource *source);
static void missing_plugin_row_deleted_cb (GtkTreeModel *model,
					   GtkTreePath *path,
					   RBImportErrorsSource *source);

enum {
	PROP_0,
	PROP_NORMAL_ENTRY_TYPE,
	PROP_IGNORE_ENTRY_TYPE
};

struct _RBImportErrorsSourcePrivate
{
	RhythmDB *db;
	RBEntryView *view;

	RhythmDBQueryModel *missing_plugin_model;
	GtkWidget *infobar;

	RhythmDBEntryType *normal_entry_type;
	RhythmDBEntryType *ignore_entry_type;

	GMenuModel *popup;
};

G_DEFINE_TYPE (RBImportErrorsSource, rb_import_errors_source, RB_TYPE_SOURCE);

/**
 * SECTION:rbimporterrorssource
 * @short_description: source for displaying import errors
 *
 * This source is used to display the names of files that could not
 * be imported into the library, along with any error messages from
 * the import process.  When there are no import errors to display,
 * the source is hidden.
 *
 * The source allows the user to delete the import error entries,
 * and to move the files to the trash.
 *
 * When a file import fails, a #RhythmDBEntry is created with a
 * specific entry type for import errors.  This source uses a query
 * model that matches all such import error entries.
 *
 * To keep import errors from removable devices separate from those
 * from the main library, multiple import error sources can be created,
 * with separate entry types.  The generic audio player plugin, for
 * example, creates an import error source for each device and inserts
 * it into the source list as a child of the main source for the device.
 */

static void
rb_import_errors_source_class_init (RBImportErrorsSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_import_errors_source_dispose;
	object_class->constructed = rb_import_errors_source_constructed;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;

	page_class->get_status = impl_get_status;

	source_class->get_entry_view = impl_get_entry_view;
	source_class->can_rename = (RBSourceFeatureFunc) rb_false_function;

	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;

	source_class->delete_selected = impl_delete_selected;

	source_class->try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;

	g_object_class_install_property (object_class,
					 PROP_NORMAL_ENTRY_TYPE,
					 g_param_spec_object ("normal-entry-type",
							      "Normal entry type",
							      "Entry type for successfully imported entries of this type",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IGNORE_ENTRY_TYPE,
					 g_param_spec_object ("ignore-entry-type",
							      "Ignore entry type",
							      "Entry type for entries of this type to be ignored",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBImportErrorsSourcePrivate));
}

static void
rb_import_errors_source_init (RBImportErrorsSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source, RB_TYPE_IMPORT_ERRORS_SOURCE, RBImportErrorsSourcePrivate);
}

static void
rb_import_errors_source_constructed (GObject *object)
{
	GObject *shell_player;
	RBImportErrorsSource *source;
	RBShell *shell;
	GPtrArray *query;
	RhythmDBQueryModel *model;
	RhythmDBEntryType *entry_type;
	GtkWidget *box;
	GtkWidget *label;

	RB_CHAIN_GOBJECT_METHOD (rb_import_errors_source_parent_class, constructed, object);

	source = RB_IMPORT_ERRORS_SOURCE (object);

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);
	g_object_get (shell,
		      "db", &source->priv->db,
		      "shell-player", &shell_player,
		      NULL);
	g_object_unref (shell);

	/* construct real query */
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      	RHYTHMDB_PROP_TYPE,
					entry_type,
				      RHYTHMDB_QUERY_END);

	model = rhythmdb_query_model_new (source->priv->db, query,
					  (GCompareDataFunc) rhythmdb_query_model_string_sort_func,
					  GUINT_TO_POINTER (RHYTHMDB_PROP_LOCATION), NULL, FALSE);
	rhythmdb_query_free (query);

	/* set up entry view */
	source->priv->view = rb_entry_view_new (source->priv->db, shell_player,
						FALSE, FALSE);
	g_object_unref (shell_player);

	rb_entry_view_set_model (source->priv->view, model);

	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_LOCATION, TRUE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_ERROR, TRUE);

	g_signal_connect_object (source->priv->view, "show_popup",
				 G_CALLBACK (rb_import_errors_source_songs_show_popup_cb), source, 0);

	g_object_set (source, "query-model", model, NULL);
	g_object_unref (model);

	/* set up query model for tracking missing plugin information */
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				        RHYTHMDB_PROP_TYPE,
					entry_type,
				      RHYTHMDB_QUERY_PROP_NOT_EQUAL,
				        RHYTHMDB_PROP_COMMENT,
					"",
				      RHYTHMDB_QUERY_END);

	source->priv->missing_plugin_model = rhythmdb_query_model_new_empty (source->priv->db);
	rhythmdb_do_full_query_async_parsed (source->priv->db,
					     RHYTHMDB_QUERY_RESULTS (source->priv->missing_plugin_model),
					     query);
	rhythmdb_query_free (query);

	/* set up info bar for triggering codec installation */
	source->priv->infobar = gtk_info_bar_new_with_buttons (_("Install Additional Software"), GTK_RESPONSE_OK, NULL);
	g_signal_connect_object (source->priv->infobar,
				 "response",
				 G_CALLBACK (infobar_response_cb),
				 source, 0);

	label = gtk_label_new (_("Additional software is required to play some of these files."));
	gtk_label_set_line_wrap (GTK_LABEL (label), TRUE);
	gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (source->priv->infobar))),
			   label);

	g_object_unref (entry_type);

	box = gtk_box_new (GTK_ORIENTATION_VERTICAL, 6);
	gtk_box_pack_start (GTK_BOX (box), GTK_WIDGET (source->priv->view), TRUE, TRUE, 0);
	gtk_box_pack_start (GTK_BOX (box), source->priv->infobar, FALSE, FALSE, 0);

	gtk_container_add (GTK_CONTAINER (source), box);
	gtk_widget_show_all (GTK_WIDGET (source));
	gtk_widget_hide (source->priv->infobar);

	/* show the info bar when there are missing plugin entries */
	g_signal_connect_object (source->priv->missing_plugin_model,
				 "row-inserted",
				 G_CALLBACK (missing_plugin_row_inserted_cb),
				 source, 0);
	g_signal_connect_object (source->priv->missing_plugin_model,
				 "row-deleted",
				 G_CALLBACK (missing_plugin_row_deleted_cb),
				 source, 0);

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "dialog-error-symbolic");
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (object);
	switch (prop_id) {
	case PROP_NORMAL_ENTRY_TYPE:
		g_value_set_object (value, source->priv->normal_entry_type);
		break;
	case PROP_IGNORE_ENTRY_TYPE:
		g_value_set_object (value, source->priv->ignore_entry_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (object);
	switch (prop_id) {
	case PROP_NORMAL_ENTRY_TYPE:
		source->priv->normal_entry_type = g_value_get_object (value);
		break;
	case PROP_IGNORE_ENTRY_TYPE:
		source->priv->ignore_entry_type = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_import_errors_source_dispose (GObject *object)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (object);

	if (source->priv->db) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}
	if (source->priv->missing_plugin_model) {
		g_object_unref (source->priv->missing_plugin_model);
		source->priv->missing_plugin_model = NULL;
	}

	G_OBJECT_CLASS (rb_import_errors_source_parent_class)->dispose (object);
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (asource);
	return source->priv->view;
}

/**
 * rb_import_errors_source_new:
 * @shell: the #RBShell instance
 * @entry_type: the entry type to display in the source
 * @normal_entry_type: entry type for successfully imported entries of this type
 * @ignore_entry_type: entry type for entries of this type to be ignored
 *
 * Creates a new source for displaying import errors of the
 * specified type.
 *
 * Return value: a new import error source
 */
RBSource *
rb_import_errors_source_new (RBShell *shell,
			     RhythmDBEntryType *entry_type,
			     RhythmDBEntryType *normal_entry_type,
			     RhythmDBEntryType *ignore_entry_type)
{
	RBSource *source;

	source = RB_SOURCE (g_object_new (RB_TYPE_IMPORT_ERRORS_SOURCE,
					  "name", _("Import Errors"),
					  "shell", shell,
					  "visibility", FALSE,
					  "hidden-when-empty", TRUE,
					  "entry-type", entry_type,
					  "normal-entry-type", normal_entry_type,
					  "ignore-entry-type", ignore_entry_type,
					  NULL));
	return source;
}

static void
impl_delete_selected (RBSource *asource)
{
	RBImportErrorsSource *source = RB_IMPORT_ERRORS_SOURCE (asource);
	GList *sel, *tem;

	sel = rb_entry_view_get_selected_entries (source->priv->view);
	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_delete (source->priv->db, tem->data);
		rhythmdb_commit (source->priv->db);
	}

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
impl_get_status (RBDisplayPage *page, char **text, gboolean *busy)
{
	RhythmDBQueryModel *model;
	gint count;

	g_object_get (page, "query-model", &model, NULL);
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (model);

	*text = g_strdup_printf (ngettext ("%d import error", "%d import errors", count),
				 count);
}

static void
rb_import_errors_source_songs_show_popup_cb (RBEntryView *view,
					     gboolean over_entry,
					     RBImportErrorsSource *source)
{
	GtkWidget *menu;
	GtkBuilder *builder;

	if (over_entry == FALSE)
		return;

	if (source->priv->popup == NULL) {
		builder = rb_builder_load ("import-errors-popup.ui", NULL);
		source->priv->popup = G_MENU_MODEL (gtk_builder_get_object (builder, "import-errors-popup"));
		g_object_ref (source->priv->popup);
		g_object_unref (builder);
	}

	menu = gtk_menu_new_from_model (source->priv->popup);
	gtk_menu_attach_to_widget (GTK_MENU (menu), GTK_WIDGET (source), NULL);
	gtk_menu_popup (GTK_MENU (menu),
			NULL,
			NULL,
			NULL,
			NULL,
			3,
			gtk_get_current_event_time ());
}

static void
missing_plugin_row_inserted_cb (GtkTreeModel *model,
				GtkTreePath *path,
				GtkTreeIter *iter,
				RBImportErrorsSource *source)
{
	gtk_widget_show (source->priv->infobar);
}

static void
missing_plugin_row_deleted_cb (GtkTreeModel *model,
			       GtkTreePath *path,
			       RBImportErrorsSource *source)
{
	/* row hasn't been deleted from the model yet, so the count
	 * still includes it.
	 */
	if (gtk_tree_model_iter_n_children (model, NULL) == 1) {
		gtk_widget_hide (source->priv->infobar);
	}
}

static void
missing_plugins_retry_cb (gpointer instance, gboolean installed, RBImportErrorsSource *source)
{
	GtkTreeIter iter;
	RhythmDBEntryType *error_entry_type;

	gtk_info_bar_set_response_sensitive (GTK_INFO_BAR (source->priv->infobar),
					     GTK_RESPONSE_OK,
					     TRUE);

	if (installed == FALSE) {
		rb_debug ("installer failed, not retrying imports");
		return;
	}

	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->missing_plugin_model), &iter) == FALSE) {
		return;
	}

	g_object_get (source, "entry-type", &error_entry_type, NULL);
	do {
		RhythmDBEntry *entry;

		entry = rhythmdb_query_model_iter_to_entry (source->priv->missing_plugin_model, &iter);
		rhythmdb_add_uri_with_types (source->priv->db,
					     rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
					     source->priv->normal_entry_type,
					     source->priv->ignore_entry_type,
					     error_entry_type);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->missing_plugin_model), &iter));

	g_object_unref (error_entry_type);
}

static void
missing_plugins_retry_cleanup (RBImportErrorsSource *source)
{
	g_object_unref (source);
}

static void
infobar_response_cb (GtkInfoBar *infobar, gint response, RBImportErrorsSource *source)
{
	char **details = NULL;
	GtkTreeIter iter;
	GClosure *closure;
	int i;

	/* gather plugin installer detail strings */
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (source->priv->missing_plugin_model), &iter) == FALSE) {
		return;
	}

	i = 0;
	do {
		RhythmDBEntry *entry;
		char **bits;
		int j;

		entry = rhythmdb_query_model_iter_to_entry (source->priv->missing_plugin_model, &iter);
		bits = g_strsplit (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_COMMENT), "\n", 0);

		for (j = 0; bits[j] != NULL; j++) {
			if (rb_str_in_strv (bits[j], (const char **)details) == FALSE) {
				details = g_realloc (details, sizeof (char *) * i+2);
				details[i++] = g_strdup (bits[j]);
				details[i] = NULL;
			}
		}

		g_strfreev (bits);
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (source->priv->missing_plugin_model), &iter));

	/* run the installer */
	closure = g_cclosure_new ((GCallback) missing_plugins_retry_cb,
				  g_object_ref (source),
				  (GClosureNotify) missing_plugins_retry_cleanup);
	g_closure_set_marshal (closure, g_cclosure_marshal_VOID__BOOLEAN);
	if (rb_missing_plugins_install ((const char **)details, TRUE, closure) == TRUE) {
		/* disable the button while the installer is running */
		gtk_info_bar_set_response_sensitive (infobar, response, FALSE);
	}
	g_closure_sink (closure);

	g_strfreev (details);
}
