/*
 *  arch-tag: Implementation of dialog that displays errors during library loading
 *
 *  Copyright (C) 2003 Colin Walters <walters@gnu.org>
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
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnome/gnome-i18n.h>
#include <gtk/gtk.h>

#include "rb-load-failure-dialog.h"
#include "rb-dialog.h"
#include "rb-debug.h"
#include "rb-preferences.h"
#include "rb-glade-helpers.h"
#include "eel-gconf-extensions.h"

static void rb_load_failure_dialog_class_init (RBLoadFailureDialogClass *klass);
static void rb_load_failure_dialog_init (RBLoadFailureDialog *dlg);
static void rb_load_failure_dialog_finalize (GObject *object);
static void rb_load_failure_dialog_response_cb (GtkDialog *dialog,
						int response_id,
						RBLoadFailureDialog *dlg);
static gboolean rb_load_failure_dialog_window_state_cb (GtkWidget *widget,
							GdkEvent *event,
							RBLoadFailureDialog *dlg);
static void rb_load_failure_dialog_sync_window_state (RBLoadFailureDialog *dlg);

#define RB_LOAD_FAILURE_DIALOG_COLUMN_URI 0
#define RB_LOAD_FAILURE_DIALOG_COLUMN_MESSAGE 1

#define CONF_STATE_WINDOW_WIDTH CONF_PREFIX "/state/load_failure_dialog/window_width"
#define CONF_STATE_WINDOW_HEIGHT CONF_PREFIX "/state/load_failure_dialog/window_height"

struct RBLoadFailureDialogPrivate
{
	GtkWidget *close;
	GtkWidget *treeview;
	GtkListStore *liststore;
};

static GObjectClass *parent_class = NULL;

GType
rb_load_failure_dialog_get_type (void)
{
	static GType rb_load_failure_dialog_type = 0;

	if (rb_load_failure_dialog_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLoadFailureDialogClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_load_failure_dialog_class_init,
			NULL,
			NULL,
			sizeof (RBLoadFailureDialog),
			0,
			(GInstanceInitFunc) rb_load_failure_dialog_init
		};
		
		rb_load_failure_dialog_type = g_type_register_static (GTK_TYPE_DIALOG,
								      "RBLoadFailureDialog",
								      &our_info, 0);
	}

	return rb_load_failure_dialog_type;
}

static void
rb_load_failure_dialog_class_init (RBLoadFailureDialogClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_load_failure_dialog_finalize;
}

static void
rb_load_failure_dialog_init (RBLoadFailureDialog *dlg)
{
	GladeXML *xml;
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *gcolumn;

	dlg->priv = g_new0 (RBLoadFailureDialogPrivate, 1);

	g_signal_connect (G_OBJECT (dlg),
			  "response",
			  G_CALLBACK (rb_load_failure_dialog_response_cb),
			  dlg);

	gtk_dialog_add_button (GTK_DIALOG (dlg),
			       GTK_STOCK_CLOSE,
			       GTK_RESPONSE_CLOSE);
	gtk_dialog_set_default_response (GTK_DIALOG (dlg),
					 GTK_RESPONSE_CLOSE);

	gtk_window_set_title (GTK_WINDOW (dlg), _("Error loading files into library"));

	gtk_container_set_border_width (GTK_CONTAINER (dlg), 5);
	gtk_box_set_spacing (GTK_BOX (GTK_DIALOG (dlg)->vbox), 2);
	gtk_dialog_set_has_separator (GTK_DIALOG (dlg), FALSE);

	dlg->priv->liststore = gtk_list_store_new (2, G_TYPE_STRING, G_TYPE_STRING);

	xml = rb_glade_xml_new ("load-failure.glade",
				"load_failure_dialog_content",
				dlg);

	gtk_container_add (GTK_CONTAINER (GTK_DIALOG (dlg)->vbox),
			   glade_xml_get_widget (xml, "load_failure_dialog_content"));

	dlg->priv->treeview = glade_xml_get_widget (xml, "treeview");
	gcolumn = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (gcolumn, _("_Error"));
	gtk_tree_view_append_column (GTK_TREE_VIEW (dlg->priv->treeview), gcolumn);
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
	gtk_tree_view_column_set_attributes (gcolumn, renderer,
					     "text", RB_LOAD_FAILURE_DIALOG_COLUMN_MESSAGE,
					     NULL);
	gcolumn = gtk_tree_view_column_new ();
	gtk_tree_view_column_set_title (gcolumn, _("_File"));
	gtk_tree_view_append_column (GTK_TREE_VIEW (dlg->priv->treeview), gcolumn);
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (gcolumn, renderer, TRUE);
	gtk_tree_view_column_set_attributes (gcolumn, renderer,
					     "text", RB_LOAD_FAILURE_DIALOG_COLUMN_URI,
					     NULL);

	gtk_tree_view_set_model (GTK_TREE_VIEW (dlg->priv->treeview),
				 GTK_TREE_MODEL (dlg->priv->liststore));

	gtk_window_set_modal (GTK_WINDOW (dlg), FALSE);

	g_signal_connect (G_OBJECT (dlg), "window-state-event",
			  G_CALLBACK (rb_load_failure_dialog_window_state_cb),
			  dlg);
	g_signal_connect (G_OBJECT (dlg), "configure-event",
			  G_CALLBACK (rb_load_failure_dialog_window_state_cb),
			  dlg);

	rb_load_failure_dialog_sync_window_state (dlg);
}

static void
rb_load_failure_dialog_finalize (GObject *object)
{
	RBLoadFailureDialog *dlg;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LOAD_FAILURE_DIALOG (object));

	dlg = RB_LOAD_FAILURE_DIALOG (object);

	g_return_if_fail (dlg->priv != NULL);

	g_free (dlg->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

GtkWidget *
rb_load_failure_dialog_new (void)
{
	RBLoadFailureDialog *dlg = g_object_new (RB_TYPE_LOAD_FAILURE_DIALOG, NULL);

	g_return_val_if_fail (dlg->priv != NULL, NULL);

	return GTK_WIDGET (dlg);
}

void
rb_load_failure_dialog_add (RBLoadFailureDialog *dlg, const char *uri, const char *error)
{
	char *dispuri = gnome_vfs_unescape_string_for_display (uri);
	GtkTreeIter iter;
	gtk_list_store_append (dlg->priv->liststore, &iter);

	gtk_list_store_set (dlg->priv->liststore, &iter,
			    RB_LOAD_FAILURE_DIALOG_COLUMN_URI, dispuri,
			    RB_LOAD_FAILURE_DIALOG_COLUMN_MESSAGE, error,
			    -1);
	g_free (dispuri);
}

static void
rb_load_failure_dialog_clear (RBLoadFailureDialog *dlg)
{
	gtk_list_store_clear (dlg->priv->liststore);
}

static void
rb_load_failure_dialog_response_cb (GtkDialog *dialog,
				    int response_id,
				    RBLoadFailureDialog *dlg)
{
	if (response_id == GTK_RESPONSE_CLOSE) {
		rb_load_failure_dialog_clear (dlg);
		gtk_widget_hide (GTK_WIDGET (dialog));
	}
}

static void
rb_load_failure_dialog_sync_window_state (RBLoadFailureDialog *dlg)
{
	int width = eel_gconf_get_integer (CONF_STATE_WINDOW_WIDTH); 
	int height = eel_gconf_get_integer (CONF_STATE_WINDOW_HEIGHT);

	rb_debug ("syncing window state");

	if (width < 0)
		width = 300;
	if (height < 0)
		height = 300;

	gtk_window_set_default_size (GTK_WINDOW (dlg),
				     width, height);
}


static gboolean
rb_load_failure_dialog_window_state_cb (GtkWidget *widget,
					GdkEvent *event,
					RBLoadFailureDialog *dlg)
{
	g_return_val_if_fail (widget != NULL, FALSE);
	rb_debug ("caught window state change");

	switch (event->type)
	{
	case GDK_CONFIGURE:
		eel_gconf_set_integer (CONF_STATE_WINDOW_WIDTH, event->configure.width);
		eel_gconf_set_integer (CONF_STATE_WINDOW_HEIGHT, event->configure.height);
		break;
	default:
		break;
	}

	return FALSE;
}
