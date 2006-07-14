/*
 *  arch-tag: Header for functions shared by the rating widget and cell renderer.
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef RB_RATING_HELPER_H
#define RB_RATING_HELPER_H 1

#include <gtk/gtk.h>

typedef struct _RBRatingPixbufs RBRatingPixbufs;

/* Number of stars */
#define RB_RATING_MAX_SCORE 5

gboolean rb_rating_render_stars (GtkWidget *widget, GdkWindow *window,
				 RBRatingPixbufs *pixbufs,
				 gulong x, gulong y,
				 gulong x_offset, gulong y_offset,
				 gdouble rating, gboolean selected);

double   rb_rating_get_rating_from_widget (GtkWidget *widget,
					   gint widget_x, gint widget_width,
					   double current_rating);

RBRatingPixbufs *rb_rating_pixbufs_new (void);
void             rb_rating_pixbufs_free (RBRatingPixbufs *pixbufs);

void   rb_rating_install_rating_property (GObjectClass *klass, gulong prop);

#endif
