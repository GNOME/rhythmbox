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
static void rb_library_browser_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_library_browser_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);

static void rb_library_browser_notify_changed (RBLibraryBrowser *widget);
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
	RhythmDBQueryModel *model;

	guint browser_view_notify_id;
	GSList *browser_views_group;

	GHashTable *property_views;
	GHashTable *selections;
} RBLibraryBrowserPrivate;


enum
{
	CHANGED,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_DB
};

static guint rb_library_browser_signals[LAST_SIGNAL] = { 0 };

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
	object_class->set_property = rb_library_browser_set_property;
	object_class->get_property = rb_library_browser_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "db",
							      "RhythmDB instance",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_library_browser_signals[CHANGED] =
		g_signal_new ("changed",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryBrowserClass, changed),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);

	g_type_class_add_private (klass, sizeof (RBLibraryBrowserPrivate));
}

static void
rb_library_browser_init (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	int i;

	gtk_box_set_spacing (GTK_BOX (widget), 5);

	priv->property_views = g_hash_table_new (g_direct_hash, g_direct_equal);
	priv->selections = g_hash_table_new_full (g_direct_hash, g_direct_equal, NULL, (GDestroyNotify)rb_list_deep_free);

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
					 widget, 0);
		g_signal_connect_object (G_OBJECT (view),
					 "property-selection-reset",
					 G_CALLBACK (view_selection_reset_cb),
					 widget, 0);
		gtk_widget_show_all (GTK_WIDGET (view));
		gtk_widget_set_no_show_all (GTK_WIDGET (view), TRUE);
		gtk_box_pack_start_defaults (GTK_BOX (widget), GTK_WIDGET (view));	     
	}

	update_browser_views_visibility (widget);
	priv->browser_view_notify_id =
		eel_gconf_notification_add (CONF_UI_BROWSER_VIEWS,
				    (GConfClientNotifyFunc) rb_library_browser_views_changed, widget);
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBLibraryBrowser *
rb_library_browser_new (RhythmDB *db)
{
	RBLibraryBrowser *widget;

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
rb_library_browser_notify_changed (RBLibraryBrowser *widget)
{
	g_signal_emit (G_OBJECT (widget), rb_library_browser_signals[CHANGED], 0);
}

static void
reset_view_cb (RhythmDBPropType prop, RBPropertyView *view, RBLibraryBrowser *widget)
{
	view_selection_reset_cb (view, widget);
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
rebuild_child_model (RBLibraryBrowser *widget, gint property_index)
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
	if (property_index == num_browser_properties - 1)
		return;
			
	/* get the query model for the previous property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index].type);
	prop_model = rb_property_view_get_model (view);
	g_object_get (G_OBJECT (prop_model), "query-model", &base_model, NULL);

	/* create a new query model based on it, filtered by the selections of the previous property view */
	selections = g_hash_table_lookup (priv->selections, (gpointer)browser_properties[property_index].type);
	query = g_ptr_array_new ();
	rhythmdb_query_append_prop_multiple (priv->db,
					     query,
					     browser_properties[property_index].type,
					     selections);

	child_model = rhythmdb_query_model_new_empty (priv->db);
	g_object_set (G_OBJECT (child_model), "base-model", base_model, NULL);
	g_object_unref (G_OBJECT (base_model));

	g_object_set (G_OBJECT (child_model), "query", query, NULL);
	rhythmdb_query_model_reapply_query (child_model, TRUE);
	rhythmdb_query_free (query);
	
	/* apply it as the query model of the current property view */
	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[property_index+1].type);
	prop_model = rb_property_view_get_model (view);
	g_object_set (G_OBJECT (prop_model), "query-model", child_model, NULL);
	g_object_unref (G_OBJECT (child_model));

	rebuild_child_model (widget, property_index + 1);
}

void
rb_library_browser_set_selection (RBLibraryBrowser *widget, RhythmDBPropType type, GList *selection)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	GList *old_selection;
	
	old_selection = g_hash_table_lookup (priv->selections, (gpointer)type);

	if (rb_string_list_equal (old_selection, selection))
		return;

	if (selection)
		g_hash_table_insert (priv->selections, (gpointer)type, rb_string_list_copy (selection));
	else
		g_hash_table_remove (priv->selections, (gpointer)type);

	rebuild_child_model (widget, prop_to_index (type));
	rb_library_browser_notify_changed (widget);
}

GList*
rb_library_browser_get_property_views (RBLibraryBrowser *widget)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);

	return rb_collate_hash_table_values (priv->property_views);
}

void
rb_library_browser_set_model (RBLibraryBrowser *widget, RhythmDBQueryModel *model)
{
	RBLibraryBrowserPrivate *priv = RB_LIBRARY_BROWSER_GET_PRIVATE (widget);
	RBPropertyView *view;
	RhythmDBPropertyModel *prop_model;
	
	if (priv->model)
		g_object_unref (G_OBJECT (priv->model));

	priv->model = model;
	g_object_ref (model);

	view = g_hash_table_lookup (priv->property_views, (gpointer)browser_properties[0].type);
	prop_model = rb_property_view_get_model (view);
	g_object_set (G_OBJECT (prop_model), "query-model", priv->model, NULL);

	rebuild_child_model (widget, 0);
}
