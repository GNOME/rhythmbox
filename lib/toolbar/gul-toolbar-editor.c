/*
 *  Copyright (C) 2002  Ricardo Fernández Pascual
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gtk/gtktreeview.h>
#include <gtk/gtktreeselection.h>
#include <gtk/gtkcellrendererpixbuf.h>
#include <gtk/gtkcellrenderertext.h>
#include <gtk/gtkwindow.h>
#include <libgnome/gnome-i18n.h>
#include "rb-glade-helpers.h"
#include "gul-gobject-misc.h"
#include "gul-toolbar-editor.h"
#include "gul-toolbar-tree-model.h"

#define NOT_IMPLEMENTED g_warning ("not implemented: " G_STRLOC);
//#define DEBUG_MSG(x) g_print x
#define DEBUG_MSG(x)

/**
 * Private data
 */
struct _GulTbEditorPrivate 
{
	GulToolbar *tb;
	GulToolbar *available;
	
	gchar *tb_undo_string;
	gchar *available_undo_string;

	gboolean in_toolbar_changed;
	
	GtkWidget *window;
	GtkWidget *available_view;
	GtkWidget *current_view;
	GtkWidget *close_button;
	GtkWidget *undo_button;
	GtkWidget *revert_button;
	GtkWidget *up_button;
	GtkWidget *down_button;
	GtkWidget *left_button;
	GtkWidget *right_button;
};

/**
 * Private functions, only available from this file
 */
static void		gul_tb_editor_class_init		(GulTbEditorClass *klass);
static void		gul_tb_editor_init			(GulTbEditor *tbe);
static void		gul_tb_editor_finalize_impl		(GObject *o);
static void 		gul_tb_editor_init_widgets		(GulTbEditor *tbe);
static void		gul_tb_editor_set_treeview_toolbar	(GulTbEditor *tbe, 
								 GtkTreeView *tv, GulToolbar *tb);
static void		gul_tb_editor_setup_treeview		(GulTbEditor *tbe, GtkTreeView *tv);
static GulTbItem *	gul_tb_editor_get_selected		(GulTbEditor *tbe, GtkTreeView *tv);
static gint		gul_tb_editor_get_selected_index	(GulTbEditor *tbe, GtkTreeView *tv);
static void		gul_tb_editor_select_index		(GulTbEditor *tbe, GtkTreeView *tv,
								 gint index);
static void		gul_tb_editor_remove_used_items		(GulTbEditor *tbe);

static void		gul_tb_editor_undo_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_close_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_up_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_down_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_left_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_right_clicked_cb		(GtkWidget *b, GulTbEditor *tbe);
static void		gul_tb_editor_toolbar_changed_cb	(GulToolbar *tb, GulTbEditor *tbe);
static gboolean		gul_tb_editor_treeview_button_press_event_cb (GtkWidget *widget, 
								      GdkEventButton *event, 
								      GulTbEditor *tbe);


static gpointer g_object_class;

/* treeview dnd */
enum
{
	TARGET_GTK_TREE_MODEL_ROW
};
static GtkTargetEntry tree_view_row_targets[] = {
	{ "GTK_TREE_MODEL_ROW", GTK_TARGET_SAME_APP, TARGET_GTK_TREE_MODEL_ROW }
};

/**
 * TbEditor object
 */

MAKE_GET_TYPE (gul_tb_editor, "GulTbEditor", GulTbEditor, gul_tb_editor_class_init, 
	       gul_tb_editor_init, G_TYPE_OBJECT);

static void
gul_tb_editor_class_init (GulTbEditorClass *klass)
{
	G_OBJECT_CLASS (klass)->finalize = gul_tb_editor_finalize_impl;
	
	g_object_class = g_type_class_peek_parent (klass);
}

static void 
gul_tb_editor_init (GulTbEditor *tb)
{
	GulTbEditorPrivate *p = g_new0 (GulTbEditorPrivate, 1);
	tb->priv = p;

	gul_tb_editor_init_widgets (tb);
}

static void 
gul_tb_editor_init_widgets (GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	GladeXML *gxml;
	
	gxml = rb_glade_xml_new ("toolbar-editor.glade", "toolbar-editor-dialog", tbe);
	p->window = glade_xml_get_widget (gxml, "toolbar-editor-dialog");
	p->available_view = glade_xml_get_widget (gxml, "toolbar-editor-available-view");
	p->current_view = glade_xml_get_widget (gxml, "toolbar-editor-current-view");
	p->close_button = glade_xml_get_widget (gxml, "toolbar-editor-close-button");
	p->undo_button = glade_xml_get_widget (gxml, "toolbar-editor-undo-button");
	p->revert_button = glade_xml_get_widget (gxml, "toolbar-editor-revert-button");
	p->up_button = glade_xml_get_widget (gxml, "toolbar-editor-up-button");
	p->down_button = glade_xml_get_widget (gxml, "toolbar-editor-down-button"); 
	p->left_button = glade_xml_get_widget (gxml, "toolbar-editor-left-button");
	p->right_button = glade_xml_get_widget (gxml, "toolbar-editor-right-button"); 
	g_object_unref (gxml);

	g_signal_connect_swapped (p->window, "delete_event", G_CALLBACK (g_object_unref), tbe);
	g_signal_connect (p->undo_button, "clicked", G_CALLBACK (gul_tb_editor_undo_clicked_cb), tbe);
	g_signal_connect (p->close_button, "clicked", G_CALLBACK (gul_tb_editor_close_clicked_cb), tbe);
	g_signal_connect (p->up_button, "clicked", G_CALLBACK (gul_tb_editor_up_clicked_cb), tbe);
	g_signal_connect (p->down_button, "clicked", G_CALLBACK (gul_tb_editor_down_clicked_cb), tbe);
	g_signal_connect (p->left_button, "clicked", G_CALLBACK (gul_tb_editor_left_clicked_cb), tbe);
	g_signal_connect (p->right_button, "clicked", G_CALLBACK (gul_tb_editor_right_clicked_cb), tbe);

	gul_tb_editor_setup_treeview (tbe, GTK_TREE_VIEW (p->current_view));
	gul_tb_editor_setup_treeview (tbe, GTK_TREE_VIEW (p->available_view));
}

static void
gul_tb_editor_undo_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	if (p->available_undo_string && p->available)
	{
		gul_toolbar_parse (p->available, p->available_undo_string);
	}

	if (p->tb_undo_string && p->tb)
	{
		gul_toolbar_parse (p->tb, p->tb_undo_string);
	}
}

static void
gul_tb_editor_close_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	gtk_widget_hide (tbe->priv->window);
	g_object_unref (tbe);
}

static void
gul_tb_editor_up_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	GulTbItem *item = gul_tb_editor_get_selected (tbe, GTK_TREE_VIEW (p->current_view));
	gint index = gul_tb_editor_get_selected_index (tbe, GTK_TREE_VIEW (p->current_view));
	if (item && index > 0)
	{
		g_object_ref (item);
		gul_toolbar_remove_item (p->tb, item);
		gul_toolbar_add_item (p->tb, item, index - 1);
		gul_tb_editor_select_index (tbe, GTK_TREE_VIEW (p->current_view), index - 1);
		g_object_unref (item);
	}
}

static void
gul_tb_editor_down_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	GulTbItem *item = gul_tb_editor_get_selected (tbe, GTK_TREE_VIEW (p->current_view));
	gint index = gul_tb_editor_get_selected_index (tbe, GTK_TREE_VIEW (p->current_view));
	if (item)
	{
		g_object_ref (item);
		gul_toolbar_remove_item (p->tb, item);
		gul_toolbar_add_item (p->tb, item, index + 1);
		gul_tb_editor_select_index (tbe, GTK_TREE_VIEW (p->current_view), index + 1);
		g_object_unref (item);
	}
}

static void
gul_tb_editor_left_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	GulTbItem *item = gul_tb_editor_get_selected (tbe, GTK_TREE_VIEW (p->current_view));
	/* probably is better not allowing reordering the available_view */
	gint index = gul_tb_editor_get_selected_index (tbe, GTK_TREE_VIEW (p->available_view));
	if (item)
	{
		g_object_ref (item);
		gul_toolbar_remove_item (p->tb, item);
		if (gul_tb_item_is_unique (item))
		{
			gul_toolbar_add_item (p->available, item, index);
		}
		g_object_unref (item);
	}
}

static void
gul_tb_editor_right_clicked_cb (GtkWidget *b, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	GulTbItem *item = gul_tb_editor_get_selected (tbe, GTK_TREE_VIEW (p->available_view));
	gint index = gul_tb_editor_get_selected_index (tbe, GTK_TREE_VIEW (p->current_view));
	if (item)
	{
		if (gul_tb_item_is_unique (item))
		{
			g_object_ref (item);
			gul_toolbar_remove_item (p->available, item);
		}
		else
		{
			item = gul_tb_item_clone (item);
		}
		gul_toolbar_add_item (p->tb, item, index);
		gul_tb_editor_select_index (tbe, GTK_TREE_VIEW (p->current_view), index);
		g_object_unref (item);
	}
}

static void
update_arrows_sensitivity (GulTbEditor *tbe)
{
	GtkTreeSelection *selection;
	gboolean current_sel;
	gboolean avail_sel;	
	gboolean first = FALSE;
	gboolean last = FALSE;
	GtkTreeModel *tm;
	GtkTreeIter iter;
	GtkTreePath *path;

	selection = gtk_tree_view_get_selection 
		(GTK_TREE_VIEW (tbe->priv->current_view));
	current_sel = gtk_tree_selection_get_selected (selection, &tm, &iter);
	if (current_sel)
	{
		path = gtk_tree_model_get_path (tm, &iter);
		first = !gtk_tree_path_prev (path);
		last = !gtk_tree_model_iter_next (tm, &iter);	
	}

	selection = gtk_tree_view_get_selection 
		(GTK_TREE_VIEW (tbe->priv->available_view));
	avail_sel = gtk_tree_selection_get_selected (selection, &tm, &iter);
	
	gtk_widget_set_sensitive (tbe->priv->right_button, 
				  avail_sel);
	gtk_widget_set_sensitive (tbe->priv->left_button, 
				  current_sel);
	gtk_widget_set_sensitive (tbe->priv->up_button, 
				  current_sel && !first);
	gtk_widget_set_sensitive (tbe->priv->down_button,
				  current_sel && !last);
}

static void
gul_tb_editor_treeview_selection_changed_cb (GtkTreeSelection *selection,
					     GulTbEditor *tbe)
{
	update_arrows_sensitivity (tbe);
}

static GulTbItem *
gul_tb_editor_get_selected (GulTbEditor *tbe, GtkTreeView *tv)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection (tv);
	GtkTreeModel *tm;
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected (sel, &tm, &iter))
	{
		GulTbItem *ret;
		g_return_val_if_fail (GUL_IS_TB_TREE_MODEL (tm), NULL);
		ret = gul_tb_tree_model_item_from_iter (GUL_TB_TREE_MODEL (tm), &iter);
		return ret;
	}
	else
	{
		return NULL;
	}
}

static gint
gul_tb_editor_get_selected_index (GulTbEditor *tbe, GtkTreeView *tv)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection (tv);
	GtkTreeModel *tm;
	GtkTreeIter iter;
	if (gtk_tree_selection_get_selected (sel, &tm, &iter))
	{
		GtkTreePath *p = gtk_tree_model_get_path (tm, &iter);
		if (p)
		{
			gint ret = gtk_tree_path_get_depth (p) > 0 ? gtk_tree_path_get_indices (p)[0] : -1;
			gtk_tree_path_free (p);
			return ret;
		}
		else
		{
			return -1;
		}
	}
	else
	{
		return -1;
	}
}

static void
gul_tb_editor_select_index (GulTbEditor *tbe, GtkTreeView *tv, gint index)
{
	GtkTreeSelection *sel = gtk_tree_view_get_selection (tv);
	GtkTreePath *p = gtk_tree_path_new ();
	GtkTreeModel *tm = gtk_tree_view_get_model (tv);
	gint max = gtk_tree_model_iter_n_children (tm, NULL);

	if (index < 0 || index >= max)
	{
		index = max - 1;
	}
	
	gtk_tree_path_append_index (p, index);
	gtk_tree_selection_select_path (sel, p);
	gtk_tree_path_free (p);
}

static void
gul_tb_editor_finalize_impl (GObject *o)
{
	GulTbEditor *tbe = GUL_TB_EDITOR (o);
	GulTbEditorPrivate *p = tbe->priv;
	
	if (p->tb) 
	{
		g_signal_handlers_disconnect_matched (p->tb, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbe);

		g_object_unref (p->tb);
	}
	if (p->available) 
	{
		g_signal_handlers_disconnect_matched (p->available, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbe);
		g_object_unref (p->available);
	}

	if (p->window)
	{
		gtk_widget_destroy (p->window);
	}

	g_free (p->tb_undo_string);
	g_free (p->available_undo_string);

	g_free (p);
	
	DEBUG_MSG (("GulTbEditor finalized\n"));
	
	G_OBJECT_CLASS (g_object_class)->finalize (o);
}

GulTbEditor *
gul_tb_editor_new (void)
{
	GulTbEditor *ret = g_object_new (GUL_TYPE_TB_EDITOR, NULL);
	return ret;
}

void
gul_tb_editor_set_toolbar (GulTbEditor *tbe, GulToolbar *tb)
{
	GulTbEditorPrivate *p = tbe->priv;

	if (p->tb)
	{
		g_signal_handlers_disconnect_matched (p->tb, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbe);
		g_object_unref (p->tb);
	}
	p->tb = g_object_ref (tb);

	g_free (p->tb_undo_string);
	p->tb_undo_string = gul_toolbar_to_string (p->tb);

	if (p->available)
	{
		gul_tb_editor_remove_used_items (tbe);
	}

	g_signal_connect (p->tb, "changed", G_CALLBACK (gul_tb_editor_toolbar_changed_cb), tbe);

	gul_tb_editor_set_treeview_toolbar (tbe, GTK_TREE_VIEW (p->current_view), p->tb);

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (p->current_view),
						GDK_BUTTON1_MASK,
						tree_view_row_targets,
						G_N_ELEMENTS (tree_view_row_targets),
						GDK_ACTION_MOVE);
	gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (p->current_view),
					      tree_view_row_targets,
					      G_N_ELEMENTS (tree_view_row_targets),
					      GDK_ACTION_COPY);
}

void
gul_tb_editor_set_available (GulTbEditor *tbe, GulToolbar *tb)
{
	GulTbEditorPrivate *p = tbe->priv;

	if (p->available)
	{
		g_signal_handlers_disconnect_matched (p->available, G_SIGNAL_MATCH_DATA, 0, 0, 
						      NULL, NULL, tbe);
		g_object_unref (p->available);
	}
	p->available = g_object_ref (tb);

	g_free (p->available_undo_string);
	p->available_undo_string = gul_toolbar_to_string (p->available);

	gul_toolbar_set_fixed_order (p->available, TRUE);

	if (p->tb)
	{
		gul_tb_editor_remove_used_items (tbe);
	}

	gul_tb_editor_set_treeview_toolbar (tbe, GTK_TREE_VIEW (p->available_view), p->available);

	gtk_tree_view_enable_model_drag_source (GTK_TREE_VIEW (p->available_view),
						GDK_BUTTON1_MASK,
						tree_view_row_targets,
						G_N_ELEMENTS (tree_view_row_targets),
						GDK_ACTION_COPY);
	gtk_tree_view_enable_model_drag_dest (GTK_TREE_VIEW (p->available_view),
					      tree_view_row_targets,
					      G_N_ELEMENTS (tree_view_row_targets),
					      GDK_ACTION_MOVE);
}

void 
gul_tb_editor_set_parent (GulTbEditor *tbe, GtkWidget *parent)
{
	gtk_window_set_transient_for (GTK_WINDOW (tbe->priv->window), 
				      GTK_WINDOW (parent));
}

void
gul_tb_editor_show (GulTbEditor *tbe)
{
	gtk_window_present (GTK_WINDOW (tbe->priv->window));
}

static void
gul_tb_editor_set_treeview_toolbar (GulTbEditor *tbe, GtkTreeView *tv, GulToolbar *tb)
{
	GulTbTreeModel *tm = gul_tb_tree_model_new ();
	gul_tb_tree_model_set_toolbar (tm, tb);
	gtk_tree_view_set_model (tv, GTK_TREE_MODEL (tm));
	g_object_unref (tm);
}

static void
gul_tb_editor_setup_treeview (GulTbEditor *tbe, GtkTreeView *tv)
{
	GtkCellRenderer *renderer;
	GtkTreeViewColumn *column;
	GtkTreeSelection *selection;
	
	column = gtk_tree_view_column_new ();
	renderer = gtk_cell_renderer_pixbuf_new ();
	selection = gtk_tree_view_get_selection (tv);
	
	gtk_tree_view_column_pack_start (column, renderer, FALSE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "pixbuf", GUL_TB_TREE_MODEL_COL_ICON, 
					     NULL);
	
	renderer = gtk_cell_renderer_text_new ();
	gtk_tree_view_column_pack_start (column, renderer, TRUE);
	gtk_tree_view_column_set_attributes (column, renderer,
                                             "text", GUL_TB_TREE_MODEL_COL_NAME, 
					     NULL);
	gtk_tree_view_column_set_title (column,  "Name");
	gtk_tree_view_column_set_resizable (column, TRUE);
	gtk_tree_view_append_column (GTK_TREE_VIEW (tv), column);

	g_signal_connect (tv, "button-press-event", 
			  G_CALLBACK (gul_tb_editor_treeview_button_press_event_cb), tbe);
	g_signal_connect (selection, "changed",
			  G_CALLBACK (gul_tb_editor_treeview_selection_changed_cb), tbe);
}

GulToolbar *
gul_tb_editor_get_toolbar (GulTbEditor *tbe)
{
	GulTbEditorPrivate *p;

	g_return_val_if_fail (GUL_IS_TB_EDITOR (tbe), NULL);

	p = tbe->priv;
	
	return p->tb;
}

GulToolbar *
gul_tb_editor_get_available (GulTbEditor *tbe)
{
	GulTbEditorPrivate *p;

	g_return_val_if_fail (GUL_IS_TB_EDITOR (tbe), NULL);

	p = tbe->priv;
	
	return p->available;
}


static void
gul_tb_editor_remove_used_items (GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	const GSList *current_items;
	const GSList *li;
	
	g_return_if_fail (GUL_IS_TOOLBAR (p->tb));
	g_return_if_fail (GUL_IS_TOOLBAR (p->available));

	current_items = gul_toolbar_get_item_list (p->tb);
	for (li = current_items; li; li = li->next)
	{
		GulTbItem *i = li->data;
		if (gul_tb_item_is_unique (i))
		{
			GulTbItem *j = gul_toolbar_get_item_by_id (p->available, i->id);
			if (j)
			{
				gul_toolbar_remove_item (p->available, j);
			}
		}
	}
}

static void
gul_tb_editor_toolbar_changed_cb (GulToolbar *tb, GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	
	if (p->in_toolbar_changed)
	{
		return;
	}
	
	if (p->tb && p->available)
	{
		p->in_toolbar_changed = TRUE;
		gul_tb_editor_remove_used_items (tbe);
		p->in_toolbar_changed = FALSE;
	}
}

static gboolean
gul_tb_editor_treeview_button_press_event_cb (GtkWidget *widget, 
					       GdkEventButton *event, 
					       GulTbEditor *tbe)
{
	GulTbEditorPrivate *p = tbe->priv;
	
	if (event->window != gtk_tree_view_get_bin_window (GTK_TREE_VIEW (widget))) 
	{
		return FALSE;
	}

	if (event->type == GDK_2BUTTON_PRESS)
	{
		if (widget == p->current_view)
		{
			gul_tb_editor_left_clicked_cb (NULL, tbe);
		}
		else if (widget == p->available_view)
		{
			gul_tb_editor_right_clicked_cb (NULL, tbe);
		}
		else
		{
			g_assert_not_reached ();
		}
		return TRUE;
	}

	return FALSE;
}

GtkButton *
gul_tb_editor_get_revert_button	(GulTbEditor *tbe)
{
	GulTbEditorPrivate *p;
	g_return_val_if_fail (GUL_IS_TB_EDITOR (tbe), NULL);
	p = tbe->priv;
	g_return_val_if_fail (GTK_IS_BUTTON (p->revert_button), NULL);
	
	return GTK_BUTTON (p->revert_button);

}

