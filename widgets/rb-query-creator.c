/*
 *  arch-tag: Implementation of RhythmDB query creation dialog
 *
 *  Copyright (C) 2003, 2004 Colin Walters <walters@gnome.org>
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
#include <stdlib.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "rb-query-creator.h"
#include "rb-dialog.h"
#include "rb-rating.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "rb-glade-helpers.h"
#include "eel-gconf-extensions.h"

typedef struct
{
	const char *name;
	RhythmDBPropType strict_val;
	RhythmDBPropType fuzzy_val;
} RBQueryCreatorPropertyOption;

typedef struct
{
	const char *name;
	gboolean strict;
	RhythmDBPropType val;
} RBQueryCreatorCriteriaOption;

const RBQueryCreatorPropertyOption property_options[] =
{
	{ N_("Title"), RHYTHMDB_PROP_TITLE, RHYTHMDB_PROP_TITLE_FOLDED },
	{ N_("Artist"), RHYTHMDB_PROP_ARTIST, RHYTHMDB_PROP_ARTIST_FOLDED },
	{ N_("Album"), RHYTHMDB_PROP_ALBUM, RHYTHMDB_PROP_ALBUM_FOLDED },
	{ N_("Genre"), RHYTHMDB_PROP_GENRE, RHYTHMDB_PROP_GENRE_FOLDED },
	{ N_("Rating"), RHYTHMDB_PROP_RATING, RHYTHMDB_PROP_RATING }
};

const RBQueryCreatorCriteriaOption string_criteria_options[] =
{
	{ N_("contains"), 0, RHYTHMDB_QUERY_PROP_LIKE },
	{ N_("does not contain"), 0, RHYTHMDB_QUERY_PROP_NOT_LIKE },
	{ N_("equals"), 1, RHYTHMDB_QUERY_PROP_EQUALS }
};

const RBQueryCreatorCriteriaOption numeric_criteria_options[] =
{
	{ N_("equals"), 1, RHYTHMDB_QUERY_PROP_EQUALS },
	{ N_("greater than"), 1, RHYTHMDB_QUERY_PROP_GREATER },
	{ N_("less than"), 1, RHYTHMDB_QUERY_PROP_LESS }
};

static void rb_query_creator_class_init (RBQueryCreatorClass *klass);
static void rb_query_creator_init (RBQueryCreator *dlg);
static GObject *rb_query_creator_constructor (GType type, guint n_construct_properties,
					      GObjectConstructParam *construct_properties);
static void rb_query_creator_finalize (GObject *object);
static void rb_query_creator_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_query_creator_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void select_property_from_value (GtkWidget *option_menu,
					const RBQueryCreatorPropertyOption *options,
					int length, RhythmDBPropType prop);
static void select_criteria_from_value (RBQueryCreator *creator,
					GtkWidget *option_menu,
					RhythmDBPropType prop,
					RhythmDBQueryType qtype);

static void setup_property_option_menu (RBQueryCreator *creator,
					GtkWidget *option_menu,
					const RBQueryCreatorPropertyOption *options,
					int length, gboolean activate_first);
static void setup_criteria_option_menu (GtkWidget *option_menu,
					const RBQueryCreatorCriteriaOption *options,
					int length, gboolean activate_first);
static GtkWidget * create_property_option_menu (RBQueryCreator *creator,
						const RBQueryCreatorPropertyOption *options,
						int length, gboolean activate_first);
static GtkWidget * create_criteria_option_menu (const RBQueryCreatorCriteriaOption *options,
						int length, gboolean activate_first);
static GtkWidget * option_menu_get_active_child (GtkWidget *option_menu);
static GtkWidget * get_box_widget_at_pos (GtkBox *box, guint pos);

static GtkWidget * append_row (RBQueryCreator *dialog);
static void add_button_click_cb (GtkWidget *button, RBQueryCreator *creator);
static void remove_button_click_cb (GtkWidget *button, RBQueryCreator *creator);
static void limit_toggled_cb (GtkWidget *limit, RBQueryCreator *creator);

struct RBQueryCreatorPrivate
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
	GPtrArray *queries;
};

static GObjectClass *parent_class = NULL;

enum
{
	PROP_0,
	PROP_DB,
	PROP_CREATING,
};

GType
rb_query_creator_get_type (void)
{
	static GType rb_query_creator_type = 0;

	if (rb_query_creator_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBQueryCreatorClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_query_creator_class_init,
			NULL,
			NULL,
			sizeof (RBQueryCreator),
			0,
			(GInstanceInitFunc) rb_query_creator_init
		};
		
		rb_query_creator_type = g_type_register_static (GTK_TYPE_DIALOG,
								"RBQueryCreator",
								&our_info, 0);
	}

	return rb_query_creator_type;
}

static void
rb_query_creator_class_init (RBQueryCreatorClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_query_creator_finalize;
	object_class->constructor = rb_query_creator_constructor;
	object_class->set_property = rb_query_creator_set_property;
	object_class->get_property = rb_query_creator_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_CREATING,
					 g_param_spec_boolean ("creating",
							       "creating",
							       "Whether or not we're creating a new playlist",
							       TRUE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static GObject *
rb_query_creator_constructor (GType type, guint n_construct_properties,
			      GObjectConstructParam *construct_properties)
{
	RBQueryCreator *dlg;
	RBQueryCreatorClass *klass;
	GObjectClass *parent_class;  
	GladeXML *xml;
	GtkWidget *mainbox;
	GtkBox *hbox;
	GtkWidget *first_option;
	GtkWidget *first_criteria;
	GtkWidget *first_entry;


	klass = RB_QUERY_CREATOR_CLASS (g_type_class_peek (type));
	parent_class = G_OBJECT_CLASS (g_type_class_peek_parent (klass));
	dlg = RB_QUERY_CREATOR (parent_class->constructor (type, n_construct_properties,
							   construct_properties));
	
	if (dlg->priv->creating) { 
		gtk_dialog_add_button (GTK_DIALOG (dlg),
				       GTK_STOCK_CANCEL,
				       GTK_RESPONSE_CLOSE);
		gtk_dialog_add_button (GTK_DIALOG (dlg),
				       GTK_STOCK_NEW,
				       GTK_RESPONSE_OK);
	} else {
		gtk_dialog_add_button (GTK_DIALOG (dlg),
				       GTK_STOCK_CLOSE,
				       GTK_RESPONSE_CLOSE);
	}
	gtk_dialog_set_default_response (GTK_DIALOG (dlg),
					 GTK_RESPONSE_CLOSE);

	dlg->priv->property_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	dlg->priv->criteria_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	dlg->priv->entry_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);
	dlg->priv->button_size_group = gtk_size_group_new (GTK_SIZE_GROUP_HORIZONTAL);

	gtk_window_set_title (GTK_WINDOW (dlg), _("Create Automatic Playlist"));

	gtk_container_set_border_width (GTK_CONTAINER (dlg), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 2);
	gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

	xml = rb_glade_xml_new ("create-playlist.glade",
				"main_vbox",
				dlg);
	
	dlg->priv->disjunction_check = GTK_WIDGET (glade_xml_get_widget (xml, "disjunctionCheck"));
	dlg->priv->limit_check = GTK_WIDGET (glade_xml_get_widget (xml, "limitCheck"));
	dlg->priv->limit_entry = GTK_WIDGET (glade_xml_get_widget (xml, "limitEntry"));
	dlg->priv->limit_option = GTK_WIDGET (glade_xml_get_widget (xml, "limitOption"));

	g_signal_connect (G_OBJECT (dlg->priv->limit_check), "toggled", G_CALLBACK (limit_toggled_cb),
			  dlg);
	gtk_widget_set_sensitive (dlg->priv->limit_entry, FALSE);
	gtk_widget_set_sensitive (dlg->priv->limit_option, FALSE);

	dlg->priv->vbox = GTK_BOX (glade_xml_get_widget (xml, "sub_vbox"));
	dlg->priv->addbutton = gtk_button_new_from_stock (GTK_STOCK_ADD);
	gtk_size_group_add_widget (dlg->priv->button_size_group, dlg->priv->addbutton);
	g_signal_connect (G_OBJECT (dlg->priv->addbutton), "clicked", G_CALLBACK (add_button_click_cb),
			  dlg);
	first_option = create_property_option_menu (dlg, property_options,
						    G_N_ELEMENTS (property_options),
						    FALSE);
	gtk_size_group_add_widget (dlg->priv->property_size_group, first_option);
	first_criteria = create_criteria_option_menu (string_criteria_options,
						      G_N_ELEMENTS (string_criteria_options),
						      FALSE);
	gtk_size_group_add_widget (dlg->priv->criteria_size_group, first_criteria);
	first_entry = gtk_entry_new ();
	gtk_size_group_add_widget (dlg->priv->entry_size_group, first_entry);

	hbox = GTK_BOX (gtk_hbox_new (FALSE, 5));
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (first_option));
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (first_criteria));
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (first_entry));
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (dlg->priv->addbutton));
	gtk_box_pack_start_defaults (dlg->priv->vbox, GTK_WIDGET (hbox));
	dlg->priv->rows = g_list_prepend (dlg->priv->rows, hbox);

	mainbox = glade_xml_get_widget (xml, "main_vbox");
	gtk_box_pack_start (GTK_BOX (GTK_DIALOG (dlg)->vbox), mainbox, FALSE, FALSE, 0);
	gtk_widget_show_all (GTK_WIDGET (dlg));

	return G_OBJECT (dlg);
}

static void
rb_query_creator_init (RBQueryCreator *dlg)
{
	dlg->priv = g_new0 (RBQueryCreatorPrivate, 1);
}

static void
rb_query_creator_finalize (GObject *object)
{
	RBQueryCreator *dlg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_QUERY_CREATOR (object));

	dlg = RB_QUERY_CREATOR (object);

	g_return_if_fail (dlg->priv != NULL);

	g_object_unref (G_OBJECT (dlg->priv->property_size_group));
	g_object_unref (G_OBJECT (dlg->priv->criteria_size_group));
	g_object_unref (G_OBJECT (dlg->priv->entry_size_group));
	g_object_unref (G_OBJECT (dlg->priv->button_size_group));

	g_list_free (dlg->priv->rows);
	
	g_free (dlg->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_query_creator_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec)
{
	RBQueryCreator *dlg = RB_QUERY_CREATOR (object);

	switch (prop_id)
	{
	case PROP_DB:
		dlg->priv->db = g_value_get_object (value);
		break;
	case PROP_CREATING:
		dlg->priv->creating = g_value_get_boolean (value);
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
	RBQueryCreator *dlg = RB_QUERY_CREATOR (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, dlg->priv->db);
		break;
	case PROP_CREATING:
		g_value_set_boolean (value, dlg->priv->creating);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


GtkWidget *
rb_query_creator_new (RhythmDB *db)
{
	return g_object_new (RB_TYPE_QUERY_CREATOR, "db", db, NULL);
}

static void
rb_query_creator_load_query (RBQueryCreator *creator, GPtrArray *query,
			     int limit_count, int limit_size)
{
	int i;
	GList *rows = creator->priv->rows;
	gboolean disjunction = FALSE;
	RhythmDBQueryData *qdata;
	GPtrArray *subquery;

	g_assert (query->len == 2);

	qdata = g_ptr_array_index (query, 1);
	g_assert (qdata->type == RHYTHMDB_QUERY_SUBQUERY);

	subquery = qdata->subquery;

	for (i = 0; i < subquery->len - 1; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (subquery, i);
		if (data->type != RHYTHMDB_QUERY_DISJUNCTION)
			append_row (creator);
	}
	
	for (i = 0; i < subquery->len; i++) {
		RhythmDBQueryData *data = g_ptr_array_index (subquery, i);
		GtkOptionMenu *propmenu;
		GtkOptionMenu *criteria_menu;

		if (data->type == RHYTHMDB_QUERY_DISJUNCTION) {
			disjunction = TRUE;
			continue;
		}

		propmenu = GTK_OPTION_MENU (get_box_widget_at_pos (GTK_BOX (rows->data), 0));
		
		select_property_from_value (GTK_WIDGET (propmenu), property_options,
					    G_N_ELEMENTS (property_options), data->propid);

		criteria_menu = GTK_OPTION_MENU (get_box_widget_at_pos (GTK_BOX (rows->data),
									1));

		select_criteria_from_value (creator,
					    GTK_WIDGET (criteria_menu), data->propid,
					    data->type);

		if (data->propid != RHYTHMDB_PROP_RATING) {
			GtkEntry *text = GTK_ENTRY (get_box_widget_at_pos (GTK_BOX (rows->data), 2));
			
			gtk_entry_set_text (text, g_value_get_string (data->val));
		} else {
			RBRating *rating = RB_RATING (get_box_widget_at_pos (GTK_BOX (rows->data), 2));
			g_object_set (G_OBJECT (rating), "score",
				      g_value_get_double (data->val), NULL);
		}
		rows = rows->next;
	}

	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (creator->priv->disjunction_check),
				      disjunction);
}

GtkWidget *
rb_query_creator_new_from_query (RhythmDB *db, GPtrArray *query,
				 int limit_count, int limit_size)
{
	RBQueryCreator *creator = g_object_new (RB_TYPE_QUERY_CREATOR, "db", db,
						"creating", FALSE, NULL);
	if (!creator)
		return NULL;

	rb_query_creator_load_query (creator, query, limit_count, limit_size);
	return GTK_WIDGET (creator);
}

static GtkWidget *
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

static const RBQueryCreatorCriteriaOption *
get_criteria_for_property (RBQueryCreator *creator, RhythmDBPropType prop, guint *length)
{
	GType proptype;
	const RBQueryCreatorCriteriaOption *options = NULL;

	proptype = rhythmdb_get_property_type (creator->priv->db, prop);

	if (proptype == G_TYPE_STRING) {
		options = string_criteria_options;
		if (length)
			*length = G_N_ELEMENTS (string_criteria_options);
	} else if (proptype == G_TYPE_DOUBLE) {
		options = numeric_criteria_options;
		if (length)
			*length = G_N_ELEMENTS (numeric_criteria_options);
	} else
		g_assert_not_reached ();
	return options;
}

static void
set_rating_score (RBRating *rating, gdouble score)
{
	g_object_set (G_OBJECT (rating), "score", score, NULL);
}	

static GtkWidget *
get_entry_for_property (RBQueryCreator *creator, RhythmDBPropType prop,
			gboolean *constrain)
{
	GType proptype;
	proptype = rhythmdb_get_property_type (creator->priv->db, prop);
	
	*constrain = TRUE;

	if (proptype == G_TYPE_STRING)
		return gtk_entry_new ();
	else if (prop == RHYTHMDB_PROP_RATING) {
		RBRating *rating = rb_rating_new ();
		g_signal_connect (G_OBJECT (rating), "rated",
				  G_CALLBACK (set_rating_score), NULL);
		*constrain = FALSE;
		return GTK_WIDGET (rating);
	} else
		g_assert_not_reached ();
	return NULL;
}

static int
extract_option_menu_index (GtkOptionMenu *menu)
{
	GtkWidget *active_item = option_menu_get_active_child (GTK_WIDGET (menu));
	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (active_item), "rb-query-creator-value"));
}

GPtrArray *
rb_query_creator_get_query (RBQueryCreator *dlg)
{
	GPtrArray *query;
	GPtrArray *sub_query;
	GList *rows, *row;
	gboolean disjunction;

	query = rhythmdb_query_parse (dlg->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_END);

	disjunction = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dlg->priv->disjunction_check));
	
	sub_query = g_ptr_array_new ();
	
	rows = dlg->priv->rows;
	for (row = rows; row; row = row->next) {
		GtkOptionMenu *propmenu = GTK_OPTION_MENU (get_box_widget_at_pos (GTK_BOX (row->data),
										  0));
		GtkOptionMenu *criteria_menu = GTK_OPTION_MENU (get_box_widget_at_pos (GTK_BOX (row->data),
										       1));
		guint prop_position = extract_option_menu_index (propmenu);
		const RBQueryCreatorPropertyOption *prop_option = &property_options[prop_position];
		const RBQueryCreatorCriteriaOption *criteria_options = get_criteria_for_property (dlg, prop_option->strict_val, NULL);
		const RBQueryCreatorCriteriaOption *criteria_option = &criteria_options[extract_option_menu_index (criteria_menu)];
		RhythmDBQueryType criteria = criteria_option->val;
		RhythmDBPropType prop = criteria_option->strict ? prop_option->strict_val : prop_option->fuzzy_val;

		if (prop != RHYTHMDB_PROP_RATING) {
			GtkEntry *text = GTK_ENTRY (get_box_widget_at_pos (GTK_BOX (row->data), 2));
			const char *data = gtk_entry_get_text (GTK_ENTRY (text));
			char *folded_data = criteria_option->strict ? g_strdup (data) : g_utf8_casefold (data, -1);
			
			if (disjunction && row->next)
				rhythmdb_query_append (dlg->priv->db,
						       sub_query,
						       criteria,
						       prop,
						       folded_data,
						       RHYTHMDB_QUERY_DISJUNCTION,
						       RHYTHMDB_QUERY_END);
			else
				rhythmdb_query_append (dlg->priv->db,
						       sub_query,
						       criteria,
						       prop,
						       folded_data,
						       RHYTHMDB_QUERY_END);
			g_free (folded_data);
		} else {
			double rating;

			g_object_get (G_OBJECT (get_box_widget_at_pos (GTK_BOX (row->data), 2)),
				      "score", &rating, NULL);
			if (disjunction && row->next)
				rhythmdb_query_append (dlg->priv->db,
						       sub_query,
						       criteria,
						       prop,
						       rating,
						       RHYTHMDB_QUERY_DISJUNCTION,
						       RHYTHMDB_QUERY_END);
			else
				rhythmdb_query_append (dlg->priv->db,
						       sub_query,
						       criteria,
						       prop,
						       rating,
						       RHYTHMDB_QUERY_END);
		}
	}
	rhythmdb_query_append (dlg->priv->db,
			       query,
			       RHYTHMDB_QUERY_SUBQUERY,
			       sub_query,
			       RHYTHMDB_QUERY_END);
	return query;
}

void
rb_query_creator_get_limit (RBQueryCreator *dlg, RBQueryCreatorLimitType *type,
			    guint *limit)
{
	guint limitpos;
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dlg->priv->limit_check)))
		*limit = atoi (gtk_entry_get_text (GTK_ENTRY (dlg->priv->limit_entry)));
	else
		*limit = 0;
	limitpos = gtk_option_menu_get_history (GTK_OPTION_MENU (dlg->priv->limit_option));
	switch (limitpos)
	{
	case 0:
		*type = RB_QUERY_CREATOR_LIMIT_COUNT;
		break;
	case 1:
		*type = RB_QUERY_CREATOR_LIMIT_MB;
		break;
	case 2:
		*type = RB_QUERY_CREATOR_LIMIT_MB;
		*limit *= 1000;
		break;
	default:
		g_assert_not_reached ();
	}
}

static void
limit_toggled_cb (GtkWidget *limit, RBQueryCreator *dialog)
{
	gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (limit));

	gtk_widget_set_sensitive (dialog->priv->limit_entry, active);
	gtk_widget_set_sensitive (dialog->priv->limit_option, active);
}

static GtkWidget *
lookup_row_by_widget (RBQueryCreator *dialog, GtkWidget *widget)
{
	GList *rows = dialog->priv->rows;
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
remove_button_click_cb (GtkWidget *button, RBQueryCreator *dialog)
{
	GtkWidget *row;

	row = lookup_row_by_widget (dialog, button);
	g_assert (row);
	gtk_container_remove (GTK_CONTAINER (dialog->priv->vbox),
			      GTK_WIDGET (row));
	dialog->priv->rows = g_list_remove (dialog->priv->rows, row);
}

static void
add_button_click_cb (GtkWidget *button, RBQueryCreator *creator)
{
	append_row (creator);
}

static GtkWidget *
append_row (RBQueryCreator *dialog)
{
	GtkWidget *option;
	GtkWidget *criteria;
	GtkWidget *entry;
	GtkWidget *remove_button;
	GtkBox *last_hbox;
	GtkBox *hbox;
	GList *rows;
	guint len;

	rows = dialog->priv->rows;
	len = g_list_length (rows);
	last_hbox = GTK_BOX (get_box_widget_at_pos (dialog->priv->vbox, len-1));
	g_object_ref (G_OBJECT (dialog->priv->addbutton));
	gtk_container_remove (GTK_CONTAINER (last_hbox),
			      dialog->priv->addbutton);

	remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	g_signal_connect (G_OBJECT (remove_button), "clicked", G_CALLBACK (remove_button_click_cb),
			  dialog);
	gtk_size_group_add_widget (dialog->priv->button_size_group, remove_button);
	gtk_box_pack_start_defaults (last_hbox, GTK_WIDGET (remove_button));

	hbox = GTK_BOX (gtk_hbox_new (FALSE, 5));
	gtk_box_pack_start_defaults (GTK_BOX (dialog->priv->vbox), GTK_WIDGET (hbox));
	dialog->priv->rows = g_list_prepend (dialog->priv->rows, hbox);
	gtk_box_reorder_child (dialog->priv->vbox, GTK_WIDGET (hbox), -1);

	/* This is the main (leftmost) GtkOptionMenu, for types. */
	option = create_property_option_menu (dialog, property_options,
					      G_N_ELEMENTS (property_options),
					      FALSE);
	gtk_size_group_add_widget (dialog->priv->property_size_group, option);
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (option));
	gtk_option_menu_set_history (GTK_OPTION_MENU (option), 0);
	criteria = create_criteria_option_menu (string_criteria_options,
						G_N_ELEMENTS (string_criteria_options),
						FALSE);
	gtk_size_group_add_widget (dialog->priv->criteria_size_group, criteria);
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (criteria));

	entry = gtk_entry_new ();
	gtk_size_group_add_widget (dialog->priv->entry_size_group, entry);
	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (entry));

	gtk_box_pack_start_defaults (hbox, GTK_WIDGET (dialog->priv->addbutton));
	g_object_unref (G_OBJECT (dialog->priv->addbutton));

	gtk_widget_show_all (GTK_WIDGET (dialog->priv->vbox));
	return GTK_WIDGET (hbox);
}

static void
select_property_from_value (GtkWidget *option_menu,
			    const RBQueryCreatorPropertyOption *options,
			    int length, RhythmDBPropType prop)
{
	int i;
	
	for (i = 0; i < length; i++) {
		if (prop == options[i].strict_val || prop == options[i].fuzzy_val) {
			gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), i);
			return;
		}
	}
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
	gint length;

	options = get_criteria_for_property (creator, prop, &length);
			
	for (i = 0; i < length; i++) {
		if (qtype == options[i].val) {
			gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), i);
			return;
		}
	}
	g_assert_not_reached ();
}

static void
property_option_menu_changed (GtkOptionMenu *propmenu,
			      RBQueryCreator *creator)
{
	GtkWidget *row;
	GtkWidget *criteria;
	GtkWidget *entry;
	const RBQueryCreatorPropertyOption *prop_option;
	const RBQueryCreatorCriteriaOption *criteria_options;
	int length;
	gboolean constrain;

	row = lookup_row_by_widget (creator, GTK_WIDGET (propmenu));

	criteria = get_box_widget_at_pos (GTK_BOX (row), 1);
	gtk_container_remove (GTK_CONTAINER (row), criteria);

	prop_option = &property_options[extract_option_menu_index (propmenu)];

	criteria_options = get_criteria_for_property (creator, prop_option->strict_val, &length);

	criteria = create_criteria_option_menu (criteria_options, length,
						FALSE);
	gtk_widget_show (criteria);
	gtk_size_group_add_widget (creator->priv->criteria_size_group, criteria);
	gtk_box_pack_start_defaults (GTK_BOX (row), GTK_WIDGET (criteria));
	gtk_box_reorder_child (GTK_BOX (row), criteria, 1);

	entry = get_box_widget_at_pos (GTK_BOX (row), 2);
	gtk_container_remove (GTK_CONTAINER (row), entry);
	entry = get_entry_for_property (creator, prop_option->strict_val,
					&constrain);
	gtk_widget_show (entry);
	if (constrain)
		gtk_size_group_add_widget (creator->priv->entry_size_group, entry);
	gtk_box_pack_start_defaults (GTK_BOX (row), GTK_WIDGET (entry));
	gtk_box_reorder_child (GTK_BOX (row), entry, 2);
}

/* Stolen from jamboree and hacked up */
static void
setup_property_option_menu (RBQueryCreator *creator,
			    GtkWidget *option_menu,
			    const RBQueryCreatorPropertyOption *options,
			    int length, gboolean activate_first)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	int i;

	menu = gtk_menu_new ();
	gtk_widget_show (menu);
    
	for (i = 0; i < length; i++) {
		menu_item = gtk_menu_item_new_with_label (_(options[i].name));
		g_object_set_data (G_OBJECT (menu_item),
				   "rb-query-creator-value", GINT_TO_POINTER (i));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show (menu_item);
	}

	if (activate_first)
		gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), 0);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);

	g_signal_connect (G_OBJECT (option_menu), "changed",
			  G_CALLBACK (property_option_menu_changed), creator);
}

static void
setup_criteria_option_menu (GtkWidget *option_menu,
			    const RBQueryCreatorCriteriaOption *options,
			    int length, gboolean activate_first)
{
	GtkWidget *menu;
	GtkWidget *menu_item;
	int i;

	menu = gtk_menu_new ();
	gtk_widget_show (menu);
    
	for (i = 0; i < length; i++) {
		menu_item = gtk_menu_item_new_with_label (_(options[i].name));
		g_object_set_data (G_OBJECT (menu_item),
				   "rb-query-creator-value", GINT_TO_POINTER (i));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show (menu_item);
	}

	if (activate_first)
		gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), 0);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
}

static GtkWidget*
create_property_option_menu (RBQueryCreator *creator,
			     const RBQueryCreatorPropertyOption *options,
			     int length, gboolean activate_first)
{
	GtkWidget *menu;
	GtkWidget *option_menu;
  
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
    
	option_menu = gtk_option_menu_new ();

	setup_property_option_menu (creator, option_menu, options, length, activate_first);
  
	return option_menu;
}

static GtkWidget*
create_criteria_option_menu (const RBQueryCreatorCriteriaOption *options,
			     int length, gboolean activate_first)
{
	GtkWidget *menu;
	GtkWidget *option_menu;
  
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
    
	option_menu = gtk_option_menu_new ();

	setup_criteria_option_menu (option_menu, options, length, activate_first);
  
	return option_menu;
}

static GtkWidget*
option_menu_get_active_child (GtkWidget *option_menu)
{
	GtkWidget *menu;
	GList *children;
	int pos;
	GtkWidget *child;
  
	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (option_menu));
	children = gtk_container_get_children (GTK_CONTAINER (menu));

	pos = gtk_option_menu_get_history (GTK_OPTION_MENU (option_menu));
	child = g_list_nth (children, pos)->data;
	g_assert (child != NULL);
	g_assert (GTK_IS_WIDGET (child));
  
	return child;
}

