/* 
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#include <gtk/gtklabel.h>
#include <gtk/gtkvbox.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkitemfactory.h>
#include <gtk/gtkstock.h>
#include <gtk/gtkentry.h>
#include <gtk/gtkeventbox.h>
#include <gtk/gtkdialog.h>
#include <gtk/gtkdnd.h>
#include <gtk/gtkmain.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-dialog.h"
#include "rb-sidebar.h"
#include "rb-sidebar-button.h"
#include "rb-sidebar-private.h"
#include "rb-ellipsizing-label.h"

static void rb_sidebar_button_class_init (RBSidebarButtonClass *klass);
static void rb_sidebar_button_init (RBSidebarButton *button);
static void rb_sidebar_button_finalize (GObject *object);
static void rb_sidebar_button_set_property (GObject *object,
					     guint prop_id,
					     const GValue *value,
					     GParamSpec *pspec);
static void rb_sidebar_button_get_property (GObject *object,
					     guint prop_id,
					     GValue *value,
					     GParamSpec *pspec);
static gboolean rb_sidebar_button_button_press_event_cb (GtkWidget *widget,
					                 GdkEventButton *event,
					                 RBSidebarButton *button);
static gboolean rb_sidebar_button_popup_menu_cb (GtkWidget *widget,
				                 RBSidebarButton *button);
static char *rb_sidebar_button_item_factory_translate_func (const char *path,
					                    gpointer unused);
static void rb_sidebar_button_popup_rename_cb (RBSidebarButton *button,
				               guint action,
				               GtkWidget *widget);
static void rb_sidebar_button_popup_delete_cb (RBSidebarButton *button,
				               guint action,
				               GtkWidget *widget);
static void rb_sidebar_button_drag_data_get_cb (GtkWidget *widget,
				                GdkDragContext *context,
				                GtkSelectionData *selection_data,
				                guint info, guint time,
				                RBSidebarButton *button);
static void rb_sidebar_button_drag_begin_cb (GtkWidget *widget,
				             GdkDragContext *context,
				             RBSidebarButton *button);
static void dnd_init (RBSidebarButton *button);
static void default_drag_leave (GtkWidget *widget,
		                GdkDragContext *context,
		                guint time);
static gboolean default_drag_motion (GtkWidget *widget,
		                     GdkDragContext *context,
		                     int x, int y,
		                     guint time);
static void default_drag_data_received (GtkWidget *widget,
		                        GdkDragContext *context,
			                int x, int y,
			                GtkSelectionData *selection_data,
			                guint info,
		                        guint time);
static void rb_sidebar_button_popup_open_cb (RBSidebarButton *button,
				             guint action,
				             GtkWidget *widget);

struct RBSidebarButtonPrivate
{
	char *button_name;
	char *stock_id;
	char *text;
	gboolean is_static;

	GtkItemFactory *popup_factory;

	GtkWidget *box;
	
	gboolean editing;
	
	RBSidebar *sidebar;

	GtkWidget *dnd_widget;
	GtkWidget *dnd_image;
	GtkWidget *dnd_label;

	GtkTargetList *targets;

	GtkWidget *rename_dialog;
};

enum
{
	PROP_0,
	PROP_UNIQUE_ID,
	PROP_STOCK_ID,
	PROP_TEXT,
	PROP_STATIC,
	PROP_BUTTON_NAME,
	PROP_SIDEBAR
};

enum
{
	EDITED,
	DELETED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_sidebar_button_signals[LAST_SIGNAL] = { 0 };

GType
rb_sidebar_button_get_type (void)
{
	static GType rb_sidebar_button_type = 0;

	if (rb_sidebar_button_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBSidebarButtonClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_sidebar_button_class_init,
			NULL,
			NULL,
			sizeof (RBSidebarButton),
			0,
			(GInstanceInitFunc) rb_sidebar_button_init
		};

		rb_sidebar_button_type = g_type_register_static (GTK_TYPE_RADIO_BUTTON,
								 "RBSidebarButton",
								 &our_info, 0);
	}

	return rb_sidebar_button_type;
}

static void
rb_sidebar_button_class_init (RBSidebarButtonClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_sidebar_button_finalize;

	object_class->set_property = rb_sidebar_button_set_property;
	object_class->get_property = rb_sidebar_button_get_property;

	g_object_class_install_property (object_class,
					 PROP_UNIQUE_ID,
					 g_param_spec_string ("unique_id",
							      "Unique ID",
							      "Unique ID",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STOCK_ID,
					 g_param_spec_string ("stock_id",
							      "Stock ID",
							      "Stock icon ID",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_TEXT,
					 g_param_spec_string ("text",
							      "Text",
							      "Text",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_STATIC,
					 g_param_spec_boolean ("static",
							       "Static",
							       "Static",
							       FALSE,
							       G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_SIDEBAR,
					 g_param_spec_object ("sidebar",
							      "Sidebar",
							      "Sidebar object",
							      RB_TYPE_SIDEBAR,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_BUTTON_NAME,
					 g_param_spec_string ("button_name",
							      "Button name",
							      "Button name",
							      NULL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	
	rb_sidebar_button_signals[EDITED] =
		g_signal_new ("edited",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSidebarButtonClass, edited),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
	rb_sidebar_button_signals[DELETED] =
		g_signal_new ("deleted",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSidebarButtonClass, deleted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_sidebar_button_init (RBSidebarButton *button)
{
	GtkWidget *dnd_ebox, *dnd_vbox, *dnd_border_ebox;
	GdkColor black = { 0, 0x0000, 0x0000, 0x0000 };

	static GtkItemFactoryEntry popup_menu_items[] =
	{
		{ N_("/_Open"),      "",   rb_sidebar_button_popup_open_cb,   0, "<StockItem>", GTK_STOCK_OPEN   },
		{ "/sep",            NULL, NULL,                              0, "<Separator>", NULL             },
		{ N_("/_Rename..."), NULL, rb_sidebar_button_popup_rename_cb, 0, "<Item>",      NULL             },
		{ N_("/_Delete"),    NULL, rb_sidebar_button_popup_delete_cb, 0, "<StockItem>", GTK_STOCK_DELETE }
	};

	button->priv = g_new0 (RBSidebarButtonPrivate, 1);

	gtk_button_set_relief (GTK_BUTTON (button), GTK_RELIEF_NONE);
	gtk_toggle_button_set_mode (GTK_TOGGLE_BUTTON (button), FALSE);

	button->priv->box = gtk_vbox_new (FALSE, 2);
	
	/* image */
	button->image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (button->priv->box),
			    button->image,
			    TRUE,
			    TRUE,
			    0);
	
	/* label */
	button->label = rb_ellipsizing_label_new ("");
	gtk_label_set_justify (GTK_LABEL (button->label), GTK_JUSTIFY_CENTER);
	gtk_box_pack_start (GTK_BOX (button->priv->box),
			    button->label,
			    FALSE,
			    FALSE,
			    0);

	gtk_widget_show (button->image);
	gtk_widget_show (button->label);

	gtk_container_add (GTK_CONTAINER (button), button->priv->box);

	g_signal_connect (G_OBJECT (button), "button_press_event",
			  G_CALLBACK (rb_sidebar_button_button_press_event_cb),
			  button);
	g_signal_connect (G_OBJECT (button), "popup_menu",
			  G_CALLBACK (rb_sidebar_button_popup_menu_cb),
			  button);
	
	/* popup menu */
	button->priv->popup_factory = gtk_item_factory_new (GTK_TYPE_MENU, "<main>", NULL);
	gtk_item_factory_set_translate_func (button->priv->popup_factory,
					     rb_sidebar_button_item_factory_translate_func,
					     NULL, NULL);
	gtk_item_factory_create_items (button->priv->popup_factory,
				       G_N_ELEMENTS (popup_menu_items),
				       popup_menu_items, button);

	/* dnd widget */
	button->priv->dnd_widget = gtk_window_new (GTK_WINDOW_POPUP);
	dnd_border_ebox = gtk_event_box_new ();
	gtk_widget_modify_bg (dnd_border_ebox, GTK_STATE_NORMAL, &black);
	dnd_ebox = gtk_event_box_new ();
	gtk_widget_modify_bg (dnd_ebox, GTK_STATE_NORMAL, &dnd_ebox->style->base[GTK_STATE_NORMAL]);
	dnd_vbox = gtk_vbox_new (FALSE, 2);
	gtk_container_set_border_width (GTK_CONTAINER (dnd_ebox), 1);
	gtk_container_add (GTK_CONTAINER (dnd_border_ebox), dnd_ebox);
	gtk_container_add (GTK_CONTAINER (dnd_ebox), dnd_vbox);
	gtk_container_add (GTK_CONTAINER (button->priv->dnd_widget), dnd_border_ebox);
	
	button->priv->dnd_image = gtk_image_new ();
	gtk_box_pack_start (GTK_BOX (dnd_vbox),
			    button->priv->dnd_image,
			    TRUE,
			    TRUE,
			    0);
	
	button->priv->dnd_label = rb_ellipsizing_label_new ("");
	gtk_box_pack_start (GTK_BOX (dnd_vbox),
			    button->priv->dnd_label,
			    FALSE,
			    TRUE,
			    0);

	gtk_widget_show_all (dnd_border_ebox);
	gtk_widget_realize (button->priv->dnd_widget);
}

static void
rb_sidebar_button_finalize (GObject *object)
{
	RBSidebarButton *button;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (object));

	button = RB_SIDEBAR_BUTTON (object);

	g_return_if_fail (button->priv != NULL);
	
	gtk_widget_destroy (button->priv->dnd_widget);

	if (button->priv->rename_dialog != NULL)
		gtk_widget_destroy (button->priv->rename_dialog);

	g_object_unref (G_OBJECT (button->priv->popup_factory));

	g_free (button->unique_id);
	g_free (button->priv->stock_id);
	g_free (button->priv->text);

	g_free (button->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_sidebar_button_set_property (GObject *object,
			        guint prop_id,
			        const GValue *value,
			        GParamSpec *pspec)
{
	RBSidebarButton *button = RB_SIDEBAR_BUTTON (object);

	switch (prop_id)
	{
	case PROP_UNIQUE_ID:
		button->unique_id = g_strdup (g_value_get_string (value));
		break;
	case PROP_STOCK_ID:
		if (button->priv->stock_id != NULL)
			g_free (button->priv->stock_id);
		button->priv->stock_id = g_strdup (g_value_get_string (value));
		gtk_image_set_from_stock (GTK_IMAGE (button->image),
					  button->priv->stock_id,
					  GTK_ICON_SIZE_DIALOG);
		gtk_image_set_from_stock (GTK_IMAGE (button->priv->dnd_image),
					  button->priv->stock_id,
					  GTK_ICON_SIZE_DIALOG);
		break;
	case PROP_TEXT:
		if (button->priv->text != NULL)
			g_free (button->priv->text);
		button->priv->text = g_strdup (g_value_get_string (value));
		rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (button->label),
				               button->priv->text);
		rb_ellipsizing_label_set_text (RB_ELLIPSIZING_LABEL (button->priv->dnd_label),
				               button->priv->text);
		break;
	case PROP_STATIC:
		button->priv->is_static = g_value_get_boolean (value);
		break;
	case PROP_SIDEBAR:
		button->priv->sidebar = g_value_get_object (value);
		dnd_init (button);
		break;
	case PROP_BUTTON_NAME:
		if (button->priv->button_name != NULL)
			g_free (button->priv->button_name);
		button->priv->button_name = g_strdup (g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_sidebar_button_get_property (GObject *object,
			        guint prop_id,
				GValue *value,
			        GParamSpec *pspec)
{
	RBSidebarButton *button = RB_SIDEBAR_BUTTON (object);

	switch (prop_id)
	{
	case PROP_UNIQUE_ID:
		g_value_set_string (value, button->unique_id);
		break;
	case PROP_STOCK_ID:
		g_value_set_string (value, button->priv->stock_id);
		break;
	case PROP_TEXT:
		g_value_set_string (value, button->priv->text);
		break;
	case PROP_STATIC:
		g_value_set_boolean (value, button->priv->is_static);
		break;
	case PROP_SIDEBAR:
		g_value_set_object (value, button->priv->sidebar);
		break;
	case PROP_BUTTON_NAME:
		g_value_set_string (value, button->priv->button_name);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBSidebarButton *
rb_sidebar_button_new (const char *unique_id,
		       const char *button_name)
{
	RBSidebarButton *button;

	button = RB_SIDEBAR_BUTTON (g_object_new (RB_TYPE_SIDEBAR_BUTTON,
						  "unique_id", unique_id,
						  "button_name", button_name,
						  NULL));

	g_return_val_if_fail (button->priv != NULL, NULL);

	return button;
}

void
rb_sidebar_button_set (RBSidebarButton *button,
		       const char *stock_id,
		       const char *text,
		       gboolean is_static)
{
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));
	g_return_if_fail (stock_id != NULL);
	g_return_if_fail (text != NULL);
	
	g_object_set (G_OBJECT (button),
		      "stock_id", stock_id,
		      "text", text,
		      "static", is_static,
		      NULL);
}

void
rb_sidebar_button_get (RBSidebarButton *button,
		       char **stock_id,
		       char **text,
		       gboolean *is_static)
{
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));
	
	*stock_id  = button->priv->stock_id;
	*text      = button->priv->text;
	*is_static = button->priv->is_static;
}

static void
popup_menu (RBSidebarButton *button,
	    GdkEventButton *event)
{
	GtkWidget *w;

	w = gtk_item_factory_get_widget (button->priv->popup_factory,
				         N_("/Rename..."));
	gtk_widget_set_sensitive (w, !button->priv->is_static);
	w = gtk_item_factory_get_widget (button->priv->popup_factory,
				         N_("/Delete"));
	gtk_widget_set_sensitive (w, !button->priv->is_static);
	
	gtk_item_factory_popup (button->priv->popup_factory,
				event->x_root,
				event->y_root,
				event->button,
				event->time);
}

static gboolean
rb_sidebar_button_popup_menu_cb (GtkWidget *widget,
				 RBSidebarButton *button)
{
	GdkEventButton *event;
	GtkRequisition req;
	int x, y;

	gdk_window_get_origin (widget->window, &x, &y);

	gtk_widget_size_request (button->priv->popup_factory->widget, &req);

	x += widget->allocation.width / 2;
	y += widget->allocation.height;

	x = CLAMP (x, 0, MAX (0, gdk_screen_width () - req.width));
	y = CLAMP (y, 0, MAX (0, gdk_screen_height () - req.height));

	event = g_new0 (GdkEventButton, 1);
	event->time = gtk_get_current_event_time ();
	event->x_root = x;
	event->y_root = y;
	
	popup_menu (button, event);

	g_free (event);

	return FALSE;
}

static gboolean
rb_sidebar_button_button_press_event_cb (GtkWidget *widget,
					 GdkEventButton *event,
					 RBSidebarButton *button)
{
	if (event->button != 3)
		return FALSE;

	popup_menu (button, event);

	return FALSE;
}

static char *
rb_sidebar_button_item_factory_translate_func (const char *path,
					       gpointer unused)
{
	return (char *) _(path);
}

static void
rb_sidebar_button_popup_open_cb (RBSidebarButton *button,
				 guint action,
				 GtkWidget *widget)
{
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
}

static void
rb_sidebar_button_popup_rename_cb (RBSidebarButton *button,
				   guint action,
				   GtkWidget *widget)
{
	rb_sidebar_button_rename (button);
}

static void
rb_sidebar_button_popup_delete_cb (RBSidebarButton *button,
				   guint action,
				   GtkWidget *widget)
{
	g_signal_emit (G_OBJECT (button),
		       rb_sidebar_button_signals[DELETED], 0);
}

static void
rb_sidebar_button_drag_data_get_cb (GtkWidget *widget,
				    GdkDragContext *context,
				    GtkSelectionData *selection_data,
				    guint info, guint time,
				    RBSidebarButton *button)
{
	switch (info)
	{
	case RB_SIDEBAR_DND_TYPE_BUTTON:
		gtk_selection_data_set (selection_data, selection_data->target,
					8, button->unique_id, strlen (button->unique_id));
		break;
	default:
		g_warning ("Unknown DND type");
		break;
	}
}

static void
rb_sidebar_button_drag_begin_cb (GtkWidget *widget,
				 GdkDragContext *context,
				 RBSidebarButton *button)
{
	gtk_widget_set_size_request (button->priv->dnd_widget,
				     GTK_WIDGET (button)->allocation.width,
				     GTK_WIDGET (button)->allocation.height);
	gtk_drag_set_icon_widget (context, button->priv->dnd_widget, -2, -2);
}

static void
ask_string_response_cb (GtkDialog *dialog,
			int response_id,
			RBSidebarButton *button)
{
	GtkWidget *entry;
	char *new;

	button->priv->rename_dialog = NULL;

	if (response_id != GTK_RESPONSE_OK)
	{
		gtk_widget_destroy (GTK_WIDGET (dialog));
		return;
	}

	entry = g_object_get_data (G_OBJECT (dialog), "entry");
	new = g_strdup (gtk_entry_get_text (GTK_ENTRY (entry)));

	gtk_widget_destroy (GTK_WIDGET (dialog));

	if (new == NULL)
		return;

	g_object_set (G_OBJECT (button),
		      "text", new,
		      NULL);

	g_signal_emit (G_OBJECT (button),
		       rb_sidebar_button_signals[EDITED], 0);

	g_free (new);
}

void
rb_sidebar_button_rename (RBSidebarButton *button)
{
	GtkWidget *dialog;
	char *question;

	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));

	if (button->priv->rename_dialog != NULL)
	{
		gdk_window_raise (button->priv->rename_dialog->window);
		return;
	}

	question = g_strdup_printf (_("Please enter a new name for this %s."), button->priv->button_name);
	dialog = rb_ask_string (question, _("Rename"), button->priv->text,
			        GTK_WINDOW (gtk_widget_get_toplevel (GTK_WIDGET (button))));
	g_free (question);

	g_signal_connect (G_OBJECT (dialog),
			  "response",
			  G_CALLBACK (ask_string_response_cb),
			  button);

	button->priv->rename_dialog = dialog;
}

void
rb_sidebar_button_add_dnd_targets (RBSidebarButton *button,
				   const GtkTargetEntry *targets,
				   int n_targets)
{
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));

	button->priv->targets = gtk_target_list_new (targets, n_targets);
}

static gboolean
event_for_parent (RBSidebarButton *button,
		  GdkDragContext *context)
{
	GtkTargetList *list;
	gboolean ret;

	list = gtk_target_list_new (button->priv->sidebar->dnd_targets,
				    G_N_ELEMENTS (button->priv->sidebar->dnd_targets));
	
	ret = (gtk_drag_dest_find_target (GTK_WIDGET (button), context, list) != GDK_NONE);

	gtk_target_list_unref (list);

	return ret;
}

static void
default_drag_leave (GtkWidget *widget,
		    GdkDragContext *context,
		    guint time)
{
	RBSidebarButton *button = RB_SIDEBAR_BUTTON (widget);
	
	if (event_for_parent (button, context) == FALSE)
	{
		gtk_drag_unhighlight (widget);
		return;
	}

	rb_sidebar_hide_dnd_hint (button->priv->sidebar);
}

static gboolean
default_drag_motion (GtkWidget *widget,
		     GdkDragContext *context,
		     int x, int y,
		     guint time)
{
	RBSidebarButton *button = RB_SIDEBAR_BUTTON (widget);
	
	if (event_for_parent (button, context) == FALSE)
	{
		gtk_drag_highlight (widget);
		return TRUE;
	}

	rb_sidebar_show_dnd_hint (button->priv->sidebar,
				  GTK_WIDGET (button),
				  rb_sidebar_get_button_pos (widget, y + widget->allocation.y));

	gdk_drag_status (context, context->suggested_action, time);

	return TRUE;
}

static void
default_drag_data_received (GtkWidget *widget,
		            GdkDragContext *context,
			    int x, int y,
			    GtkSelectionData *data,
			    guint info,
		            guint time)
{
	RBSidebarButton *button = RB_SIDEBAR_BUTTON (widget);

	if (event_for_parent (button, context) == FALSE)
		return;

	if (!(data->length >= 0 && data->format == 8))
		gtk_drag_finish (context, FALSE, FALSE, time);

	rb_sidebar_button_dropped (button->priv->sidebar,
				   button,
				   rb_sidebar_button_from_id (button->priv->sidebar, data->data),
				   rb_sidebar_get_button_pos (widget, y + widget->allocation.y));

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static void
dnd_init (RBSidebarButton *button)
{
	GtkTargetList *targets;

	static GtkTargetEntry drag_types[] =
	{
		{ "RBSidebarButton", 0, RB_SIDEBAR_DND_TYPE_BUTTON }
	};
	
	/* source */
	g_signal_connect (G_OBJECT (button), "drag_data_get",
			  G_CALLBACK (rb_sidebar_button_drag_data_get_cb), button);
	g_signal_connect (G_OBJECT (button), "drag_begin",
			  G_CALLBACK (rb_sidebar_button_drag_begin_cb), button);
	gtk_drag_source_set (GTK_WIDGET (button), GDK_BUTTON1_MASK,
			     drag_types, G_N_ELEMENTS (drag_types),
			     GDK_ACTION_COPY | GDK_ACTION_MOVE);

	/* dest */
	if (button->priv->targets == NULL)
	{
		targets = gtk_target_list_new (button->priv->sidebar->dnd_targets,
					       G_N_ELEMENTS (button->priv->sidebar->dnd_targets));
	}
	else
	{
		targets = button->priv->targets;
		gtk_target_list_add_table (button->priv->targets,
					   button->priv->sidebar->dnd_targets,
					   G_N_ELEMENTS (button->priv->sidebar->dnd_targets));
	}

	gtk_drag_dest_set (GTK_WIDGET (button), GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
			   NULL, 0, GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_set_target_list (GTK_WIDGET (button), targets);

	gtk_target_list_unref (targets);

	g_signal_connect (G_OBJECT (button),
			  "drag_motion",
			  G_CALLBACK (default_drag_motion),
			  NULL);
	g_signal_connect (G_OBJECT (button),
			  "drag_leave",
			  G_CALLBACK (default_drag_leave),
			  NULL);
	g_signal_connect (G_OBJECT (button),
			  "drag_data_received",
			  G_CALLBACK (default_drag_data_received),
			  NULL);
}
