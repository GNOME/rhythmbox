/* 
 *  Copyright (C) 2002 Olivier Martin <oleevye@wanadoo.fr>
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

#include <string.h>
#include <gtk/gtklabel.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkiconfactory.h>

#include "rb-rating.h"
#include "rb-stock-icons.h"
#include "rb-cut-and-paste-code.h"

#define OFFSET 8
#define MAX_SCORE 5

static void rb_rating_class_init (RBRatingClass *class);
static void rb_rating_init (RBRating *label);
static void rb_rating_finalize (GObject *object);
static void rb_rating_get_property (GObject *object,
				    guint param_id,
				    GValue *value,
				    GParamSpec *pspec);
static void rb_rating_set_property (GObject *object,
				    guint param_id,
				    const GValue *value,
				    GParamSpec *pspec);
static void rb_rating_size_request (GtkWidget *widget,
				    GtkRequisition *requisition);
static gboolean rb_rating_expose(GtkWidget *widget, 
				 GdkEventExpose *event);
static gboolean rb_rating_button_press_cb (GtkWidget *widget,
					   GdkEventButton *event,
					   RBRating *rating);

struct RBRatingPrivate
{
	int score;

	GdkPixbuf *pix_star;
	GdkPixbuf *pix_blank;
};

enum
{
	PROP_0,
	PROP_SCORE
};

enum
{
	RATED,
	LAST_SIGNAL
};

static GObjectClass *parent_class = NULL;

static guint rb_rating_signals[LAST_SIGNAL] = { 0 };

GType 
rb_rating_get_type (void)
{
        static GType rb_rating_type = 0;

        if (rb_rating_type == 0)
        {
                static const GTypeInfo our_info =
                {
                        sizeof (RBRatingClass),
                        NULL, /* base_init */
                        NULL, /* base_finalize */
                        (GClassInitFunc) rb_rating_class_init,
                        NULL,
                        NULL, /* class_data */
                        sizeof (RBRating),
                        0, /* n_preallocs */
                        (GInstanceInitFunc) rb_rating_init
                };

                rb_rating_type = g_type_register_static (GTK_TYPE_EVENT_BOX,
							 "RBRating",
							 &our_info, 0);
        }

        return rb_rating_type;
}

static void
rb_rating_class_init (RBRatingClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	GtkWidgetClass *widget_class;

	widget_class = (GtkWidgetClass*) klass;
	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_rating_finalize;
	object_class->get_property = rb_rating_get_property;
	object_class->set_property = rb_rating_set_property;

	widget_class->expose_event = rb_rating_expose;
	widget_class->size_request = rb_rating_size_request;

	g_object_class_install_property (object_class,
					 PROP_SCORE,
					 g_param_spec_int ("score",
							   "Rating score",
							   "Rating score",
							   0, 5, 0,
							   G_PARAM_READWRITE));

	rb_rating_signals[RATED] = 
		g_signal_new ("rated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBRatingClass, rated),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__INT,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_INT);
}

static void
rb_rating_init (RBRating *rating)
{
	GtkWidget *dummy;

	rating->priv = g_new0 (RBRatingPrivate, 1);

	/* create the needed icons */
	dummy = gtk_label_new (NULL);
	rating->priv->pix_star = gtk_widget_render_icon (dummy,
							 RB_STOCK_SET_STAR,
							 GTK_ICON_SIZE_MENU,
							 NULL);
	rating->priv->pix_blank = gtk_widget_render_icon (dummy,
							  RB_STOCK_NO_STAR,
							  GTK_ICON_SIZE_MENU,
							  NULL);
	gtk_widget_destroy (dummy);

	/* register some signals */
	g_signal_connect (G_OBJECT (rating),
			  "button_press_event",
			  G_CALLBACK (rb_rating_button_press_cb),
			  rating);
}

static void
rb_rating_finalize (GObject *object)
{
	RBRating *rating;

	rating = RB_RATING (object);

	g_object_unref (G_OBJECT (rating->priv->pix_star));
	g_object_unref (G_OBJECT (rating->priv->pix_blank));
	g_free (rating->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_rating_get_property (GObject *object,
			guint param_id,
			GValue *value,
			GParamSpec *pspec)
{
	RBRating *rating = RB_RATING (object);
  
	switch (param_id)
	{
	case PROP_SCORE:
		g_value_set_int (value, rating->priv->score);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}


static void
rb_rating_set_property (GObject *object,
			guint param_id,
			const GValue *value,
			GParamSpec *pspec)
{
	RBRating *rating= RB_RATING (object);
  
	switch (param_id)
	{
	case PROP_SCORE:
		rating->priv->score = g_value_get_int (value);
		gtk_widget_queue_draw (GTK_WIDGET (rating));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

RBRating *
rb_rating_new ()
{
	RBRating *rating;
  
	rating = g_object_new (RB_TYPE_RATING, NULL);

	g_return_val_if_fail (rating->priv != NULL, NULL);
  
	return rating;
}

static void
rb_rating_size_request (GtkWidget *widget,
			GtkRequisition *requisition)
{
	int icon_size;

	g_return_if_fail (requisition != NULL);

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_size, NULL);

	requisition->width = 5 * icon_size + OFFSET;
	requisition->height = icon_size;
}

static gboolean
rb_rating_expose (GtkWidget *widget, 
		  GdkEventExpose *event)
{
	int icon_size;

	g_return_val_if_fail (RB_IS_RATING (widget), FALSE);

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_size, NULL);

	if (GTK_WIDGET_DRAWABLE (widget) == TRUE)
	{
		int i, y_offset;
		RBRating *rating = RB_RATING (widget);

		/* make the widget prettier */
		gtk_paint_flat_box (widget->style, widget->window,
				    GTK_STATE_NORMAL, GTK_SHADOW_NONE,
				    NULL, widget, "text", 0, 0,
				    widget->allocation.width,
				    widget->allocation.height);

		gtk_paint_shadow (widget->style, widget->window,
				  GTK_STATE_NORMAL, GTK_SHADOW_IN,
				  NULL, widget, "text", 0, 0,
				  widget->requisition.width + 2,
				  widget->allocation.height);

		y_offset = (widget->allocation.height - widget->requisition.height) / 2;

		/* draw a blank area at the beggining, this lets the user click
		 * in this area to unset the rating */
		gdk_pixbuf_render_to_drawable_alpha (rating->priv->pix_blank,
						     widget->window,
						     0, 0,
						     0, 0,
						     OFFSET, icon_size,
						     GDK_PIXBUF_ALPHA_FULL, 0,
						     GDK_RGB_DITHER_NORMAL, 0, 0);


		/* draw the stars */
		for (i = 0; i < MAX_SCORE; i++)
		{
			GdkPixbuf *pixbuf;
			GtkStateType state = GTK_STATE_INSENSITIVE;
			int offset = 0;

			if (i < rating->priv->score)
			{
				pixbuf = rating->priv->pix_star;
				offset = 120;
			}
			else
			{
				pixbuf = rating->priv->pix_blank;
			}

			if (pixbuf == NULL)
				return FALSE;

			pixbuf = eel_create_colorized_pixbuf (pixbuf,
							      widget->style->text[state].red + offset,
							      widget->style->text[state].green + offset,
							      widget->style->text[state].blue + offset);

			gdk_pixbuf_render_to_drawable_alpha (pixbuf,
							     widget->window,
							     0, 0,
							     OFFSET + i * icon_size, y_offset,
							     icon_size, icon_size,
							     GDK_PIXBUF_ALPHA_FULL, 0,
							     GDK_RGB_DITHER_NORMAL, 0, 0);
			g_object_unref (G_OBJECT (pixbuf));
		}


		return TRUE;
	}

	return FALSE;
}

static gboolean
rb_rating_button_press_cb (GtkWidget *widget,
			   GdkEventButton *event,
			   RBRating *rating)
{
	int mouse_x, mouse_y, icon_size, score = 0;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (RB_IS_RATING (rating), FALSE);

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_size, NULL);
	gtk_widget_get_pointer (widget, &mouse_x, &mouse_y);

	/* ensure the user clicks within the good area */
	if (mouse_x >= 0 && mouse_x <= widget->requisition.width)
	{
		if (mouse_x <= OFFSET)
			score = 0;
		else
			score = ((mouse_x - OFFSET) / icon_size) + 1;

		if (score > 5) score = 5;
		if (score < 0) score = 0;

		g_signal_emit (G_OBJECT (rating), 
			       rb_rating_signals[RATED], 
			       0, score);

		return TRUE;
	}

	return FALSE;
}
