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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

/*
 * This source lists files rhythmbox cannot find, and maybe tries to stop
 * you from trying to play them.
 */

#include "config.h"

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-entry-view.h"
#include "rb-missing-files-source.h"
#include "rb-song-info.h"
#include "rb-util.h"
#include "rb-debug.h"

static void rb_missing_files_source_class_init (RBMissingFilesSourceClass *klass);
static void rb_missing_files_source_init (RBMissingFilesSource *source);
static GObject *rb_missing_files_source_constructor (GType type, guint n_construct_properties,
						     GObjectConstructParam *construct_properties);
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
static void impl_delete (RBSource *source);
static void impl_get_status (RBSource *source, char **text, char **progress_text, float *progress);

static void rb_missing_files_source_songs_show_popup_cb (RBEntryView *view,
							 gboolean over_entry,
							 RBMissingFilesSource *source);
static void rb_missing_files_source_songs_sort_order_changed_cb (RBEntryView *view,
								 RBMissingFilesSource *source);

#define MISSING_FILES_SOURCE_SONGS_POPUP_PATH "/MissingFilesViewPopup"

struct RBMissingFilesSourcePrivate
{
	RhythmDB *db;
	RBEntryView *view;
	RhythmDBEntryType entry_type;
};

enum
{
	PROP_0,
	PROP_ENTRY_TYPE
};

G_DEFINE_TYPE (RBMissingFilesSource, rb_missing_files_source, RB_TYPE_SOURCE);

static void
rb_missing_files_source_class_init (RBMissingFilesSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);

	object_class->dispose = rb_missing_files_source_dispose;
	object_class->constructor = rb_missing_files_source_constructor;

	object_class->set_property = rb_missing_files_source_set_property;
	object_class->get_property = rb_missing_files_source_get_property;

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

	source_class->impl_song_properties = impl_song_properties;
	source_class->impl_try_playlist = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_pause = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_have_url = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_get_status = impl_get_status;

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
							     "Entry type",
							     "Type of the entries which should be displayed by this source",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBMissingFilesSourcePrivate));
}

static void
rb_missing_files_source_init (RBMissingFilesSource *source)
{
	gint size;
	GdkPixbuf *pixbuf;

	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source, RB_TYPE_MISSING_FILES_SOURCE, RBMissingFilesSourcePrivate);

	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (gtk_icon_theme_get_default (),
					   GTK_STOCK_MISSING_IMAGE,
					   size,
					   0, NULL);
	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	if (pixbuf != NULL) {
		g_object_unref (pixbuf);
	}

	source->priv->entry_type = RHYTHMDB_ENTRY_TYPE_SONG;
}

static GObject *
rb_missing_files_source_constructor (GType type, guint n_construct_properties,
				     GObjectConstructParam *construct_properties)
{
	GObject *shell_player;
	RBMissingFilesSource *source;
	RBMissingFilesSourceClass *klass;
	RBShell *shell;
	GPtrArray *query;
	RhythmDBQueryModel *model;

	klass = RB_MISSING_FILES_SOURCE_CLASS (g_type_class_peek (RB_TYPE_MISSING_FILES_SOURCE));

	source = RB_MISSING_FILES_SOURCE (G_OBJECT_CLASS (rb_missing_files_source_parent_class)->
			constructor (type, n_construct_properties, construct_properties));

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &source->priv->db, NULL);
	shell_player = rb_shell_get_player (shell);
	g_object_unref (shell);

	/* construct real query */
	query = rhythmdb_query_parse (source->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      	RHYTHMDB_PROP_TYPE,
					source->priv->entry_type,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      	RHYTHMDB_PROP_HIDDEN,
					TRUE,
				      RHYTHMDB_QUERY_END);
	model = rhythmdb_query_model_new (source->priv->db, query,
					  NULL, NULL, NULL, FALSE);

	rhythmdb_query_free (query);

	g_object_set (model, "show-hidden", TRUE, NULL);

	/* set up entry view */
	source->priv->view = rb_entry_view_new (source->priv->db, shell_player,
						NULL, FALSE, FALSE);

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
	g_signal_connect_object (source->priv->view, "sort-order-changed",
				 G_CALLBACK (rb_missing_files_source_songs_sort_order_changed_cb), source, 0);

	gtk_widget_show_all (GTK_WIDGET (source));

	g_object_set (source, "query-model", model, NULL);
	g_object_unref (model);

	return G_OBJECT (source);
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
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_boxed (value);
		break;
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
	RBMissingFilesSource *source = RB_MISSING_FILES_SOURCE (object);

	switch (prop_id)
	{
	case PROP_ENTRY_TYPE:
		g_value_set_boxed (value, source->priv->entry_type);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSource *
rb_missing_files_source_new (RBShell *shell,
			     RBLibrarySource *library)
{
	RBSource *source;
	RhythmDBEntryType entry_type;

	g_object_get (library, "entry-type", &entry_type, NULL);
	source = RB_SOURCE (g_object_new (RB_TYPE_MISSING_FILES_SOURCE,
					  "name", _("Missing Files"),
					  "entry-type", entry_type,
					  "shell", shell,
					  "visibility", FALSE,
					  "hidden-when-empty", TRUE,
					  NULL));
	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	return source;
}

static void
rb_missing_files_source_songs_show_popup_cb (RBEntryView *view,
					     gboolean over_entry,
					     RBMissingFilesSource *source)
{
	if (over_entry)
		_rb_source_show_popup (RB_SOURCE (source), MISSING_FILES_SOURCE_SONGS_POPUP_PATH);
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
impl_delete (RBSource *asource)
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
rb_missing_files_source_songs_sort_order_changed_cb (RBEntryView *view,
						     RBMissingFilesSource *source)
{
	rb_entry_view_resort_model (view);
}

static void
impl_get_status (RBSource *asource, char **text, char **progress_text, float *progress)
{
	RhythmDBQueryModel *model;
	gint count;

	g_object_get (asource, "query-model", &model, NULL);
	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL);
	g_object_unref (model);

	*text = g_strdup_printf (ngettext ("%d missing file", "%d missing files", count),
				 count);
}
