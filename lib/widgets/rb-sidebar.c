/*
 * Copyright (C) 2002 CodeFactory AB
 * Copyright (C) 2002 Richard Hult <rhult@codefactory.se>
 * Copyright (C) 2002 Mikael Hallendal <micke@codefactory.se>
 * Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * $Id$
 */

#include <math.h>
#include <gtk/gtk.h>
#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libxml/tree.h>
#include <string.h>
#include <unistd.h>

#include "rb-dialog.h"
#include "rb-sidebar.h"
#include "rb-sidebar-private.h"
#include "rb-marshal.h"

#define DARKEN 1.4

#define RB_SIDEBAR_XML_VERSION "1.0"

struct _RBSidebarPriv
{
	GList *buttons;

	GtkWidget *event_box;
	GtkWidget *vbox;

	GSList *radiogroup;

	GtkWidget *dnd_hint;
};

enum
{
	DRAG_FINISHED,
	LAST_SIGNAL
};

static void rb_sidebar_class_init (RBSidebarClass *klass);
static void rb_sidebar_init (RBSidebar *bar);
static void rb_sidebar_finalize (GObject *object);
static void rb_sidebar_destroy (GtkObject *object);
static void rb_sidebar_event_box_realize_cb (GtkWidget *widget,
				             gpointer user_data);
static void rb_sidebar_button_style_set (RBSidebarButton *button);
static void rb_sidebar_move_item (RBSidebar *sidebar,
		                  RBSidebarButton *button,
		                  int pos);
static void rb_sidebar_event_box_drag_data_received_cb (GtkWidget *widget,
					                GdkDragContext *context,
					                int x, int y,
					                GtkSelectionData *data,
					                guint info,
					                guint time,
					                RBSidebar *sidebar);
static gboolean rb_sidebar_event_box_drag_motion_cb (GtkWidget *widget,
			                             GdkDragContext *context,
			                             int x, int y,
			                             guint time,
			                             RBSidebar *sidebar);
static GtkWidget *rb_sidebar_get_dnd_info (RBSidebar *sidebar,
			                   int x, int y,
			                   RBSidebarDNDPosition *pos);
static void rb_sidebar_event_box_drag_leave_cb (GtkWidget *widget,
				                GdkDragContext *context,
				                guint time,
				                RBSidebar *sidebar);
static void rb_sidebar_get_button_coords (GtkWidget *w,
			                  int *x1, int *y1, 
			                  int *x2, int *y2);

static GtkVBoxClass *parent_class = NULL;

static guint rb_sidebar_signals[LAST_SIGNAL] = { 0 };

GType
rb_sidebar_get_type (void)
{
	static GType rb_sidebar_type = 0;

	if (!rb_sidebar_type)
	{
		static const GTypeInfo rb_sidebar_info = {
			sizeof (RBSidebarClass),
			NULL,		/* base_init */
			NULL,		/* base_finalize */
			(GClassInitFunc) rb_sidebar_class_init,
			NULL,		/* class_finalize */
			NULL,		/* class_data */
			sizeof (RBSidebar),
			0,              /* n_preallocs */
			(GInstanceInitFunc) rb_sidebar_init
		};

		rb_sidebar_type = g_type_register_static (GTK_TYPE_SCROLLED_WINDOW, "RBSidebar",
							  &rb_sidebar_info, 0);
	}
	
	return rb_sidebar_type;
}

static void
rb_sidebar_class_init (RBSidebarClass *class)
{
	GObjectClass   *o_class;
	GtkObjectClass *object_class;

	parent_class = g_type_class_peek_parent (class);

	o_class = (GObjectClass *) class;
	object_class = (GtkObjectClass *) class;

	o_class->finalize = rb_sidebar_finalize;

	object_class->destroy = rb_sidebar_destroy;

	rb_sidebar_signals[DRAG_FINISHED] =
		g_signal_new ("drag_finished",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBSidebarClass, drag_finished),
			      NULL, NULL,
			      rb_marshal_VOID__OBJECT_INT_INT_BOXED_UINT_UINT,
			      G_TYPE_NONE,
			      6,
			      GDK_TYPE_DRAG_CONTEXT,
			      G_TYPE_INT,
			      G_TYPE_INT,
			      GTK_TYPE_SELECTION_DATA | G_SIGNAL_TYPE_STATIC_SCOPE,
			      G_TYPE_UINT,
			      G_TYPE_UINT);
}

static void
rb_sidebar_init (RBSidebar *bar)
{
	GtkWidget *dnd_ebox, *viewport;
	GdkColor black = { 0, 0x0000, 0x0000, 0x0000 };
	
	bar->priv = g_new0 (RBSidebarPriv, 1);

	bar->dnd_targets[0].target = "RBSidebarNewButton";
	bar->dnd_targets[0].flags = 0;
	bar->dnd_targets[0].info = RB_SIDEBAR_DND_TYPE_NEW_BUTTON;

	bar->dnd_targets[1].target = "RBSidebarButton";
	bar->dnd_targets[1].flags = 0;
	bar->dnd_targets[1].info = RB_SIDEBAR_DND_TYPE_BUTTON;

	gtk_scrolled_window_set_hadjustment (GTK_SCROLLED_WINDOW (bar), NULL);
	gtk_scrolled_window_set_vadjustment (GTK_SCROLLED_WINDOW (bar), NULL);
	gtk_scrolled_window_set_policy (GTK_SCROLLED_WINDOW (bar),
					GTK_POLICY_NEVER,
					GTK_POLICY_AUTOMATIC);

	bar->priv->event_box = gtk_event_box_new ();
	gtk_drag_dest_set (bar->priv->event_box,
			   GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
			   bar->dnd_targets, G_N_ELEMENTS (bar->dnd_targets),
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_widget_show (bar->priv->event_box);
	g_signal_connect (G_OBJECT (bar->priv->event_box),
			  "realize",
			  G_CALLBACK (rb_sidebar_event_box_realize_cb),
			  NULL);
	g_signal_connect (G_OBJECT (bar->priv->event_box),
			  "drag_motion",
			  G_CALLBACK (rb_sidebar_event_box_drag_motion_cb),
			  bar);
	g_signal_connect (G_OBJECT (bar->priv->event_box),
			  "drag_data_received",
			  G_CALLBACK (rb_sidebar_event_box_drag_data_received_cb),
			  bar);
	g_signal_connect (G_OBJECT (bar->priv->event_box),
			  "drag_leave",
			  G_CALLBACK (rb_sidebar_event_box_drag_leave_cb),
			  bar);
	
	bar->priv->vbox = gtk_vbox_new (FALSE, 0);
	gtk_widget_show (bar->priv->vbox);
	
	viewport = gtk_viewport_new (NULL, NULL);
	gtk_viewport_set_shadow_type (GTK_VIEWPORT (viewport), GTK_SHADOW_ETCHED_IN);
	g_signal_connect (G_OBJECT (viewport),
			  "realize",
			  G_CALLBACK (rb_sidebar_event_box_realize_cb),
			  NULL);
	gtk_container_add (GTK_CONTAINER (viewport), bar->priv->event_box);
	gtk_container_add (GTK_CONTAINER (bar), viewport);

	gtk_container_add (GTK_CONTAINER (bar->priv->event_box), bar->priv->vbox);
	
	/* init dnd widgets */
	bar->priv->dnd_hint = gtk_window_new (GTK_WINDOW_POPUP);
	dnd_ebox = gtk_event_box_new ();
	gtk_container_add (GTK_CONTAINER (bar->priv->dnd_hint), dnd_ebox);
	gtk_widget_modify_bg (dnd_ebox, GTK_STATE_NORMAL, &black);
	gtk_widget_realize (bar->priv->dnd_hint);
}

static void
rb_sidebar_finalize (GObject *object)
{
	RBSidebar *bar = RB_SIDEBAR (object);

	g_list_free (bar->priv->buttons);

	gtk_widget_destroy (bar->priv->dnd_hint);

	g_free (bar->priv);

	if (G_OBJECT_CLASS (parent_class)->finalize)
	{
		(* G_OBJECT_CLASS (parent_class)->finalize) (object);
	}
}

static void
rb_sidebar_destroy (GtkObject *object)
{
	if (GTK_OBJECT_CLASS (parent_class)->destroy)
	{
		(* GTK_OBJECT_CLASS (parent_class)->destroy) (object);
	}
}

static guint16
rb_sidebar_shift_color_component (guint16 component, float shift_by)
{
	guint16 result;
	
	if (shift_by > 1.0)
		result = component * (2 - shift_by);
	else
		result = 0xffff - shift_by * (0xffff - component);

	return result & 0xffff;
}

static void
rb_sidebar_shift_color (GdkColor *color, float shift_by)
{
	color->red = rb_sidebar_shift_color_component (color->red, shift_by);
	color->green = rb_sidebar_shift_color_component (color->green, shift_by);
	color->blue = rb_sidebar_shift_color_component (color->blue, shift_by);
}

static void
rb_sidebar_event_box_realize_cb (GtkWidget *widget, gpointer user_data)
{
	GdkColor color = widget->style->bg[GTK_STATE_NORMAL];

	rb_sidebar_shift_color (&color, DARKEN);
		
	gtk_widget_modify_bg (widget, GTK_STATE_NORMAL, &color);
}

static void
rb_sidebar_button_style_set (RBSidebarButton *button)
{
	GtkWidget *widget = GTK_WIDGET (button);
	GdkColor color, white = { 0, 0xffff, 0xffff, 0xffff }, black = { 0, 0x0000, 0x0000, 0x0000 };

	color = widget->style->bg[GTK_STATE_NORMAL];

	rb_sidebar_shift_color (&color, DARKEN);
	
	gtk_widget_modify_bg (widget,
			      GTK_STATE_PRELIGHT,
			      &color);
		
	gtk_widget_modify_bg (widget,
			      GTK_STATE_ACTIVE,
			      &color);

	gtk_widget_modify_fg (button->label,
			      GTK_STATE_PRELIGHT,
			      &white);

	gtk_widget_modify_fg (button->label,
			      GTK_STATE_ACTIVE,
			      &black);
}

GtkWidget *
rb_sidebar_new (void)
{
	return g_object_new (RB_TYPE_SIDEBAR, NULL);
}

void
rb_sidebar_append (RBSidebar *sidebar,
		   RBSidebarButton *button)
{
	g_return_if_fail (RB_IS_SIDEBAR (sidebar));
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));

	rb_sidebar_button_style_set (button);

	sidebar->priv->buttons = g_list_append (sidebar->priv->buttons,
						button);

	gtk_radio_button_set_group (GTK_RADIO_BUTTON (button), sidebar->priv->radiogroup);
	sidebar->priv->radiogroup = gtk_radio_button_get_group (GTK_RADIO_BUTTON (button));

	gtk_widget_show_all (GTK_WIDGET (button));
	
	gtk_box_pack_start (GTK_BOX (sidebar->priv->vbox),
			    GTK_WIDGET (button),
			    FALSE,
			    TRUE,
			    0);

	g_object_set (G_OBJECT (button), "sidebar", sidebar, NULL);
}

void
rb_sidebar_remove (RBSidebar *sidebar,
		   RBSidebarButton *button)
{
	GList *l, *next;

	g_return_if_fail (RB_IS_SIDEBAR (sidebar));
	g_return_if_fail (RB_IS_SIDEBAR_BUTTON (button));

	l = g_list_find (sidebar->priv->buttons, button);		
	next = g_list_next (l);
	if (next == NULL)
		next = g_list_previous (l);
	if (next != NULL)
	{
		if (GTK_TOGGLE_BUTTON (button)->active == TRUE)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (next->data), TRUE);
		sidebar->priv->radiogroup = gtk_radio_button_get_group (GTK_RADIO_BUTTON (next->data));
	}
	else
		sidebar->priv->radiogroup = NULL;

	sidebar->priv->buttons = g_list_remove (sidebar->priv->buttons,
						button);

	gtk_widget_destroy (GTK_WIDGET (button));
}

void
rb_sidebar_save_layout (RBSidebar *sidebar,
			const char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr root;
	GList *l;
	
	g_return_if_fail (RB_IS_SIDEBAR (sidebar));
	g_return_if_fail (filename != NULL);
	
	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");
	
	root = xmlNewDocNode (doc, NULL, "rhythmbox_sidebar_layout", NULL);
	xmlSetProp (root, "version", RB_SIDEBAR_XML_VERSION);
	xmlDocSetRootElement (doc, root);

	for (l = sidebar->priv->buttons; l != NULL; l = g_list_next (l))
	{
		RBSidebarButton *button = RB_SIDEBAR_BUTTON (l->data);
		xmlNodePtr node;
		char *active;

		node = xmlNewChild (root, NULL, "button", NULL);

		xmlSetProp (node, "id", button->unique_id);
		active = g_strdup_printf ("%d", GTK_TOGGLE_BUTTON (button)->active);
		xmlSetProp (node, "active", active);
		g_free (active);
	}

	xmlSaveFormatFile (filename, doc, 1);
	xmlFreeDoc (doc);
}

void
rb_sidebar_load_layout (RBSidebar *sidebar,
			const char *filename)
{
	xmlDocPtr doc;
	xmlNodePtr child, root;
	int position = 0;
	char *tmp;
	
	g_return_if_fail (RB_IS_SIDEBAR (sidebar));
	g_return_if_fail (filename != NULL);

	if (g_file_test (filename, G_FILE_TEST_EXISTS) == FALSE)
		return;

	doc = xmlParseFile (filename);

	if (doc == NULL)
	{
		rb_warning_dialog (_("Failed to parse %s as sidebar layout file"), filename);
		return;
	}

	root = xmlDocGetRootElement (doc);
	tmp = xmlGetProp (root, "version");
	if (tmp == NULL || strcmp (tmp, RB_SIDEBAR_XML_VERSION) != 0)
	{
		g_free (tmp);
		xmlFreeDoc (doc);
		unlink (filename);
		return;
	}
	g_free (tmp);

	for (child = root->children; child != NULL; child = child->next)
	{
		char *unique_id, *active;
		RBSidebarButton *button;

		unique_id = xmlGetProp (child, "id");
		if (unique_id == NULL)
			continue;
		button = rb_sidebar_button_from_id (sidebar, unique_id);
		g_free (unique_id);

		if (button == NULL)
			continue;

		active = xmlGetProp (child, "active");
		if (active != NULL && atoi (active) == 1)
			gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);
		g_free (active);

		rb_sidebar_move_item (sidebar, button, position);

		position++;
	}

	xmlFreeDoc (doc);
}

static void
rb_sidebar_move_item (RBSidebar *sidebar,
		      RBSidebarButton *button,
		      int pos)
{
	if (pos == g_list_index (sidebar->priv->buttons, button))
		return;

	gtk_box_reorder_child (GTK_BOX (sidebar->priv->vbox),
			       GTK_WIDGET (button),
			       pos);

	sidebar->priv->buttons = g_list_remove (sidebar->priv->buttons, button);
	sidebar->priv->buttons = g_list_insert (sidebar->priv->buttons, button, pos);
}

static gboolean
event_for_self (GtkWidget *widget,
		GdkDragContext *context,
		RBSidebar *sidebar)
{
	GtkTargetList *list;
	gboolean ret;

	list = gtk_target_list_new (sidebar->dnd_targets,
				    G_N_ELEMENTS (sidebar->dnd_targets));

	ret = (gtk_drag_dest_find_target (widget, context, list) != GDK_NONE);

	gtk_target_list_unref (list);

	return ret;
}

static void
rb_sidebar_event_box_drag_data_received_cb (GtkWidget *widget,
					    GdkDragContext *context,
					    int x, int y,
					    GtkSelectionData *data,
					    guint info,
					    guint time,
					    RBSidebar *sidebar)
{
	RBSidebarButton *button;
	char *dnd_info;

	if (event_for_self (widget, context, sidebar) == FALSE)
	{
		g_signal_emit (G_OBJECT (sidebar),
			       rb_sidebar_signals[DRAG_FINISHED],
			       0,
			       context, x, y, data, info, time);
		return;
	}
	
	if (!(data->length >= 0 && data->format == 8))
		gtk_drag_finish (context, FALSE, FALSE, time);

	dnd_info = (char *) data->data;

	switch (info)
	{
	case RB_SIDEBAR_DND_TYPE_BUTTON:
		{
			RBSidebarDNDPosition pos;
			GtkWidget *over;

			/* DND data is in format: unique_id */
			button = rb_sidebar_button_from_id (sidebar, dnd_info);

			g_assert (button != NULL);

		        over = rb_sidebar_get_dnd_info (sidebar, x, y, &pos);

			if (over == NULL)
				break;

			rb_sidebar_button_dropped (sidebar,
						   RB_SIDEBAR_BUTTON (over),
						   button,
						   pos);

			break;
		}
	case RB_SIDEBAR_DND_TYPE_NEW_BUTTON:
		{
			char **parts;

			/* DND data is in the format: unique_id\nbutton_name\nstock_id\ntext */
			parts = g_strsplit (dnd_info, "\n", -1);

			g_assert (parts[0] != NULL);
			g_assert (parts[1] != NULL);
			g_assert (parts[2] != NULL);
			g_assert (parts[3] != NULL);

			button = rb_sidebar_button_new (parts[0],
							parts[1]);
			rb_sidebar_button_set (button,
					       parts[2],
					       parts[3],
					       FALSE);
			rb_sidebar_append (sidebar, button);

			break;
		}
	default:
		g_warning ("Unknown DND type!");
		break;
	}

	gtk_drag_finish (context, TRUE, FALSE, time);
}

static gboolean
rb_sidebar_event_box_drag_motion_cb (GtkWidget *widget,
			             GdkDragContext *context,
			             int x, int y,
			             guint time,
			             RBSidebar *sidebar)
{
	GtkWidget *button;
	RBSidebarDNDPosition pos;

	if (event_for_self (widget, context, sidebar) == FALSE)
	{
		gtk_drag_highlight (widget);
		return TRUE;
	}
		
	button = rb_sidebar_get_dnd_info (sidebar, x, y, &pos);
	rb_sidebar_show_dnd_hint (sidebar, button, pos);

	gdk_drag_status (context, context->suggested_action, time);

	return TRUE;
}

static GtkWidget *
rb_sidebar_get_dnd_info (RBSidebar *sidebar,
			 int x, int y,
			 RBSidebarDNDPosition *pos)
{
	GtkWidget *widget = NULL;
	GList *l;

	for (l = sidebar->priv->buttons; l != NULL; l = g_list_next (l))
	{
		int btn_begin;
		int btn_end;

		widget = GTK_WIDGET (l->data);
		btn_begin = widget->allocation.y;
		btn_end = widget->allocation.y + widget->allocation.height;

		if (y >= btn_begin && y <= btn_end)
		{
			*pos = rb_sidebar_get_button_pos (widget, y);

			return widget;
		}
	}

	*pos = RB_SIDEBAR_DND_POSITION_BOTTOM;
	return widget;
}

void
rb_sidebar_show_dnd_hint (RBSidebar *sidebar,
			  GtkWidget *button,
			  RBSidebarDNDPosition pos)
{
	if (pos != RB_SIDEBAR_DND_POSITION_MID)
	{
		int x1, x2, y1, y2;
		rb_sidebar_get_button_coords (button, &x1, &y1, &x2, &y2);

		gtk_widget_set_size_request (sidebar->priv->dnd_hint, x2 - x1, 2);
		gdk_window_move (sidebar->priv->dnd_hint->window, x1,
				 (pos == RB_SIDEBAR_DND_POSITION_TOP) ? y1 : y2);
		gtk_widget_show_all (sidebar->priv->dnd_hint);
	}
	else
	{
		rb_sidebar_hide_dnd_hint (sidebar);
	}
}

static void
rb_sidebar_event_box_drag_leave_cb (GtkWidget *widget,
				    GdkDragContext *context,
				    guint time,
				    RBSidebar *sidebar)
{
	if (event_for_self (widget, context, sidebar) == FALSE)
	{
		gtk_drag_unhighlight (widget);
		return;
	}
	
	rb_sidebar_hide_dnd_hint (sidebar);
}

static void
rb_sidebar_get_button_coords (GtkWidget *w,
			      int *x1, int *y1, 
			      int *x2, int *y2)
{
	int ox, oy, width, height;

	if (w->parent && (w->parent->window == w->window))
	{
		rb_sidebar_get_button_coords (w->parent, &ox, &oy, NULL, NULL);
		ox += w->allocation.x;
		oy += w->allocation.y;
		height = w->allocation.height;
		width = w->allocation.width;
	}
	else
	{
		gdk_window_get_origin (w->window, &ox, &oy);
		gdk_window_get_size (w->window, &width, &height);
	}

	if (x1) *x1 = ox;
	if (y1) *y1 = oy;
	if (x2) *x2 = ox + width;
	if (y2) *y2 = oy + height;
}

RBSidebarButton *
rb_sidebar_button_from_id (RBSidebar *sidebar,
			   const char *unique_id)
{
	GList *l;

	for (l = sidebar->priv->buttons; l != NULL; l = g_list_next (l))
	{
		RBSidebarButton *btn = RB_SIDEBAR_BUTTON (l->data);

		if (strcmp (btn->unique_id, unique_id) == 0)
			return btn;
	}

	return NULL;
}

void
rb_sidebar_hide_dnd_hint (RBSidebar *sidebar)
{
	gtk_widget_hide (sidebar->priv->dnd_hint);
}

void
rb_sidebar_button_dropped (RBSidebar *sidebar,
			   RBSidebarButton *over,
			   RBSidebarButton *button,
			   RBSidebarDNDPosition pos)
{
	int idx = 0;
	gboolean move = TRUE;

	/* Yuk! This makes my head hurt. */
	switch (pos)
	{
	case RB_SIDEBAR_DND_POSITION_TOP:
		idx = g_list_index (sidebar->priv->buttons, over);
		if (idx > g_list_index (sidebar->priv->buttons, button))
			idx--;
		break;
	case RB_SIDEBAR_DND_POSITION_MID:
		move = FALSE;
		break;
	case RB_SIDEBAR_DND_POSITION_BOTTOM:
		idx = g_list_index (sidebar->priv->buttons, over);
		if (idx < g_list_index (sidebar->priv->buttons, button))
			idx++;
		break;
	}

	if (move == FALSE)
		return;

	rb_sidebar_move_item (sidebar,
			      button,
			      idx);
}

RBSidebarDNDPosition
rb_sidebar_get_button_pos (GtkWidget *widget,
			   int y)
{
	int btn_begin;
	int btn_end;

	btn_begin = widget->allocation.y;
	btn_end = widget->allocation.y + widget->allocation.height;

	if ((y >= (btn_begin + ((btn_end - btn_begin) / 2))) && ((btn_end - y) <= 20))
	{
		return RB_SIDEBAR_DND_POSITION_BOTTOM;
	}
	else if ((y - btn_begin) <= 15)
	{
		return RB_SIDEBAR_DND_POSITION_TOP;
	}
	else
	{
		return RB_SIDEBAR_DND_POSITION_MID;
	}
}

void
rb_sidebar_add_dnd_targets (RBSidebar *sidebar,
			    const GtkTargetEntry *targets,
			    int n_targets)
{
	GtkTargetList *list;

	g_return_if_fail (RB_IS_SIDEBAR (sidebar));

	list = gtk_target_list_new (targets, n_targets);
	gtk_target_list_add_table (list,
				   sidebar->dnd_targets,
				   G_N_ELEMENTS (sidebar->dnd_targets));

	gtk_drag_dest_set (sidebar->priv->event_box,
			   GTK_DEST_DEFAULT_MOTION | GTK_DEST_DEFAULT_DROP,
			   NULL, 0,
			   GDK_ACTION_COPY | GDK_ACTION_MOVE);
	gtk_drag_dest_set_target_list (sidebar->priv->event_box, list);

	gtk_target_list_unref (list);
}
