/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include <time.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-source.h"
#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-util.h"
#include "rb-static-playlist-source.h"
#include "rb-play-order.h"

static void rb_source_class_init (RBSourceClass *klass);
static void rb_source_init (RBSource *source);
static void rb_source_dispose (GObject *object);
static void rb_source_finalize (GObject *object);
static void rb_source_set_property (GObject *object,
					guint prop_id,
					const GValue *value,
					GParamSpec *pspec);
static void rb_source_get_property (GObject *object,
					guint prop_id,
					GValue *value,
					GParamSpec *pspec);

static void default_get_status (RBDisplayPage *page, char **text, gboolean *busy);
static void default_activate (RBDisplayPage *page);
static GList *default_get_property_views (RBSource *source);
static gboolean default_can_rename (RBSource *source);
static GList *default_copy (RBSource *source);
static void default_reset_filters (RBSource *source);
static gboolean default_try_playlist (RBSource *source);
static RBSourceEOFType default_handle_eos (RBSource *source);
static RBEntryView *default_get_entry_view (RBSource *source);
static void default_add_to_queue (RBSource *source, RBSource *queue);
static void default_move_to_trash (RBSource *source);
static char *default_get_delete_label (RBSource *source);
static gboolean default_check_entry_type (RBSource *source, RhythmDBEntry *entry);

static void rb_source_status_changed_cb (RBDisplayPage *page);
static void rb_source_post_entry_deleted_cb (GtkTreeModel *model,
					     RhythmDBEntry *entry,
					     RBSource *source);
static void rb_source_row_inserted_cb (GtkTreeModel *model,
				       GtkTreePath *path,
				       GtkTreeIter *iter,
				       RBSource *source);

G_DEFINE_ABSTRACT_TYPE (RBSource, rb_source, RB_TYPE_DISPLAY_PAGE)

/**
 * SECTION:rbsource
 * @short_description: base class for sources
 *
 * This class provides methods for requesting information
 * about the UI capabilities of the source, and defines the
 * expectations that apply to all sources - that they will
 * provide #RBEntryView and #RhythmDBQueryModel objects, mostly.
 *
 * Many of the methods on this class come in can_do_x and do_x
 * pairs.  When can_do_x always returns FALSE, the class does not
 * need to implement the do_x method.
 *
 * Useful subclasses include #RBBrowserSource, which includes a #RBLibraryBrowser
 * and takes care of constructing an #RBEntryView too; #RBRemovableMediaSource,
 * which takes care of many aspects of implementing a source that represents a
 * removable device; and #RBPlaylistSource, which provides functionality for
 * playlist-like sources.
 */

struct _RBSourcePrivate
{
	RhythmDBQueryModel *query_model;
	guint hidden_when_empty : 1;
	guint update_visibility_id;
	guint update_status_id;
	guint status_changed_idle_id;
	RhythmDBEntryType *entry_type;
	RBSourceLoadStatus load_status;

	GSettings *settings;

	GMenu *toolbar_menu;
	GMenuModel *playlist_menu;
};

enum
{
	PROP_0,
	PROP_QUERY_MODEL,
	PROP_HIDDEN_WHEN_EMPTY,
	PROP_ENTRY_TYPE,
	PROP_BASE_QUERY_MODEL,
	PROP_PLAY_ORDER,
	PROP_SETTINGS,
	PROP_SHOW_BROWSER,
	PROP_LOAD_STATUS,
	PROP_TOOLBAR_MENU,
	PROP_PLAYLIST_MENU
};

enum
{
	FILTER_CHANGED,
	RESET_FILTERS,
	PLAYBACK_STATUS_CHANGED,
	LAST_SIGNAL
};

static guint rb_source_signals[LAST_SIGNAL] = { 0 };

static void
rb_source_class_init (RBSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	object_class->dispose = rb_source_dispose;
	object_class->finalize = rb_source_finalize;
	object_class->set_property = rb_source_set_property;
	object_class->get_property = rb_source_get_property;

	page_class->activate = default_activate;
	page_class->get_status = default_get_status;
	page_class->status_changed = rb_source_status_changed_cb;

	klass->reset_filters = default_reset_filters;
	klass->get_property_views = default_get_property_views;
	klass->can_rename = default_can_rename;
	klass->can_cut = (RBSourceFeatureFunc) rb_false_function;
	klass->can_paste = (RBSourceFeatureFunc) rb_false_function;
	klass->can_delete = (RBSourceFeatureFunc) rb_false_function;
	klass->can_copy = (RBSourceFeatureFunc) rb_false_function;
	klass->can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;
	klass->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	klass->can_pause = (RBSourceFeatureFunc) rb_true_function;
	klass->get_entry_view = default_get_entry_view;
	klass->copy = default_copy;
	klass->handle_eos = default_handle_eos;
	klass->try_playlist = default_try_playlist;
	klass->add_to_queue = default_add_to_queue;
	klass->get_delete_label = default_get_delete_label;
	klass->move_to_trash = default_move_to_trash;
	klass->check_entry_type = default_check_entry_type;

	/**
	 * RBSource:hidden-when-empty:
	 *
	 * If TRUE, the source will not be displayed in the source list
	 * when it contains no entries.
	 */
	g_object_class_install_property (object_class,
					 PROP_HIDDEN_WHEN_EMPTY,
					 g_param_spec_boolean ("hidden-when-empty",
							       "hidden-when-empty",
							       "Whether the source should be displayed in the source list when it is empty",
							       FALSE,
							       G_PARAM_READWRITE));

	/**
	 * RBSource:query-model:
	 *
	 * The current query model for the source.  This is used in
	 * various places, including the play order, to find the
	 * set of entries within the source.
	 */
	g_object_class_install_property (object_class,
					 PROP_QUERY_MODEL,
					 g_param_spec_object ("query-model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel object",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));
	/**
	 * RBSource:entry-type:
	 *
	 * Entry type for entries in this source.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_object ("entry-type",
							      "Entry type",
							      "Type of the entries which should be displayed by this source",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBSource:base-query-model:
	 *
	 * The unfiltered query model for the source, containing all entries in the source.
	 * Source classes should override this if they perform filtering based on the search
	 * box or a browser.
	 */
	g_object_class_install_property (object_class,
					 PROP_BASE_QUERY_MODEL,
					 g_param_spec_object ("base-query-model",
							      "RhythmDBQueryModel",
							      "RhythmDBQueryModel object (unfiltered)",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READABLE));
	/**
	 * RBSource:play-order:
	 *
	 * If the source provides its own play order, it can override this property.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAY_ORDER,
					 g_param_spec_object ("play-order",
							      "play order",
							      "optional play order specific to the source",
							      RB_TYPE_PLAY_ORDER,
							      G_PARAM_READABLE));

	/**
	 * RBSource:load-status:
	 *
	 * Indicates whether the source is not loaded, is currently loading data, or is
	 * fully loaded.
	 */
	g_object_class_install_property (object_class,
					 PROP_LOAD_STATUS,
					 g_param_spec_enum ("load-status",
							    "load-status",
							    "load status",
							    RB_TYPE_SOURCE_LOAD_STATUS,
							    RB_SOURCE_LOAD_STATUS_LOADED,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	/**
	 * RBSource:settings:
	 *
	 * The #GSettings instance storing settings for the source.  The instance must
	 * have a schema of org.gnome.Rhythmbox.Source.
	 */
	g_object_class_install_property (object_class,
					 PROP_SETTINGS,
					 g_param_spec_object ("settings",
							      "settings",
							      "GSettings instance",
							      G_TYPE_SETTINGS,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * RBSource:show-browser:
	 *
	 * Whether the browser widget for the source (if any) should be displayed.
	 * This should be overridden in sources that include a browser widget.
	 */
	g_object_class_install_property (object_class,
					 PROP_SHOW_BROWSER,
					 g_param_spec_boolean ("show-browser",
							       "show browser",
							       "whether the browser widget should be shown",
							       TRUE,
							       G_PARAM_READWRITE));
	/**
	 * RBSource:toolbar-menu:
	 *
	 * A GMenu instance describing the contents of a toolbar to display at
	 * the top of the source.  The #RBSource class doesn't actually display
	 * the toolbar anywhere.  Adding the toolbar to a container is the
	 * responsibility of a subclass such as #RBBrowserSource.
	 */
	g_object_class_install_property (object_class,
					 PROP_TOOLBAR_MENU,
					 g_param_spec_object ("toolbar-menu",
							      "toolbar menu",
							      "toolbar menu",
							      G_TYPE_MENU_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBSource:playlist-menu:
	 *
	 * A GMenu instance to attach to the 'add to playlist' item in the edit menu.
	 * If NULL, the item will be disabled.
	 */
	g_object_class_install_property (object_class,
					 PROP_PLAYLIST_MENU,
					 g_param_spec_object ("playlist-menu",
							      "playlist menu",
							      "playlist menu",
							      G_TYPE_MENU_MODEL,
							      G_PARAM_READWRITE));

	/**
	 * RBSource::filter-changed:
	 * @source: the #RBSource
	 *
	 * Fires when the user changes the filter, either by changing the
	 * contents of the search box or by selecting a different browser
	 * entry.
	 */
	rb_source_signals[FILTER_CHANGED] =
		g_signal_new ("filter_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, filter_changed),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBSource::reset-filters:
	 * @source: the #RBSource
	 *
	 * Action signal used to reset the source's filters.
	 */
	rb_source_signals[RESET_FILTERS] =
		g_signal_new ("reset-filters",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST | G_SIGNAL_ACTION,
			      G_STRUCT_OFFSET (RBSourceClass, reset_filters),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);
	/**
	 * RBSource::playback-status-changed:
	 * @source: the #RBSource
	 *
	 * Emitted to indicate playback status (buffering etc.) has changed
	 */
	rb_source_signals[PLAYBACK_STATUS_CHANGED] =
		g_signal_new ("playback-status-changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (RBSourcePrivate));
}

static void
rb_source_init (RBSource *source)
{
	source->priv = G_TYPE_INSTANCE_GET_PRIVATE (source, RB_TYPE_SOURCE, RBSourcePrivate);
}

static void
rb_source_dispose (GObject *object)
{
	RBSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);

	if (source->priv->update_visibility_id != 0) {
		g_source_remove (source->priv->update_visibility_id);
		source->priv->update_visibility_id = 0;
	}
	if (source->priv->update_status_id != 0) {
		g_source_remove (source->priv->update_status_id);
		source->priv->update_status_id = 0;
	}
	if (source->priv->status_changed_idle_id != 0) {
		g_source_remove (source->priv->status_changed_idle_id);
		source->priv->status_changed_idle_id = 0;
	}

	g_clear_object (&source->priv->settings);
	g_clear_object (&source->priv->toolbar_menu);
	g_clear_object (&source->priv->playlist_menu);

	G_OBJECT_CLASS (rb_source_parent_class)->dispose (object);
}

static void
rb_source_finalize (GObject *object)
{
	RBSource *source;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);

	if (source->priv->query_model != NULL) {
		rb_debug ("Unreffing model %p count: %d",
			  source->priv->query_model,
			  G_OBJECT (source->priv->query_model)->ref_count);
		g_object_unref (source->priv->query_model);
	}

	G_OBJECT_CLASS (rb_source_parent_class)->finalize (object);
}

static gboolean
update_visibility_idle (RBSource *source)
{
	gint count;

	count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (source->priv->query_model), NULL);
	g_object_set (source, "visibility", (count > 0), NULL);

	source->priv->update_visibility_id = 0;
	return FALSE;
}

static void
queue_update_visibility (RBSource *source)
{
	if (source->priv->update_visibility_id != 0) {
		g_source_remove (source->priv->update_visibility_id);
	}
	source->priv->update_visibility_id = g_idle_add ((GSourceFunc) update_visibility_idle, source);
}

/**
 * rb_source_set_hidden_when_empty:
 * @source: a #RBSource
 * @hidden: if TRUE, automatically hide the source
 *
 * Enables or disables automatic hiding of the source when
 * there are no entries in it.
 */
void
rb_source_set_hidden_when_empty (RBSource *source,
				 gboolean  hidden)
{
	g_return_if_fail (RB_IS_SOURCE (source));

	if (source->priv->hidden_when_empty != hidden) {
		source->priv->hidden_when_empty = hidden;
		queue_update_visibility (source);
	}
}

static void
rb_source_set_query_model_internal (RBSource *source,
				    RhythmDBQueryModel *model)
{
	if (source->priv->query_model == model) {
		return;
	}

	if (source->priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (source->priv->query_model,
						      G_CALLBACK (rb_source_post_entry_deleted_cb),
						      source);
		g_signal_handlers_disconnect_by_func (source->priv->query_model,
						      G_CALLBACK (rb_source_row_inserted_cb),
						      source);
		g_object_unref (source->priv->query_model);
	}

	source->priv->query_model = model;
	if (source->priv->query_model != NULL) {
		g_object_ref (source->priv->query_model);
		g_signal_connect_object (model, "post-entry-delete",
					 G_CALLBACK (rb_source_post_entry_deleted_cb),
					 source, 0);
		g_signal_connect_object (model, "row_inserted",
					 G_CALLBACK (rb_source_row_inserted_cb),
					 source, 0);
	}

	rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));
}

static void
rb_source_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id) {
	case PROP_HIDDEN_WHEN_EMPTY:
		rb_source_set_hidden_when_empty (source, g_value_get_boolean (value));
		break;
	case PROP_QUERY_MODEL:
		rb_source_set_query_model_internal (source, g_value_get_object (value));
		break;
	case PROP_ENTRY_TYPE:
		source->priv->entry_type = g_value_get_object (value);
		break;
	case PROP_SETTINGS:
		source->priv->settings = g_value_dup_object (value);
		break;
	case PROP_SHOW_BROWSER:
		/* not connected to anything here */
		break;
	case PROP_LOAD_STATUS:
		source->priv->load_status = g_value_get_enum (value);
		rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));
		break;
	case PROP_TOOLBAR_MENU:
		source->priv->toolbar_menu = g_value_dup_object (value);
		break;
	case PROP_PLAYLIST_MENU:
		source->priv->playlist_menu = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_source_get_property (GObject *object,
			guint prop_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBSource *source = RB_SOURCE (object);

	switch (prop_id) {
	case PROP_QUERY_MODEL:
		g_value_set_object (value, source->priv->query_model);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_object (value, source->priv->entry_type);
		break;
	case PROP_BASE_QUERY_MODEL:
		/* unless the subclass overrides it, just assume the
		 * current query model is the base model.
		 */
		g_value_set_object (value, source->priv->query_model);
		break;
	case PROP_PLAY_ORDER:
		g_value_set_object (value, NULL);		/* ? */
		break;
	case PROP_SETTINGS:
		g_value_set_object (value, source->priv->settings);
		break;
	case PROP_SHOW_BROWSER:
		g_value_set_boolean (value, FALSE);
		break;
	case PROP_LOAD_STATUS:
		g_value_set_enum (value, source->priv->load_status);
		break;
	case PROP_TOOLBAR_MENU:
		g_value_set_object (value, source->priv->toolbar_menu);
		break;
	case PROP_PLAYLIST_MENU:
		g_value_set_object (value, source->priv->playlist_menu);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
default_activate (RBDisplayPage *page)
{
	RBShell *shell;

	g_object_get (page, "shell", &shell, NULL);
	rb_shell_activate_source (shell,
				  RB_SOURCE (page),
				  RB_SHELL_ACTIVATION_ALWAYS_PLAY,
				  NULL);
}

static void
default_get_status (RBDisplayPage *page,
		    char **text,
		    gboolean *busy)
{
	RBSource *source = RB_SOURCE (page);
	RBSourceLoadStatus status;

	/* hack to get these strings marked for translation */
	if (0) {
		ngettext ("%d song", "%d songs", 0);
	}

	if (source->priv->query_model) {
		*text = rhythmdb_query_model_compute_status_normal (source->priv->query_model,
								    "%d song",
								    "%d songs");
	}

	g_object_get (source, "load-status", &status, NULL);
	switch (status) {
	case RB_SOURCE_LOAD_STATUS_WAITING:
	case RB_SOURCE_LOAD_STATUS_LOADING:
		*busy = TRUE;
		break;
	default:
		break;
	}
}

/**
 * rb_source_notify_filter_changed:
 * @source: a #RBSource
 *
 * Source implementations call this when their filter state changes
 */
void
rb_source_notify_filter_changed (RBSource *source)
{
	g_signal_emit (G_OBJECT (source), rb_source_signals[FILTER_CHANGED], 0);
}

/**
 * rb_source_update_play_statistics:
 * @source: a #RBSource
 * @db: the #RhythmDB instance
 * @entry: the #RhythmDBEntry to update
 *
 * Updates play count and play time statistics for a database entry.
 * Sources containing entries that do not normally reach EOS should
 * call this for an entry when it is no longer being played.
 */
void
rb_source_update_play_statistics (RBSource *source,
				  RhythmDB *db,
				  RhythmDBEntry *entry)
{
	time_t now;
	gulong current_count;
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_ULONG);

	current_count = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_PLAY_COUNT);

	g_value_set_ulong (&value, current_count + 1);

	/* Increment current play count */
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_PLAY_COUNT, &value);
	g_value_unset (&value);

	/* Reset the last played time */
	time (&now);

	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value, now);
	rhythmdb_entry_set (db, entry, RHYTHMDB_PROP_LAST_PLAYED, &value);
	g_value_unset (&value);

	rhythmdb_commit (db);
}

/**
 * rb_source_get_entry_view:
 * @source: a #RBSource
 *
 * Returns the entry view widget for the source.
 *
 * Return value: (transfer none): the #RBEntryView instance for the source
 */
RBEntryView *
rb_source_get_entry_view (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->get_entry_view (source);
}

static GList *
default_get_property_views (RBSource *source)
{
	return NULL;
}

/**
 * rb_source_get_property_views:
 * @source: a #RBSource
 *
 * Returns a list containing the #RBPropertyView instances for the
 * source, if any.
 *
 * Return value: (element-type RB.PropertyView) (transfer container): list of property views
 */
GList *
rb_source_get_property_views (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->get_property_views (source);
}

static gboolean
default_can_rename (RBSource *source)
{
	return FALSE;
}

/**
 * rb_source_can_rename:
 * @source: a #RBSource.
 *
 * Determines whether the source can be renamed.
 *
 * Return value: TRUE if this source can be renamed
 */
gboolean
rb_source_can_rename (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->can_rename (source);
}

/**
 * rb_source_search:
 * @source: a #RBSource
 * @search: (allow-none): the active #RBSourceSearch instance
 * @cur_text: (allow-none): the current search text
 * @new_text: the new search text
 *
 * Updates the source with new search text.  The source
 * should recreate the database query that feeds into the
 * browser (if any).
 */
void
rb_source_search (RBSource *source,
		  RBSourceSearch *search,
		  const char *cur_text,
		  const char *new_text)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	g_assert (new_text != NULL);

	if (klass->search != NULL)
		klass->search (source, search, cur_text, new_text);
}


/**
 * rb_source_can_cut:
 * @source: a #RBSource
 *
 * Determines whether the source supporst the typical cut
 * (as in cut-and-paste) operation.
 *
 * Return value: TRUE if cutting is supported
 */
gboolean
rb_source_can_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->can_cut (source);
}

/**
 * rb_source_can_paste:
 * @source: a #RBSource
 *
 * Determines whether the source supports paste operations.
 *
 * Return value: TRUE if the pasting is supported
 */
gboolean
rb_source_can_paste (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->can_paste (source);
}

/**
 * rb_source_can_delete:
 * @source: a #RBSource
 *
 * Determines whether the source allows the user to delete
 * a selected set of entries.
 *
 * Return value: TRUE if deletion is supported
 */
gboolean
rb_source_can_delete (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->can_delete (source);
}

/**
 * rb_source_can_move_to_trash:
 * @source: a #RBSource
 *
 * Determines whether the source allows the user to trash
 * the files backing a selected set of entries.
 *
 * Return value: TRUE if trashing is supported
 */
gboolean
rb_source_can_move_to_trash (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->can_move_to_trash (source);
}

/**
 * rb_source_can_copy:
 * @source: a #RBSource
 *
 * Determines whether the source supports the copy part
 * of a copy-and-paste operation.
 *
 * Return value: TRUE if copying is supported
 */
gboolean
rb_source_can_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->can_copy (source);
}

/**
 * rb_source_cut:
 * @source: a #RBSource
 *
 * Removes the currently selected entries from the source and
 * returns them so they can be pasted into another source.
 *
 * Return value: (element-type RB.RhythmDBEntry) (transfer full): entries cut
 * from the source.
 */
GList *
rb_source_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->cut (source);
}

static GList *
default_copy (RBSource *source)
{
	RBEntryView *entry_view;
	entry_view = rb_source_get_entry_view (source);
	if (entry_view == NULL)
		return NULL;

	return rb_entry_view_get_selected_entries (entry_view);
}

/**
 * rb_source_copy:
 * @source: a #RBSource
 *
 * Copies the selected entries to the clipboard.
 *
 * Return value: (element-type RB.RhythmDBEntry) (transfer full): a list containing
 * the currently selected entries from the source.
 */
GList *
rb_source_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->copy (source);
}

/**
 * rb_source_paste:
 * @source: a #RBSource
 * @entries: (element-type RB.RhythmDBEntry): a list of #RhythmDBEntry objects to paste in
 *
 * Adds a list of entries previously cut or copied from another
 * source.  If the entries are not of the type used by the source,
 * the entries will be copied and possibly converted into an acceptable format.
 * This can be used for transfers to and from devices and network shares.
 *
 * If the transfer is performed using an #RBTrackTransferBatch, the batch object
 * is returned so the caller can monitor the transfer progress.  The caller does not
 * own a reference on the batch object.
 *
 * Return value: (transfer none): the #RBTrackTransferBatch used to perform the transfer (if any)
 */
RBTrackTransferBatch *
rb_source_paste (RBSource *source, GList *entries)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->paste (source, entries);
}

/**
 * rb_source_can_add_to_queue:
 * @source: a #RBSource
 *
 * Determines whether the source can add the selected entries to
 * the play queue.
 *
 * Return value: TRUE if adding to the play queue is supported
 */
gboolean
rb_source_can_add_to_queue (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->can_add_to_queue (source);
}

static void
default_add_to_queue (RBSource *source,
		      RBSource *queue)
{
	RBEntryView *songs;
	GList *selection;
	GList *iter;

	songs = rb_source_get_entry_view (source);
	if (songs == NULL)
		return;

	selection = rb_entry_view_get_selected_entries (songs);
	if (selection == NULL)
		return;

	for (iter = selection; iter; iter = iter->next) {
		rb_static_playlist_source_add_entry (RB_STATIC_PLAYLIST_SOURCE (queue),
						     (RhythmDBEntry *)iter->data, -1);
	}

	g_list_foreach (selection, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selection);
}

/**
 * rb_source_add_to_queue:
 * @source: a #RBSource
 * @queue: the #RBSource for the play queue
 *
 * Adds the currently selected entries to the end of the
 * play queue.
 */
void
rb_source_add_to_queue (RBSource *source,
			RBSource *queue)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	klass->add_to_queue (source, queue);
}

/**
 * rb_source_delete_selected:
 * @source: a #RBSource
 *
 * Deletes the currently selected entries from the source.
 */
void
rb_source_delete_selected (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->delete_selected (source);
}

static void
default_move_to_trash (RBSource *source)
{
	GList *sel, *tem;
	RBEntryView *entry_view;
	RhythmDB *db;

	g_object_get (source->priv->query_model, "db", &db, NULL);

	sel = NULL;
	entry_view = rb_source_get_entry_view (source);
	if (entry_view != NULL) {
		sel = rb_entry_view_get_selected_entries (entry_view);
	}

	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_move_to_trash (db, (RhythmDBEntry *)tem->data);
		rhythmdb_commit (db);
	}

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
	g_object_unref (db);
}

/**
 * rb_source_move_to_trash:
 * @source: a #RBSource
 *
 * Trashes the files backing the currently selected set of entries.
 * In general, this should use #rhythmdb_entry_move_to_trash to
 * perform the actual trash operation.
 */
void
rb_source_move_to_trash (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->move_to_trash (source);
}

static void
default_reset_filters (RBSource *source)
{
	rb_debug ("no implementation of reset_filters for this source");
}

/**
 * rb_source_can_show_properties:
 * @source: a #RBSource
 *
 * Determines whether the source can display a properties
 * window for the currently selected entry (or set of entries)
 *
 * Return value: TRUE if showing properties is supported
 */
gboolean
rb_source_can_show_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return (klass->song_properties != NULL);
}

/**
 * rb_source_song_properties:
 * @source: a #RBSource
 *
 * Displays a properties window for the currently selected entries.
 */
void
rb_source_song_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	g_assert (klass->song_properties);
	klass->song_properties (source);
}

/**
 * rb_source_try_playlist:
 * @source: a #RBSource
 *
 * Determines whether playback URIs for entries in the source should
 * be parsed as playlists rather than just played.
 *
 * Return value: TRUE to attempt playlist parsing
 */
gboolean
rb_source_try_playlist (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->try_playlist (source);
}

/**
 * rb_source_want_uri:
 * @source: a #RBSource
 * @uri: a URI for the source to consider
 *
 * Returns an indication of how much the source wants to handle
 * the specified URI.  100 is the highest usual value, and should
 * only be used when the URI can only be associated with this source.
 * 0 should be used when the URI does not match the source at all.
 *
 * Return value: value from 0 to 100 indicating how much the
 *  source wants this URI.
 */
guint
rb_source_want_uri (RBSource *source, const char *uri)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	if (klass->want_uri)
		return klass->want_uri (source, uri);
	return 0;
}

/**
 * rb_source_uri_is_source:
 * @source: a #RBSource
 * @uri: a URI for the source to consider
 *
 * Checks if the URI matches the source itself.  A source
 * should return TRUE here if the URI points to the device that
 * the source represents, for example.
 *
 * Return value: TRUE if the URI identifies the source itself.
 */
gboolean
rb_source_uri_is_source (RBSource *source, const char *uri)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	if (klass->uri_is_source)
		return klass->uri_is_source (source, uri);
	return FALSE;
}

/**
 * rb_source_add_uri:
 * @source: a #RBSource
 * @uri: a URI to add
 * @title: theoretically, the title of the entity the URI points to
 * @genre: theoretically, the genre of the entity the URI points to
 * @callback: a callback function to call when complete
 * @data: data to pass to the callback
 * @destroy_data: function to call to destroy the callback data
 *
 * Adds an entry corresponding to the URI to the source.  The
 * @title and @genre parameters are not really used.
 */
void
rb_source_add_uri (RBSource *source,
		   const char *uri,
		   const char *title,
		   const char *genre,
		   RBSourceAddCallback callback,
		   gpointer data,
		   GDestroyNotify destroy_data)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	if (klass->add_uri)
		klass->add_uri (source, uri, title, genre, callback, data, destroy_data);
}

/**
 * rb_source_can_pause:
 * @source: a #RBSource
 *
 * Determines whether playback of entries from the source can
 * be paused.
 *
 * Return value: TRUE if pausing is supported
 */
gboolean
rb_source_can_pause (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->can_pause (source);
}

static gboolean
default_try_playlist (RBSource *source)
{
	return FALSE;
}

static RBSourceEOFType
default_handle_eos (RBSource *source)
{
	return RB_SOURCE_EOF_NEXT;
}

/**
 * rb_source_handle_eos:
 * @source: a #RBSource
 *
 * Determines how EOS events should be handled when playing entries
 * from the source.
 *
 * Return value: EOS event handling type
 */
RBSourceEOFType
rb_source_handle_eos (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->handle_eos (source);
}

static RBEntryView*
default_get_entry_view (RBSource *source)
{
	return NULL;
}

static char *
default_get_delete_label (RBSource *source)
{
	return g_strdup (_("Remove"));
}

/**
 * rb_source_get_delete_label:
 * @source: a #RBSource
 *
 * Returns a translated label for the 'delete' menu item, allowing
 * sources to better describe what happens to deleted entries.
 * Playlists, for example, return "Remove from Playlist" here.
 *
 * Return value: allocated string holding the label string
 */
char *
rb_source_get_delete_label (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->get_delete_label (source);
}

static gboolean
_update_status_idle (RBSource *source)
{
	rb_display_page_notify_status_changed (RB_DISPLAY_PAGE (source));

	if (source->priv->hidden_when_empty)
		update_visibility_idle (source);

	source->priv->update_status_id = 0;
	return FALSE;
}

static void
rb_source_row_inserted_cb (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   RBSource *source)
{
	if (source->priv->update_status_id == 0)
		source->priv->update_status_id = g_idle_add ((GSourceFunc)_update_status_idle, source);
}

static void
rb_source_post_entry_deleted_cb (GtkTreeModel *model,
				 RhythmDBEntry *entry,
				 RBSource *source)
{
	if (source->priv->update_status_id == 0)
		source->priv->update_status_id = g_idle_add ((GSourceFunc)_update_status_idle, source);
}

static void
rb_source_gather_hash_keys (char *key,
			    gpointer unused,
			    GList **data)
{
	*data = g_list_prepend (*data, key);
}

/**
 * rb_source_gather_selected_properties:
 * @source: a #RBSource
 * @prop: property for which to gather selection
 *
 * Returns a list containing the values of the specified
 * property from the selected entries in the source.
 * This is used to implement the 'browse this artist' (etc.)
 * actions.
 *
 * Return value: (element-type utf8) (transfer full): list of property values
 */
GList *
rb_source_gather_selected_properties (RBSource *source,
				      RhythmDBPropType prop)
{
	RBEntryView *entryview;
	GList *selected, *tem;
	GHashTable *selected_set;

	entryview = rb_source_get_entry_view (source);
	if (entryview == NULL)
		return NULL;

	selected_set = g_hash_table_new (g_str_hash, g_str_equal);
	selected = rb_entry_view_get_selected_entries (entryview);

	for (tem = selected; tem; tem = tem->next) {
		RhythmDBEntry *entry = tem->data;
		char *val = g_strdup (rhythmdb_entry_get_string (entry, prop));
		g_hash_table_insert (selected_set, val, NULL);
	}

	g_list_foreach (selected, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selected);

	tem = NULL;
	g_hash_table_foreach (selected_set, (GHFunc) rb_source_gather_hash_keys,
			      &tem);
	g_hash_table_destroy (selected_set);
	return tem;
}

static gboolean
default_check_entry_type (RBSource *source, RhythmDBEntry *entry)
{
	RhythmDBEntryType *entry_type;
	gboolean ret = TRUE;

	g_object_get (source, "entry-type", &entry_type, NULL);
	if (entry_type != NULL) {
		if (rhythmdb_entry_get_entry_type (entry) != entry_type) {
			ret = FALSE;
		}
		g_object_unref (entry_type);
	}
	return ret;
}

/**
 * rb_source_check_entry_type:
 * @source: a #RBSource
 * @entry: a #RhythmDBEntry
 *
 * Checks if a database entry matches the entry type for the source.
 *
 * Return value: %TRUE if the entry matches the source's entry type.
 */
gboolean
rb_source_check_entry_type (RBSource *source, RhythmDBEntry *entry)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->check_entry_type (source, entry);
}

static gboolean
sort_order_get_mapping (GValue *value, GVariant *variant, gpointer data)
{
	const char *column;
	gboolean sort_type;
	char *str;

	g_variant_get (variant, "(&sb)", &column, &sort_type);
	str = g_strdup_printf ("%s,%s", column, sort_type ? "ascending" : "descending");
	g_value_take_string (value, str);
	return TRUE;
}

static GVariant *
sort_order_set_mapping (const GValue *value, const GVariantType *expected_type, gpointer data)
{
	gboolean sort_type;
	GVariant *var;
	char **strs;

	strs = g_strsplit (g_value_get_string (value), ",", 0);
	if (!strcmp ("ascending", strs[1])) {
		sort_type = TRUE;
	} else if (!strcmp ("descending", strs[1])) {
		sort_type = FALSE;
	} else {
		g_warning ("atttempting to sort in unknown direction");
		sort_type = TRUE;
	}

	var = g_variant_new ("(sb)", strs[0], sort_type);
	g_strfreev (strs);
	return var;
}

static void
sync_paned_position (GSettings *settings, GObject *paned)
{
	int pos;
	g_object_get (paned, "position", &pos, NULL);

	if (pos != g_settings_get_int (settings, "paned-position")) {
		g_settings_set_int (settings, "paned-position", pos);
	}
}

static void
paned_position_changed_cb (GObject *paned, GParamSpec *pspec, GSettings *settings)
{
	rb_settings_delayed_sync (settings,
				  (RBDelayedSyncFunc) sync_paned_position,
				  g_object_ref (paned),
				  g_object_unref);
}

/**
 * rb_source_bind_settings:
 * @source: the #RBSource
 * @entry_view: (allow-none): the #RBEntryView for the source
 * @paned: (allow-none): the #GtkPaned containing the entry view and the browser
 * @browser: (allow-none):  the browser (typically a #RBLibraryBrowser) for the source
 * @sort_order: whether to bind the entry view sort order
 *
 * Binds the source's #GSettings instance to the given widgets.  Should be called
 * from the source's constructed method.
 *
 * If the browser widget has a browser-views property, it will be bound to the
 * browser-views settings key.
 */
void
rb_source_bind_settings (RBSource *source, GtkWidget *entry_view, GtkWidget *paned, GtkWidget *browser, gboolean sort_order)
{
	char *name;
	GSettings *common_settings;

	common_settings = g_settings_new ("org.gnome.rhythmbox.sources");
	g_object_get (source, "name", &name, NULL);

	if (entry_view != NULL) {
		if (sort_order && source->priv->settings) {
			rb_debug ("binding entry view sort order for %s", name);
			g_settings_bind_with_mapping (source->priv->settings, "sorting", entry_view, "sort-order",
						      G_SETTINGS_BIND_GET | G_SETTINGS_BIND_SET | G_SETTINGS_BIND_NO_SENSITIVITY,
						      (GSettingsBindGetMapping) sort_order_get_mapping,
						      (GSettingsBindSetMapping) sort_order_set_mapping,
						      NULL, NULL);
		}

		g_settings_bind (common_settings, "visible-columns",
				 entry_view, "visible-columns",
				 G_SETTINGS_BIND_DEFAULT);
	}

	if (paned != NULL && source->priv->settings != NULL) {
		rb_debug ("binding paned position for %s", name);
		/* can't use a normal binding here, as we want to delay writing to the
		 * setting while the separator is being dragged.
		 */
		g_settings_bind (source->priv->settings, "paned-position", paned, "position", G_SETTINGS_BIND_GET);
		g_signal_connect_object (paned, "notify::position", G_CALLBACK (paned_position_changed_cb), source->priv->settings, 0);
	}

	if (browser) {
		rb_debug ("binding show-browser for %s", name);
		if (source->priv->settings) {
			g_settings_bind (source->priv->settings, "show-browser", source, "show-browser", G_SETTINGS_BIND_DEFAULT);
		}

		if (g_object_class_find_property (G_OBJECT_GET_CLASS (browser), "browser-views")) {
			g_settings_bind (common_settings, "browser-views", browser, "browser-views", G_SETTINGS_BIND_DEFAULT);
		}
	}

	g_free (name);
}

/**
 * rb_source_notify_playback_status_changed:
 * @source: a #RBSource
 *
 * Source implementations call this when their playback status
 * changes.
 */
void
rb_source_notify_playback_status_changed (RBSource *source)
{
	g_signal_emit (G_OBJECT (source), rb_source_signals[PLAYBACK_STATUS_CHANGED], 0);
}

/**
 * rb_source_get_playback_status:
 * @source: a #RBSource
 * @text: (inout) (allow-none) (transfer full): holds returned playback status text
 * @progress: (inout) (allow-none): holds returned playback status progress value
 *
 * Retrieves playback status details, such as buffering progress.
 */
void
rb_source_get_playback_status (RBSource *source, char **text, float *progress)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	if (klass->get_playback_status)
		klass->get_playback_status (source, text, progress);
}

static gboolean
status_changed_idle_cb (RBSource *source)
{
	RBEntryView *entry_view;
	char *status = NULL;
	gboolean busy = FALSE;

	rb_display_page_get_status (RB_DISPLAY_PAGE (source), &status, &busy);

	entry_view = rb_source_get_entry_view (source);
	if (entry_view != NULL)
		rb_entry_view_set_status (entry_view, status, busy);

	g_free (status);
	source->priv->status_changed_idle_id = 0;
	return FALSE;
}

static void
rb_source_status_changed_cb (RBDisplayPage *page)
{
	RBSource *source = RB_SOURCE (page);
	if (source->priv->status_changed_idle_id == 0) {
		source->priv->status_changed_idle_id =
			g_idle_add ((GSourceFunc) status_changed_idle_cb, source);
	}
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_source_eof_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCE_EOF_ERROR, "error"),
			ENUM_ENTRY (RB_SOURCE_EOF_STOP, "stop"),
			ENUM_ENTRY (RB_SOURCE_EOF_RETRY, "retry"),
			ENUM_ENTRY (RB_SOURCE_EOF_NEXT, "next"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourceEOFType", values);
	}

	return etype;
}

GType
rb_source_load_status_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCE_LOAD_STATUS_NOT_LOADED, "not-loaded"),
			ENUM_ENTRY (RB_SOURCE_LOAD_STATUS_WAITING, "waiting"),
			ENUM_ENTRY (RB_SOURCE_LOAD_STATUS_LOADING, "loading"),
			ENUM_ENTRY (RB_SOURCE_LOAD_STATUS_LOADED, "loaded"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourceLoadStatus", values);
	}

	return etype;
}

