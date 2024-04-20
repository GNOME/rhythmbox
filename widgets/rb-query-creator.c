/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
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
#include <string.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "rb-query-creator.h"
#include "rb-query-creator-private.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-builder-helpers.h"
#include "rb-util.h"

static void rb_query_creator_class_init (RBQueryCreatorClass *klass);
static void rb_query_creator_constructed (GObject *object);
static void rb_query_creator_dispose (GObject *object);
static void rb_query_creator_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_query_creator_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void select_criteria_from_value (RBQueryCreator *creator,
					GtkWidget *option_menu,
					RhythmDBPropType prop,
					RhythmDBQueryType qtype);
static GtkWidget * create_property_option_menu (RBQueryCreator *creator,
						const RBQueryCreatorPropertyOption *options,
						int length);
static GtkWidget * create_criteria_option_menu (const RBQueryCreatorCriteriaOption *options,
						int length);
static void setup_sort_option_menu (RBQueryCreator *creator,
				    GtkWidget *option_menu,
				    const RBQueryCreatorSortOption *options,
				    int length);

static GtkWidget * append_row (RBQueryCreator *creator);
static void add_button_click_cb (GtkWidget *button, RBQueryCreator *creator);
static void remove_button_click_cb (GtkWidget *button, RBQueryCreator *creator);
static void limit_toggled_cb (GtkWidget *limit, RBQueryCreator *creator);

static int get_property_index_from_proptype (const RBQueryCreatorPropertyOption *options,
			    	  int length, RhythmDBPropType prop);
static void sort_option_menu_changed (GtkComboBox *propmenu, RBQueryCreator *creator);

typedef struct
{
	RhythmDB *db;

	gboolean creating;

	GtkSizeGroup *property_size_group;
	GtkSizeGroup *criteria_size_group;
	GtkSizeGroup *entry_size_group;
	GtkSizeGroup *button_size_group;

	GtkBox *vbox;
	GList *rows;

	GtkWidget *addbutton;
	GtkWidget *disjunction_check;
	GtkWidget *limit_check;
	GtkWidget *limit_entry;
	GtkWidget *limit_option;
	GtkWidget *sort_label;
	GtkWidget *sort_menu;
	GtkWidget *sort_desc;
} RBQueryCreatorPrivate;

G_DEFINE_TYPE (RBQueryCreator, rb_query_creator, GTK_TYPE_DIALOG)
#define QUERY_CREATOR_GET_PRIVATE(o)  (G_TYPE_INSTANCE_GET_PRIVATE ((o), rb_query_creator_get_type(), RBQueryCreatorPrivate))

/**
 * SECTION:rbquerycreator
 * @short_description: database query creator widget
 *
 * The query creator is used to create and edit automatic playlists.
 * It is only capable of constructing queries that consist of a flat
 * list of criteria.  It cannot nested criteria or represent full 
 * boolean logic expressions.
 *
 * In addition to query criteria, the query creator also allows the user
 * to specify limits on the size of the result set, in terms of the number
 * of entries, the total duration, or the total file size; and also the
 * order in which the results are to be sorted.
 *
 * The structure of the query creator is defined in the builder file
 * create-playlist.ui.
 */

enum
{
	PROP_0,
	PROP_DB,
	PROP_CREATING,
};

static void
rb_query_creator_class_init (RBQueryCreatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_query_creator_dispose;
	object_class->constructed = rb_query_creator_constructed;
	object_class->set_property = rb_query_creator_set_property;
	object_class->get_property = rb_query_creator_get_property;

	/**
	 * RBQueryCreator:db:
	 *
	 * The #RhythmDB instance
	 */
	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBQueryCreator:creating:
	 *
	 * TRUE if a new playlist is being created.
	 */
	g_object_class_install_property (object_class,
					 PROP_CREATING,
					 g_param_spec_boolean ("creating",
							       "creating",
							       "Whether or not we're creating a new playlist",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBQueryCreatorPrivate));
}

static void
rb_query_creator_init (RBQueryCreator *creator)
{

}

static void
rb_query_creator_constructed (GObject *object)
{
	RBQueryCreatorPrivate *priv;
	RBQueryCreator *creator;
	GtkWidget *mainbox;
	GtkWidget *content_area;
	GtkBuilder *builder;

	RB_CHAIN_GOBJECT_METHOD (rb_query_creator_parent_class, constructed, object);

	creator = RB_QUERY_CREATOR (object);
	priv = QUERY_CREATOR_GET_PRIVATE (creator);

	if (priv->creating) {
		gtk_dialog_add_button (GTK_DIALOG (creator),
				       _("_Cancel"),
				       GTK_RESPONSE_CLOSE);
		gtk_dialog_add_button (GTK_DIALOG (creator),
				       _("_New"),
				       GTK_RESPONSE_OK);
	} else {
		gtk_dialog_add_button (GTK_DIALOG (creator),
				       _("_Close"),
				       GTK_RESPONSE_CLOSE);
	}
	gtk_dialog_set_default_response (GTK_DIALOG (creator),
					 GTK_RESPONSE_CLOSE);

	priv->property_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->criteria_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->entry_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	priv->button_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	if (priv->creating)
		gtk_window_set_title (GTK_WINDOW (creator), _("Create Automatic Playlist"));
	else
		gtk_window_set_title (GTK_WINDOW (creator), _("Edit Automatic Playlist"));

	content_area = gtk_dialog_get_content_area (GTK_DIALOG (creator));

	gtk_container_set_border_width (GTK_CONTAINER (creator), 5);
	gtk_box_set_spacing (GTK_BOX (content_area), 2);

	builder = rb_builder_load ("create-playlist.ui", creator);

	priv->disjunction_check = GTK_WIDGET (gtk_builder_get_object (builder, "disjunctionCheck"));
	priv->limit_check = GTK_WIDGET (gtk_builder_get_object (builder, "limitCheck"));
	priv->limit_entry = GTK_WIDGET (gtk_builder_get_object (builder, "limitEntry"));
	priv->limit_option = GTK_WIDGET (gtk_builder_get_object (builder, "limitOption"));
	priv->addbutton = GTK_WIDGET (gtk_builder_get_object (builder, "addButton"));
	priv->sort_label = GTK_WIDGET (gtk_builder_get_object (builder, "sortLabel"));
	priv->sort_menu = GTK_WIDGET (gtk_builder_get_object (builder, "sortMenu"));
	priv->sort_desc = GTK_WIDGET (gtk_builder_get_object (builder, "sortDesc"));

	gtk_combo_box_set_active (GTK_COMBO_BOX (priv->limit_option), 0);

	g_signal_connect_object (G_OBJECT (priv->limit_check), "toggled", G_CALLBACK (limit_toggled_cb),
				 creator, 0);
	limit_toggled_cb (priv->limit_check, creator);

	gtk_size_group_add_widget (priv->button_size_group, priv->addbutton);
	g_signal_connect_object (G_OBJECT (priv->addbutton), "clicked", G_CALLBACK (add_button_click_cb),
				 creator, 0);

	setup_sort_option_menu (creator, priv->sort_menu, sort_options, num_sort_options);

	priv->vbox = GTK_BOX (gtk_builder_get_object (builder, "sub_vbox"));
	if (priv->creating)
		append_row (creator);

	mainbox = GTK_WIDGET (gtk_builder_get_object (builder, "complex-playlist-creator"));
	gtk_box_pack_start (GTK_BOX (content_area), mainbox, FALSE, FALSE, 0);
	gtk_widget_show_all (GTK_WIDGET (creator));

	g_object_unref (builder);
}

static void
rb_query_creator_dispose (GObject *object)
{
	RBQueryCreatorPrivate *priv;

	g_return_if_fail (RB_IS_QUERY_CREATOR (object));

	priv = QUERY_CREATOR_GET_PRIVATE (object);
	g_return_if_fail (priv != NULL);

	if (priv->property_size_group != NULL) {
		g_object_unref (priv->property_size_group);
		priv->property_size_group = NULL;
	}

	if (priv->criteria_size_group != NULL) {
		g_object_unref (priv->criteria_size_group);
		priv->criteria_size_group = NULL;
	}
	if (priv->entry_size_group != NULL) {
		g_object_unref (priv->entry_size_group);
		priv->entry_size_group = NULL;
	}

	if (priv->button_size_group != NULL) {
		g_object_unref (priv->button_size_group);
		priv->button_size_group = NULL;
	}

	if (priv->rows) {
		g_list_free (priv->rows);
		priv->rows = NULL;
	}

	G_OBJECT_CLASS (rb_query_creator_parent_class)->dispose (object);
}

static void
rb_query_creator_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	RBQueryCreator *creator = RB_QUERY_CREATOR (object);
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);

	switch (prop_id)
	{
	case PROP_DB:
		priv->db = g_value_get_object (value);
		break;
	case PROP_CREATING:
		priv->creating = g_value_get_boolean (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_query_creator_get_property (GObject *object,
                               guint prop_id,
                               GValue *value,
                               GParamSpec *pspec)
{
	RBQueryCreator *creator = RB_QUERY_CREATOR (object);
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, priv->db);
		break;
	case PROP_CREATING:
		g_value_set_boolean (value, priv->creating);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_query_creator_new:
 * @db: the #RhythmDB instance
 *
 * Creates a new query creator widget.
 *
 * Return value: new query creator widget
 */
GtkWidget *
rb_query_creator_new (RhythmDB *db)
{
	return g_object_new (RB_TYPE_QUERY_CREATOR, "db", db, NULL);
}

static gboolean
rb_query_creator_load_query (RBQueryCreator *creator,
                             GPtrArray *query,
			     RhythmDBQueryModelLimitType limit_type,
                             GVariant *limit_value)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	int i;
	GList *rows;
	gboolean disjunction = FALSE;
	RhythmDBQueryData *qdata;
	GPtrArray *subquery;
	guint64 limit;

	g_return_val_if_fail (query->len == 2, FALSE);

	qdata = g_ptr_array_index (query, 1);
	g_return_val_if_fail (qdata->type == RHYTHMDB_QUERY_SUBQUERY, FALSE);

	subquery = qdata->subquery;

	if (subquery->len > 0) {
		for (i = 0; i < subquery->len; i++) {
			RhythmDBQueryData *data = g_ptr_array_index (subquery, i);
			if (data->type != RHYTHMDB_QUERY_DISJUNCTION)
				append_row (creator);
		}
	}

	rows = priv->rows;

	for (i = 0; i < subquery->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (subquery, i);
		GtkComboBox *propmenu;
		GtkWidget *criteria_menu;
		int index;
		const RBQueryCreatorPropertyType *property_type;

		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {
			disjunction = TRUE;
			continue;
		}

		propmenu = GTK_COMBO_BOX (get_box_widget_at_pos (GTK_BOX (rows->data), 0));
		index = get_property_index_from_proptype (property_options, num_property_options, data->propid);
		gtk_combo_box_set_active (propmenu, index);

		criteria_menu = get_box_widget_at_pos (GTK_BOX (rows->data), 1);
		select_criteria_from_value (creator, criteria_menu, data->propid, data->type);

		property_type = property_options[index].property_type;
		g_assert (property_type->criteria_set_widget_data != NULL);
		property_type->criteria_set_widget_data (get_box_widget_at_pos (GTK_BOX (rows->data), 2),
					      data->val);

		rows = rows->next;
	}

	/* setup the limits */
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->disjunction_check),
				      disjunction);
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->limit_check),
				      limit_type != RHYTHMDB_QUERY_MODEL_LIMIT_NONE);

	switch (limit_type) {
	case RHYTHMDB_QUERY_MODEL_LIMIT_NONE:
		limit = 0;
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_COUNT:
		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->limit_option), 0);
		limit = g_variant_get_uint64 (limit_value);
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_TIME:
		gtk_combo_box_set_active (GTK_COMBO_BOX (priv->limit_option), 3);
		/* convert to minutes */
		limit = g_variant_get_uint64 (limit_value) / 60;
		break;

	case RHYTHMDB_QUERY_MODEL_LIMIT_SIZE:
		limit = g_variant_get_uint64 (limit_value);

		if (limit % 1000 == 0) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (priv->limit_option), 2);
			limit /= 1000;
		} else {
			gtk_combo_box_set_active (GTK_COMBO_BOX (priv->limit_option), 1);
		}

		break;
	default:
		g_assert_not_reached ();
	}

	gtk_spin_button_set_value (GTK_SPIN_BUTTON (priv->limit_entry), limit);

	return TRUE;
}

static gboolean
rb_query_creator_set_sorting (RBQueryCreator *creator,
                              const char *sort_column,
                              gint sort_direction)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	int i;

	if (!sort_column || ! *sort_column) {
		g_warning("No playlist sorting order");

		sort_column = sort_options[DEFAULT_SORTING_COLUMN].sort_key;
		sort_direction = DEFAULT_SORTING_ORDER;
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (priv->sort_desc), (sort_direction == GTK_SORT_DESCENDING));

	for (i = 0; i < num_sort_options; i++)
		if (strcmp (sort_options[i].sort_key, sort_column) == 0)
			break;

	/* check that it is a valid sort option */
	g_return_val_if_fail (i < num_sort_options, FALSE);

	gtk_combo_box_set_active (GTK_COMBO_BOX (priv->sort_menu), i);
	sort_option_menu_changed (GTK_COMBO_BOX (priv->sort_menu), creator); /* force the checkbox to change label */

	return TRUE;
}

/**
 * rb_query_creator_new_from_query:
 * @db: the #RhythmDB instance
 * @query: an existing query to start from
 * @limit_type: the type of result set limit
 * @limit_value: the result set limit value
 * @sort_column: the column on which to sort query results
 * @sort_direction: the direction in which to sort query results
 *
 * Constructs a new query creator with an existing query and limit and sort
 * settings.
 *
 * Return value: new query creator widget
 */
GtkWidget *
rb_query_creator_new_from_query (RhythmDB *db,
                                 GPtrArray *query,
				 RhythmDBQueryModelLimitType limit_type,
                                 GVariant *limit_value,
				 const char *sort_column,
                                 gint sort_direction)
{
	RBQueryCreator *creator = g_object_new (RB_TYPE_QUERY_CREATOR, "db", db,
						"creating", FALSE, NULL);
	if (!creator)
		return NULL;

	if ( !rb_query_creator_load_query (creator, query, limit_type, limit_value)
	   | !rb_query_creator_set_sorting (creator, sort_column, sort_direction)) {
		gtk_widget_destroy (GTK_WIDGET (creator));
		return NULL;
	}

	return GTK_WIDGET (creator);
}

/**
 * get_box_widget_at_pos:
 * @box: #GtkBox to extract child from
 * @pos: index of the child to retrieve
 *
 * Extracts a child widget from a #GtkBox.
 *
 * Return value: (transfer none): child widget from the box
 */
GtkWidget *
get_box_widget_at_pos (GtkBox *box, guint pos)
{
	GtkWidget *ret = NULL;
	GList *children = gtk_container_get_children (GTK_CONTAINER (box));
	GList *tem;
	for (tem = children; tem; tem = tem->next) {
		GValue thispos = { 0, };
		g_value_init (&thispos, G_TYPE_INT);
		gtk_container_child_get_property (GTK_CONTAINER (box),
						  GTK_WIDGET (tem->data),
						  "position", &thispos);
		if (g_value_get_int (&thispos) == pos) {
			ret = tem->data;
			break;
		}
	}
	g_list_free (children);
	return GTK_WIDGET (ret);
}

static GtkWidget *
get_entry_for_property (RBQueryCreator *creator,
                        RhythmDBPropType prop,
			gboolean *constrain)
{
	const RBQueryCreatorPropertyType *property_type;
	int index = get_property_index_from_proptype (property_options, num_property_options, prop);

	property_type = property_options[index].property_type;
	g_assert (property_type->criteria_create_widget != NULL);

	*constrain = TRUE;
	return property_type->criteria_create_widget (constrain);
}

/**
 * rb_query_creator_get_query:
 * @creator: #RBQueryCreator instance
 *
 * Constructs a database query that represents the criteria in the query creator.
 *
 * Return value: (transfer full): database query array
 */
GPtrArray *
rb_query_creator_get_query (RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv;
	GPtrArray *query;
	GPtrArray *sub_query;
	GList *rows, *row;
	gboolean disjunction;

	g_return_val_if_fail (RB_IS_QUERY_CREATOR (creator), NULL);

	priv = QUERY_CREATOR_GET_PRIVATE (creator);

	disjunction = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->disjunction_check));
	sub_query = g_ptr_array_new ();
	rows = priv->rows;

	for (row = rows; row; row = row->next) {
		GtkComboBox *propmenu = GTK_COMBO_BOX (get_box_widget_at_pos (GTK_BOX (row->data),
										  0));
		GtkComboBox *criteria_menu = GTK_COMBO_BOX (get_box_widget_at_pos (GTK_BOX (row->data),
										       1));
		guint prop_position = gtk_combo_box_get_active (propmenu);
		const RBQueryCreatorPropertyOption *prop_option = &property_options[prop_position];
		const RBQueryCreatorCriteriaOption *criteria_options = prop_option->property_type->criteria_options;
		const RBQueryCreatorCriteriaOption *criteria_option = &criteria_options[gtk_combo_box_get_active (criteria_menu)];

		g_assert (prop_option->property_type->criteria_get_widget_data != NULL);
		{
			RhythmDBQueryData *data = g_new0 (RhythmDBQueryData, 1);
			GValue *val = g_new0 (GValue, 1);

			data->type = criteria_option->val;
			data->propid = criteria_option->strict ? prop_option->strict_val : prop_option->fuzzy_val;

			prop_option->property_type->criteria_get_widget_data (get_box_widget_at_pos (GTK_BOX (row->data), 2), val);
			data->val = val;

			g_ptr_array_add (sub_query, data);
			}

			if (disjunction && row->next)
				rhythmdb_query_append (priv->db,
						       sub_query,
						       RHYTHMDB_QUERY_DISJUNCTION,
						       RHYTHMDB_QUERY_END);
		}
	query = rhythmdb_query_parse (priv->db,
				      /* type=songs */
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_SONG,
				      /* the constructed query */
                                      RHYTHMDB_QUERY_SUBQUERY,
                                      sub_query,
                                      RHYTHMDB_QUERY_END);
	return query;
}

/**
 * rb_query_creator_get_limit:
 * @creator: #RBQueryCreator instance
 * @type: (out): used to return the limit type
 * @limit: (out): used to return the limit value
 *
 * Retrieves the limit type and value from the query creator.
 */
void
rb_query_creator_get_limit (RBQueryCreator *creator,
			    RhythmDBQueryModelLimitType *type,
                            GVariant **limit)
{
	RBQueryCreatorPrivate *priv;

	g_return_if_fail (RB_IS_QUERY_CREATOR (creator));

	priv = QUERY_CREATOR_GET_PRIVATE (creator);

	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->limit_check))) {
		guint64 l;
		l = gtk_spin_button_get_value(GTK_SPIN_BUTTON (priv->limit_entry));

		switch (gtk_combo_box_get_active (GTK_COMBO_BOX (priv->limit_option))) {
		case 0:
			*type = RHYTHMDB_QUERY_MODEL_LIMIT_COUNT;
			*limit = g_variant_new_uint64 (l);
			break;
		case 1:
			*type = RHYTHMDB_QUERY_MODEL_LIMIT_SIZE;
			*limit = g_variant_new_uint64 (l);
			break;

		case 2:
			*type = RHYTHMDB_QUERY_MODEL_LIMIT_SIZE;
			*limit = g_variant_new_uint64 (l * 1000);
			break;

		case 3:
			*type = RHYTHMDB_QUERY_MODEL_LIMIT_TIME;
			*limit = g_variant_new_uint64 (l * 60);
			break;

		default:
			g_assert_not_reached ();
		}
	} else {
		*type = RHYTHMDB_QUERY_MODEL_LIMIT_NONE;
		*limit = NULL;
	}
}

/**
 * rb_query_creator_get_sort_order:
 * @creator: #RBQueryCreator instance
 * @sort_key: (out) (allow-none): returns the sort key name
 * @sort_direction: (out) (allow-none): returns the sort direction
 *
 * Retrieves the sort settings from the query creator.
 * The sort direction is returned as a #GtkSortType value.
 */
void
rb_query_creator_get_sort_order (RBQueryCreator *creator,
                                 const char **sort_key,
                                 gint *sort_direction)
{
	RBQueryCreatorPrivate *priv;

	g_return_if_fail (RB_IS_QUERY_CREATOR (creator));

	priv = QUERY_CREATOR_GET_PRIVATE (creator);

	if (sort_direction != NULL) {
		if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (priv->sort_desc)))
			*sort_direction = GTK_SORT_DESCENDING;
		else
			*sort_direction = GTK_SORT_ASCENDING;
	}

	if (sort_key != NULL) {
		int i;
		i = gtk_combo_box_get_active (GTK_COMBO_BOX (priv->sort_menu));
		*sort_key = sort_options[i].sort_key;
	}
}

static void
limit_toggled_cb (GtkWidget *limit,
                  RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (limit));

	gtk_widget_set_sensitive (priv->limit_entry, active);
	gtk_widget_set_sensitive (priv->limit_option, active);
	gtk_widget_set_sensitive (priv->sort_menu, active);
	gtk_widget_set_sensitive (priv->sort_label, active);
	gtk_widget_set_sensitive (priv->sort_desc, active);
}

static GtkWidget *
lookup_row_by_widget (RBQueryCreator *creator,
                      GtkWidget *widget)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	GList *rows = priv->rows;
	GList *row;
	GtkWidget *ret = NULL;
	guint i;

	for (row = rows, i = 0; row; row = row->next, i++) {
		GList *columns = gtk_container_get_children (GTK_CONTAINER (row->data));
		gboolean found = g_list_find (columns, widget) != NULL;
		g_list_free (columns);
		if (found) {
			ret = row->data;
			break;
		}
	}
	return ret;
}

static void
remove_button_click_cb (GtkWidget *button,
                        RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	GtkWidget *row;

	row = lookup_row_by_widget (creator, button);
	g_assert (row);
	gtk_container_remove (GTK_CONTAINER (priv->vbox),
			      GTK_WIDGET (row));
	priv->rows = g_list_remove (priv->rows, row);
}

static void
add_button_click_cb (GtkWidget *button,
                     RBQueryCreator *creator)
{
	append_row (creator);
}

static GtkWidget *
append_row (RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	GtkWidget *option;
	GtkWidget *criteria;
	GtkWidget *entry;
	GtkWidget *remove_button;
	GtkBox *hbox;
	gboolean constrain;

	hbox = GTK_BOX (gtk_box_new (GTK_ORIENTATION_HORIZONTAL, 5));
	gtk_box_pack_start (GTK_BOX (priv->vbox), GTK_WIDGET (hbox), TRUE, TRUE, 0);
	priv->rows = g_list_prepend (priv->rows, hbox);
	gtk_box_reorder_child (priv->vbox, GTK_WIDGET (hbox), -1);

	/* This is the main (leftmost) GtkComboBox, for types. */
	option = create_property_option_menu (creator, property_options, num_property_options);
	gtk_size_group_add_widget (priv->property_size_group, option);
	gtk_box_pack_start (hbox, GTK_WIDGET (option), TRUE, TRUE, 0);
	gtk_combo_box_set_active (GTK_COMBO_BOX (option), 0);
	criteria = create_criteria_option_menu (property_options[0].property_type->criteria_options,
						property_options[0].property_type->num_criteria_options);
	gtk_size_group_add_widget (priv->criteria_size_group, criteria);
	gtk_box_pack_start (hbox, GTK_WIDGET (criteria), TRUE, TRUE, 0);

	entry = get_entry_for_property (creator, property_options[0].strict_val, &constrain);
	if (constrain)
		gtk_size_group_add_widget (priv->entry_size_group, entry);
	gtk_box_pack_start (hbox, GTK_WIDGET (entry), TRUE, TRUE, 0);

	remove_button = gtk_button_new_with_label (_("Remove"));
	g_signal_connect_object (G_OBJECT (remove_button), "clicked", G_CALLBACK (remove_button_click_cb),
				 creator, 0);
	gtk_size_group_add_widget (priv->button_size_group, remove_button);
	gtk_box_pack_start (hbox, GTK_WIDGET (remove_button), TRUE, TRUE, 0);

	gtk_widget_show_all (GTK_WIDGET (priv->vbox));
	return GTK_WIDGET (hbox);
}

static int
get_property_index_from_proptype (const RBQueryCreatorPropertyOption *options,
                                  int length,
                                  RhythmDBPropType prop)
{
	int i;

	for (i = 0; i < length; i++)
		if (prop == options[i].strict_val || prop == options[i].fuzzy_val)
			return i;

	g_assert_not_reached ();
}

static void
select_criteria_from_value (RBQueryCreator *creator,
			    GtkWidget *option_menu,
			    RhythmDBPropType prop,
			    RhythmDBQueryType qtype)
{
	int i;
	const RBQueryCreatorCriteriaOption *options;
	guint length;

	i = get_property_index_from_proptype (property_options, num_property_options, prop);
	length = property_options[i].property_type->num_criteria_options;
	options =  property_options[i].property_type->criteria_options;

	for (i = 0; i < length; i++) {
		if (qtype == options[i].val) {
			gtk_combo_box_set_active (GTK_COMBO_BOX (option_menu), i);
			return;
		}
	}
	g_assert_not_reached ();
}

static void
property_option_menu_changed (GtkComboBox *propmenu,
			      RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	GtkWidget *row;
	GtkWidget *criteria;
	GtkWidget *entry;
	const RBQueryCreatorPropertyOption *prop_option;
	const RBQueryCreatorCriteriaOption *criteria_options;
	guint length;
	guint old_value;
	gboolean constrain;

	prop_option = &property_options[gtk_combo_box_get_active (propmenu)];
	old_value = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (propmenu), "prop-menu old-value"));

	/* don't recreate the criteria menu and entry if they will be the same*/
	if (prop_option->property_type == property_options[old_value].property_type)
		return;

	g_object_set_data (G_OBJECT (propmenu), "prop-menu old-value",
			   GINT_TO_POINTER (gtk_combo_box_get_active (propmenu)));

	row = lookup_row_by_widget (creator, GTK_WIDGET (propmenu));

	criteria = get_box_widget_at_pos (GTK_BOX (row), 1);
	gtk_container_remove (GTK_CONTAINER (row), criteria);

	criteria_options = prop_option->property_type->criteria_options;
	length = prop_option->property_type->num_criteria_options;

	criteria = create_criteria_option_menu (criteria_options, length);
	gtk_widget_show (criteria);
	gtk_size_group_add_widget (priv->criteria_size_group, criteria);
	gtk_box_pack_start (GTK_BOX (row), GTK_WIDGET (criteria), TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (row), criteria, 1);

	entry = get_box_widget_at_pos (GTK_BOX (row), 2);
	gtk_container_remove (GTK_CONTAINER (row), entry);
	entry = get_entry_for_property (creator, prop_option->strict_val,
					&constrain);
	gtk_widget_show (entry);

	if (constrain)
		gtk_size_group_add_widget (priv->entry_size_group, entry);
	gtk_box_pack_start (GTK_BOX (row), GTK_WIDGET (entry), TRUE, TRUE, 0);
	gtk_box_reorder_child (GTK_BOX (row), entry, 2);
}

static GtkWidget*
create_property_option_menu (RBQueryCreator *creator,
                             const RBQueryCreatorPropertyOption *options,
			     int length)
{
	GtkWidget *combo;
	int i;

	combo = gtk_combo_box_text_new ();
	for (i = 0; i < length; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), g_dpgettext2 (NULL, "query-criteria", options[i].name));
	}

	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);
	
	g_object_set_data (G_OBJECT (combo), "prop-menu old value", GINT_TO_POINTER (0));

	g_signal_connect_object (G_OBJECT (combo), "changed",
				 G_CALLBACK (property_option_menu_changed), creator, 0);

	return combo;
}

static GtkWidget*
create_criteria_option_menu (const RBQueryCreatorCriteriaOption *options,
			     int length)
{
	GtkWidget *combo;
	int i;

	combo = gtk_combo_box_text_new ();
	for (i = 0; i < length; i++) {
		gtk_combo_box_text_append_text (GTK_COMBO_BOX_TEXT (combo), _(options[i].name));
	}
	gtk_combo_box_set_active (GTK_COMBO_BOX (combo), 0);

	return combo;
}

static void
sort_option_menu_changed (GtkComboBox *propmenu,
			  RBQueryCreator *creator)
{
	RBQueryCreatorPrivate *priv = QUERY_CREATOR_GET_PRIVATE (creator);
	int index = gtk_combo_box_get_active (propmenu);

	gtk_button_set_label (GTK_BUTTON (priv->sort_desc), _(sort_options[index].sort_descending_name));
	rb_debug("changing descending label to %s[%d]", sort_options[index].sort_descending_name, index);
}

static void
setup_sort_option_menu (RBQueryCreator *creator,
			GtkWidget *option_menu,
			const RBQueryCreatorSortOption *options,
			int length)
{
	GtkListStore *store;
	int i;

	store = gtk_list_store_new (1, G_TYPE_STRING);

	for (i = 0; i < length; i++) {
		GtkTreeIter iter;

		gtk_list_store_append (store, &iter);
		gtk_list_store_set (store, &iter, 0, g_dpgettext2 (NULL, "query-sort", options[i].name), -1);
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (option_menu), GTK_TREE_MODEL (store));

	g_signal_connect_object (G_OBJECT (option_menu), "changed",
				 G_CALLBACK (sort_option_menu_changed), creator, 0);
	gtk_combo_box_set_active (GTK_COMBO_BOX (option_menu), 0);
}
