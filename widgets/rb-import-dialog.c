/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2012 Jonathan Matthew <jonathan@d14n.org>
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

#include "config.h"

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-import-dialog.h"
#include "rb-shell-player.h"
#include "rb-builder-helpers.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-text-helpers.h"
#include "rb-cut-and-paste-code.h"
#include "rb-search-entry.h"
#include "rhythmdb-entry-type.h"
#include "rb-device-source.h"
#include "rb-file-helpers.h"
#include "rb-task-list.h"
#include "rhythmdb-import-job.h"

/* normal entries */
typedef struct _RhythmDBEntryType RBImportDialogEntryType;
typedef struct _RhythmDBEntryTypeClass RBImportDialogEntryTypeClass;

static void rb_import_dialog_entry_type_class_init (RBImportDialogEntryTypeClass *klass);
static void rb_import_dialog_entry_type_init (RBImportDialogEntryType *etype);
GType rb_import_dialog_entry_type_get_type (void);

G_DEFINE_TYPE (RBImportDialogEntryType, rb_import_dialog_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

/* ignored files */
typedef struct _RhythmDBEntryType RBImportDialogIgnoreType;
typedef struct _RhythmDBEntryTypeClass RBImportDialogIgnoreTypeClass;

static void rb_import_dialog_ignore_type_class_init (RBImportDialogIgnoreTypeClass *klass);
static void rb_import_dialog_ignore_type_init (RBImportDialogIgnoreType *etype);
GType rb_import_dialog_ignore_type_get_type (void);

G_DEFINE_TYPE (RBImportDialogIgnoreType, rb_import_dialog_ignore_type, RHYTHMDB_TYPE_ENTRY_TYPE);


static void rb_import_dialog_class_init (RBImportDialogClass *klass);
static void rb_import_dialog_init (RBImportDialog *dialog);

enum {
	PROP_0,
	PROP_SHELL,
};

enum {
	CLOSE,
	CLOSED,
	LAST_SIGNAL
};

struct RBImportDialogPrivate
{
	RhythmDB *db;
	RBShell *shell;
	RBShellPlayer *shell_player;

	RhythmDBQueryModel *query_model;
	RBEntryView *entry_view;

	GtkWidget *info_bar;
	GtkWidget *info_bar_container;
	GtkWidget *file_chooser;
	GtkWidget *copy_check;
	GtkWidget *import_button;

	RhythmDBEntryType *entry_type;
	RhythmDBEntryType *ignore_type;
	RhythmDBImportJob *import_job;
	int entry_count;

	GList *add_entry_list;
	guint add_entries_id;
	guint added_entries_id;
	guint update_status_id;

	char *current_uri;
};

static guint signals[LAST_SIGNAL] = {0,};

G_DEFINE_TYPE (RBImportDialog, rb_import_dialog, GTK_TYPE_GRID);

static void
rb_import_dialog_entry_type_class_init (RBImportDialogEntryTypeClass *klass)
{
}

static void
rb_import_dialog_entry_type_init (RBImportDialogEntryType *etype)
{
}

static void
rb_import_dialog_ignore_type_class_init (RBImportDialogIgnoreTypeClass *klass)
{
}

static void
rb_import_dialog_ignore_type_init (RBImportDialogIgnoreType *etype)
{
}


static void
sort_changed_cb (GObject *object, GParamSpec *pspec, RBImportDialog *dialog)
{
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
hide_import_job (RBImportDialog *dialog)
{
	if (dialog->priv->import_job) {
		RBTaskList *tasklist;
		g_object_get (dialog->priv->shell, "task-list", &tasklist, NULL);
		rb_task_list_remove_task (tasklist, RB_TASK_PROGRESS (dialog->priv->import_job));
		g_object_unref (tasklist);
	}
}

static void
impl_close (RBImportDialog *dialog)
{
	hide_import_job (dialog);

	if (dialog->priv->import_job) {
		rhythmdb_import_job_cancel (dialog->priv->import_job);
	}
	g_signal_emit (dialog, signals[CLOSED], 0);
}

static void
entry_activated_cb (RBEntryView *entry_view, RhythmDBEntry *entry, RBImportDialog *dialog)
{
	rb_debug ("import dialog entry %s activated", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	rb_shell_load_uri (dialog->priv->shell,
			   rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
			   TRUE,
			   NULL);
}

static void
clear_info_bar (RBImportDialog *dialog)
{
	if (dialog->priv->info_bar != NULL) {
		gtk_container_remove (GTK_CONTAINER (dialog->priv->info_bar_container), dialog->priv->info_bar);
		dialog->priv->info_bar = NULL;
	}
}

static gboolean
collect_entries (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, GList **list)
{
	RhythmDBEntry *entry;

	entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (model), iter);
	*list = g_list_prepend (*list, rhythmdb_entry_ref (entry));
	return FALSE;
}

static GList *
get_entries (RBImportDialog *dialog)
{
	/* selection if non-empty, all entries otherwise */
	if (rb_entry_view_have_selection (dialog->priv->entry_view)) {
		return rb_entry_view_get_selected_entries (dialog->priv->entry_view);
	} else {
		GList *l = NULL;

		gtk_tree_model_foreach (GTK_TREE_MODEL (dialog->priv->query_model),
					(GtkTreeModelForeachFunc) collect_entries,
					&l);
		return g_list_reverse (l);
	}
}

static gboolean
add_entries_done (RBImportDialog *dialog)
{
	/* if we added all the tracks, close the dialog */
	if (dialog->priv->entry_count == 0) {
		hide_import_job (dialog);
		g_signal_emit (dialog, signals[CLOSED], 0);
	}

	dialog->priv->added_entries_id = 0;
	return FALSE;
}

static gboolean
add_entries (RBImportDialog *dialog)
{
	int i;
	GValue new_type = {0,};

	g_value_init (&new_type, G_TYPE_OBJECT);
	g_value_set_object (&new_type, RHYTHMDB_ENTRY_TYPE_SONG);

	for (i = 0; i < 1000; i++) {
		RhythmDBEntry *entry;

		entry = dialog->priv->add_entry_list->data;
		dialog->priv->add_entry_list = g_list_delete_link (dialog->priv->add_entry_list, dialog->priv->add_entry_list);

		rhythmdb_entry_set (dialog->priv->db, entry, RHYTHMDB_PROP_TYPE, &new_type);
		rhythmdb_entry_unref (entry);

		if (dialog->priv->add_entry_list == NULL)
			break;
	}
	
	rhythmdb_commit (dialog->priv->db);

	if (dialog->priv->add_entry_list == NULL) {
		dialog->priv->add_entries_id = 0;

		dialog->priv->added_entries_id = g_idle_add ((GSourceFunc) add_entries_done, dialog);

		return FALSE;
	} else {
		return TRUE;
	}
}

static void
copy_track_done_cb (RBTrackTransferBatch *batch,
		    RhythmDBEntry *entry,
		    const char *dest,
		    guint64 dest_size,
		    const char *mediatype,
		    GError *error,
		    RBImportDialog *dialog)
{
	rhythmdb_entry_delete (dialog->priv->db, entry);
	rhythmdb_commit (dialog->priv->db);
}

static void
copy_complete_cb (RBTrackTransferBatch *batch, RBImportDialog *dialog)
{
	/* if we copied everything, close the dialog */
	if (dialog->priv->entry_count == 0) {
		hide_import_job (dialog);
		g_signal_emit (dialog, signals[CLOSED], 0);
	}
}

static void
import_clicked_cb (GtkButton *button, RBImportDialog *dialog)
{
	GList *entries;
	RBSource *library_source;
	RBTrackTransferBatch *batch;

	entries = get_entries (dialog);
	if (entries == NULL)
		return;

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dialog->priv->copy_check)) == FALSE) {
		dialog->priv->add_entry_list = g_list_concat (dialog->priv->add_entry_list, entries);

		if (dialog->priv->add_entries_id == 0) {
			dialog->priv->add_entries_id = g_idle_add ((GSourceFunc) add_entries, dialog);
		}
	} else {
		g_object_get (dialog->priv->shell, "library-source", &library_source, NULL);

		batch = rb_source_paste (library_source, entries);
		g_list_free_full (entries, (GDestroyNotify) rhythmdb_entry_unref);
		g_object_unref (library_source);

		/* delete source entries as they finish being copied */
		g_signal_connect (batch, "track-done", G_CALLBACK (copy_track_done_cb), dialog);
		g_signal_connect (batch, "complete", G_CALLBACK (copy_complete_cb), dialog);
	}

}

static void
close_clicked_cb (GtkButton *button, RBImportDialog *dialog)
{
	hide_import_job (dialog);
	if (dialog->priv->import_job) {
		rhythmdb_import_job_cancel (dialog->priv->import_job);
	}
	g_signal_emit (dialog, signals[CLOSED], 0);
}

static void
import_complete_cb (RhythmDBImportJob *job, int total, RBImportDialog *dialog)
{
	g_object_unref (job);
	dialog->priv->import_job = NULL;
}

static void
start_scanning (RBImportDialog *dialog)
{
	RBTaskList *tasklist;

	rhythmdb_entry_delete_by_type (dialog->priv->db, dialog->priv->entry_type);
	rhythmdb_entry_delete_by_type (dialog->priv->db, dialog->priv->ignore_type);
	rhythmdb_commit (dialog->priv->db);

	rb_debug ("starting %s", dialog->priv->current_uri);
	dialog->priv->import_job = rhythmdb_import_job_new (dialog->priv->db,
							    dialog->priv->entry_type,
							    dialog->priv->ignore_type,
							    dialog->priv->ignore_type);
	g_object_set (dialog->priv->import_job, "task-label", _("Examining files"), NULL);
	g_signal_connect (dialog->priv->import_job, "complete", G_CALLBACK (import_complete_cb), dialog);
	rhythmdb_import_job_add_uri (dialog->priv->import_job, dialog->priv->current_uri);
	rhythmdb_import_job_start (dialog->priv->import_job);

	g_object_get (dialog->priv->shell, "task-list", &tasklist, NULL);
	rb_task_list_add_task (tasklist, RB_TASK_PROGRESS (dialog->priv->import_job));
	g_object_unref (tasklist);
}

static void
start_deferred_scan (RhythmDBImportJob *job, int total, RBImportDialog *dialog)
{
	rb_debug ("previous import job finished");
	start_scanning (dialog);
}

static void
device_info_bar_response_cb (GtkInfoBar *bar, gint response, RBImportDialog *dialog)
{
	RBSource *source;
	const char *uri;

	hide_import_job (dialog);
	g_signal_emit (dialog, signals[CLOSED], 0);
	uri = gtk_file_chooser_get_uri (GTK_FILE_CHOOSER (dialog->priv->file_chooser));
	source = rb_shell_guess_source_for_uri (dialog->priv->shell, uri);
	rb_shell_activate_source (dialog->priv->shell, source, FALSE, NULL);
}


static void
current_folder_changed_cb (GtkFileChooser *chooser, RBImportDialog *dialog)
{
	GSettings *settings;
	RBSource *source;
	GtkWidget *label;
	const char *uri;
	char **locations;
	int i;
	
	uri = gtk_file_chooser_get_uri (chooser);
	if (g_strcmp0 (uri, dialog->priv->current_uri) == 0)
		return;
	g_free (dialog->priv->current_uri);
	dialog->priv->current_uri = g_strdup (uri);

	if (dialog->priv->import_job != NULL) {
		rhythmdb_import_job_cancel (dialog->priv->import_job);
	}

	clear_info_bar (dialog);

	source = rb_shell_guess_source_for_uri (dialog->priv->shell, uri);
	if (source != NULL) {
		if (RB_IS_DEVICE_SOURCE (source)) {
			char *msg;
			char *name;
			GtkWidget *content;

			rhythmdb_entry_delete_by_type (dialog->priv->db, dialog->priv->entry_type);
			rhythmdb_entry_delete_by_type (dialog->priv->db, dialog->priv->ignore_type);
			rhythmdb_commit (dialog->priv->db);

			dialog->priv->info_bar = gtk_info_bar_new ();
			g_object_set (dialog->priv->info_bar, "hexpand", TRUE, NULL);

			g_object_get (source, "name", &name, NULL);

			/* this isn't a terribly helpful message. */
			msg = g_strdup_printf (_("The location you have selected is on the device %s."), name);
			label = gtk_label_new (msg);
			g_free (msg);
			content = gtk_info_bar_get_content_area (GTK_INFO_BAR (dialog->priv->info_bar));
			gtk_container_add (GTK_CONTAINER (content), label);

			msg = g_strdup_printf (_("Show %s"), name);
			gtk_info_bar_add_button (GTK_INFO_BAR (dialog->priv->info_bar), msg, GTK_RESPONSE_ACCEPT);
			g_free (msg);

			g_signal_connect (dialog->priv->info_bar, "response", G_CALLBACK (device_info_bar_response_cb), dialog);

			gtk_widget_show_all (dialog->priv->info_bar);
			gtk_container_add (GTK_CONTAINER (dialog->priv->info_bar_container), dialog->priv->info_bar);
			return;
		}
	}

	/* disable copy if the selected location is already inside the library */
	settings = g_settings_new ("org.gnome.rhythmbox.rhythmdb");
	locations = g_settings_get_strv (settings, "locations");
	gtk_widget_set_sensitive (dialog->priv->copy_check, TRUE);
	for (i = 0; locations[i] != NULL; i++) {
		if ((g_strcmp0 (uri, locations[i]) == 0) || rb_uri_is_descendant (uri, locations[i])) {
			gtk_widget_set_sensitive (dialog->priv->copy_check, FALSE);
			break;
		}
	}
	g_strfreev (locations);
	g_object_unref (settings);

	if (dialog->priv->import_job != NULL) {
		/* wait for the previous job to finish up */
		rb_debug ("need to wait for previous import job to finish");
		g_signal_connect (dialog->priv->import_job, "complete", G_CALLBACK (start_deferred_scan), dialog);
	} else {
		start_scanning (dialog);
	}
}

static gboolean
update_status_idle (RBImportDialog *dialog)
{
	int count;
	const char *fmt;
	char *text;

	if (rb_entry_view_have_selection (dialog->priv->entry_view)) {
		GList *sel;

		sel = rb_entry_view_get_selected_entries (dialog->priv->entry_view);
		count = g_list_length (sel);
		g_list_free_full (sel, (GDestroyNotify) rhythmdb_entry_unref);

		fmt = ngettext ("Import %d selected track", "Import %d selected tracks", count);
	} else {
		count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (dialog->priv->query_model), NULL);
		fmt = ngettext ("Import %d listed track", "Import %d listed tracks", count);
	}
	text = g_strdup_printf (fmt, count);
	gtk_button_set_label (GTK_BUTTON (dialog->priv->import_button), text);
	/* a new child label is created each time button label is set */
	gtk_label_set_attributes (GTK_LABEL (gtk_bin_get_child (GTK_BIN (dialog->priv->import_button))),
				  rb_text_numeric_get_pango_attr_list ());
	g_free (text);

	/* hack to get these strings marked for translation */
	if (0) {
		ngettext ("%d song", "%d songs", 0);
	}
	text = rhythmdb_query_model_compute_status_normal (dialog->priv->query_model, "%d song", "%d songs");
	rb_entry_view_set_status (dialog->priv->entry_view, text, FALSE);
	g_free (text);

	dialog->priv->update_status_id = 0;
	return FALSE;
}

static void
update_status (RBImportDialog *dialog)
{
	if (dialog->priv->update_status_id != 0)
		return;

	dialog->priv->update_status_id = g_idle_add ((GSourceFunc) update_status_idle, dialog);
}


static void
entry_inserted_cb (GtkTreeModel *model, GtkTreePath *path, GtkTreeIter *iter, RBImportDialog *dialog)
{
	if (dialog->priv->entry_count == 0) {
		gtk_widget_set_sensitive (dialog->priv->import_button, TRUE);
	}

	dialog->priv->entry_count++;
	update_status (dialog);
}

static void
entry_deleted_cb (GtkTreeModel *model, RhythmDBEntry *entry, RBImportDialog *dialog)
{
	dialog->priv->entry_count--;
	if (dialog->priv->entry_count == 0) {
		gtk_widget_set_sensitive (dialog->priv->import_button, FALSE);
	}

	update_status (dialog);
}

static void
selection_changed_cb (RBEntryView *view, RBImportDialog *dialog)
{
	update_status (dialog);
}

static void
impl_constructed (GObject *object)
{
	GtkStyleContext *context;
	RBImportDialog *dialog;
	RhythmDBQuery *query;
	GtkBuilder *builder;
	GSettings *settings;
	char **locations;

	RB_CHAIN_GOBJECT_METHOD (rb_import_dialog_parent_class, constructed, object);
	dialog = RB_IMPORT_DIALOG (object);

	g_object_get (dialog->priv->shell,
		      "db", &dialog->priv->db,
		      "shell-player", &dialog->priv->shell_player,
		      NULL);

	/* create entry types */
	dialog->priv->entry_type = g_object_new (rb_import_dialog_entry_type_get_type (),
						 "db", dialog->priv->db,
						 "name", "import-dialog",
						 NULL);
	dialog->priv->ignore_type = g_object_new (rb_import_dialog_ignore_type_get_type (),
						  "db", dialog->priv->db,
						  "name", "import-dialog-ignore",
						  NULL);
	rhythmdb_register_entry_type (dialog->priv->db, dialog->priv->entry_type);
	rhythmdb_register_entry_type (dialog->priv->db, dialog->priv->ignore_type);


	builder = rb_builder_load ("import-dialog.ui", NULL);

	dialog->priv->import_button = GTK_WIDGET (gtk_builder_get_object (builder, "import-button"));
	context = gtk_widget_get_style_context (GTK_WIDGET (dialog->priv->import_button));
	gtk_style_context_add_class (context, GTK_STYLE_CLASS_SUGGESTED_ACTION);
	g_signal_connect_object (dialog->priv->import_button, "clicked", G_CALLBACK (import_clicked_cb), dialog, 0);
	gtk_widget_set_sensitive (dialog->priv->import_button, FALSE);

	dialog->priv->copy_check = GTK_WIDGET (gtk_builder_get_object (builder, "copy-check"));

	g_signal_connect (gtk_builder_get_object (builder, "close-button"),
			  "clicked",
			  G_CALLBACK (close_clicked_cb),
			  dialog);

	dialog->priv->file_chooser = GTK_WIDGET (gtk_builder_get_object (builder, "file-chooser-button"));
	
	/* select the first library location, since the default may be
	 * the user's home dir or / or something that will take forever to scan.
	 */
	settings = g_settings_new ("org.gnome.rhythmbox.rhythmdb");
	locations = g_settings_get_strv (settings, "locations");
	if (locations[0] != NULL) {
		dialog->priv->current_uri = g_strdup (locations[0]);
	} else {
		dialog->priv->current_uri = g_filename_to_uri (rb_music_dir (), NULL, NULL);
	}
	gtk_file_chooser_set_current_folder_uri (GTK_FILE_CHOOSER (dialog->priv->file_chooser),
						 dialog->priv->current_uri);
	g_strfreev (locations);
	g_object_unref (settings);

	g_signal_connect_object (dialog->priv->file_chooser, "selection-changed", G_CALLBACK (current_folder_changed_cb), dialog, 0);

	/* not sure why we have to set this, it should be the default */
	gtk_widget_set_vexpand (gtk_widget_get_parent (dialog->priv->file_chooser), FALSE);

	dialog->priv->info_bar_container = GTK_WIDGET (gtk_builder_get_object (builder, "info-bar-container"));

	/* set up entry view */
	dialog->priv->entry_view = rb_entry_view_new (dialog->priv->db, G_OBJECT (dialog->priv->shell_player), TRUE, FALSE);

	g_signal_connect (dialog->priv->entry_view, "entry-activated", G_CALLBACK (entry_activated_cb), dialog);
	g_signal_connect (dialog->priv->entry_view, "selection-changed", G_CALLBACK (selection_changed_cb), dialog);

	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_GENRE, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_YEAR, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_DURATION, FALSE);
 	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_QUALITY, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_PLAY_COUNT, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_BPM, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_COMMENT, FALSE);
	rb_entry_view_append_column (dialog->priv->entry_view, RB_ENTRY_VIEW_COL_LOCATION, FALSE);

	settings = g_settings_new ("org.gnome.rhythmbox.sources");
	g_settings_bind (settings, "visible-columns", dialog->priv->entry_view, "visible-columns", G_SETTINGS_BIND_DEFAULT);
	g_object_unref (settings);

	g_signal_connect (dialog->priv->entry_view,
			  "notify::sort-order",
			  G_CALLBACK (sort_changed_cb),
			  dialog);
	rb_entry_view_set_sorting_order (dialog->priv->entry_view, "Album", GTK_SORT_ASCENDING);

	gtk_container_add (GTK_CONTAINER (gtk_builder_get_object (builder, "entry-view-container")),
			   GTK_WIDGET (dialog->priv->entry_view));

	dialog->priv->query_model = rhythmdb_query_model_new_empty (dialog->priv->db);
	rb_entry_view_set_model (dialog->priv->entry_view, dialog->priv->query_model);
	query = rhythmdb_query_parse (dialog->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, dialog->priv->entry_type,
				      RHYTHMDB_QUERY_END);
	rhythmdb_do_full_query_async_parsed (dialog->priv->db, RHYTHMDB_QUERY_RESULTS (dialog->priv->query_model), query);
	rhythmdb_query_free (query);

	g_signal_connect (dialog->priv->query_model, "post-entry-delete", G_CALLBACK (entry_deleted_cb), dialog);
	g_signal_connect (dialog->priv->query_model, "row-inserted", G_CALLBACK (entry_inserted_cb), dialog);

	gtk_container_add (GTK_CONTAINER (dialog), GTK_WIDGET (gtk_builder_get_object (builder, "import-dialog")));

	gtk_widget_show_all (GTK_WIDGET (dialog));
	g_object_unref (builder);
}

static void
impl_dispose (GObject *object)
{
	RBImportDialog *dialog = RB_IMPORT_DIALOG (object);

	if (dialog->priv->add_entries_id) {
		g_source_remove (dialog->priv->add_entries_id);
		dialog->priv->add_entries_id = 0;
	}
	if (dialog->priv->added_entries_id) {
		g_source_remove (dialog->priv->added_entries_id);
		dialog->priv->added_entries_id = 0;
	}
	if (dialog->priv->update_status_id) {
		g_source_remove (dialog->priv->update_status_id);
		dialog->priv->update_status_id = 0;
	}

	if (dialog->priv->query_model != NULL) {
		g_object_unref (dialog->priv->query_model);
		dialog->priv->query_model = NULL;
	}
	if (dialog->priv->shell != NULL) {
		g_object_unref (dialog->priv->shell);
		dialog->priv->shell = NULL;
	}
	if (dialog->priv->shell_player != NULL) {
		g_object_unref (dialog->priv->shell_player);
		dialog->priv->shell_player = NULL;
	}
	if (dialog->priv->db != NULL) {
		g_object_unref (dialog->priv->db);
		dialog->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_import_dialog_parent_class)->dispose (object);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBImportDialog *dialog = RB_IMPORT_DIALOG (object);

	switch (prop_id) {
	case PROP_SHELL:
		dialog->priv->shell = g_value_dup_object (value);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBImportDialog *dialog = RB_IMPORT_DIALOG (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, dialog->priv->shell);
		break;
	default:
		g_assert_not_reached ();
		break;
	}
}

static void
rb_import_dialog_init (RBImportDialog *dialog)
{
	dialog->priv = G_TYPE_INSTANCE_GET_PRIVATE (dialog,
						    RB_TYPE_IMPORT_DIALOG,
						    RBImportDialogPrivate);
}

static void
rb_import_dialog_class_init (RBImportDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	klass->close = impl_close;

	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "shell",
							      "RBShell instance",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[CLOSE] = g_signal_new ("close",
				       RB_TYPE_IMPORT_DIALOG,
				       G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
				       G_STRUCT_OFFSET (RBImportDialogClass, close),
				       NULL, NULL,
				       NULL,
				       G_TYPE_NONE,
				       0);
	signals[CLOSED] = g_signal_new ("closed",
					RB_TYPE_IMPORT_DIALOG,
					G_SIGNAL_RUN_LAST,
					G_STRUCT_OFFSET (RBImportDialogClass, closed),
					NULL, NULL,
					NULL,
					G_TYPE_NONE,
					0);

	g_type_class_add_private (object_class, sizeof (RBImportDialogPrivate));

	gtk_binding_entry_add_signal (gtk_binding_set_by_class (klass),
				      GDK_KEY_Escape,
				      0,
				      "close",
				      0);
}

void
rb_import_dialog_reset (RBImportDialog *dialog)
{
	g_free (dialog->priv->current_uri);
	dialog->priv->current_uri = NULL;

	current_folder_changed_cb (GTK_FILE_CHOOSER (dialog->priv->file_chooser), dialog);
}

GtkWidget *
rb_import_dialog_new (RBShell *shell)
{
	return GTK_WIDGET (g_object_new (RB_TYPE_IMPORT_DIALOG,
					 "shell", shell,
					 "orientation", GTK_ORIENTATION_VERTICAL,
					 NULL));
}
