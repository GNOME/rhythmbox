/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2006  Jonathan Matthew <jonathan@kaolin.wh9.net>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-entry-view.h"
#include "rb-missing-files-source.h"
#include "rb-song-info.h"
#include "rb-util.h"
#include "rb-debug.h"
#include "rb-builder-helpers.h"

/**
 * SECTION:rbmissingfilessource
 * @short_description: source displaying files missing from the library
 *
 * This source displays files that rhythmbox cannot find at the expected
 * locations.  On startup, it does a file access check for every file
 * in the library, hiding those that fail.  This source sets up a
 * query model that matches only hidden entries.  It displays the file
 * location and the last time the file was successfully accessed.
 *
 * The source only displayed in the source list when there are hidden
 * entries to show.
 */

static void rb_missing_files_source_class_init (RBMissingFilesSourceClass *klass);
static void rb_missing_files_source_init (RBMissingFilesSource *source);
static void rb_missing_files_source_constructed (GObject *object);
static void rb_missing_files_source_dispose (GObject *object);
static void rb_missing_files_source_set_property (GObject *object,
						  guint prop_id,
						  const GValue *value,
						  GParamSpec *pspec);
static void rb_missing_files_source_get_property (GObject *object,
						  guint prop_id,
						  GValue *value,
						  GParamSpec *pspec);

static RBEntryView *impl_get_entry_view (RBSource *source);
static void impl_song_properties (RBSource *source);
static void impl_delete_selected (RBSource *source);
static void impl_get_status (RBDisplayPage *page, char **text, gboolean *busy);

static void rb_missing_files_source_songs_show_popup_cb (RBEntryView *view,
							 gboolean over_entry,
							 RBMissingFilesSource *source);
static void rb_missing_files_source_songs_sort_order_changed_cb (GObject *object,
								 GParamSpec *pspec,
								 RBMissingFilesSource *source);

struct RBMissingFilesSourcePrivate
{
	RhythmDB *db;
	RBEntryView *view;
	GMenuModel *popup;
};

G_DEFINE_TYPE (RBMissingFilesSource, rb_missing_files_source, RB_TYPE_SOURCE);

static void
rb_missing_files_source_class_init (RBMissingFilesSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_missing_files_source_dispose;
	object_class->constructed = rb_missing_files_source_constructed;

	object_class->set_property = rb_missing_files_source_set_property;
	object_class->get_property = rb_missing_files_source_get_property;

	page_class->get_status = impl_get_status;

	source_class->get_entry_view = impl_get_entry_view;
	source_class->can_rename = (RBSourceFeatureFunc) rb_false_function;

	source_class->can_cut = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_delete = (RBSourceFeatureFunc) rb_true_function;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_copy = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;

	source_class->delete_selected = impl_delete_selected;

	source_class->song_properties = impl_song_properties;
	source_class->try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_pause = (RBSourceFeatureFunc) rb_false_function;

	g_type_class_add_private (klass, sizeof (RBMissingFilesSourcePrivate));
}

static void
rb_missing_files_source_init (RBMissingFilesSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source, RB_TYPE_MISSING_FILES_SOURCE, RBMissingFilesSourcePrivate);
}

static void
rb_missing_files_source_constructed (GObject *object)
{
	GObject *shell_player;
	RBMissingFilesSource *source;
	RBShell *shell;
	GPtrArray *query;
	RhythmDBQueryModel *model;
	RhythmDBEntryType *entry_type;

	RB_CHAIN_GOBJECT_METHOD (rb_missing_files_source_parent_class, constructed, object);
	source = RB_MISSING_FILES_SOURCE (object);

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
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      	RHYTHMDB_PROP_HIDDEN,
					TRUE,
				      RHYTHMDB_QUERY_END);
	g_object_unref (entry_type);

	model = rhythmdb_query_model_new (source->priv->db, query,
					  NULL, NULL, NULL, FALSE);

	rhythmdb_query_free (query);

	g_object_set (model, "show-hidden", TRUE, NULL);

	/* set up entry view */
	source->priv->view = rb_entry_view_new (source->priv->db, shell_player,
						FALSE, FALSE);
	g_object_unref (shell_player);

	rb_entry_view_set_model (source->priv->view, model);

	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_TRACK_NUMBER, FALSE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_TITLE, TRUE);
/*	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_GENRE, FALSE); */
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_ARTIST, FALSE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_ALBUM, FALSE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_LOCATION, TRUE);
	rb_entry_view_append_column (source->priv->view, RB_ENTRY_VIEW_COL_LAST_SEEN, TRUE);

	rb_entry_view_set_columns_clickable (source->priv->view, TRUE);

	gtk_container_add (GTK_CONTAINER (source), GTK_WIDGET (source->priv->view));
	g_signal_connect_object (source->priv->view, "show_popup",
				 G_CALLBACK (rb_missing_files_source_songs_show_popup_cb), source, 0);
	g_signal_connect_object (source->priv->view, "notify::sort-order",
				 G_CALLBACK (rb_missing_files_source_songs_sort_order_changed_cb), source, 0);

	gtk_widget_show_all (GTK_WIDGET (source));

	g_object_set (source, "query-model", model, NULL);
	g_object_unref (model);

	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (source), "dialog-warning-symbolic");
}

static void
rb_missing_files_source_dispose (GObject *object)
{
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (object);

	if (source->priv->db != NULL) {
		g_object_unref (source->priv->db);
		source->priv->db = NULL;
	}

	G_OBJECT_CLASS (rb_missing_files_source_parent_class)->dispose (object);
}

static RBEntryView *
impl_get_entry_view (RBSource *asource)
{
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (asource);
	return source->priv->view;
}

static void
rb_missing_files_source_set_property (GObject *object,
				      guint prop_id,
				      const GValue *value,
				      GParamSpec *pspec)
{
	/*RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (object);*/

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_missing_files_source_get_property (GObject *object,
				      guint prop_id,
				      GValue *value,
				      GParamSpec *pspec)
{
	/*RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (object);*/

	switch (prop_id)
	{
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_missing_files_source_new:
 * @shell: the #RBShell instance
 * @library: the #RBLibrarySource instance
 *
 * Creates the missing files source.  It extracts the
 * entry type from the library source instance, so it
 * currently only works for files in the library, but
 * it would be trivial to make it use any source type
 * that did file access checks for its contents.
 * 
 * Return value: the #RBMissingFilesSource
 */
RBSource *
rb_missing_files_source_new (RBShell *shell,
			     RBLibrarySource *library)
{
	RBSource *source;
	RhythmDBEntryType *entry_type;

	g_object_get (library, "entry-type", &entry_type, NULL);
	source = RB_SOURCE (g_object_new (RB_TYPE_MISSING_FILES_SOURCE,
					  "name", _("Missing Files"),
					  "entry-type", entry_type,
					  "shell", shell,
					  "visibility", FALSE,
					  "hidden-when-empty", TRUE,
					  NULL));
	g_object_unref (entry_type);
	return source;
}

static void
rb_missing_files_source_songs_show_popup_cb (RBEntryView *view,
					     gboolean over_entry,
					     RBMissingFilesSource *source)
{
	GtkWidget *menu;
	GtkBuilder *builder;

	if (over_entry == FALSE)
		return;

	if (source->priv->popup == NULL) {
		builder = rb_builder_load ("missing-files-popup.ui", NULL);
		source->priv->popup = G_MENU_MODEL (gtk_builder_get_object (builder, "missing-files-popup"));
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
impl_song_properties (RBSource *asource)
{
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (asource);
	GtkWidget *song_info = NULL;

	g_return_if_fail (source->priv->view != NULL);

	song_info = rb_song_info_new (asource, NULL);
	if (song_info)
		gtk_widget_show_all (song_info);
	else
		rb_debug ("failed to create dialog, or no selection!");
}

static void
impl_delete_selected (RBSource *asource)
{
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (asource);
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
rb_missing_files_source_songs_sort_order_changed_cb (GObject *object,
						     GParamSpec *pspec,
						     RBMissingFilesSource *source)
{
	rb_entry_view_resort_model (RB_ENTRY_VIEW (object));
}

static void
impl_get_status (RBDisplayPage *page, char **text, gboolean *busy)
{
	RhythmDBQueryModel *model;
	gint count;

	g_object_get (page, "query-model", &model, NULL);
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (model);

	*text = g_strdup_printf (ngettext ("%d missing file", "%d missing files", count),
				 count);
}
