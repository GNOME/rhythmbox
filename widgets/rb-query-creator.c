/*
 *  arch-tag: Implementation of RhythmDB query creation dialog
 *
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
#include "rb-debug.h"
#include "rb-preferences.h"
#include "rb-glade-helpers.h"
#include "eel-gconf-extensions.h"

typedef struct
{
	const char *name;
	int val;
} RBQueryCreatorOption;

const RBQueryCreatorOption property_options[] =
{
	{ N_("Title"),	RHYTHMDB_PROP_TITLE_FOLDED },
	{ N_("Artist"),	RHYTHMDB_PROP_ARTIST_FOLDED },
	{ N_("Album"),	RHYTHMDB_PROP_ALBUM_FOLDED },
	{ N_("Genre"),	RHYTHMDB_PROP_GENRE_FOLDED },
};

const RBQueryCreatorOption criteria_options[] =
{
	{ N_("contains"),		RHYTHMDB_QUERY_PROP_LIKE },
	{ N_("does not contain"),	RHYTHMDB_QUERY_PROP_LIKE },
};

static void rb_query_creator_class_init (RBQueryCreatorClass *klass);
static void rb_query_creator_init (RBQueryCreator *dlg);
static void rb_query_creator_finalize (GObject *object);
static void rb_query_creator_set_property (GObject *object,
					      guint prop_id,
					      const GValue *value,
					      GParamSpec *pspec);
static void rb_query_creator_get_property (GObject *object,
					      guint prop_id,
					      GValue *value,
					      GParamSpec *pspec);
static void setup_option_menu (GtkWidget *option_menu,
			       const RBQueryCreatorOption *options,
			       int length, gboolean activate_first);
static GtkWidget * create_option_menu (const RBQueryCreatorOption *options,
				      int length, gboolean activate_first);
static GtkWidget * option_menu_get_active_child (GtkWidget *option_menu);
static void append_row (RBQueryCreator *dialog);
static void add_button_click_cb (GtkWidget *button, RBQueryCreator *creator);
static void limit_toggled_cb (GtkWidget *limit, RBQueryCreator *creator);

struct RBQueryCreatorPrivate
{
	RhythmDB *db;
	
	GtkTable *table;
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
	object_class->set_property = rb_query_creator_set_property;
	object_class->get_property = rb_query_creator_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
}

static void
rb_query_creator_init (RBQueryCreator *dlg)
{
	GladeXML *xml;
	GtkWidget *label;
	char *text;	
	GtkWidget *first_option;
	GtkWidget *first_criteria;
	GtkWidget *first_entry;

	dlg->priv = g_new0 (RBQueryCreatorPrivate, 1);

	dlg->priv->queries = g_ptr_array_new ();

	gtk_dialog_add_button (GTK_DIALOG (dlg),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	gtk_dialog_add_button (GTK_DIALOG (dlg),
			       GTK_STOCK_NEW,
			       GTK_RESPONSE_OK);
	gtk_dialog_set_default_response (GTK_DIALOG (dlg),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (dlg), _("Create automatic playlist"));

	gtk_container_set_border_width (GTK_CONTAINER (dlg), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 2);
	gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

	xml = rb_glade_xml_new ("create-playlist.glade",
				"main_vbox",
				dlg);
	
		label = GTK_WIDGET (glade_xml_get_widget (xml, "createLabel"));
	text = g_strdup_printf ("<b>%s</b>", gtk_label_get_text (GTK_LABEL (label)));
	gtk_label_set_markup (GTK_LABEL (label), text);
	g_free (text);

	dlg->priv->disjunction_check = GTK_WIDGET (glade_xml_get_widget (xml, "disjunctionCheck"));
	dlg->priv->limit_check = GTK_WIDGET (glade_xml_get_widget (xml, "limitCheck"));
	dlg->priv->limit_entry = GTK_WIDGET (glade_xml_get_widget (xml, "limitEntry"));
	dlg->priv->limit_option = GTK_WIDGET (glade_xml_get_widget (xml, "limitOption"));

	g_signal_connect (G_OBJECT (dlg->priv->limit_check), "toggled", G_CALLBACK (limit_toggled_cb),
			  dlg);
	gtk_widget_set_sensitive (dlg->priv->limit_entry, FALSE);
	gtk_widget_set_sensitive (dlg->priv->limit_option, FALSE);

	dlg->priv->table = GTK_TABLE (glade_xml_get_widget (xml, "main_table"));
	gtk_table_resize (dlg->priv->table, 1, 4);
	dlg->priv->addbutton = GTK_WIDGET (glade_xml_get_widget (xml, "addButton"));
	g_signal_connect (G_OBJECT (dlg->priv->addbutton), "clicked", G_CALLBACK (add_button_click_cb),
			  dlg);
	first_option = create_option_menu (property_options,
					   G_N_ELEMENTS (property_options),
					   FALSE);
	first_criteria = create_option_menu (criteria_options,
					     G_N_ELEMENTS (criteria_options),
					     FALSE);
	first_entry = gtk_entry_new ();

	gtk_table_attach_defaults (dlg->priv->table, first_option, 0, 1, 0, 1);
	gtk_table_attach_defaults (dlg->priv->table, first_criteria, 1, 2, 0, 1);
	gtk_table_attach_defaults (dlg->priv->table, first_entry, 2, 3, 0, 1);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
			   glade_xml_get_widget (xml, "main_vbox"));
	gtk_widget_show_all (GTK_WIDGET (dlg));
}

static void
rb_query_creator_finalize (GObject *object)
{
	RBQueryCreator *dlg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_QUERY_CREATOR (object));

	dlg = RB_QUERY_CREATOR (object);

	g_return_if_fail (dlg->priv != NULL);

	g_ptr_array_free (dlg->priv->queries, TRUE);

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

static GtkWidget *
lookup_table_child (GtkTable *table, guint row, guint col)
{
	GList *children;

	for (children = table->children; children; children = children->next) {
		GtkTableChild *child = children->data;
		if (child->top_attach == row &&
		    child->left_attach == col)
			return child->widget;
	}
	return NULL;
}

static int
extract_option_menu_val (GtkOptionMenu *menu)
{
	GtkWidget *active_item = option_menu_get_active_child (GTK_WIDGET (menu));

	return GPOINTER_TO_INT (g_object_get_data (G_OBJECT (active_item),
						   "rb-query-creator-value"));
}

GPtrArray *
rb_query_creator_get_query (RBQueryCreator *dlg)
{
	GPtrArray *query;
	GPtrArray *sub_query;
	guint i, n_rows;
	gboolean disjunction;

	query = rhythmdb_query_parse (dlg->priv->db,
				      RHYTHMDB_QUERY_PROP_EQUALS,
				      RHYTHMDB_PROP_TYPE,
				      RHYTHMDB_ENTRY_TYPE_SONG,
				      RHYTHMDB_QUERY_END);

	disjunction = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dlg->priv->disjunction_check));
	
	g_object_get (G_OBJECT (dlg->priv->table), "n-rows", &n_rows, NULL);

	sub_query = g_ptr_array_new ();

	for (i = 0; i < n_rows; i++) {
		GtkOptionMenu *propmenu = GTK_OPTION_MENU (lookup_table_child (dlg->priv->table,
									       i, 0));
		GtkOptionMenu *criteria_menu = GTK_OPTION_MENU (lookup_table_child (dlg->priv->table,
										    i, 1));
		GtkEntry *text = GTK_ENTRY (lookup_table_child (dlg->priv->table,
								i, 2));
		RhythmDBPropType prop = extract_option_menu_val (propmenu);
		RhythmDBQueryType criteria = extract_option_menu_val (criteria_menu);
		const char *data = gtk_entry_get_text (GTK_ENTRY (text));

		if (disjunction && i < n_rows - 1)
			rhythmdb_query_append (dlg->priv->db,
					       sub_query,
					       criteria,
					       prop,
					       data,
					       RHYTHMDB_QUERY_DISJUNCTION,
					       RHYTHMDB_QUERY_END);
		else
			rhythmdb_query_append (dlg->priv->db,
					       sub_query,
					       criteria,
					       prop,
					       data,
					       RHYTHMDB_QUERY_END);
	}
	rhythmdb_query_append (dlg->priv->db,
			       query,
			       RHYTHMDB_QUERY_SUBQUERY,
			       sub_query,
			       RHYTHMDB_QUERY_END);
	
	return query;
}

guint
rb_query_creator_get_limit (RBQueryCreator *dlg)
{
	if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (dlg->priv->limit_check)))
		return atoi (gtk_entry_get_text (GTK_ENTRY (dlg->priv->limit_entry)));

	return 0;
}

static void
limit_toggled_cb (GtkWidget *limit, RBQueryCreator *dialog)
{
	gboolean active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (limit));

	gtk_widget_set_sensitive (dialog->priv->limit_entry, active);
	gtk_widget_set_sensitive (dialog->priv->limit_option, active);
}

static void
add_button_click_cb (GtkWidget *button, RBQueryCreator *creator)
{
	append_row (creator);
}

static void
append_row (RBQueryCreator *dialog)
{
	guint n_rows, n_columns;
	GtkWidget *option;
	GtkWidget *criteria;
	GtkWidget *entry;
	GtkWidget *remove_button;

	g_object_get (G_OBJECT (dialog->priv->table), "n-columns", &n_columns,
		      "n-rows", &n_rows, NULL);

	g_object_ref (G_OBJECT (dialog->priv->addbutton));
	gtk_container_remove (GTK_CONTAINER (dialog->priv->table), dialog->priv->addbutton);

	remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	gtk_table_attach_defaults (dialog->priv->table, remove_button, 3, 4, n_rows-1, n_rows);

	gtk_table_resize (dialog->priv->table, n_rows+1, n_columns);
	/* This is the main (leftmost) GtkOptionMenu, for types. */
	option = create_option_menu (property_options,
				     G_N_ELEMENTS (property_options),
				     FALSE);
	gtk_table_attach_defaults (dialog->priv->table, option, 0, 1, n_rows, n_rows+1);
	gtk_option_menu_set_history (GTK_OPTION_MENU (option), 0);
	criteria = create_option_menu (criteria_options,
				       G_N_ELEMENTS (criteria_options),
				       FALSE);
	gtk_table_attach_defaults (dialog->priv->table, criteria, 1, 2, n_rows, n_rows+1);

	entry = gtk_entry_new ();
	gtk_table_attach_defaults (dialog->priv->table, entry, 2, 3, n_rows, n_rows+1);

	gtk_table_attach_defaults (dialog->priv->table, dialog->priv->addbutton, 3, 4, n_rows, n_rows+1);
	g_object_unref (G_OBJECT (dialog->priv->addbutton));

	gtk_widget_show_all (GTK_WIDGET (dialog->priv->table));
}

/* Stolen from jamboree */
static void
setup_option_menu (GtkWidget *option_menu,
		   const RBQueryCreatorOption *options,
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
				   "rb-query-creator-value", GINT_TO_POINTER (options[i].val));
		gtk_menu_shell_append (GTK_MENU_SHELL (menu), menu_item);
		gtk_widget_show (menu_item);
	}

	if (activate_first)
		gtk_option_menu_set_history (GTK_OPTION_MENU (option_menu), 0);

	gtk_option_menu_set_menu (GTK_OPTION_MENU (option_menu), menu);
}

static GtkWidget*
create_option_menu (const RBQueryCreatorOption *options,
		    int length, gboolean activate_first)
{
	GtkWidget *menu;
	GtkWidget *option_menu;
  
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
    
	option_menu = gtk_option_menu_new ();

	setup_option_menu (option_menu, options, length, activate_first);
  
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

