/*
 *  arch-tag: Header for rating renderer object
 *
 *  Copyright (C) 2002 Olivier Martin <oleevye@wanadoo.fr>
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

#ifndef RB_RATING_H
#define RB_RATING_H

#include <gtk/gtkeventbox.h>

G_BEGIN_DECLS

#define RB_TYPE_RATING            (rb_rating_get_type ())
#define RB_RATING(obj)            (GTK_CHECK_CAST ((obj), RB_TYPE_RATING, RBRating))
#define RB_RATING_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), RB_TYPE_RATING, RBRatingClass))
#define RB_IS_RATING(obj)         (GTK_CHECK_TYPE ((obj), RB_TYPE_RATING))
#define RB_IS_RATING_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), RB_TYPE_RATING))

typedef struct RBRatingPrivate RBRatingPrivate;

typedef struct 
{
	GtkEventBox parent;

	RBRatingPrivate *priv;
} RBRating;

typedef struct 
{
	GtkEventBoxClass parent;

	void (*rated) (int score);
} RBRatingClass;


GtkType    rb_rating_get_type (void);

RBRating  *rb_rating_new      (void);

G_END_DECLS

#endif /* RB_RATING_H */
