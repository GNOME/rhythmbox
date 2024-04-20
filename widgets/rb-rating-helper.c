/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2004 Christophe Fergeau <teuf@gnome.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include "config.h"

#include <math.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include "rb-cut-and-paste-code.h"
#include "rb-rating-helper.h"
#include "rb-stock-icons.h"


/**
 * SECTION:rbratinghelper
 * @short_description: helper functions for displaying song ratings
 *
 * A few helper functions for dealing with ratings.  These are shared
 * between #RBRating and #RBCellRendererRating.
 */

struct _RBRatingPixbufs {
	GdkPixbuf *pix_star;
	GdkPixbuf *pix_dot;
	GdkPixbuf *pix_blank;
};

/**
 * rb_rating_pixbufs_free:
 * @pixbufs: #RBRatingPixbufs instance
 *
 * Frees a set of rating pixbufs.
 */
void
rb_rating_pixbufs_free (RBRatingPixbufs *pixbufs)
{
	if (pixbufs->pix_star != NULL)
		g_object_unref (pixbufs->pix_star);
	if (pixbufs->pix_dot != NULL)
		g_object_unref (pixbufs->pix_dot);
	if (pixbufs->pix_blank != NULL)
		g_object_unref (pixbufs->pix_blank);
}

/**
 * rb_rating_install_rating_property:
 * @klass: a #GObjectClass to add the property to
 * @prop: property index to use
 *
 * Installs a 'rating' property in the specified class.
 */
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

/**
 * rb_rating_pixbufs_load:
 *
 * Creates and returns a structure holding a set of pixbufs
 * to use to display ratings.
 *
 * Return value: #RBRatingPixbufs structure, or NULL if not all of
 * the pixbufs could be loaded.
 */
RBRatingPixbufs *
rb_rating_pixbufs_load (void)
{
	RBRatingPixbufs *pixbufs;
	GtkIconTheme *theme;
	gint width;

	pixbufs = g_new0 (RBRatingPixbufs, 1);
	if (pixbufs == NULL) {
		return NULL;
	}

	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, NULL, &width);

	pixbufs->pix_star = gtk_icon_theme_load_icon (theme,
						      RB_STOCK_SET_STAR,
						      width,
						      0,
						      NULL);
	pixbufs->pix_dot = gtk_icon_theme_load_icon (theme,
						     RB_STOCK_UNSET_STAR,
						     width,
						     0,
						     NULL);
	pixbufs->pix_blank = gtk_icon_theme_load_icon (theme,
						       RB_STOCK_NO_STAR,
						       width,
						       0,
						       NULL);
	if (pixbufs->pix_star != NULL &&
	    pixbufs->pix_dot != NULL &&
	    pixbufs->pix_blank != NULL) {
		return pixbufs;
	}

	rb_rating_pixbufs_free (pixbufs);
	g_free (pixbufs);
	return NULL;
}

/**
 * rb_rating_render_stars:
 * @widget: a #GtkWidget to render on behalf of
 * @cr: cairo context to render into
 * @pixbufs: a #RBRatingPixbufs structure
 * @x: source X coordinate within the rating pixbufs (usually 0)
 * @y: source Y coordinate within the rating pixbufs (usually 0)
 * @x_offset: destination X coordinate within the window
 * @y_offset: destination Y coordinate within the window
 * @rating: the rating to display (between 0.0 and 5.0)
 * @selected: TRUE if the widget is currently selected for input
 *
 * Renders a rating as a row of stars.  floor(@rating) large stars
 * are drawn, followed by 5-floor(@rating) small stars.
 *
 * Return value: TRUE if the stars were drawn successfully
 */
gboolean
rb_rating_render_stars (GtkWidget *widget,
			cairo_t *cr,
			RBRatingPixbufs *pixbufs,
			int x,
			int y,
			int x_offset,
			int y_offset,
			gdouble rating,
			gboolean selected)
{
	int i, icon_width;
	gboolean rtl;

	g_return_val_if_fail (widget != NULL, FALSE);
	g_return_val_if_fail (pixbufs != NULL, FALSE);

	rtl = (gtk_widget_get_direction (widget) == GTK_TEXT_DIR_RTL);
	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, NULL);

	for (i = 0; i < RB_RATING_MAX_SCORE; i++) {
		GdkPixbuf *buf;
		gint star_offset;
		int offset;
		GdkRGBA color;

		if (selected == TRUE) {
			offset = 0;
		} else {
			offset = 120;
		}

		if (i < rating)
			buf = pixbufs->pix_star;
		else if (i >= rating && i < RB_RATING_MAX_SCORE)
			buf = pixbufs->pix_dot;
		else
			buf = pixbufs->pix_blank;

		if (buf == NULL) {
			return FALSE;
		}

		gtk_style_context_get_color (gtk_widget_get_style_context (widget), gtk_widget_get_state_flags (widget), &color);
		buf = eel_create_colorized_pixbuf (buf,
						   ((guint16)(color.red * G_MAXUINT16) + offset) >> 8,
						   ((guint16)(color.green * G_MAXUINT16) + offset) >> 8,
						   ((guint16)(color.blue * G_MAXUINT16) + offset) >> 8);
		if (buf == NULL) {
			return FALSE;
		}

		if (rtl) {
			star_offset = (RB_RATING_MAX_SCORE - i - 1) * icon_width;
		} else {
			star_offset = i * icon_width;
		}

		gdk_cairo_set_source_pixbuf (cr, buf, x_offset + star_offset, y_offset);
		cairo_paint (cr);
		g_object_unref (buf);
	}

	return TRUE;
}

/**
 * rb_rating_get_rating_from_widget:
 * @widget: the #GtkWidget displaying the rating
 * @widget_x: x component of the position to query
 * @widget_width: width of the widget
 * @current_rating: the current rating displayed in the widget
 *
 * Updates the rating for a widget after the user clicks on the
 * rating.  If the user clicks on the Nth star, the rating is set
 * to N, unless the rating is already N, in which case the rating is
 * set to N-1.  This allows the user to set the rating to 0.
 *
 * Return value: the updated rating
 */
double
rb_rating_get_rating_from_widget (GtkWidget *widget,
				  gint widget_x,
				  gint widget_width,
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

/**
 * rb_rating_set_accessible_description:
 * @widget: a #GtkWidget for which to set the accessible name
 * @rating: the rating value to set
 *
 * Sets the accessible object description for the specified widget to reflect the
 * rating.
 *
 * We use the description instead of the name because the name might be set
 * indirectly from a relation, so it would be ignored anyway.
 */
void
rb_rating_set_accessible_description (GtkWidget *widget, gdouble rating)
{
	AtkObject *aobj;
	int stars;
	g_autofree gchar *adescription;

	aobj = gtk_widget_get_accessible (widget);

	stars = floor (rating);
	if (stars == 0) {
		adescription = g_strdup (_("No Stars"));
	} else {
		adescription = g_strdup_printf (ngettext ("%d Star", "%d Stars", stars), stars);
	}

	atk_object_set_description (aobj, adescription);
}

