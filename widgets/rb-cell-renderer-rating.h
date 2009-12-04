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

#ifndef __RB_CELL_RENDERER_RATING_H
#define __RB_CELL_RENDERER_RATING_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_CELL_RENDERER_RATING		(rb_cell_renderer_rating_get_type ())
#define RB_CELL_RENDERER_RATING(obj)		(G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRating))
#define RB_CELL_RENDERER_RATING_CLASS(klass)	(G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRatingClass))
#define RB_IS_CELL_RENDERER_RATING(obj)		(G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_CELL_RENDERER_RATING))
#define RB_IS_CELL_RENDERER_RATING_CLASS(klass)	(G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_CELL_RENDERER_RATING))
#define RB_CELL_RENDERER_RATING_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_CELL_RENDERER_RATING, RBCellRendererRatingClass))

typedef struct RBCellRendererRatingPrivate RBCellRendererRatingPrivate;
typedef struct RBCellRendererRatingClassPrivate RBCellRendererRatingClassPrivate;
typedef struct _RBCellRendererRating RBCellRendererRating;
typedef struct _RBCellRendererRatingClass RBCellRendererRatingClass;

struct _RBCellRendererRating
{
	GtkCellRenderer parent;

	RBCellRendererRatingPrivate *priv;
};

struct _RBCellRendererRatingClass
{
	GtkCellRendererClass parent_class;

	void (*rated) (RBCellRendererRating *renderer, const char *path, double rating);

	RBCellRendererRatingClassPrivate *priv;

};

GType		 rb_cell_renderer_rating_get_type (void);

GtkCellRenderer *rb_cell_renderer_rating_new      (void);

G_END_DECLS

#endif /* __RB_CELL_RENDERER_RATING_H */
