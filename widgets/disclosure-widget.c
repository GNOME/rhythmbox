/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  arch-tag: Implementation of the "disclosure" widget
 *
 *  Authors: Iain Holmes <iain@ximian.com>
 *
 *  Copyright 2002 Iain Holmes
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
 *  Foundation, Inc., 59 Temple Street #330, Boston, MA 02111-1307, USA.
 *
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <gtk/gtktogglebutton.h>
#include <libgnome/gnome-i18n.h>

#include "disclosure-widget.h"

static GtkCheckButtonClass *parent_class = NULL;

struct _CDDBDisclosurePrivate {
	GtkWidget *container;
	char *shown;
	char *hidden;
	
	guint32 expand_id;
	GtkExpanderStyle style;

	int expander_size;
	int direction;
};

static void
finalize (GObject *object)
{
	CDDBDisclosure *disclosure;

	disclosure = CDDB_DISCLOSURE (object);
	if (disclosure->priv == NULL) {
		return;
	}

	g_free (disclosure->priv->hidden);
	g_free (disclosure->priv->shown);

	if (disclosure->priv->container != NULL) {
		g_object_unref (G_OBJECT (disclosure->priv->container));
	}
	
	g_free (disclosure->priv);
	disclosure->priv = NULL;

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
cddb_disclosure_destroy (GtkObject *obj)
{
	CDDBDisclosure *disclosure = CDDB_DISCLOSURE (obj);

	if (disclosure->priv->expand_id) {
		g_source_remove (disclosure->priv->expand_id);
		disclosure->priv->expand_id = 0;
	}
}

static void
get_x_y (CDDBDisclosure *disclosure,
	 int *x,
	 int *y,
	 GtkStateType *state_type)
{
	GtkCheckButton *check_button;
	int indicator_size, indicator_spacing;
	int focus_width;
	int focus_pad;
	gboolean interior_focus;
	GtkWidget *widget = GTK_WIDGET (disclosure);
	GtkBin *bin = GTK_BIN (disclosure);
	int width;
	
	if (GTK_WIDGET_VISIBLE (disclosure) &&
	    GTK_WIDGET_MAPPED (disclosure)) {
		check_button = GTK_CHECK_BUTTON (disclosure);
		
		gtk_widget_style_get (GTK_WIDGET (check_button),
				      "indicator_size", &indicator_size,
				      "indicator_spacing", &indicator_spacing,
				      NULL);

		gtk_widget_style_get (widget,
				      "interior_focus", &interior_focus,
				      "focus-line-width", &focus_width,
				      "focus-padding", &focus_pad,
				      NULL);
		
		*state_type = GTK_WIDGET_STATE (widget);
		if ((*state_type != GTK_STATE_NORMAL) &&
		    (*state_type != GTK_STATE_PRELIGHT)) {
			*state_type = GTK_STATE_NORMAL;
		}

		if (bin->child) {
			width = indicator_spacing * 3 + indicator_size ;
		} else {
			width = widget->allocation.width - 2 * GTK_CONTAINER (widget)->border_width;
		}
		
		*x = widget->allocation.x + GTK_CONTAINER (widget)->border_width + (width) / 2;
		*y = widget->allocation.y + widget->allocation.height / 2;

		if (interior_focus == FALSE) {
			*x += focus_width + focus_pad;
		}

		*state_type = GTK_WIDGET_STATE (widget) == GTK_STATE_ACTIVE ? GTK_STATE_NORMAL : GTK_WIDGET_STATE (widget);

		if (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL) {
			*x = widget->allocation.x + widget->allocation.width - (*x - widget->allocation.x);
		}
	} else {
		*x = 0;
		*y = 0;
		*state_type = GTK_STATE_NORMAL;
	}
}

static gboolean
expand_collapse_timeout (gpointer data)
{
	GtkWidget *widget = data;
	CDDBDisclosure *disclosure = data;
	GtkStateType state_type;
	int x, y;
	gboolean ret = TRUE;

	GDK_THREADS_ENTER ();

	g_return_val_if_fail (GTK_IS_WIDGET (widget), FALSE);
	g_return_val_if_fail (IS_CDDB_DISCLOSURE (disclosure), FALSE);
	
	if (widget->window) {
		gdk_window_invalidate_rect (widget->window, &widget->allocation, TRUE);
		get_x_y (disclosure, &x, &y, &state_type);
		
		gtk_paint_expander (widget->style,
				    widget->window,
				    state_type,
				    &widget->allocation,
				    widget,
				    "disclosure",
				    x, y,
				    disclosure->priv->style);
	}

	disclosure->priv->style += disclosure->priv->direction;
	if ((int) disclosure->priv->style > (int) GTK_EXPANDER_EXPANDED) {
		disclosure->priv->style = GTK_EXPANDER_EXPANDED;

		if (disclosure->priv->container != NULL) {
			gtk_widget_show (disclosure->priv->container);
		}

		g_object_set (G_OBJECT (disclosure),
			      "label", disclosure->priv->hidden,
			      NULL);
			      
		ret = FALSE;
	} else if ((int) disclosure->priv->style < (int) GTK_EXPANDER_COLLAPSED) {
		disclosure->priv->style = GTK_EXPANDER_COLLAPSED;

		if (disclosure->priv->container != NULL) {
			gtk_widget_hide (disclosure->priv->container);
				       
		}

		g_object_set (G_OBJECT (disclosure),
			      "label", disclosure->priv->shown,
			      NULL);

		ret = FALSE;
	} 

	if (ret == FALSE)
		disclosure->priv->expand_id = 0;

	GDK_THREADS_LEAVE ();
	return ret;
}

static void
do_animation (CDDBDisclosure *disclosure,
	      gboolean opening)
{
	g_return_if_fail (IS_CDDB_DISCLOSURE (disclosure));

	if (disclosure->priv->expand_id > 0) {
		g_source_remove (disclosure->priv->expand_id);
		disclosure->priv->expand_id = 0;
	}

	disclosure->priv->direction = opening ? 1 : -1;
	disclosure->priv->expand_id = g_timeout_add (50, expand_collapse_timeout, disclosure);
}

static void
toggled (GtkToggleButton *tb)
{
	CDDBDisclosure *disclosure;

	disclosure = CDDB_DISCLOSURE (tb);
	do_animation (disclosure, gtk_toggle_button_get_active (tb));

	if (disclosure->priv->container == NULL) {
		return;
	}
}

static void
draw_indicator (GtkCheckButton *check,
		GdkRectangle *area)
{
	GtkWidget *widget = GTK_WIDGET (check);
	CDDBDisclosure *disclosure = CDDB_DISCLOSURE (check);
	GtkStateType state_type;
	int x, y;

	/* GtkCheckButton-like prelighting, unsurprisingly from gtkcheckbutton.c */
	if (GTK_WIDGET_STATE (check) == GTK_STATE_PRELIGHT)
	{
		GdkRectangle restrict_area;
		GdkRectangle new_area;

		restrict_area.x = widget->allocation.x + GTK_CONTAINER (widget)->border_width;
		restrict_area.y = widget->allocation.y + GTK_CONTAINER (widget)->border_width;
		restrict_area.width = widget->allocation.width - (2 * GTK_CONTAINER (widget)->border_width);
		restrict_area.height = widget->allocation.height - (2 * GTK_CONTAINER (widget)->border_width);

		if (gdk_rectangle_intersect (area, &restrict_area, &new_area))
		{
			gtk_paint_flat_box (widget->style, widget->window, GTK_STATE_PRELIGHT,
					GTK_SHADOW_ETCHED_OUT, 
					area, widget, "checkbutton",
					new_area.x, new_area.y,
					new_area.width, new_area.height);
		}
	}

	get_x_y (disclosure, &x, &y, &state_type);
	gtk_paint_expander (widget->style,
			    widget->window,
			    state_type,
			    area,
			    widget,
			    "treeview",
			    x, y,
			    disclosure->priv->style);
}

static void
class_init (CDDBDisclosureClass *klass)
{
	GObjectClass *object_class;
	GtkObjectClass *gtk_object_class = (GtkObjectClass *) klass;
	GtkWidgetClass *widget_class;
	GtkCheckButtonClass *button_class;
	GtkToggleButtonClass *toggle_class;
	
	object_class = G_OBJECT_CLASS (klass);
	widget_class = GTK_WIDGET_CLASS (klass);
	button_class = GTK_CHECK_BUTTON_CLASS (klass);
	toggle_class = GTK_TOGGLE_BUTTON_CLASS (klass);
	
	toggle_class->toggled = toggled;
	button_class->draw_indicator = draw_indicator;

	object_class->finalize = finalize;

	gtk_object_class->destroy = cddb_disclosure_destroy;

	parent_class = g_type_class_peek_parent (klass);

	gtk_widget_class_install_style_property (widget_class,
						 g_param_spec_int ("expander_size",
								   _("Expander Size"),
								   _("Size of the expander arrow"),
								   0, G_MAXINT,
								   10, G_PARAM_READABLE));
}

static void
init (CDDBDisclosure *disclosure)
{
	disclosure->priv = g_new0 (CDDBDisclosurePrivate, 1);
	disclosure->priv->expander_size = 10;
}

GType
cddb_disclosure_get_type (void)
{
	static GType type = 0;

	if (type == 0) {
		GTypeInfo info = {
			sizeof (CDDBDisclosureClass),
			NULL, NULL, (GClassInitFunc) class_init, NULL, NULL,
			sizeof (CDDBDisclosure), 0, (GInstanceInitFunc) init
		};

		type = g_type_register_static (GTK_TYPE_CHECK_BUTTON, "CDDBDisclosure", &info, 0);
	}

	return type;
}

GtkWidget *
cddb_disclosure_new (const char *shown,
		     const char *hidden)
{
	CDDBDisclosure *disclosure;

	disclosure = g_object_new (cddb_disclosure_get_type (), "label", shown, "use_underline", TRUE, NULL);

	disclosure->priv->shown = g_strdup (shown);
	disclosure->priv->hidden = g_strdup (hidden);
	return GTK_WIDGET (disclosure);
}

void
cddb_disclosure_set_container 	(CDDBDisclosure *cddb, GtkWidget *widget)
{
	if (widget != NULL) {
		g_object_ref (widget);
	}
	if (cddb->priv->container != NULL) {
		g_object_unref (cddb->priv->container);
	}
	cddb->priv->container = widget;
}

/* Strings for custom glade widgets (see below) can't be translated, so provide
 * this function for setting translatable labels after the widget has been
 * created by glade.
 */
void
cddb_disclosure_set_labels	(CDDBDisclosure *cddb,
		                 const char *label_when_shown,
		                 const char *label_when_hidden)
{
	gboolean active;

	g_free (cddb->priv->shown);	
	g_free (cddb->priv->hidden);	
	cddb->priv->shown = g_strdup (label_when_shown);
	cddb->priv->hidden = g_strdup (label_when_hidden);

	/* update the correct label text depending on button state */
	active = gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON(cddb));
	g_object_set (G_OBJECT(cddb),
		      "label", active ? cddb->priv->shown : cddb->priv->hidden,
		      NULL);
}

/* Custom widget creation function for glade */
GtkWidget *
cddb_disclosure_new_from_glade	(gchar *widget_name,
		                 gchar *string1, gchar *string2,
				 gint int1, gint int2)
{
	GtkWidget *w = cddb_disclosure_new ("", "");
	gtk_widget_set_name (w, widget_name);
	gtk_widget_show (w);
	return w;
}
