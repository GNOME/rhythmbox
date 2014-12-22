/*
 *  Copyright (C) 2009 Paul Bellamy  <paul.a.bellamy@gmail.com>
 *  Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include "rb-sync-settings-ui.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-playlist-manager.h"
#include "rb-podcast-entry-types.h"

struct _RBSyncSettingsUIPrivate
{
	RBMediaPlayerSource *source;
	RBSyncSettings *sync_settings;

	GtkTreeStore *sync_tree_store;
};

enum {
	PROP_0,
	PROP_SOURCE,
	PROP_SYNC_SETTINGS
};

G_DEFINE_TYPE (RBSyncSettingsUI, rb_sync_settings_ui, GTK_TYPE_BOX)


static void
set_treeview_children (RBSyncSettingsUI *ui,
		       GtkTreeIter *parent,
		       const char *category,
		       gboolean value,
		       gboolean apply_to_settings)
{
	GtkTreeIter iter;
	char *group;
	gboolean valid;

	valid = gtk_tree_model_iter_children (GTK_TREE_MODEL (ui->priv->sync_tree_store), &iter, parent);

	while (valid) {
		gtk_tree_model_get (GTK_TREE_MODEL (ui->priv->sync_tree_store), &iter,
				    2, &group,
				    -1);

		if (apply_to_settings) {
			rb_sync_settings_set_group (ui->priv->sync_settings, category, group, value);
		}
		gtk_tree_store_set (ui->priv->sync_tree_store, &iter,
		/* Active */	    0, value,
				    -1);

		g_free (group);
		valid = gtk_tree_model_iter_next (GTK_TREE_MODEL (ui->priv->sync_tree_store), &iter);
	}
}

static void
sync_entries_changed_cb (GtkCellRendererToggle *cell_renderer,
			 gchar *path,
			 RBSyncSettingsUI *ui)
{
	GtkTreeIter iter;
	char *group;
	char *category_name;
	gboolean is_category;
	gboolean value;

	if (gtk_tree_model_get_iter_from_string (GTK_TREE_MODEL (ui->priv->sync_tree_store), &iter, path) == FALSE) {
		return;
	}

	gtk_tree_model_get (GTK_TREE_MODEL (ui->priv->sync_tree_store),
			    &iter,
			    2, &group,
			    4, &is_category,
			    5, &category_name,
			    -1);

	value = !gtk_cell_renderer_toggle_get_active (cell_renderer);

	if (is_category) {
		rb_debug ("state for category %s changed to %d", category_name, value);
		rb_sync_settings_set_category (ui->priv->sync_settings, category_name, value);
		rb_sync_settings_clear_groups (ui->priv->sync_settings, category_name);

		gtk_tree_store_set (ui->priv->sync_tree_store,
				    &iter,
				    0, value,
				    1, FALSE,		/* category is no longer inconsistent */
				    -1);
		set_treeview_children (ui, &iter, category_name, value, FALSE);
	} else {
		gboolean parent_value;
		gboolean parent_inconsistent;
		GtkTreeIter parent;
		rb_debug ("state for group %s in category %s changed to %d", group, category_name, value);

		/* update parent state.  if the parent was previously enabled or disabled, then we
		 * set all the other groups to that state before setting the row that was just changed,
		 * and set the parent to inconsistent state.
		 */
		gtk_tree_model_iter_parent (GTK_TREE_MODEL (ui->priv->sync_tree_store), &parent, &iter);
		gtk_tree_model_get (GTK_TREE_MODEL (ui->priv->sync_tree_store), &parent,
				    0, &parent_value,
				    1, &parent_inconsistent,
				    -1);
		if (parent_inconsistent == FALSE) {
			/* category is now inconsistent */
			rb_debug ("setting category %s to disabled, inconsistent", category_name);
			rb_sync_settings_set_category (ui->priv->sync_settings, category_name, FALSE);
			gtk_tree_store_set (ui->priv->sync_tree_store,
					    &parent,
					    0, FALSE,
					    1, TRUE,
					    -1);

			/* set all groups in the category to the parent's state */
			set_treeview_children (ui, &parent, category_name, parent_value, TRUE);
		}

		rb_sync_settings_set_group (ui->priv->sync_settings, category_name, group, value);
		gtk_tree_store_set (ui->priv->sync_tree_store, &iter,
				    0, value,
				    -1);

		/* if no groups are enabled, the category is no longer inconsistent */
		if (value == FALSE && rb_sync_settings_has_enabled_groups (ui->priv->sync_settings, category_name) == FALSE) {
			rb_debug ("no enabled groups left in category %s", category_name);
			gtk_tree_store_set (ui->priv->sync_tree_store, &parent,
					    1, FALSE,
					    -1);
		} else if (value == FALSE) {
			rb_debug ("category %s still has some groups", category_name);
		}
	}

	g_free (category_name);
	g_free (group);
}


GtkWidget *
rb_sync_settings_ui_new (RBMediaPlayerSource *source, RBSyncSettings *settings)
{
	GObject *ui;
	ui = g_object_new (RB_TYPE_SYNC_SETTINGS_UI,
			   "source", source,
			   "sync-settings", settings,
			   NULL);
	return GTK_WIDGET (ui);
}

static void
rb_sync_settings_ui_init (RBSyncSettingsUI *ui)
{
	ui->priv = G_TYPE_INSTANCE_GET_PRIVATE (ui, RB_TYPE_SYNC_SETTINGS_UI, RBSyncSettingsUIPrivate);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (ui), GTK_ORIENTATION_VERTICAL);
}

static void
impl_constructed (GObject *object)
{
	RBSyncSettingsUI *ui = RB_SYNC_SETTINGS_UI (object);
	GtkTreeIter tree_iter;
	GtkTreeIter parent_iter;
	GtkTreeModel *query_model;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *col;
	GtkWidget *tree_view;
	GList *l;
	GList *playlists;
	RBShell *shell;
	RhythmDB *db;
	RBPlaylistManager *playlist_manager;
	gboolean valid;

	g_object_get (ui->priv->source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, "playlist-manager", &playlist_manager, NULL);

	/* tree_store columns are: active, inconsistent, name, display-name, is-category, category name */
	ui->priv->sync_tree_store = gtk_tree_store_new (6, G_TYPE_BOOLEAN, G_TYPE_BOOLEAN, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_BOOLEAN, G_TYPE_STRING);

	/* music library parent */
	gtk_tree_store_append (ui->priv->sync_tree_store, &parent_iter, NULL);
	gtk_tree_store_set (ui->priv->sync_tree_store, &parent_iter,
			    0, rb_sync_settings_sync_category (ui->priv->sync_settings, SYNC_CATEGORY_MUSIC),
			    1, rb_sync_settings_has_enabled_groups (ui->priv->sync_settings, SYNC_CATEGORY_MUSIC),
			    2, _("Music"),
			    3, _("Music"),
			    4, TRUE,
			    5, SYNC_CATEGORY_MUSIC,
			    -1);

	/* 'all music' entry */
	gtk_tree_store_append (ui->priv->sync_tree_store, &tree_iter, &parent_iter);
	gtk_tree_store_set (ui->priv->sync_tree_store, &tree_iter,
			    0, rb_sync_settings_sync_group (ui->priv->sync_settings, SYNC_CATEGORY_MUSIC, SYNC_GROUP_ALL_MUSIC),
			    1, FALSE,
			    2, SYNC_GROUP_ALL_MUSIC,
			    3, _("All Music"),
			    4, FALSE,
			    5, SYNC_CATEGORY_MUSIC,
			    -1);

	/* playlist entries */
	playlists = rb_playlist_manager_get_playlists (playlist_manager);
	for (l = playlists; l != NULL; l = l->next) {
		char *name;

		gtk_tree_store_append (ui->priv->sync_tree_store, &tree_iter, &parent_iter);
		/* set playlists data here */
		g_object_get (l->data, "name", &name, NULL);

		/* set this row's data */
		gtk_tree_store_set (ui->priv->sync_tree_store, &tree_iter,
				    0, rb_sync_settings_sync_group (ui->priv->sync_settings, SYNC_CATEGORY_MUSIC, name),
				    1, FALSE,
				    2, name,
				    3, name,
				    4, FALSE,
				    5, SYNC_CATEGORY_MUSIC,
				    -1);

		g_free (name);
	}

	/* Append the Podcasts parent */
	gtk_tree_store_append (ui->priv->sync_tree_store,
			       &parent_iter,
			       NULL);
	gtk_tree_store_set (ui->priv->sync_tree_store, &parent_iter,
			    0, rb_sync_settings_sync_category (ui->priv->sync_settings, SYNC_CATEGORY_PODCAST),
			    1, rb_sync_settings_has_enabled_groups (ui->priv->sync_settings, SYNC_CATEGORY_PODCAST),
			    2, _("Podcasts"),
			    3, _("Podcasts"),
			    4, TRUE,
			    5, SYNC_CATEGORY_PODCAST,
			    -1);

	/* this really needs to use a live query model */
	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	rhythmdb_query_model_set_sort_order (RHYTHMDB_QUERY_MODEL (query_model),
					     (GCompareDataFunc) rhythmdb_query_model_title_sort_func,
					     NULL, NULL, FALSE);
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, RHYTHMDB_ENTRY_TYPE_PODCAST_FEED,
				RHYTHMDB_QUERY_END);
	valid = gtk_tree_model_get_iter_first (query_model, &tree_iter);
	while (valid) {
		RhythmDBEntry *entry;
		GtkTreeIter tree_iter2;
		const char *name;
		const char *feed;

		entry = rhythmdb_query_model_iter_to_entry (RHYTHMDB_QUERY_MODEL (query_model), &tree_iter);
		gtk_tree_store_append (ui->priv->sync_tree_store, &tree_iter2, &parent_iter);

		/* set up this row */
		name = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
		feed = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		rb_debug ("adding feed %s (name %s)", feed, name);
		gtk_tree_store_set (ui->priv->sync_tree_store, &tree_iter2,
				    0, rb_sync_settings_sync_group (ui->priv->sync_settings, SYNC_CATEGORY_PODCAST, feed),
				    1, FALSE,
				    2, feed,
				    3, name,
				    4, FALSE,
				    5, SYNC_CATEGORY_PODCAST,
				    -1);

		valid = gtk_tree_model_iter_next (query_model, &tree_iter);
	}

	/* Set up the treeview */
	tree_view = gtk_tree_view_new ();
	gtk_tree_view_set_headers_visible (GTK_TREE_VIEW (tree_view), FALSE);
	gtk_box_pack_start (GTK_BOX (ui), tree_view, TRUE, TRUE, 0);

	/* First column */
	renderer = gtk_cell_renderer_toggle_new ();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"active", 0,
							"inconsistent", 1,
							NULL);
	g_signal_connect (G_OBJECT (renderer),
			  "toggled", G_CALLBACK (sync_entries_changed_cb),
			  ui);
	gtk_tree_view_append_column(GTK_TREE_VIEW (tree_view), col);

	/* Second column */
	renderer = gtk_cell_renderer_text_new ();
	col = gtk_tree_view_column_new_with_attributes (NULL,
							renderer,
							"text", 3,
							NULL);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tree_view), col);
	gtk_tree_view_set_model (GTK_TREE_VIEW (tree_view),
				 GTK_TREE_MODEL (ui->priv->sync_tree_store));
	gtk_tree_selection_set_mode (gtk_tree_view_get_selection (GTK_TREE_VIEW (tree_view)),
				    GTK_SELECTION_NONE);

	g_object_unref (playlist_manager);
	g_object_unref (shell);
	g_object_unref (db);

	gtk_widget_show_all (GTK_WIDGET (ui));

	RB_CHAIN_GOBJECT_METHOD(rb_sync_settings_ui_parent_class, constructed, object);
}


static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBSyncSettingsUI *ui = RB_SYNC_SETTINGS_UI (object);
	switch (prop_id) {
	case PROP_SOURCE:
		ui->priv->source = g_value_get_object (value);
		break;
	case PROP_SYNC_SETTINGS:
		ui->priv->sync_settings = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBSyncSettingsUI *ui = RB_SYNC_SETTINGS_UI (object);
	switch (prop_id) {
	case PROP_SOURCE:
		g_value_set_object (value, ui->priv->source);
		break;
	case PROP_SYNC_SETTINGS:
		g_value_set_object (value, ui->priv->sync_settings);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBSyncSettingsUI *ui = RB_SYNC_SETTINGS_UI (object);

	if (ui->priv->sync_tree_store) {
		g_object_unref (ui->priv->sync_tree_store);
		ui->priv->sync_tree_store = NULL;
	}

	G_OBJECT_CLASS (rb_sync_settings_ui_parent_class)->dispose (object);
}

static void
rb_sync_settings_ui_class_init (RBSyncSettingsUIClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = impl_dispose;
	object_class->constructed = impl_constructed;

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;

	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "source",
							      "RBMediaPlayerSource instance",
							      RB_TYPE_MEDIA_PLAYER_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_SYNC_SETTINGS,
					 g_param_spec_object ("sync-settings",
							      "sync settings",
							      "RBSyncSettings instance",
							      RB_TYPE_SYNC_SETTINGS,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (object_class, sizeof (RBSyncSettingsUIPrivate));
}
