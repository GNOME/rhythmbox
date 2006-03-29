/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of library browser widget
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 *  Copyright (C) 2006 James Livingston <jrl@ids.org.au>
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

#include <config.h>

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-library-browser.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"
#include "rhythmdb-property-model.h"
#include "rhythmdb-query-model.h"
#include "rb-property-view.h"
#include "rb-debug.h"
#include "rb-util.h"

static void rb_library_browser_class_init (RBLibraryBrowserClass *klass);
static void rb_library_browser_init (RBLibraryBrowser *entry);
static void rb_library_browser_finalize (GObject *object);
static GObject* rb_library_browser_constructor (GType type, guint n_construct_properties,
						GObjectConstructParam *construct_properties);
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
static void rb_library_browser_views_changed (GConfClient *client,
					      guint cnxn_id,
					      GConfEntry *entry,
					      RBLibraryBrowser *widget);



G_DEFINE_TYPE (RBLibraryBrowser, rb_library_browser, GTK_TYPE_HBOX)
#define RB_LIBRARY_BROWSER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_LIBRARY_BROWSER, RBLibraryBrowserPrivate))

typedef struct
{
	RhythmDB *db;
	RhythmDBQueryModel *input_model;
	RhythmDBQueryModel *output_model;

	guint browser_view_notify_id;
	GSList *browser_views_group;

	GHashTable *property_views;
	GHashTable *selections;
} RBLibraryBrowserPrivate;


enum
{
	PROP_0,
	PROP_DB,
	PROP_INPUT_MODEL,
	PROP_OUTPUT_MODEL
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
	object_class->constructor = rb_library_browser_constructor;
	object_class->set_property = rb_library_browser_set_property;
	object_class->get_property = rb_library_browser_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "RhythmDB instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_INPUT_MODEL,
					 g_param_spec_object ("input-model",
							      "input-model",
							      "input RhythmDBQueryModel instance",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_OUTPUT_MODEL,
					 g_param_spec_object ("output-model",
							      "output-model",
							      "output RhythmDBQueryModel instance",
							      RHYTHMDB_TYPE_QUERY_MODEL,
							      G_PARAM_READABLE));


	g_type_class_add_private (klass, sizeof (RBLibraryBrowserPrivate));
}

static void
rb_library_browser_init (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	gtk_box_set_spacing (GTK_BOX (widget), 5);

	priv->property_views = g_hash_table_new (g_direct_hash, g_direct_equal);
	priv->selections = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rb_list_deep_free);
}

static GObject *
rb_library_browser_constructor (GType type, guint n_construct_properties,
				GObjectConstructParam *construct_properties)
{
	RBLibraryBrowserClass *klass;
	RBLibraryBrowser *browser;
	RBLibraryBrowserPrivate *priv;
	int i;

	klass = RB_LIBRARY_BROWSER_CLASS (g_type_class_peek (RB_TYPE_LIBRARY_BROWSER));

	browser = RB_LIBRARY_BROWSER (G_OBJECT_CLASS (rb_library_browser_parent_class)->
			constructor (type, n_construct_properties, construct_properties));
	priv = RB_LIBRARY_BROWSER_GET_PRIVATE (browser);

	for (i = 0; i < num_browser_properties; i++) {
		RBPropertyView *view;

		view = rb_property_view_new (priv->db,
					     browser_properties[i].type,
					     browser_properties[i].name);
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
		gtk_box_pack_start_defaults (GTK_BOX (browser), GTK_WIDGET (view));	     
	}

	update_browser_views_visibility (browser);
	priv->browser_view_notify_id =
		eel_gconf_notification_add (CONF_UI_BROWSER_VIEWS,
				(GConfClientNotifyFunc) rb_library_browser_views_changed, browser);

	return G_OBJECT (browser);
}
static void
rb_library_browser_finalize (GObject *object)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);

	eel_gconf_notification_remove (priv->browser_view_notify_id);

	g_object_unref (G_OBJECT (priv->db));

	g_hash_table_destroy (priv->property_views);
	g_hash_table_destroy (priv->selections);

	G_OBJECT_CLASS (rb_library_browser_parent_class)->finalize (object);
}

static void
rb_library_browser_set_property (GObject *object,
				 guint prop_id,
				 const GValue *value,
				 GParamSpec *pspec)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (object);

	switch (prop_id)
	{
	case PROP_DB:
		if (priv->db) {
			g_object_unref (G_OBJECT (priv->db));
		}
		priv->db = g_value_get_object (value);

		if (priv->db)
			g_object_ref (priv->db);
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

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, priv->db);
		break;
	case PROP_INPUT_MODEL:
		g_value_set_object (value, priv->input_model);
		break;
	case PROP_OUTPUT_MODEL:
		g_value_set_object (value, priv->output_model);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBLibraryBrowser *
rb_library_browser_new (RhythmDB *db)
{
	RBLibraryBrowser *widget;

	g_assert (db);
	widget = RB_LIBRARY_BROWSER (g_object_new (RB_TYPE_LIBRARY_BROWSER,
						   "db", db,
						   NULL));
	return widget;
}

static void
update_browser_property_visibilty (RhythmDBPropType prop, RBPropertyView *view, GList *properties)
{
	gboolean old_vis, new_vis;
	
	old_vis = GTK_WIDGET_VISIBLE (view);
	new_vis = (g_list_find (properties, (gpointer)prop) != NULL);

	if (old_vis != new_vis) {
		if (new_vis) {
			gtk_widget_show (GTK_WIDGET (view));
		} else {
			gtk_widget_hide (GTK_WIDGET (view));
			rb_property_view_reset (view);
		}
	}
}

static void
update_browser_views_visibility (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GList *properties = NULL;

	{
		int views = eel_gconf_get_integer (CONF_UI_BROWSER_VIEWS);

		if (views == 0 || views == 2)
			properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_ALBUM);
		properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_ARTIST);
		if (views == 1 || views == 2)
			properties = g_list_prepend (properties, (gpointer)RHYTHMDB_PROP_GENRE);
	}

	g_hash_table_foreach (priv->property_views, (GHFunc)update_browser_property_visibilty, properties);
}

static void
rb_library_browser_views_changed (GConfClient *client,
			          guint cnxn_id,
			          GConfEntry *entry,
			          RBLibraryBrowser *widget)
{
	update_browser_views_visibility (widget);
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
view_selection_reset_cb (RBPropertyView *view, RBLibraryBrowser *widget)
{
	RhythmDBPropType prop;

	g_object_get (G_OBJECT (view), "prop", &prop, NULL);
	rb_library_browser_set_selection (widget, prop, NULL);
}

static void
reset_view_cb (RhythmDBPropType prop, RBPropertyView *view, RBLibraryBrowser *widget)
{
	rb_property_view_set_selection (view, NULL);
}

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
	GPtrArray *query;
} ConstructQueryData;

static void
construct_query_cb (RhythmDBPropType type, GList *selections, ConstructQueryData *data)
{
	rhythmdb_query_append_prop_multiple (data->db,
					     data->query,
					     type,
					     selections);
}

GPtrArray*
rb_library_browser_construct_query (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GPtrArray *query;
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
ignore_selection_changes (RBLibraryBrowser *widget, RBPropertyView *view, gboolean block)
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
		g_object_ref (G_OBJECT (widget));

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
rebuild_output_model (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;
	RhythmDBPropertyModel *prop_model;
	int prop_index = num_browser_properties - 1;
	GList *selections;
	RhythmDBQueryModel *base_model;
	GPtrArray *query;

	if (priv->output_model)
		g_object_unref (G_OBJECT (priv->output_model));

	/* get the query model for the previous property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[prop_index].type);
	prop_model = rb_property_view_get_model (view);
	g_object_get (G_OBJECT (prop_model), "query-model", &base_model, NULL);
	g_object_unref (G_OBJECT (prop_model));

	selections = g_hash_table_lookup (priv->selections, (gpointer)browser_properties[prop_index].type);
	if (selections != NULL) {
		query = g_ptr_array_new ();
		rhythmdb_query_append_prop_multiple (priv->db,
						     query,
						     browser_properties[prop_index].type,
						     selections);

		priv->output_model = rhythmdb_query_model_new_empty (priv->db);
		g_object_set (G_OBJECT (priv->output_model), "base-model", base_model, NULL);


		g_object_set (G_OBJECT (priv->output_model), "query", query, NULL);
		rhythmdb_query_model_reapply_query (priv->output_model, TRUE);
		rhythmdb_query_free (query);
	} else {
		priv->output_model = base_model;
		g_object_ref (G_OBJECT (priv->output_model));
	}

	g_object_notify (G_OBJECT (widget), "output-model");
	g_object_unref (G_OBJECT (base_model));
}

static void
rebuild_child_model (RBLibraryBrowser *widget, gint property_index, gboolean query_pending)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RhythmDBPropertyModel *prop_model;
	RhythmDBQueryModel *base_model, *child_model;
	RBPropertyView *view;
	GPtrArray *query;
	GList *selections;

	g_assert (property_index >= 0);
	g_assert (property_index < num_browser_properties);

	/* there is no model after the last one to update*/
	if (property_index == num_browser_properties - 1) {
		rebuild_output_model (widget);
		return;
	}

	/* get the query model for the previous property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index].type);
	prop_model = rb_property_view_get_model (view);
	g_object_get (G_OBJECT (prop_model), "query-model", &base_model, NULL);
	g_object_unref (G_OBJECT (prop_model));

	/* create a new query model based on it, filtered by the selections of the previous property view */
	selections = g_hash_table_lookup (priv->selections, (gpointer)browser_properties[property_index].type);
	if (selections != NULL) {
		query = g_ptr_array_new ();
		rhythmdb_query_append_prop_multiple (priv->db,
						     query,
						     browser_properties[property_index].type,
						     selections);

		child_model = rhythmdb_query_model_new_empty (priv->db);
		g_object_set (G_OBJECT (child_model), "base-model", base_model, NULL);

		g_object_set (G_OBJECT (child_model), "query", query, NULL);
		rhythmdb_query_model_reapply_query (child_model, TRUE);
		rhythmdb_query_free (query);
	} else {
		child_model = base_model;
	}
	
	/* apply it as the query model of the current property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index+1].type);
	ignore_selection_changes (widget, view, TRUE);

	prop_model = rb_property_view_get_model (view);
	g_object_set (G_OBJECT (prop_model), "query-model", child_model, NULL);
	g_object_unref (G_OBJECT (prop_model));

	g_object_unref (G_OBJECT (base_model));
	if (child_model != base_model)
		g_object_unref (G_OBJECT (child_model));

	rebuild_child_model (widget, property_index + 1, query_pending);
	restore_selection (widget, property_index + 1, query_pending);
}

void
rb_library_browser_set_selection (RBLibraryBrowser *widget, RhythmDBPropType type, GList *selection)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GList *old_selection;
	RBPropertyView *view;
	
	old_selection = g_hash_table_lookup (priv->selections, (gpointer)type);

	if (rb_string_list_equal (old_selection, selection))
		return;

	if (selection)
		g_hash_table_insert (priv->selections, (gpointer)type, rb_string_list_copy (selection));
	else
		g_hash_table_remove (priv->selections, (gpointer)type);

	view = g_hash_table_lookup (priv->property_views, (gpointer)type);
	if (view)
		ignore_selection_changes (widget, view, TRUE);

	rebuild_child_model (widget, prop_to_index (type), FALSE);
	if (view)
		ignore_selection_changes (widget, view, FALSE);
}

GList*
rb_library_browser_get_property_views (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	return rb_collate_hash_table_values (priv->property_views);
}

void
rb_library_browser_set_model (RBLibraryBrowser *widget, 
			      RhythmDBQueryModel *model,
			      gboolean query_pending)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;
	RhythmDBPropertyModel *prop_model;

	if (priv->input_model)
		g_object_unref (G_OBJECT (priv->input_model));

	priv->input_model = model;
	g_object_ref (G_OBJECT (model));

	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[0].type);
	ignore_selection_changes (widget, view, TRUE);

	prop_model = rb_property_view_get_model (view);
	g_object_set (G_OBJECT (prop_model), "query-model", priv->input_model, NULL);
	g_object_unref (G_OBJECT (prop_model));

	rebuild_child_model (widget, 0, query_pending);
	restore_selection (widget, 0, query_pending);
}

