/* rbcellrendererpixbuf.h
 *
 * arch-tag: Header for star rating GtkTreeView cell renderer
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifndef __RB_CELL_RENDERER_RATING_H
#define __RB_CELL_RENDERER_RATING_H

#include <gtk/gtkcellrenderer.h>

G_BEGIN_DECLS

#define RB_TYPE_CELL_RENDERER_RATING		(rb_cell_renderer_rating_get_type ())
#define RB_CELL_RENDERER_RATING(obj)		(GTK_CHECK_CAST ((obj), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRating))
#define RB_CELL_RENDERER_RATING_CLASS(klass)	(GTK_CHECK_CLASS_CAST ((klass), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRatingClass))
#define RB_IS_CELL_RENDERER_RATING(obj)		(GTK_CHECK_TYPE ((obj), RB_TYPE_CELL_RENDERER_RATING))
#define RB_IS_CELL_RENDERER_RATING_CLASS(klass)	(GTK_CHECK_CLASS_TYPE ((klass), RB_TYPE_CELL_RENDERER_RATING))
#define RB_CELL_RENDERER_RATING_GET_CLASS(obj)  (GTK_CHECK_GET_CLASS ((obj), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRatingClass))

typedef struct RBCellRendererRatingPrivate RBCellRendererRatingPrivate;

typedef struct
{
	GtkCellRenderer parent;

	RBCellRendererRatingPrivate *priv;
} RBCellRendererRating;

typedef struct
{
	GtkCellRendererClass parent_class;

	void (*rated) (GtkTreePath *path, int rating);
} RBCellRendererRatingClass;

GtkType          rb_cell_renderer_rating_get_type (void);

GtkCellRenderer *rb_cell_renderer_rating_new      (void);

G_END_DECLS

#endif /* __RB_CELL_RENDERER_RATING_H */
