/*
 *  arch-tag: Implementation of functions shared by the rating widget and cell renderer.
 *
 *  Copyright (C) 2004 Christophe Fergeau <teuf@gnome.org>
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


#include "rb-cut-and-paste-code.h"
#include "rb-rating-helper.h"
#include "rb-stock-icons.h"

struct _RBRatingPixbufs {
	GdkPixbuf *pix_star;
	GdkPixbuf *pix_dot;
	GdkPixbuf *pix_blank;
};

void
rb_rating_pixbufs_free (RBRatingPixbufs *pixbufs)
{
	g_object_unref (G_OBJECT (pixbufs->pix_star));
	g_object_unref (G_OBJECT (pixbufs->pix_dot));
	g_object_unref (G_OBJECT (pixbufs->pix_blank));
}

void 
rb_rating_install_rating_property (GObjectClass *klass, gulong prop)
{
	g_object_class_install_property (klass, prop,
					 g_param_spec_double ("rating",
							     ("Rating Value"),
							     ("Rating Value"),
							     0.0, (double)RB_RATING_MAX_SCORE, 
							      (double)RB_RATING_MAX_SCORE/2.0,
							     G_PARAM_READWRITE));

}

RBRatingPixbufs *
rb_rating_pixbufs_new (void)
{
	GtkWidget *dummy;
	RBRatingPixbufs *pixbufs;

	pixbufs = g_new0 (RBRatingPixbufs, 1);
	if (pixbufs == NULL) {
		return NULL;
	}

	dummy = gtk_label_new (NULL);
	pixbufs->pix_star = gtk_widget_render_icon (dummy,
						    RB_STOCK_SET_STAR,
						    GTK_ICON_SIZE_MENU,
						    NULL);
	if (pixbufs->pix_star == NULL) {
		goto error;
	}

	pixbufs->pix_dot = gtk_widget_render_icon (dummy,
						   RB_STOCK_UNSET_STAR,
						   GTK_ICON_SIZE_MENU,
						   NULL);
	if (pixbufs->pix_dot == NULL) {
		goto error;
	}

	pixbufs->pix_blank = gtk_widget_render_icon (dummy,
						     RB_STOCK_NO_STAR,
						     GTK_ICON_SIZE_MENU,
						     NULL);
	if (pixbufs->pix_blank == NULL) {
		goto error;
	}

	gtk_widget_destroy (dummy);
	return pixbufs;

 error:
	if (pixbufs->pix_star != NULL) {
		g_object_unref (G_OBJECT (pixbufs->pix_star));
	}
	if (pixbufs->pix_dot != NULL) {
		g_object_unref (G_OBJECT (pixbufs->pix_dot));
	}
	if (pixbufs->pix_blank != NULL) {
		g_object_unref (G_OBJECT (pixbufs->pix_blank));
	}
	gtk_widget_destroy (dummy);
	g_free (pixbufs);
	return NULL;
}

gboolean
rb_rating_render_stars (GtkWidget *widget, GdkWindow *window, 
			RBRatingPixbufs *pixbufs,
			gulong x, gulong y, gulong x_offset, gulong y_offset,
			gdouble rating, gboolean selected)
{
	int i, icon_width;
	gboolean rtl;

	rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, NULL);

	for (i = 0; i < RB_RATING_MAX_SCORE; i++) {
		GdkPixbuf *buf;
		GtkStateType state;
		gint star_offset;
		int offset;

		if (selected == TRUE) {
			offset = 0;
			if (GTK_WIDGET_HAS_FOCUS (widget))
				state = GTK_STATE_SELECTED;
			else
				state = GTK_STATE_ACTIVE;
		} else {
			offset = 120;
			if (GTK_WIDGET_STATE (widget) == GTK_STATE_INSENSITIVE)
				state = GTK_STATE_INSENSITIVE;
			else
				state = GTK_STATE_NORMAL;
		}

		if (i < rating)
			buf = pixbufs->pix_star;
		else if (i >= rating && i < RB_RATING_MAX_SCORE)
			buf = pixbufs->pix_dot;
		else
			buf = pixbufs->pix_blank;

		buf = eel_create_colorized_pixbuf (buf,
						   widget->style->text[state].red + offset,
						   widget->style->text[state].green + offset,
						   widget->style->text[state].blue + offset);
		if (buf == NULL)
			return FALSE;

		if (rtl) {
			star_offset = (RB_RATING_MAX_SCORE-i-1) * icon_width;
		} else {
			star_offset = i * icon_width;
		}


		gdk_pixbuf_render_to_drawable_alpha (buf,
						     window,
						     x, y, 
						     x_offset + star_offset,
						     y_offset,
						     icon_width,
						     icon_width,
						     GDK_PIXBUF_ALPHA_FULL,
						     0,
						     GDK_RGB_DITHER_NORMAL,
						     0, 0);
		g_object_unref (G_OBJECT (buf));
	}

	return TRUE;
}

double
rb_rating_get_rating_from_widget (GtkWidget *widget,
				  gint widget_x, gint widget_width,
				  double current_rating)
{
	int icon_width;
	double rating = -1.0;

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, NULL);

	/* ensure the user clicks within the good cell */
	if (widget_x >= 0 && widget_x <= widget_width) {
		gboolean rtl;

		rating = (int) (widget_x / icon_width) + 1;

		rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);
		if (rtl) {
			rating = RB_RATING_MAX_SCORE - rating + 1;
		}

		if (rating < 0) 
			rating = 0;

		if (rating > RB_RATING_MAX_SCORE)
			rating = RB_RATING_MAX_SCORE;
		
		if (rating == current_rating) {
			/* Make it possible to give a 0 rating to a song */
			rating--;
		}
	}

	return rating;
}
