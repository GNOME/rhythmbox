/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 *  Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-library-browser.h"
#include "rhythmdb-property-model.h"
#include "rhythmdb-query-model.h"
#include "rb-property-view.h"
#include "rb-debug.h"
#include "rb-util.h"

static void rb_library_browser_class_init (RBLibraryBrowserClass *klass);
static void rb_library_browser_init (RBLibraryBrowser *entry);
static void rb_library_browser_finalize (GObject *object);
static void rb_library_browser_dispose (GObject *object);
static void rb_library_browser_constructed (GObject *object);
static void rb_library_browser_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_library_browser_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);

static void view_property_selected_cb (RBPropertyView *view,
				       GList *selection,
				       RBLibraryBrowser *widget);
static void view_selection_reset_cb (RBPropertyView *view,
				     RBLibraryBrowser *widget);

static void update_browser_views_visibility (RBLibraryBrowser *widget);

typedef struct _RBLibraryBrowserRebuildData RBLibraryBrowserRebuildData;

static void destroy_idle_rebuild_model (RBLibraryBrowserRebuildData *data);

G_DEFINE_TYPE (RBLibraryBrowser, rb_library_browser, GTK_TYPE_BOX)
#define RB_LIBRARY_BROWSER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserPrivate))

/**
 * SECTION:rblibrarybrowser
 * @short_description: album/artist/genre browser widget
 * @include: rb-library-browser.h
 *
 * This widget contains a set of #RBPropertyView<!-- -->s backed by
 * #RhythmDBPropertyModel<!-- -->s and constructs a chain of
 * #RhythmDBQueryModel<!-- -->s to perform filtering of the entries
 * in a source.
 *
 * It operates on an input query model, containing the full set of
 * entries that may be displayed in the source, and produces an
 * output query model containing those entries that match the current
 * selection.
 *
 * When the selection in any of the property views changes, or when
 * #rb_library_browser_reset or #rb_library_browser_set_selection are
 * called to manipulate the selection, the query chain is rebuilt
 * asynchronously to update the property views.
 */

struct _RBLibraryBrowserRebuildData
{
	RBLibraryBrowser *widget;
	int rebuild_prop_index;
	int rebuild_idle_id;
};

typedef struct
{
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
	RhythmDBQueryModel *input_model;
	RhythmDBQueryModel *output_model;

	GSList *browser_views_group;
	char *browser_views;

	GHashTable *property_views;
	GHashTable *selections;

	RBLibraryBrowserRebuildData *rebuild_data;
} RBLibraryBrowserPrivate;

enum
{
	PROP_0,
	PROP_DB,
	PROP_INPUT_MODEL,
	PROP_OUTPUT_MODEL,
	PROP_ENTRY_TYPE,
	PROP_BROWSER_VIEWS
};

typedef struct {
	RhythmDBPropType type;
	const char *name;
} BrowserPropertyInfo;

static BrowserPropertyInfo browser_properties[] = {
	{RHYTHMDB_PROP_GENRE, N_("Genre")},
	{RHYTHMDB_PROP_ARTIST, N_("Artist")},
	{RHYTHMDB_PROP_ALBUM, N_("Album")}
};
const int num_browser_properties = G_N_ELEMENTS (browser_properties);

static void
rb_library_browser_class_init (RBLibraryBrowserClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = rb_library_browser_finalize;
	object_class->dispose = rb_library_browser_dispose;
	object_class->constructed = rb_library_browser_constructed;
	object_class->set_property = rb_library_browser_set_property;
	object_class->get_property = rb_library_browser_get_property;

	/**
	 * RBLibraryBrowser:db:
	 *
	 * #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "RhythmDB instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBLibraryBrowser:input-model:
	 *
	 * This #RhythmDBQueryModel defines the set of entries that
	 * the browser filters.  This property is not writeable.
	 * To set a new input query model, use 
	 * #rb_library_browser_set_model.
	 */
	g_object_class_install_property (object_class,
					 PROP_INPUT_MODEL,
					 g_param_spec_object ("input-model",
							      "input-model",
							      "input RhythmDBQueryModel instance",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READABLE));
	/**
	 * RBLibraryBrowser:output-model:
	 *
	 * This #RhythmDBQueryModel contains the filtered set of
	 * entries.  It is a subset of the entries contained in the
	 * input model.  This should be used as the model backing
	 * the source's entry view.
	 *
	 * Sources using this widget should connect to the notify
	 * signal for this property, updating their entry view when
	 * it changes.
	 */
	g_object_class_install_property (object_class,
					 PROP_OUTPUT_MODEL,
					 g_param_spec_object ("output-model",
							      "output-model",
							      "output RhythmDBQueryModel instance",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READABLE));
	/**
	 * RBLibraryBrowser:entry-type:
	 *
	 * The type of entries to use in the browser.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_TYPE,
					 g_param_spec_object ("entry-type",
							      "Entry type",
							      "Type of entry to display in this browser",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBLibraryBrowser:browser-views:
	 *
	 * The set of browsers to display.
	 */
	g_object_class_install_property (object_class,
					 PROP_BROWSER_VIEWS,
					 g_param_spec_string ("browser-views",
							      "browser views",
							      "browser view selection",
							      "artists-albums",
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));

	g_type_class_add_private (klass, sizeof (RBLibraryBrowserPrivate));
}

static void
rb_library_browser_init (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	gtk_box_set_spacing (GTK_BOX (widget), 1);
	gtk_orientable_set_orientation (GTK_ORIENTABLE (widget), GTK_ORIENTATION_HORIZONTAL);

	priv->property_views = g_hash_table_new (g_direct_hash, g_direct_equal);
	priv->selections = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rb_list_deep_free);
}

static void
rb_library_browser_constructed (GObject *object)
{
	RBLibraryBrowser *browser;
	RBLibraryBrowserPrivate *priv;
	int i;

	RB_CHAIN_GOBJECT_METHOD (rb_library_browser_parent_class, constructed, object);

	browser = RB_LIBRARY_BROWSER (object);
	priv = RB_LIBRARY_BROWSER_GET_PRIVATE (browser);

	for (i = 0; i < num_browser_properties; i++) {
		RBPropertyView *view;

		view = rb_property_view_new (priv->db,
					     browser_properties[i].type,
					     _(browser_properties[i].name));
		g_hash_table_insert (priv->property_views, (gpointer)(browser_properties[i].type), view);

		rb_property_view_set_selection_mode (view, GTK_SELECTION_MULTIPLE);
		g_signal_connect_object (G_OBJECT (view),
					 "properties-selected",
					 G_CALLBACK (view_property_selected_cb),
					 browser, 0);
		g_signal_connect_object (G_OBJECT (view),
					 "property-selection-reset",
					 G_CALLBACK (view_selection_reset_cb),
					 browser, 0);
		gtk_widget_show_all (GTK_WIDGET (view));
		gtk_widget_set_no_show_all (GTK_WIDGET (view), TRUE);
		gtk_box_pack_start (GTK_BOX (browser), GTK_WIDGET (view), TRUE, TRUE, 0);
	}

	update_browser_views_visibility (browser);
}

static void
rb_library_browser_dispose (GObject *object)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);
	
	if (priv->rebuild_data != NULL) {
		/* this looks a bit odd, but removing the idle handler cleans up the
		 * data too.
		 */
		guint id = priv->rebuild_data->rebuild_idle_id;
		priv->rebuild_data = NULL;
		g_source_remove (id);
	}
	
	if (priv->db != NULL) {
		g_object_unref (priv->db);
		priv->db = NULL;
	}

	if (priv->input_model != NULL) {
		g_object_unref (priv->input_model);
		priv->input_model = NULL;
	}

	if (priv->output_model != NULL) {
		g_object_unref (priv->output_model);
		priv->output_model = NULL;
	}

	G_OBJECT_CLASS (rb_library_browser_parent_class)->dispose (object);
}

static void
rb_library_browser_finalize (GObject *object)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);

	g_hash_table_destroy (priv->property_views);
	g_hash_table_destroy (priv->selections);
	g_free (priv->browser_views);

	G_OBJECT_CLASS (rb_library_browser_parent_class)->finalize (object);
}

static void
rb_library_browser_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_DB:
		if (priv->db != NULL) {
			g_object_unref (priv->db);
		}
		priv->db = g_value_get_object (value);

		if (priv->db != NULL) {
			g_object_ref (priv->db);
		}
		break;
	case PROP_ENTRY_TYPE:
		priv->entry_type = g_value_get_object (value);
		break;
	case PROP_BROWSER_VIEWS:
		g_free (priv->browser_views);
		priv->browser_views = g_value_dup_string (value);
		update_browser_views_visibility (RB_LIBRARY_BROWSER (object));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_browser_get_property (GObject *object,
				 guint prop_id,
				 GValue *value,
				 GParamSpec *pspec)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_DB:
		g_value_set_object (value, priv->db);
		break;
	case PROP_INPUT_MODEL:
		g_value_set_object (value, priv->input_model);
		break;
	case PROP_OUTPUT_MODEL:
		g_value_set_object (value, priv->output_model);
		break;
	case PROP_ENTRY_TYPE:
		g_value_set_object (value, priv->entry_type);
		break;
	case PROP_BROWSER_VIEWS:
		g_value_set_string (value, priv->browser_views);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_library_browser_new:
 * @db: the #RhythmDB instance
 * @entry_type: the entry type to use in the browser
 *
 * Creates a new library browser.
 *
 * Return value: a new RBLibraryBrowser
 */
RBLibraryBrowser *
rb_library_browser_new (RhythmDB *db,
			RhythmDBEntryType *entry_type)
{
	RBLibraryBrowser *widget;

	g_assert (db);
	widget = RB_LIBRARY_BROWSER (g_object_new (RB_TYPE_LIBRARY_BROWSER,
						   "db", db,
						   "entry-type", entry_type,
						   NULL));
	return widget;
}

static void
update_browser_property_visibilty (RhythmDBPropType prop,
				   RBPropertyView *view,
				   GList *properties)
{
	gboolean old_vis, new_vis;

	old_vis = gtk_widget_get_visible (GTK_WIDGET (view));
	new_vis = (g_list_find (properties, (gpointer)prop) != NULL);

	if (old_vis != new_vis) {
		if (new_vis) {
			gtk_widget_show (GTK_WIDGET (view));
		} else {
			gtk_widget_hide (GTK_WIDGET (view));
			rb_property_view_set_selection (view, NULL);
		}
	}
}

static void
update_browser_views_visibility (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GList *properties = NULL;

	if (strstr (priv->browser_views, "albums") != NULL)
		properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_ALBUM);
	properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_ARTIST);
	if (strstr (priv->browser_views, "genres") != NULL)
		properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_GENRE);

	g_hash_table_foreach (priv->property_views, (GHFunc)update_browser_property_visibilty, properties);
	g_list_free (properties);
}

static void
view_property_selected_cb (RBPropertyView *view,
			   GList *selection,
			   RBLibraryBrowser *widget)
{
	RhythmDBPropType prop;

	g_object_get (G_OBJECT (view), "prop", &prop, NULL);
	rb_library_browser_set_selection (widget, prop, selection);
}
static void
view_selection_reset_cb (RBPropertyView *view,
			 RBLibraryBrowser *widget)
{
	RhythmDBPropType prop;

	g_object_get (G_OBJECT (view), "prop", &prop, NULL);
	rb_library_browser_set_selection (widget, prop, NULL);
}

static void
reset_view_cb (RhythmDBPropType prop,
	       RBPropertyView *view,
	       RBLibraryBrowser *widget)
{
	rb_property_view_set_selection (view, NULL);
}

/**
 * rb_library_browser_reset:
 * @widget: a #RBLibraryBrowser
 *
 * Clears all selections in the browser.
 *
 * Return value: TRUE if anything was changed
 */
gboolean
rb_library_browser_reset (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	if (rb_library_browser_has_selection (widget)) {
		g_hash_table_foreach (priv->property_views, (GHFunc)reset_view_cb, widget);
		return TRUE;
	} else {
		return FALSE;
	}
}

typedef struct {
	RBLibraryBrowser *widget;
	RhythmDB *db;
	RhythmDBQuery *query;
} ConstructQueryData;

static void
construct_query_cb (RhythmDBPropType type,
		    GList *selections,
		    ConstructQueryData *data)
{
	rhythmdb_query_append_prop_multiple (data->db,
					     data->query,
					     type,
					     selections);
}

/**
 * rb_library_browser_construct_query:
 * @widget: a #RBLibraryBrowser
 *
 * Constructs a #RhythmDBQuery from the current selections in the browser.
 *
 * Return value: (transfer full): a #RhythmDBQuery constructed from the current selection.
 */
RhythmDBQuery *
rb_library_browser_construct_query (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RhythmDBQuery *query;
	ConstructQueryData *data;

	query = g_ptr_array_new ();
	data = g_new0 (ConstructQueryData, 1);
	data->widget = widget;
	data->db = priv->db;
	data->query = query;

	g_hash_table_foreach (priv->selections, (GHFunc)construct_query_cb, data);
	g_free (data);

	return query;
}

/**
 * rb_library_browser_has_selection:
 * @widget: a #RBLibraryBrowser
 *
 * Determines whether the browser has an active selection.
 *
 * Return value: TRUE if any items in the browser are selected.
 */
gboolean
rb_library_browser_has_selection (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	return (g_hash_table_size (priv->selections) > 0);
}

static gint
prop_to_index (RhythmDBPropType type)
{
	int i;

	for (i = 0; i < num_browser_properties; i++)
		if (browser_properties[i].type == type)
			return i;

	return -1;
}

static void
ignore_selection_changes (RBLibraryBrowser *widget,
			  RBPropertyView *view,
			  gboolean block)
{
	if (block) {
		g_signal_handlers_block_by_func (view, view_selection_reset_cb, widget);
		g_signal_handlers_block_by_func (view, view_property_selected_cb, widget);
	} else {
		g_signal_handlers_unblock_by_func (view, view_selection_reset_cb, widget);
		g_signal_handlers_unblock_by_func (view, view_property_selected_cb, widget);
	}
}

typedef struct {
	RBLibraryBrowser *widget;
	RBPropertyView *view;
	GList *selections;
	RhythmDBQueryModel *model;
	guint handler_id;
} SelectionRestoreData;

static void
selection_restore_data_destroy (SelectionRestoreData *data)
{
	g_object_unref (G_OBJECT (data->widget));
	g_free (data);
}

static void
query_complete_cb (RhythmDBQueryModel *model,
		   SelectionRestoreData *data)
{
	ignore_selection_changes (data->widget, data->view, FALSE);
	rb_property_view_set_selection (data->view, data->selections);

	g_signal_handler_disconnect (data->model, data->handler_id);
}

static void
restore_selection (RBLibraryBrowser *widget,
		   gint property_index,
		   gboolean query_pending)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;
	GList *selections;
	SelectionRestoreData *data;

	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index].type);
	selections = g_hash_table_lookup (priv->selections, (gpointer)browser_properties[property_index].type);

	if (query_pending) {
		g_object_ref (widget);

		data = g_new0 (SelectionRestoreData, 1);
		data->widget = widget;
		data->view = view;
		data->selections = selections;
		data->model = priv->input_model;

		data->handler_id =
			g_signal_connect_data (priv->input_model,
					       "complete",
					       G_CALLBACK (query_complete_cb),
					       data,
					       (GClosureNotify) selection_restore_data_destroy,
					       0);
	} else {
		ignore_selection_changes (widget, view, FALSE);
		rb_property_view_set_selection (view, selections);
	}
}

static void
rebuild_child_model (RBLibraryBrowser *widget,
		     gint property_index,
		     gboolean query_pending)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RhythmDBPropertyModel *prop_model;
	RhythmDBQueryModel *base_model, *child_model;
	RBPropertyView *view;
	RhythmDBQuery *query;
	GList *selections;

	g_assert (property_index >= 0);
	g_assert (property_index < num_browser_properties);

	/* get the query model for the previous property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index].type);
	prop_model = rb_property_view_get_model (view);
	g_object_get (prop_model, "query-model", &base_model, NULL);

	selections = g_hash_table_lookup (priv->selections, (gpointer)browser_properties[property_index].type);
	if (selections != NULL) {

		/* create a new query model based on it, filtered by
		 * the selections of the previous property view.
		 * we need the entry type query criteria to allow the
		 * backend to optimise the query.
		 */
		query = rhythmdb_query_parse (priv->db,
				              RHYTHMDB_QUERY_PROP_EQUALS, RHYTHMDB_PROP_TYPE, priv->entry_type,
					      RHYTHMDB_QUERY_END);
		rhythmdb_query_append_prop_multiple (priv->db,
						     query,
						     browser_properties[property_index].type,
						     selections);

		child_model = rhythmdb_query_model_new_empty (priv->db);
		if (query_pending) {
			rb_debug ("rebuilding child model for browser %d; query is pending", property_index);
			g_object_set (child_model,
				      "query", query,
				      "base-model", base_model,
				      NULL);
		} else {
			rb_debug ("rebuilding child model for browser %d; running new query", property_index);
			rhythmdb_query_model_chain (child_model, base_model, FALSE);
			rhythmdb_do_full_query_parsed (priv->db,
						       RHYTHMDB_QUERY_RESULTS (child_model),
						       query);
		}
		rhythmdb_query_free (query);
	} else {
		rb_debug ("no selection for browser %d - reusing parent model", property_index);
		child_model = g_object_ref (base_model);
	}

	/* If this is the last property, use the child model as the output model
	 * for the browser.  Otherwise, use it as the input for the next property
	 * view.
	 */
	if (property_index == num_browser_properties-1) {
		if (priv->output_model != NULL) {
			g_object_unref (priv->output_model);
		}

		priv->output_model = child_model;

		g_object_notify (G_OBJECT (widget), "output-model");

	} else {
		view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index+1].type);
		ignore_selection_changes (widget, view, TRUE);

		prop_model = rb_property_view_get_model (view);
		g_object_set (prop_model, "query-model", child_model, NULL);

		g_object_unref (child_model);

		rebuild_child_model (widget, property_index + 1, query_pending);
		restore_selection (widget, property_index + 1, query_pending);
	}

	g_object_unref (base_model);
}

static gboolean
idle_rebuild_model (RBLibraryBrowserRebuildData *data)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (data->widget);

	priv->rebuild_data = NULL;
	rebuild_child_model (data->widget, data->rebuild_prop_index, FALSE);
	return FALSE;
}

static void
destroy_idle_rebuild_model (RBLibraryBrowserRebuildData *data)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (data->widget);
	RBPropertyView *view;
	RhythmDBPropType prop_type;
	
	prop_type = browser_properties[data->rebuild_prop_index].type;
	view = g_hash_table_lookup (priv->property_views, (gpointer)prop_type);
	if (view != NULL) {
		ignore_selection_changes (data->widget, view, FALSE);
	}

	priv->rebuild_data = NULL;
	g_object_unref (data->widget);
	g_free (data);
}

/**
 * rb_library_browser_set_selection:
 * @widget: a #RBLibraryBrowser
 * @type: the property for which to set the selection
 * @selection: (element-type utf8) (transfer none): a list of strings to select
 *
 * Replaces any current selection for the specified property.
 */
void
rb_library_browser_set_selection (RBLibraryBrowser *widget,
				  RhythmDBPropType type,
				  GList *selection)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GList *old_selection;
	RBPropertyView *view;
	int rebuild_index;
	RBLibraryBrowserRebuildData *rebuild_data;

	old_selection = g_hash_table_lookup (priv->selections, (gpointer)type);

	if (rb_string_list_equal (old_selection, selection))
		return;

	if (selection)
		g_hash_table_insert (priv->selections, (gpointer)type, rb_string_list_copy (selection));
	else
		g_hash_table_remove (priv->selections, (gpointer)type);

	rebuild_index = prop_to_index (type);
	if (priv->rebuild_data != NULL) {
		rebuild_data = priv->rebuild_data;
		if (rebuild_data->rebuild_prop_index <= rebuild_index) {
			/* already rebuilding a model further up the chain,
			 * so we don't need to do anything for this one.
			 */
			return;
		}
		g_source_remove (rebuild_data->rebuild_idle_id);
		rebuild_data = NULL;
	}

	view = g_hash_table_lookup (priv->property_views, (gpointer)type);
	if (view) {
		ignore_selection_changes (widget, view, TRUE);
	}

	rebuild_data = g_new0 (RBLibraryBrowserRebuildData, 1);
	rebuild_data->widget = g_object_ref (widget);
	rebuild_data->rebuild_prop_index = rebuild_index;
	rebuild_data->rebuild_idle_id =
		g_idle_add_full (G_PRIORITY_DEFAULT_IDLE,
				 (GSourceFunc) idle_rebuild_model,
				 rebuild_data,
				 (GDestroyNotify) destroy_idle_rebuild_model);
	priv->rebuild_data = rebuild_data;
}

/**
 * rb_library_browser_get_property_views:
 * @widget: a #RBLibraryBrowser
 *
 * Retrieves the property view widgets from the browser.
 *
 * Return value: (element-type RBPropertyView) (transfer container): a #GList
 * containing the #RBPropertyView widgets in the browser.
 */
GList*
rb_library_browser_get_property_views (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	return rb_collate_hash_table_values (priv->property_views);
}

/**
 * rb_library_browser_get_property_view:
 * @widget: a #RBLibraryBrowser
 * @type: the property
 *
 * Retrieves the property view widget for the specified property,
 * if there is one.
 *
 * Return value: (transfer none): #RBPropertyView widget, or NULL
 */
RBPropertyView *
rb_library_browser_get_property_view (RBLibraryBrowser *widget,
				      RhythmDBPropType type)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;

	view = g_hash_table_lookup (priv->property_views, GINT_TO_POINTER (type));
	return view;
}

/**
 * rb_library_browser_set_model:
 * @widget: a #RBLibraryBrowser
 * @model: (transfer none): the new input #RhythmDBQueryModel
 * @query_pending: if TRUE, the caller promises to run a
 *  query to populate the input query model.
 *
 * Specifies a new input query model for the browser.
 * This should be the query model constructed from the 
 * current search text, or the basic query model for the 
 * source if there is no search text.
 */
void
rb_library_browser_set_model (RBLibraryBrowser *widget,
			      RhythmDBQueryModel *model,
			      gboolean query_pending)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;
	RhythmDBPropertyModel *prop_model;

	if (priv->input_model != NULL) {
		g_object_unref (priv->input_model);
	}

	priv->input_model = model;

	if (priv->input_model != NULL) {
		g_object_ref (priv->input_model);
	}

	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[0].type);
	ignore_selection_changes (widget, view, TRUE);

	prop_model = rb_property_view_get_model (view);
	g_object_set (prop_model, "query-model", priv->input_model, NULL);

	rebuild_child_model (widget, 0, query_pending);
	restore_selection (widget, 0, query_pending);
}
