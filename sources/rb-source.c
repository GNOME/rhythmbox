/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of the abstract base class of all sources
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@gnome.org>
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

#include "config.h"

#include <time.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtkuimanager.h>

#include "rb-cut-and-paste-code.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-shell.h"
#include "rb-source.h"
#include "rb-util.h"
#include "rb-static-playlist-source.h"
#include "rb-plugin.h"

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

static char * default_get_browser_key (RBSource *source);
static GList *default_get_property_views (RBSource *source);
static gboolean default_can_rename (RBSource *source);
static gboolean default_can_search (RBSource *source);
static GList *default_copy (RBSource *source);
static void default_reset_filters (RBSource *source);
static gboolean default_try_playlist (RBSource *source);
static RBSourceEOFType default_handle_eos (RBSource *source);
static gboolean default_show_popup  (RBSource *source);
static void default_delete_thyself (RBSource *source);
static RBEntryView *default_get_entry_view (RBSource *source);
static void default_activate (RBSource *source);
static void default_deactivate (RBSource *source);
static void default_add_to_queue (RBSource *source, RBSource *queue);
static void default_get_status (RBSource *source, char **text, char **progress_text, float *progress);
static void default_move_to_trash (RBSource *source);
static GList * default_get_ui_actions (RBSource *source);
static GList * default_get_search_actions (RBSource *source);

static void rb_source_post_entry_deleted_cb (GtkTreeModel *model,
					     RhythmDBEntry *entry,
					     RBSource *source);
static void rb_source_row_inserted_cb (GtkTreeModel *model,
				       GtkTreePath *path,
				       GtkTreeIter *iter,
				       RBSource *source);
G_DEFINE_ABSTRACT_TYPE (RBSource, rb_source, GTK_TYPE_HBOX)
#define RB_SOURCE_GET_PRIVATE(object)(G_TYPE_INSTANCE_GET_PRIVATE ((object), RB_TYPE_SOURCE, RBSourcePrivate))

struct _RBSourcePrivate
{
	char *name;

	RBShell *shell;
	gboolean visible;
	RhythmDBQueryModel *query_model;
	GdkPixbuf *pixbuf;
	RBSourceListGroup sourcelist_group;
	guint hidden_when_empty : 1;
	guint update_visibility_id;
	guint update_status_id;
	RhythmDBEntryType entry_type;
	RBPlugin *plugin;
};

enum
{
	PROP_0,
	PROP_NAME,
	PROP_ICON,
	PROP_SHELL,
	PROP_UI_MANAGER,
	PROP_VISIBLE,
	PROP_QUERY_MODEL,
	PROP_HIDDEN_WHEN_EMPTY,
	PROP_SOURCELIST_GROUP,
	PROP_ENTRY_TYPE,
	PROP_PLUGIN
};

enum
{
	STATUS_CHANGED,
	FILTER_CHANGED,
	DELETED,
	LAST_SIGNAL
};

static guint rb_source_signals[LAST_SIGNAL] = { 0 };

static void
rb_source_class_init (RBSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_source_dispose;
	object_class->finalize = rb_source_finalize;

	object_class->set_property = rb_source_set_property;
	object_class->get_property = rb_source_get_property;

	klass->impl_can_browse = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_get_browser_key = default_get_browser_key;
	klass->impl_browser_toggled = NULL;
	klass->impl_get_property_views = default_get_property_views;
	klass->impl_can_rename = default_can_rename;
	klass->impl_can_search = default_can_search;
	klass->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_can_paste = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_can_delete = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_can_copy = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_can_add_to_queue = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	klass->impl_get_entry_view = default_get_entry_view;
	klass->impl_copy = default_copy;
	klass->impl_reset_filters = default_reset_filters;
	klass->impl_handle_eos = default_handle_eos;
	klass->impl_get_config_widget = NULL;
	klass->impl_receive_drag = NULL;
	klass->impl_show_popup = default_show_popup;
	klass->impl_delete_thyself = default_delete_thyself;
	klass->impl_activate = default_activate;
	klass->impl_deactivate = default_deactivate;
	klass->impl_try_playlist = default_try_playlist;
	klass->impl_add_to_queue = default_add_to_queue;
	klass->impl_get_status = default_get_status;
	klass->impl_get_ui_actions = default_get_ui_actions;
	klass->impl_get_search_actions = default_get_search_actions;
	klass->impl_move_to_trash = default_move_to_trash;

	g_object_class_install_property (object_class,
					 PROP_NAME,
					 g_param_spec_string ("name",
							      "UI name",
							      "Interface name",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_ICON,
					 g_param_spec_object ("icon",
							      "Icon",
							      "Source Icon",
							      GDK_TYPE_PIXBUF,
							      G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							       "RBShell",
							       "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							       "GtkUIManager",
							       "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READABLE));

	g_object_class_install_property (object_class,
					 PROP_VISIBLE,
					 /* FIXME: This property could probably
					  * be named better, there's already
					  * a GtkWidget 'visible' property,
					  * since RBSource derives from
					  * GtkWidget, this can be confusing
					  */
					 g_param_spec_boolean ("visibility",
							       "visibility",
							       "Whether the source should be displayed in the source list",
							       TRUE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_HIDDEN_WHEN_EMPTY,
					 g_param_spec_boolean ("hidden-when-empty",
							       "hidden-when-empty",
							       "Whether the source should be displayed in the source list when it is empty",
							       FALSE,
							       G_PARAM_READWRITE));

	g_object_class_install_property (object_class,
					 PROP_QUERY_MODEL,
					 g_param_spec_object ("query-model",
						 	      "RhythmDBQueryModel",
							      "RhythmDBQueryModel object",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_SOURCELIST_GROUP,
					 g_param_spec_enum ("sourcelist-group",
						 	    "sourcelist group",
							    "sourcelist group",
							    RB_TYPE_SOURCELIST_GROUP,
							    RB_SOURCELIST_GROUP_FIXED,
							    G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_boxed ("entry-type",
							     "Entry type",
							     "Type of the entries which should be displayed by this source",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
						 	      "RBPlugin",
							      "RBPlugin instance for the plugin that created the source",
							      RB_TYPE_PLUGIN,
							      G_PARAM_READWRITE));

	rb_source_signals[DELETED] =
		g_signal_new ("deleted",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	rb_source_signals[STATUS_CHANGED] =
		g_signal_new ("status_changed",
			      RB_TYPE_SOURCE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSourceClass, status_changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	/*
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
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (object_class, sizeof (RBSourcePrivate));
}

static void
rb_source_init (RBSource *source)
{
	RB_SOURCE_GET_PRIVATE (source)->visible = TRUE;
}

static void
rb_source_dispose (GObject *object)
{
	RBSource *source;
	RBSourcePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);
	priv = RB_SOURCE_GET_PRIVATE (source);
	g_return_if_fail (priv != NULL);

	rb_debug ("Disposing source %p: '%s'", source, priv->name);

	if (priv->update_visibility_id != 0) {
		g_source_remove (priv->update_visibility_id);
		priv->update_visibility_id = 0;
	}
	if (priv->update_status_id != 0) {
		g_source_remove (priv->update_status_id);
		priv->update_status_id = 0;
	}

	G_OBJECT_CLASS (rb_source_parent_class)->dispose (object);
}

static void
rb_source_finalize (GObject *object)
{
	RBSource *source;
	RBSourcePrivate *priv;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SOURCE (object));

	source = RB_SOURCE (object);
	priv = RB_SOURCE_GET_PRIVATE (source);
	g_return_if_fail (priv != NULL);

	rb_debug ("Finalizing source %p: '%s'", source, priv->name);

	if (priv->query_model != NULL) {
		rb_debug ("Unreffing model %p count: %d", priv->query_model, G_OBJECT (priv->query_model)->ref_count);
		g_object_unref (priv->query_model);
	}

	if (priv->pixbuf != NULL) {
		g_object_unref (priv->pixbuf);
	}

	g_free (priv->name);

	G_OBJECT_CLASS (rb_source_parent_class)->finalize (object);
}

void
rb_source_set_pixbuf (RBSource  *source,
		      GdkPixbuf *pixbuf)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	g_return_if_fail (RB_IS_SOURCE (source));

	if (priv->pixbuf != NULL) {
		g_object_unref (priv->pixbuf);
	}

	priv->pixbuf = pixbuf;

	if (priv->pixbuf != NULL) {
		g_object_ref (priv->pixbuf);
	}
}

static gboolean
update_visibility_idle (RBSource *source)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);
	gboolean visibility;

	GDK_THREADS_ENTER ();

	gint count = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (priv->query_model), NULL);

	visibility = (count > 0);

	if (visibility != priv->visible) {
		g_object_set (G_OBJECT (source), "visibility", visibility, NULL);
	}

	priv->update_visibility_id = 0;
	GDK_THREADS_LEAVE ();
	return FALSE;
}

static void
queue_update_visibility (RBSource *source)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (priv->update_visibility_id != 0) {
		g_source_remove (priv->update_visibility_id);
	}
	priv->update_visibility_id = g_idle_add ((GSourceFunc) update_visibility_idle, source);
}

void
rb_source_set_hidden_when_empty (RBSource *source,
				 gboolean  hidden)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	g_return_if_fail (RB_IS_SOURCE (source));

	if (priv->hidden_when_empty != hidden) {
		priv->hidden_when_empty = hidden;
		queue_update_visibility (source);
	}
}

static void
rb_source_set_query_model_internal (RBSource *source,
				    RhythmDBQueryModel *model)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (priv->query_model == model) {
		return;
	}

	if (priv->query_model != NULL) {
		g_signal_handlers_disconnect_by_func (priv->query_model,
						      G_CALLBACK (rb_source_post_entry_deleted_cb),
						      source);
		g_signal_handlers_disconnect_by_func (priv->query_model,
						      G_CALLBACK (rb_source_row_inserted_cb),
						      source);
		g_object_unref (priv->query_model);
	}

	priv->query_model = model;
	if (priv->query_model != NULL) {
		g_object_ref (priv->query_model);
		g_signal_connect_object (model, "post-entry-delete",
					 G_CALLBACK (rb_source_post_entry_deleted_cb),
					 source, 0);
		g_signal_connect_object (model, "row_inserted",
					 G_CALLBACK (rb_source_row_inserted_cb),
					 source, 0);
	}

	/* g_object_notify (G_OBJECT (source), "query-model"); */
	rb_source_notify_status_changed (source);
}

static void
rb_source_set_property (GObject *object,
			guint prop_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (object);
	RBSource *source = RB_SOURCE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_free (priv->name);
		priv->name = g_strdup (g_value_get_string (value));
		break;
	case PROP_ICON:
		rb_source_set_pixbuf (source, g_value_get_object (value));
		break;
	case PROP_SHELL:
		priv->shell = g_value_get_object (value);
		break;
	case PROP_VISIBLE:
		priv->visible = g_value_get_boolean (value);
		rb_debug ("Setting %s visibility to %u",
			  priv->name,
			  priv->visible);
		break;
	case PROP_HIDDEN_WHEN_EMPTY:
		rb_source_set_hidden_when_empty (source, g_value_get_boolean (value));
		break;
	case PROP_QUERY_MODEL:
		rb_source_set_query_model_internal (source, g_value_get_object (value));
		break;
	case PROP_SOURCELIST_GROUP:
		priv->sourcelist_group = g_value_get_enum (value);
		break;
	case PROP_ENTRY_TYPE:
		priv->entry_type = g_value_get_boxed (value);
		break;
	case PROP_PLUGIN:
		priv->plugin = g_value_get_object (value);
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
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_NAME:
		g_value_set_string (value, priv->name);
		break;
	case PROP_ICON:
		g_value_set_object (value, priv->pixbuf);
		break;
	case PROP_SHELL:
		g_value_set_object (value, priv->shell);
		break;
	case PROP_VISIBLE:
		g_value_set_boolean (value, priv->visible);
		break;
	case PROP_UI_MANAGER:
	{
		GtkUIManager *manager;
		g_object_get (priv->shell,
			      "ui-manager", &manager,
			      NULL);
		g_value_set_object (value, manager);
		g_object_unref (manager);
		break;
	}
	case PROP_QUERY_MODEL:
		g_value_set_object (value, priv->query_model);
		break;
	case PROP_SOURCELIST_GROUP:
		g_value_set_enum (value, priv->sourcelist_group);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_boxed (value, priv->entry_type);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, priv->plugin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
default_get_status (RBSource *source,
		    char **text,
		    char **progress_text,
		    float *progress)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	/* hack to get these strings marked for translation */
	if (0) {
		ngettext ("%d song", "%d songs", 0);
	}

	if (priv->query_model) {
		*text = rhythmdb_query_model_compute_status_normal (priv->query_model,
								    "%d song",
								    "%d songs");
		if (rhythmdb_query_model_has_pending_changes (priv->query_model))
			*progress = -1.0f;
	} else {
		*text = g_strdup ("");
	}
}

/**
 * rb_source_get_status:
 * @status: a #RBSource
 * @text: holds the returned status text (allocated)
 * @progress_text: holds the returned text for the progress bar (allocated)
 * @progress: holds the progress value
 *
 * Returns the details to display in the status bar for the source.
 * If the progress value returned is less than zero, the progress bar
 * will pulse.  If the progress value is greater than or equal to 1,
 * the progress bar will be hidden.
 **/
void
rb_source_get_status (RBSource *source,
		      char **text,
		      char **progress_text,
		      float *progress)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_get_status (source, text, progress_text, progress);
}

static char *
default_get_browser_key (RBSource *source)
{
	return NULL;
}

char *
rb_source_get_browser_key (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_browser_key (source);
}

gboolean
rb_source_can_browse (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_browse (source);
}

void
rb_source_browser_toggled (RBSource *source,
			   gboolean enabled)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	if (klass->impl_browser_toggled != NULL)
		klass->impl_browser_toggled (source, enabled);
}

void
rb_source_notify_status_changed (RBSource *source)
{
	g_signal_emit (G_OBJECT (source), rb_source_signals[STATUS_CHANGED], 0);
}

void
rb_source_notify_filter_changed (RBSource *source)
{
	g_signal_emit (G_OBJECT (source), rb_source_signals[FILTER_CHANGED], 0);
}

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

RBEntryView *
rb_source_get_entry_view (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_entry_view (source);
}

static GList *
default_get_property_views (RBSource *source)
{
	return NULL;
}

GList *
rb_source_get_property_views (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_property_views (source);
}

static gboolean
default_can_rename (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_can_rename (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (rb_shell_get_party_mode (priv->shell)) {
		return FALSE;
	}

	return klass->impl_can_rename (source);
}

static gboolean
default_can_search (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_can_search (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_search (source);
}

void
rb_source_search (RBSource *source,
		  const char *text)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	/* several sources don't have a search ability */
	if (klass->impl_search != NULL)
		klass->impl_search (source, text);
}

GtkWidget *
rb_source_get_config_widget (RBSource *source,
			     RBShellPreferences *prefs)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	if (klass->impl_get_config_widget) {
		return klass->impl_get_config_widget (source, prefs);
	} else {
		return NULL;
	}
}

gboolean
rb_source_can_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_cut (source);
}

gboolean
rb_source_can_paste (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_paste (source);
}

gboolean
rb_source_can_delete (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (rb_shell_get_party_mode (priv->shell)) {
		return FALSE;
	}

	return klass->impl_can_delete (source);
}

gboolean
rb_source_can_move_to_trash (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (rb_shell_get_party_mode (priv->shell)) {
		return FALSE;
	}

	return klass->impl_can_move_to_trash (source);
}

gboolean
rb_source_can_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_copy (source);
}

GList *
rb_source_cut (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_cut (source);
}

static GList *
default_copy (RBSource *source)
{
	return rb_entry_view_get_selected_entries (rb_source_get_entry_view (source));
}

GList *
rb_source_copy (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_copy (source);
}

void
rb_source_paste (RBSource *source, GList *nodes)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_paste (source, nodes);
}

gboolean
rb_source_can_add_to_queue (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	return klass->impl_can_add_to_queue (source);
}

static void
default_add_to_queue (RBSource *source,
		      RBSource *queue)
{
	RBEntryView *songs = rb_source_get_entry_view (source);
	GList *selection = rb_entry_view_get_selected_entries (songs);
	GList *iter;

	if (selection == NULL)
		return;

	for (iter = selection; iter; iter = iter->next) {
		rb_static_playlist_source_add_entry (RB_STATIC_PLAYLIST_SOURCE (queue),
						     (RhythmDBEntry *)iter->data, -1);
	}

	g_list_foreach (selection, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (selection);
}

void
rb_source_add_to_queue (RBSource *source,
			RBSource *queue)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	klass->impl_add_to_queue (source, queue);
}

void
rb_source_delete (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_delete (source);
}

static void
default_move_to_trash (RBSource *source)
{
	GList *sel, *tem;
	RBEntryView *entry_view;
	RhythmDB *db;
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	g_object_get (priv->shell, "db", &db, NULL);

	entry_view = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (entry_view);

	for (tem = sel; tem != NULL; tem = tem->next) {
		rhythmdb_entry_move_to_trash (db, (RhythmDBEntry *)tem->data);
		rhythmdb_commit (db);
	}

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
	g_object_unref (db);
}

void
rb_source_move_to_trash (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_move_to_trash (source);
}

static void
default_reset_filters (RBSource *source)
{
	rb_debug ("no implementation of reset_filters for this source");
}

void
rb_source_reset_filters (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_reset_filters (source);
}

gboolean
rb_source_can_show_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return (klass->impl_song_properties != NULL);
}

void
rb_source_song_properties (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	g_assert (klass->impl_song_properties);
	klass->impl_song_properties (source);
}

gboolean
rb_source_try_playlist (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_try_playlist (source);
}

guint
rb_source_want_uri (RBSource *source, const char *uri)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	if (klass->impl_want_uri)
		return klass->impl_want_uri (source, uri);
	return 0;
}

gboolean
rb_source_add_uri (RBSource *source, const char *uri, const char *title, const char *genre)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);
	if (klass->impl_add_uri)
		return klass->impl_add_uri (source, uri, title, genre);
	return FALSE;
}

gboolean
rb_source_can_pause (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_can_pause (source);
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

RBSourceEOFType
rb_source_handle_eos (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_handle_eos (source);
}

gboolean
rb_source_receive_drag (RBSource *source,
			GtkSelectionData *data)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	if (klass->impl_receive_drag)
		return klass->impl_receive_drag (source, data);
	else
		return FALSE;
}

void
_rb_source_show_popup (RBSource *source, const char *ui_path)
{
	GtkUIManager *uimanager;

	g_object_get (RB_SOURCE_GET_PRIVATE (source)->shell,
		      "ui-manager", &uimanager, NULL);
	rb_gtk_action_popup_menu (uimanager, ui_path);
	g_object_unref (uimanager);

}

static gboolean
default_show_popup  (RBSource *source)
{
	return FALSE;
}

gboolean
rb_source_show_popup (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_show_popup (source);
}

static void
default_delete_thyself (RBSource *source)
{
}

void
rb_source_delete_thyself (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_delete_thyself (source);
	g_signal_emit (G_OBJECT (source), rb_source_signals[DELETED], 0);
}

static RBEntryView*
default_get_entry_view (RBSource *source)
{
	return NULL;
}

static void default_activate (RBSource *source)
{
	return;
}

static void default_deactivate (RBSource *source)
{
	return;
}

void rb_source_activate (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_activate (source);

	return;
}

void rb_source_deactivate (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	klass->impl_deactivate (source);

	return;
}

static GList *
default_get_ui_actions (RBSource *source)
{
	return NULL;
}

GList *
rb_source_get_ui_actions (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_ui_actions (source);
}

static GList *
default_get_search_actions (RBSource *source)
{
	return NULL;
}

GList *
rb_source_get_search_actions (RBSource *source)
{
	RBSourceClass *klass = RB_SOURCE_GET_CLASS (source);

	return klass->impl_get_search_actions (source);
}

static gboolean
_update_status_idle (RBSource *source)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	rb_source_notify_status_changed (source);

	if (priv->hidden_when_empty)
		update_visibility_idle (source);

	priv->update_status_id = 0;
	return FALSE;
}

static void
rb_source_row_inserted_cb (GtkTreeModel *model,
			   GtkTreePath *path,
			   GtkTreeIter *iter,
			   RBSource *source)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (priv->update_status_id == 0)
		priv->update_status_id = g_idle_add ((GSourceFunc)_update_status_idle, source);
}

static void
rb_source_post_entry_deleted_cb (GtkTreeModel *model,
				 RhythmDBEntry *entry,
				 RBSource *source)
{
	RBSourcePrivate *priv = RB_SOURCE_GET_PRIVATE (source);

	if (priv->update_status_id == 0)
		priv->update_status_id = g_idle_add ((GSourceFunc)_update_status_idle, source);
}

static void
rb_source_gather_hash_keys (char *key,
			    gpointer unused,
			    GList **data)
{
	*data = g_list_prepend (*data, key);
}

GList *
rb_source_gather_selected_properties (RBSource *source,
				      RhythmDBPropType prop)
{
	GList *selected, *tem;
	GHashTable *selected_set;

	selected_set = g_hash_table_new (g_str_hash, g_str_equal);
	selected = rb_entry_view_get_selected_entries (rb_source_get_entry_view (RB_SOURCE (source)));

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

GtkActionGroup *
_rb_source_register_action_group (RBSource *source,
				  const char *group_name,
				  GtkActionEntry *actions,
				  int num_actions,
				  gpointer user_data)
{
	GtkUIManager *uimanager;
	GList *actiongroups;
	GList *i;
	GtkActionGroup *group;

	g_return_val_if_fail (group_name != NULL, NULL);

	g_object_get (source, "ui-manager", &uimanager, NULL);
	actiongroups = gtk_ui_manager_get_action_groups (uimanager);

	/* Don't create the action group if it's already registered */
	for (i = actiongroups; i != NULL; i = i->next) {
		const char *name;

		name = gtk_action_group_get_name (GTK_ACTION_GROUP (i->data));
		if (name != NULL && strcmp (name, group_name) == 0) {
			group = GTK_ACTION_GROUP (i->data);
			/* Add a reference */
			g_object_ref (group);
			goto out;
		}
	}

	group = gtk_action_group_new (group_name);
	gtk_action_group_set_translation_domain (group,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_actions (group,
				      actions, num_actions,
				      user_data);
	gtk_ui_manager_insert_action_group (uimanager, group, 0);

 out:
	g_object_unref (uimanager);

	return group;
}

/* This should really be standard. */
#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_source_eof_type_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCE_EOF_ERROR, "Display error when playing entry ends"),
			ENUM_ENTRY (RB_SOURCE_EOF_STOP, "Stop playback when playing entry ends"),
			ENUM_ENTRY (RB_SOURCE_EOF_RETRY, "Restart playing when playing entry ends"),
			ENUM_ENTRY (RB_SOURCE_EOF_NEXT, "Start next entry when playing entry ends"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourceEOFType", values);
	}

	return etype;
}

GType
rb_sourcelist_group_get_type (void)
{
	static GType etype = 0;

	if (etype == 0) {
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_SOURCELIST_GROUP_FIXED, "Fixed single instance source"),
			ENUM_ENTRY (RB_SOURCELIST_GROUP_PERSISTANT, "Persistant multiple-instance source"),
			ENUM_ENTRY (RB_SOURCELIST_GROUP_REMOVABLE, "Source representing a removable device"),
			ENUM_ENTRY (RB_SOURCELIST_GROUP_TRANSIENT, "Transient source (eg. network shares)"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBSourcelistGroupType", values);
	}

	return etype;
}
