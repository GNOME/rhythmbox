/*
 *  Copyright (C) 2002 Olivier Martin <olive.martin@gmail.com>
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

#ifndef RB_RATING_H
#define RB_RATING_H

#include <gtk/gtk.h>

G_BEGIN_DECLS

#define RB_TYPE_RATING            (rb_rating_get_type ())
#define RB_RATING(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_RATING, RBRating))
#define RB_RATING_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_RATING, RBRatingClass))
#define RB_IS_RATING(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_RATING))
#define RB_IS_RATING_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_RATING))

typedef struct _RBRating RBRating;
typedef struct _RBRatingClass RBRatingClass;
typedef struct _RBRatingPrivate RBRatingPrivate;

struct _RBRating
{
	GtkWidget parent;

	RBRatingPrivate *priv;
};

struct _RBRatingClass
{
	GtkWidgetClass parent;

	void (*rated) (RBRating *rating, double score);
	gboolean (*set_rating) (RBRating *rating, double score);
	gboolean (*adjust_rating) (RBRating *rating, double adjust);
};

GType      rb_rating_get_type (void);

RBRating  *rb_rating_new      (void);

G_END_DECLS

#endif /* RB_RATING_H */
