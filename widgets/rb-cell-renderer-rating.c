/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * rb-cell-renderer-rating.c
 *
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 * Copyright (C) 2002  Olivier Martin <olive.martin@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <stdlib.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-cell-renderer-rating.h"
#include "rb-rating-helper.h"

static void rb_cell_renderer_rating_get_property (GObject *object,
						  guint param_id,
						  GValue *value,
						  GParamSpec *pspec);
static void rb_cell_renderer_rating_set_property (GObject *object,
						  guint param_id,
						  const GValue *value,
						  GParamSpec *pspec);
static void rb_cell_renderer_rating_init (RBCellRendererRating *celltext);
static void rb_cell_renderer_rating_class_init (RBCellRendererRatingClass *class);
static void rb_cell_renderer_rating_get_size  (GtkCellRenderer *cell,
					       GtkWidget *widget,
					       const GdkRectangle *rectangle,
					       gint *x_offset,
					       gint *y_offset,
					       gint *width,
					       gint *height);
static void rb_cell_renderer_rating_render (GtkCellRenderer *cell,
					    cairo_t *cr,
					    GtkWidget *widget,
					    const GdkRectangle *background_area,
					    const GdkRectangle *cell_area,
					    GtkCellRendererState flags);
static gboolean rb_cell_renderer_rating_activate (GtkCellRenderer *cell,
					          GdkEvent *event,
					          GtkWidget *widget,
					          const gchar *path,
					          const GdkRectangle *background_area,
					          const GdkRectangle *cell_area,
					          GtkCellRendererState flags);

struct RBCellRendererRatingPrivate
{
	double rating;
};

struct RBCellRendererRatingClassPrivate
{
	RBRatingPixbufs *pixbufs;
};

G_DEFINE_TYPE (RBCellRendererRating, rb_cell_renderer_rating, GTK_TYPE_CELL_RENDERER)
#define RB_CELL_RENDERER_RATING_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), \
						RB_TYPE_CELL_RENDERER_RATING, \
						RBCellRendererRatingPrivate))


/**
 * SECTION:rbcellrendererrating
 * @short_description: a #GtkCellRenderer for displaying song ratings
 *
 * This cell renderer is used to display song ratings in the #RBEntryView,
 * and allows the user to modify ratings directly in the track listing.
 */

enum
{
	PROP_0,
	PROP_RATING
};

enum
{
	RATED,
	LAST_SIGNAL
};

static guint rb_cell_renderer_rating_signals[LAST_SIGNAL] = { 0 };

static void
rb_cell_renderer_rating_init (RBCellRendererRating *cellrating)
{
	RBCellRendererRatingClass *klass;

	klass = RB_CELL_RENDERER_RATING_GET_CLASS (cellrating);
	if (klass->priv->pixbufs == NULL) {
		klass->priv->pixbufs = rb_rating_pixbufs_load ();
	}

	cellrating->priv = RB_CELL_RENDERER_RATING_GET_PRIVATE (cellrating);

	/* set the renderer able to be activated */
	g_object_set (cellrating,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      NULL);

}

static void
rb_cell_renderer_rating_class_init (RBCellRendererRatingClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (class);

	object_class->get_property = rb_cell_renderer_rating_get_property;
	object_class->set_property = rb_cell_renderer_rating_set_property;

	cell_class->get_size = rb_cell_renderer_rating_get_size;
	cell_class->render   = rb_cell_renderer_rating_render;
	cell_class->activate = rb_cell_renderer_rating_activate;

	class->priv = g_new0 (RBCellRendererRatingClassPrivate, 1);

	/**
	 * RBCellRendererRating:rating:
	 *
	 * The rating displayed by the renderer, as a floating point value
	 * between 0.0 and 5.0.
	 */
	rb_rating_install_rating_property (object_class, PROP_RATING);

	/**
	 * RBCellRendererRating::rated:
	 * @renderer: the #RBCellRendererRating
	 * @score: the new rating
	 * @path: string form of the #GtkTreePath to the row that was changed
	 *
	 * Emitted when the user changes the rating.
	 */
	rb_cell_renderer_rating_signals[RATED] =
		g_signal_new ("rated",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBCellRendererRatingClass, rated),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_DOUBLE);

	g_type_class_add_private (class, sizeof (RBCellRendererRatingPrivate));
}

static void
rb_cell_renderer_rating_get_property (GObject *object,
				      guint param_id,
				      GValue *value,
				      GParamSpec *pspec)
{
	RBCellRendererRating *cellrating = RB_CELL_RENDERER_RATING (object);

	switch (param_id) {
	case PROP_RATING:
		g_value_set_double (value, cellrating->priv->rating);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

static void
rb_cell_renderer_rating_set_property (GObject *object,
				      guint param_id,
				      const GValue *value,
				      GParamSpec *pspec)
{
	RBCellRendererRating *cellrating= RB_CELL_RENDERER_RATING (object);

	switch (param_id) {
	case PROP_RATING:
		cellrating->priv->rating = g_value_get_double (value);
		if (cellrating->priv->rating < 0)
			cellrating->priv->rating = 0;
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
		break;
	}
}

/**
 * rb_cell_renderer_rating_new:
 *
 * create a cell renderer that will
 * display some pixbufs for representing the rating of a song.
 * It is also able to update the rating.
 *
 * Return value: the new cell renderer
 **/
GtkCellRenderer *
rb_cell_renderer_rating_new (void)
{
	return GTK_CELL_RENDERER (g_object_new (rb_cell_renderer_rating_get_type (), NULL, NULL));
}

/* XXX implement get_preferred_height/width/height_for_width/width_for_height */
static void
rb_cell_renderer_rating_get_size (GtkCellRenderer *cell,
				  GtkWidget *widget,
				  const GdkRectangle *cell_area,
				  gint *x_offset,
				  gint *y_offset,
				  gint *width,
				  gint *height)
{
	gint icon_width;
	gint xpad, ypad;
	int h;
	RBCellRendererRating *cellrating = (RBCellRendererRating *) cell;

	gtk_icon_size_lookup (GTK_ICON_SIZE_MENU, &icon_width, NULL);
	gtk_cell_renderer_get_padding (GTK_CELL_RENDERER (cellrating), &xpad, &ypad);

	h = ypad * 2 + icon_width;

	if (x_offset)
		*x_offset = 0;

	if (y_offset)
		*y_offset = ((cell_area->height + 1) - h) / 2;

	if (width)
		*width = xpad * 2 + icon_width * RB_RATING_MAX_SCORE;

	if (height)
		*height = h;
}

static void
rb_cell_renderer_rating_render (GtkCellRenderer  *cell,
				cairo_t *cr,
				GtkWidget *widget,
				const GdkRectangle *background_area,
				const GdkRectangle *cell_area,
				GtkCellRendererState flags)

{
	gint xpad, ypad;
	gboolean selected;
	GdkRectangle pix_rect, draw_rect;
	RBCellRendererRating *cellrating = (RBCellRendererRating *) cell;
	RBCellRendererRatingClass *cell_class;

	cellrating = RB_CELL_RENDERER_RATING (cell);
	cell_class = RB_CELL_RENDERER_RATING_GET_CLASS (cellrating);
	rb_cell_renderer_rating_get_size (cell, widget, cell_area,
					  &pix_rect.x,
					  &pix_rect.y,
					  &pix_rect.width,
					  &pix_rect.height);

	pix_rect.x += cell_area->x;
	pix_rect.y += cell_area->y;
	gtk_cell_renderer_get_padding (cell, &xpad, &ypad);
	pix_rect.width -= xpad * 2;
	pix_rect.height -= ypad * 2;

	if (gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect) == FALSE)
		return;

	selected = (flags & GTK_CELL_RENDERER_SELECTED);

	rb_rating_render_stars (widget, cr, cell_class->priv->pixbufs,
				draw_rect.x - pix_rect.x,
				draw_rect.y - pix_rect.y,
				draw_rect.x, draw_rect.y,
				cellrating->priv->rating, selected);
}

static gboolean
rb_cell_renderer_rating_activate (GtkCellRenderer *cell,
				  GdkEvent *event,
				  GtkWidget *widget,
				  const gchar *path,
				  const GdkRectangle *background_area,
				  const GdkRectangle *cell_area,
				  GtkCellRendererState flags)
{
	int mouse_x, mouse_y;
	double rating;

	RBCellRendererRating *cellrating = (RBCellRendererRating *) cell;

	g_return_val_if_fail (RB_IS_CELL_RENDERER_RATING (cellrating), FALSE);

	gdk_window_get_device_position (gtk_widget_get_window (widget),
					gdk_event_get_device (event),
					&mouse_x,
					&mouse_y,
					NULL);
	gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget),
							   mouse_x, mouse_y,
							   &mouse_x, &mouse_y);

	rating = rb_rating_get_rating_from_widget (widget,
						   mouse_x - cell_area->x,
						   cell_area->width,
						   cellrating->priv->rating);

	if (rating != -1.0) {
		g_signal_emit (G_OBJECT (cellrating),
			       rb_cell_renderer_rating_signals[RATED],
			       0, path, rating);
	}

	return TRUE;
}
