/* 
 * arch-tag: Implementation of Rhythmbox first-time druid
 *
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
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

#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomeui/gnome-druid.h>
#include <libgnomeui/gnome-druid-page-edge.h>
#include <libgnomeui/gnome-druid-page-standard.h>

#include "rb-druid.h"
#include "rb-preferences.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-glade-helpers.h"
#include "eel-gconf-extensions.h"

static void rb_druid_class_init (RBDruidClass *klass);
static void rb_druid_init (RBDruid *druid);
static void rb_druid_finalize (GObject *object);
static void rb_druid_set_property (GObject *object,
				   guint prop_id,
				   const GValue *value,
				   GParamSpec *pspec);
static void rb_druid_get_property (GObject *object,
				   guint prop_id,
				   GValue *value,
				   GParamSpec *pspec);
static void rb_druid_browse_clicked_cb (GtkButton *button, RBDruid *druid);
static void rb_druid_entry_changed_cb (GtkEntry *entry, RBDruid *druid);
static gboolean rb_druid_page2_prepare_cb (GnomeDruidPage *druid_page, GtkWidget *druid_widget,
					   RBDruid *druid);
static void rb_druid_skip_toggled_cb (GtkToggleButton *button, RBDruid *druid);
static void rb_druid_page3_finish_cb (GnomeDruidPage *druid_page, GtkWidget *druid_widget,
				      RBDruid *druid);
static void do_response (RBDruid *druid);

struct RBDruidPrivate
{
	RhythmDB *db;
	GnomeDruid *druid;

	GtkWidget *window;
	GtkWidget *page2_vbox;
	GtkWidget *browse_button;
	GtkWidget *page2_skip_radiobutton;
	GtkWidget *path_entry;
};

enum
{
	PROP_0,
	PROP_DB,
};

static GObjectClass *parent_class = NULL;

GType
rb_druid_get_type (void)
{
	static GType rb_druid_type = 0;

	if (rb_druid_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBDruidClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_druid_class_init,
			NULL,
			NULL,
			sizeof (RBDruid),
			0,
			(GInstanceInitFunc) rb_druid_init
		};

		rb_druid_type = g_type_register_static (G_TYPE_OBJECT,
							"RBDruid",
							&our_info, 0);
	}

	return rb_druid_type;
}

static void
rb_druid_class_init (RBDruidClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_druid_finalize;

	object_class->set_property = rb_druid_set_property;
	object_class->get_property = rb_druid_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE));
}

static void
rb_druid_init (RBDruid *druid)
{
	GladeXML *xml;
	druid->priv = g_new0 (RBDruidPrivate, 1);

	xml = rb_glade_xml_new ("druid.glade", "druid_toplevel", druid);

	druid->priv->page2_vbox = glade_xml_get_widget (xml, "page2_vbox");
	gtk_object_ref (GTK_OBJECT (druid->priv->page2_vbox));
	gtk_container_remove (GTK_CONTAINER (gtk_widget_get_parent (druid->priv->page2_vbox)),
			      druid->priv->page2_vbox);
	druid->priv->browse_button = glade_xml_get_widget (xml, "browse_button");
	druid->priv->path_entry = glade_xml_get_widget (xml, "path_entry");
	druid->priv->page2_skip_radiobutton = glade_xml_get_widget (xml, "page2_skip_radiobutton");

	g_signal_connect (G_OBJECT (druid->priv->page2_skip_radiobutton), "toggled",
			  G_CALLBACK (rb_druid_skip_toggled_cb), druid);
	g_signal_connect (G_OBJECT (druid->priv->browse_button), "clicked",
			  G_CALLBACK (rb_druid_browse_clicked_cb), druid);
	g_signal_connect (G_OBJECT (druid->priv->path_entry), "changed",
			  G_CALLBACK (rb_druid_entry_changed_cb), druid);

	g_object_unref (G_OBJECT (xml));
}

static void
rb_druid_finalize (GObject *object)
{
	RBDruid *druid;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_DRUID (object));

	druid = RB_DRUID (object);

	g_return_if_fail (druid->priv != NULL);

	if (druid->priv->window)
		gtk_widget_destroy (GTK_WIDGET (druid->priv->window));

	g_free (druid->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_druid_set_property (GObject *object,
			   guint prop_id,
			   const GValue *value,
			   GParamSpec *pspec)
{
	RBDruid *druid = RB_DRUID (object);

	switch (prop_id)
	{
	case PROP_DB:
		druid->priv->db = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_druid_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBDruid *druid = RB_DRUID (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, druid->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_druid_init_widgets (RBDruid *druid)
{
	GnomeDruidPage *page;

	g_return_if_fail (RB_IS_DRUID (druid));
	g_return_if_fail (druid->priv->window == NULL);
	
	druid->priv->window = gtk_dialog_new ();
	gtk_window_set_title (GTK_WINDOW (druid->priv->window),_("Rhythmbox"));
	gtk_window_set_modal (GTK_WINDOW (druid->priv->window), TRUE);

	druid->priv->druid = GNOME_DRUID (gnome_druid_new ());
	gtk_widget_show (GTK_WIDGET (druid->priv->druid));
	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (druid->priv->window)->vbox),
			   GTK_WIDGET (druid->priv->druid));
	gnome_druid_set_show_help (druid->priv->druid, FALSE);

	g_signal_connect_object (druid->priv->druid, "cancel",
				 G_CALLBACK (do_response),
				 druid, G_CONNECT_SWAPPED);

	g_signal_connect_object (druid->priv->druid, "destroy",
				 G_CALLBACK (do_response),
				 druid, G_CONNECT_SWAPPED);

	/* page 1 */
	page = GNOME_DRUID_PAGE (gnome_druid_page_edge_new (GNOME_EDGE_START));
	gtk_widget_show (GTK_WIDGET (page));
	gnome_druid_page_edge_set_title (GNOME_DRUID_PAGE_EDGE (page), 
					 _("Welcome to Rhythmbox"));
	gnome_druid_append_page (druid->priv->druid, page);
	gnome_druid_set_page (druid->priv->druid, page);
	gnome_druid_page_edge_set_text (GNOME_DRUID_PAGE_EDGE (page),
					_("Rhythmbox is the GNOME music player that lets you do everything: play your music files, listen to Internet Radio, import music from CDs, and much more.\n\nThis assistant will help you get started by asking you some simple questions."));

	/* page 2 */
	page = GNOME_DRUID_PAGE (gnome_druid_page_standard_new ());
	gtk_widget_show (GTK_WIDGET (page));
	gnome_druid_page_standard_set_title (GNOME_DRUID_PAGE_STANDARD (page), 
					     _("Music library setup"));
	gtk_container_add (GTK_CONTAINER (GNOME_DRUID_PAGE_STANDARD (page)->vbox),
			   druid->priv->page2_vbox);
	gnome_druid_append_page (druid->priv->druid, page);
	g_signal_connect (G_OBJECT (page), "prepare", G_CALLBACK (rb_druid_page2_prepare_cb), druid);
	
	/* page 3 */
	page = GNOME_DRUID_PAGE (gnome_druid_page_edge_new (GNOME_EDGE_FINISH));
	gtk_widget_show (GTK_WIDGET (page));
	gnome_druid_page_edge_set_title (GNOME_DRUID_PAGE_EDGE (page), _("Finish"));
	gnome_druid_page_edge_set_text (GNOME_DRUID_PAGE_EDGE (page),
					_("You are now ready to start Rhythmbox.\n\nRemember that you may add music to the library using \"Organize\" then \"Add to Library\", or by importing it from CDs."));
	g_signal_connect (G_OBJECT (page), "finish", G_CALLBACK (rb_druid_page3_finish_cb), druid);
	gnome_druid_append_page (druid->priv->druid, page);

/* 	g_signal_connect (page, "prepare", G_CALLBACK (gb_export_druid_page_5_prepare_cb), d); */

	/* misc */
	gnome_druid_set_show_help (druid->priv->druid, FALSE);
	gtk_button_set_label (GTK_BUTTON (druid->priv->druid->cancel), GTK_STOCK_CLOSE);
}

RBDruid *
rb_druid_new (RhythmDB *db) 
{
	RBDruid *druid = g_object_new (RB_TYPE_DRUID, "db", db, NULL);

	g_return_val_if_fail (druid->priv != NULL, NULL);

	rb_druid_init_widgets (druid);


	return druid;
}

static void
path_dialog_response_cb (GtkDialog *dialog,
			 int response_id,
			 RBDruid *druid)
{
	const char *file;
	char *utf8_file;
	GError *error = NULL;

	rb_debug ("got response");

	if (response_id != GTK_RESPONSE_OK) {
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	file = gtk_file_selection_get_filename (GTK_FILE_SELECTION (dialog));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (file == NULL)
		return;

	utf8_file = g_filename_to_utf8 (file, -1, NULL, NULL, &error);
	if (error != NULL) {
		rb_error_dialog (_("Error reading filename: %s"), error->message);
		return;
	}

	gtk_entry_set_text (GTK_ENTRY (druid->priv->path_entry), utf8_file);
	g_free (utf8_file);
}


static void
rb_druid_browse_clicked_cb (GtkButton *button, RBDruid *druid)
{
	GtkWidget *dialog;
	rb_debug ("browse");

	dialog = rb_ask_file (_("Choose a directory"), NULL,
			      GTK_WINDOW (druid->priv->window));
	gtk_window_set_modal (GTK_WINDOW (dialog), TRUE);

	g_signal_connect (G_OBJECT (dialog), "response",
			  G_CALLBACK (path_dialog_response_cb), druid);
}

void
rb_druid_show (RBDruid *druid)
{
	g_return_if_fail (RB_IS_DRUID (druid));
	rb_debug ("showing druid");

	gtk_dialog_run (GTK_DIALOG (druid->priv->window));
}

static void
rb_druid_page2_sync_sensitive (RBDruid *druid)
{
	gboolean next_enabled;
	gboolean skip_active;
	const char *text;
	rb_debug ("syncing sensitivity");

	text = gtk_entry_get_text (GTK_ENTRY (druid->priv->path_entry));

	skip_active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (druid->priv->page2_skip_radiobutton));
	next_enabled = g_utf8_strlen (text, -1) > 0 || skip_active;

	gtk_widget_set_sensitive (druid->priv->path_entry, !skip_active);
	gtk_widget_set_sensitive (druid->priv->druid->next, next_enabled);
}

static void
rb_druid_skip_toggled_cb (GtkToggleButton *button, RBDruid *druid)
{
	rb_debug ("skip changed");

	rb_druid_page2_sync_sensitive (druid);
}

static void
rb_druid_entry_changed_cb (GtkEntry *entry, RBDruid *druid)
{
	rb_debug ("entry changed");

	rb_druid_page2_sync_sensitive (druid);
}

static gboolean
idle_set_sensitive (RBDruid *druid)
{
	g_return_val_if_fail (RB_IS_DRUID (druid), FALSE);

	rb_druid_page2_sync_sensitive (druid);
	
	return FALSE;
}

static gboolean
rb_druid_page2_prepare_cb (GnomeDruidPage *druid_page, GtkWidget *druid_widget,
			   RBDruid *druid)
{
	g_return_val_if_fail (RB_IS_DRUID (druid), FALSE);

	rb_debug ("page2 prepare");

	/* FIXME: this is a gross hack, but setting the next button
	 * to not be sensitive at this point doesn't work! */
	g_idle_add ((GSourceFunc) idle_set_sensitive, druid);
	return FALSE;
}

static void
do_response (RBDruid *druid)
{
	g_return_if_fail (RB_IS_DRUID (druid));
	gtk_dialog_response (GTK_DIALOG (druid->priv->window), GTK_RESPONSE_OK);
}	

static void
rb_druid_page3_finish_cb (GnomeDruidPage *druid_page, GtkWidget *druid_widget,
			  RBDruid *druid)
{
	rb_debug ("druid finished!");
	if (!gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (druid->priv->page2_skip_radiobutton))) {
		char * uri = g_strdup (gtk_entry_get_text (GTK_ENTRY (druid->priv->path_entry)));
		
		rb_debug ("page2 next; adding %s to library", uri);
		    
		rhythmdb_add_uri_async (druid->priv->db, uri);
		g_free (uri);
	}
	eel_gconf_set_boolean (CONF_FIRST_TIME, TRUE);
	gnome_druid_set_buttons_sensitive (GNOME_DRUID (druid_widget),
					   FALSE, FALSE, FALSE, FALSE);
	do_response (druid);
}
