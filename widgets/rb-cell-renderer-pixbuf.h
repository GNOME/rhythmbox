/* rbcellrendererpixbuf.h
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

#ifndef __RB_CELL_RENDERER_PIXBUF_H__
#define __RB_CELL_RENDERER_PIXBUF_H__

#include <gtk/gtk.h>

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define RB_TYPE_CELL_RENDERER_PIXBUF		(rb_cell_renderer_pixbuf_get_type ())
#define RB_CELL_RENDERER_PIXBUF(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_CELL_RENDERER_PIXBUF, RBCellRendererPixbuf))
#define RB_CELL_RENDERER_PIXBUF_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_CELL_RENDERER_PIXBUF, RBCellRendererPixbufClass))
#define RB_IS_CELL_RENDERER_PIXBUF(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_CELL_RENDERER_PIXBUF))
#define RB_IS_CELL_RENDERER_PIXBUF_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_CELL_RENDERER_PIXBUF))
#define RB_CELL_RENDERER_PIXBUF_GET_CLASS(obj)  (G_TYPE_CHECK_GET_CLASS ((obj), RB_TYPE_CELL_RENDERER_PIXBUF, RBCellRendererPixbufClass))

typedef struct _RBCellRendererPixbuf RBCellRendererPixbuf;
typedef struct _RBCellRendererPixbufClass RBCellRendererPixbufClass;

struct _RBCellRendererPixbuf
{
  GtkCellRendererPixbuf parent;
};

struct _RBCellRendererPixbufClass
{
  GtkCellRendererPixbufClass parent_class;

  void (*pixbuf_clicked) (RBCellRendererPixbuf *renderer, GtkTreePath *path);
};

GType            rb_cell_renderer_pixbuf_get_type (void);
GtkCellRenderer *rb_cell_renderer_pixbuf_new      (void);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* __RB_CELL_RENDERER_PIXBUF_H__ */
