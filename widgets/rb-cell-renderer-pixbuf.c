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

#include "rb-cell-renderer-pixbuf.h"
#include "rb-cut-and-paste-code.h"

static void rb_cell_renderer_pixbuf_init       (RBCellRendererPixbuf      *celltext);
static void rb_cell_renderer_pixbuf_class_init (RBCellRendererPixbufClass *class);
static gboolean rb_cell_renderer_pixbuf_activate (GtkCellRenderer     *cell,
						  GdkEvent            *event,
						  GtkWidget           *widget,
						  const gchar         *path,
						  const GdkRectangle  *background_area,
						  const GdkRectangle  *cell_area,
						  GtkCellRendererState flags);

enum
{
	PIXBUF_CLICKED,
	LAST_SIGNAL
};

G_DEFINE_TYPE (RBCellRendererPixbuf, rb_cell_renderer_pixbuf, GTK_TYPE_CELL_RENDERER_PIXBUF)

/**
 * SECTION:rbcellrendererpixbuf
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

	cell_class->activate = rb_cell_renderer_pixbuf_activate;

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
			      NULL,
			      G_TYPE_NONE,
			      1,
			      G_TYPE_STRING);
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

static gboolean
rb_cell_renderer_pixbuf_activate (GtkCellRenderer *cell,
				  GdkEvent *event,
				  GtkWidget *widget,
				  const gchar *path,
				  const GdkRectangle *background_area,
				  const GdkRectangle *cell_area,
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

  gdk_window_get_device_position (gtk_widget_get_window (widget),
				  gdk_event_get_device (event),
				  &mouse_x,
				  &mouse_y,
				  NULL);
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
