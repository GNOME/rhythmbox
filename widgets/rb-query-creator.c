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
	{ N_("Title"),	RHYTHMDB_PROP_TITLE },
	{ N_("Artist"),	RHYTHMDB_PROP_ARTIST },
	{ N_("Album"),	RHYTHMDB_PROP_ALBUM },
	{ N_("Genre"),	RHYTHMDB_PROP_GENRE },
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
/* static GtkWidget * option_menu_get_active_child (GtkWidget *option_menu); */
static void append_row (RBQueryCreator *dialog);

struct RBQueryCreatorPrivate
{
	RhythmDB *db;
	
	GtkTable *table;
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

	dlg->priv->table = GTK_TABLE (glade_xml_get_widget (xml, "main_table"));

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
			   glade_xml_get_widget (xml, "main_vbox"));
	append_row (dlg);
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

GPtrArray *
rb_query_creator_get_query (RBQueryCreator *dlg)
{
	GPtrArray *query = rhythmdb_query_parse (dlg->priv->db,
						 RHYTHMDB_QUERY_PROP_EQUALS,
						 RHYTHMDB_PROP_TYPE,
						 RHYTHMDB_ENTRY_TYPE_SONG,
						 RHYTHMDB_QUERY_PROP_LIKE,
						 RHYTHMDB_PROP_TITLE,
						 "26",
						 RHYTHMDB_QUERY_END);
	return query;
}

guint
rb_query_creator_get_limit (RBQueryCreator *dlg)
{
	return 0;
}

static void
append_row (RBQueryCreator *dialog)
{
	guint n_rows, n_columns;
	GtkWidget *option;
	GtkWidget *criteria;
	GtkWidget *entry;
	GtkWidget *remove_button;
	GtkWidget *add_button;
	GtkTableChild *tablechild;

	g_object_get (G_OBJECT (dialog->priv->table), "n-columns", &n_columns,
		      "n-rows", &n_rows, NULL);

	tablechild = (g_list_nth (dialog->priv->table->children,
				  ((n_rows-1) * n_columns) + 3))->data;
	add_button = tablechild->widget;
	g_object_ref (G_OBJECT (add_button));
	gtk_container_remove (GTK_CONTAINER (dialog->priv->table), add_button);

	remove_button = gtk_button_new_from_stock (GTK_STOCK_REMOVE);
	gtk_table_attach_defaults (dialog->priv->table, remove_button, n_rows-1, n_rows, 3, 4);

	gtk_table_resize (dialog->priv->table, n_rows+1, n_columns);
	/* This is the main (leftmost) GtkOptionMenu, for types. */
	option = create_option_menu (property_options,
				     G_N_ELEMENTS (property_options),
				     FALSE);
	gtk_table_attach_defaults (dialog->priv->table, option, n_rows, n_rows+1, 0, 1);
	gtk_option_menu_set_history (GTK_OPTION_MENU (option), 0);
	criteria = create_option_menu (criteria_options,
				       G_N_ELEMENTS (criteria_options),
				       FALSE);
	gtk_table_attach_defaults (dialog->priv->table, criteria, n_rows, n_rows+1, 1, 2);

	entry = gtk_entry_new ();
	gtk_table_attach_defaults (dialog->priv->table, entry, n_rows, n_rows+1, 2, 3);

	gtk_table_attach_defaults (dialog->priv->table, add_button, n_rows, n_rows+1, 3, 4);

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
				   "value", GINT_TO_POINTER (options[i].val));
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

/* static GtkWidget* */
/* option_menu_get_active_child (GtkWidget *option_menu) */
/* { */
/* 	GtkWidget *menu; */
/* 	GList *children; */
/* 	int pos; */
/* 	GtkWidget *child; */
  
/* 	menu = gtk_option_menu_get_menu (GTK_OPTION_MENU (option_menu)); */
/* 	children = gtk_container_get_children (GTK_CONTAINER (menu)); */

/* 	pos = gtk_option_menu_get_history (GTK_OPTION_MENU (option_menu)); */
/* 	child = g_list_nth (children, pos)->data; */
/* 	g_assert (child != NULL); */
/* 	g_assert (GTK_IS_WIDGET (child)); */
  
/* 	return child; */
/* } */

