/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/* rbcellrendererpixbuf.c
 *
 * Copyright (C) 2000  Red Hat, Inc.,  Jonathan Blandford <jrb@redhat.com>
 * Copyright (C) 2002  Jorn Baayen <jorn@nl.linux.org>
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
 */

#include <config.h>

#include <stdlib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "gseal-gtk-compat.h"
#include "rb-cell-renderer-pixbuf.h"
#include "rb-cut-and-paste-code.h"

static void rb_cell_renderer_pixbuf_get_property  (GObject                    *object,
						   guint                       param_id,
						   GValue                     *value,
						   GParamSpec                 *pspec);
static void rb_cell_renderer_pixbuf_set_property  (GObject                    *object,
						   guint                       param_id,
						   const GValue               *value,
						   GParamSpec                 *pspec);
static void rb_cell_renderer_pixbuf_init       (RBCellRendererPixbuf      *celltext);
static void rb_cell_renderer_pixbuf_class_init (RBCellRendererPixbufClass *class);
static void rb_cell_renderer_pixbuf_get_size   (GtkCellRenderer            *cell,
						GtkWidget                  *widget,
						GdkRectangle               *rectangle,
						gint                       *x_offset,
						gint                       *y_offset,
						gint                       *width,
						gint                       *height);
static void rb_cell_renderer_pixbuf_render     (GtkCellRenderer            *cell,
						GdkWindow                  *window,
						GtkWidget                  *widget,
						GdkRectangle               *background_area,
						GdkRectangle               *cell_area,
						GdkRectangle               *expose_area,
						guint                       flags);
static gboolean rb_cell_renderer_pixbuf_activate (GtkCellRenderer     *cell,
						  GdkEvent            *event,
						  GtkWidget           *widget,
						  const gchar         *path,
						  GdkRectangle        *background_area,
						  GdkRectangle        *cell_area,
						  GtkCellRendererState flags);

enum {
	PROP_ZERO,
	PROP_PIXBUF
};

enum
{
	PIXBUF_CLICKED,
	LAST_SIGNAL
};

G_DEFINE_TYPE (RBCellRendererPixbuf, rb_cell_renderer_pixbuf, GTK_TYPE_CELL_RENDERER)

/**
 * SECTION:rb-cell-renderer-pixbuf
 * @short_description: #GtkCellRenderer for displaying pixbufs in tree views
 *
 * This is similar to #GtkCellRendererPixbuf, except that it also emits a signal
 * when the pixbuf is clicked on, and it can only use pixbuf objects.
 */

static guint rb_cell_renderer_pixbuf_signals [LAST_SIGNAL] = { 0 };

static void
rb_cell_renderer_pixbuf_init (RBCellRendererPixbuf *cellpixbuf)
{
	/* set the renderer able to be activated */
	g_object_set (cellpixbuf,
		      "mode", GTK_CELL_RENDERER_MODE_ACTIVATABLE,
		      NULL);
}

static void
rb_cell_renderer_pixbuf_class_init (RBCellRendererPixbufClass *class)
{
	GObjectClass *object_class = G_OBJECT_CLASS (class);
	GtkCellRendererClass *cell_class = GTK_CELL_RENDERER_CLASS (class);

	object_class->get_property = rb_cell_renderer_pixbuf_get_property;
	object_class->set_property = rb_cell_renderer_pixbuf_set_property;

	cell_class->get_size = rb_cell_renderer_pixbuf_get_size;
	cell_class->render = rb_cell_renderer_pixbuf_render;
	cell_class->activate = rb_cell_renderer_pixbuf_activate;

	/**
	 * RBCellRendererPixbuf:pixbuf:
	 *
	 * The pixbuf to render in the cell.
	 */
	g_object_class_install_property (object_class,
					 PROP_PIXBUF,
					 g_param_spec_object ("pixbuf",
							      _("Pixbuf Object"),
							      _("The pixbuf to render."),
							      GDK_TYPE_PIXBUF,
							      G_PARAM_READABLE |
							      G_PARAM_WRITABLE));

	/**
	 * RBCellRendererPixbuf::pixbuf-clicked:
	 * @renderer: the #RBCellRendererPixbuf
	 * @path: the #GtkTreePath to the row that was clicked
	 *
	 * Emitted when the user clicks on the pixbuf cell.
	 */
	rb_cell_renderer_pixbuf_signals[PIXBUF_CLICKED] =
		g_signal_new ("pixbuf-clicked",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_FIRST,
			      G_STRUCT_OFFSET (RBCellRendererPixbufClass, pixbuf_clicked),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__STRING,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
}

static void
rb_cell_renderer_pixbuf_get_property (GObject        *object,
				      guint           param_id,
				      GValue         *value,
				      GParamSpec     *pspec)
{
  RBCellRendererPixbuf *cellpixbuf = RB_CELL_RENDERER_PIXBUF (object);

  switch (param_id)
    {
    case PROP_PIXBUF:
      g_value_set_object (value,
                          cellpixbuf->pixbuf ? G_OBJECT (cellpixbuf->pixbuf) : NULL);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

static void
rb_cell_renderer_pixbuf_set_property (GObject      *object,
				      guint         param_id,
				      const GValue *value,
				      GParamSpec   *pspec)
{
  GdkPixbuf *pixbuf;
  RBCellRendererPixbuf *cellpixbuf = RB_CELL_RENDERER_PIXBUF (object);

  switch (param_id)
    {
    case PROP_PIXBUF:
      pixbuf = (GdkPixbuf*) g_value_get_object (value);
      if (pixbuf)
        g_object_ref (G_OBJECT (pixbuf));
      if (cellpixbuf->pixbuf)
	g_object_unref (G_OBJECT (cellpixbuf->pixbuf));
      cellpixbuf->pixbuf = pixbuf;
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, param_id, pspec);
      break;
    }
}

/**
 * rb_cell_renderer_pixbuf_new:
 *
 * Creates a new #RBCellRendererPixbuf.
 *
 * Return value: the new cell renderer
 **/
GtkCellRenderer *
rb_cell_renderer_pixbuf_new (void)
{
  return GTK_CELL_RENDERER (g_object_new (rb_cell_renderer_pixbuf_get_type (), NULL, NULL));
}

static void
rb_cell_renderer_pixbuf_get_size (GtkCellRenderer *cell,
				  GtkWidget       *widget,
				  GdkRectangle    *cell_area,
				  gint            *x_offset,
				  gint            *y_offset,
				  gint            *width,
				  gint            *height)
{
  RBCellRendererPixbuf *cellpixbuf = (RBCellRendererPixbuf *) cell;
  gint pixbuf_width = 0;
  gint pixbuf_height = 0;
  gint calc_width;
  gint calc_height;
  gint xpad, ypad;
  gfloat xalign, yalign;

  if (cellpixbuf->pixbuf)
    {
      pixbuf_width = gdk_pixbuf_get_width (cellpixbuf->pixbuf);
      pixbuf_height = gdk_pixbuf_get_height (cellpixbuf->pixbuf);
    }

  gtk_cell_renderer_get_padding (GTK_CELL_RENDERER (cellpixbuf), &xpad, &ypad);
  calc_width = xpad * 2 + pixbuf_width;
  calc_height = ypad * 2 + pixbuf_height;

  if (x_offset) *x_offset = 0;
  if (y_offset) *y_offset = 0;

  if (cell_area && pixbuf_width > 0 && pixbuf_height > 0)
    {
      gtk_cell_renderer_get_alignment (GTK_CELL_RENDERER (cellpixbuf), &xalign, &yalign);

      if (x_offset)
	{
	  *x_offset = xalign * (cell_area->width - calc_width - (2 * xpad));
	  *x_offset = MAX (*x_offset, 0) + xpad;
	}
      if (y_offset)
	{
	  *y_offset = yalign * (cell_area->height - calc_height - (2 * ypad));
	  *y_offset = MAX (*y_offset, 0) + ypad;
	}
    }

  if (calc_width)
    *width = calc_width;

  if (height)
    *height = calc_height;
}

static void
rb_cell_renderer_pixbuf_render (GtkCellRenderer    *cell,
				GdkWindow          *window,
				GtkWidget          *widget,
				GdkRectangle       *background_area,
				GdkRectangle       *cell_area,
				GdkRectangle       *expose_area,
				guint               flags)

{
  RBCellRendererPixbuf *cellpixbuf = (RBCellRendererPixbuf *) cell;
  GdkRectangle pix_rect;
  GdkRectangle draw_rect;
  GtkStateType state;
  gint xpad, ypad;

  if ((flags & GTK_CELL_RENDERER_SELECTED) == GTK_CELL_RENDERER_SELECTED)
    {
      if (gtk_widget_has_focus (widget))
        state = GTK_STATE_SELECTED;
      else
        state = GTK_STATE_ACTIVE;
    }
  else
    {
      if (gtk_widget_get_state (widget) == GTK_STATE_INSENSITIVE)
        state = GTK_STATE_INSENSITIVE;
      else
        state = GTK_STATE_NORMAL;
    }

  if (!cellpixbuf->pixbuf)
    return;

  rb_cell_renderer_pixbuf_get_size (cell, widget, cell_area,
				     &pix_rect.x,
				     &pix_rect.y,
				     &pix_rect.width,
				     &pix_rect.height);

  pix_rect.x += cell_area->x;
  pix_rect.y += cell_area->y;
  gtk_cell_renderer_get_padding (cell, &xpad, &ypad);
  pix_rect.width -= xpad * 2;
  pix_rect.height -= ypad * 2;

  if (gdk_rectangle_intersect (cell_area, &pix_rect, &draw_rect)) {
    cairo_t *cr = gdk_cairo_create (window);
    gdk_cairo_set_source_pixbuf (cr, cellpixbuf->pixbuf, pix_rect.x, pix_rect.y);
    gdk_cairo_rectangle (cr, &draw_rect);
    cairo_paint (cr);
    cairo_destroy (cr);
  }
}

static gboolean
rb_cell_renderer_pixbuf_activate (GtkCellRenderer *cell,
				  GdkEvent *event,
				  GtkWidget *widget,
				  const gchar *path,
				  GdkRectangle *background_area,
				  GdkRectangle *cell_area,
				  GtkCellRendererState flags)
{
  int mouse_x, mouse_y;
  RBCellRendererPixbuf *cellpixbuf = (RBCellRendererPixbuf *) cell;

  g_return_val_if_fail (RB_IS_CELL_RENDERER_PIXBUF (cellpixbuf), FALSE);

  if (event == NULL) {
    return FALSE;
  }
  /* only handle mouse events */
  switch (event->type) {
    case GDK_BUTTON_PRESS:
    case GDK_BUTTON_RELEASE:
      break;
    default:
      return FALSE;
  }

  gtk_widget_get_pointer (widget, &mouse_x, &mouse_y);
  gtk_tree_view_convert_widget_to_bin_window_coords (GTK_TREE_VIEW (widget),
						     mouse_x, mouse_y,
						     &mouse_x, &mouse_y);

  /* ensure the user clicks within the good cell */
  if (mouse_x - cell_area->x >= 0
      && mouse_x - cell_area->x <= cell_area->width) {
    g_signal_emit (G_OBJECT (cellpixbuf), rb_cell_renderer_pixbuf_signals [PIXBUF_CLICKED], 0, path);
  }
  return TRUE;
}
