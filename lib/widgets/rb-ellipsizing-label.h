/* 
   arch-tag: Header for subclass of GtkLabel that ellipsizes the text.

   Copyright (C) 2001 Eazel, Inc.

   The Gnome Library is free software; you can redistribute it and/or
   modify it under the terms of the GNU Library General Public License as
   published by the Free Software Foundation; either version 2 of the
   License, or (at your option) any later version.

   The Gnome Library is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   Library General Public License for more details.

   You should have received a copy of the GNU Library General Public
   License along with the Gnome Library; see the file COPYING.LIB.  If not,
   write to the Free Software Foundation, Inc., 59 Temple Place - Suite 330,
   Boston, MA 02111-1307, USA.

   Author: John Sullivan <sullivan@eazel.com>,
 */

#ifndef RB_ELLIPSIZING_LABEL_H
#define RB_ELLIPSIZING_LABEL_H

#include <gtk/gtklabel.h>

#define RB_TYPE_ELLIPSIZING_LABEL            (rb_ellipsizing_label_get_type ())
#define RB_ELLIPSIZING_LABEL(obj)            (GTK_CHECK_CAST ((obj), RB_TYPE_ELLIPSIZING_LABEL, RBEllipsizingLabel))
#define RB_ELLIPSIZING_LABEL_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), RB_TYPE_ELLIPSIZING_LABEL, RBEllipsizingLabelClass))
#define RB_IS_ELLIPSIZING_LABEL(obj)         (GTK_CHECK_TYPE ((obj), RB_TYPE_ELLIPSIZING_LABEL))
#define RB_IS_ELLIPSIZING_LABEL_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), RB_TYPE_ELLIPSIZING_LABEL))

typedef struct RBEllipsizingLabelPrivate RBEllipsizingLabelPrivate;

typedef enum {
        RB_ELLIPSIZE_START,
        RB_ELLIPSIZE_MIDDLE,
        RB_ELLIPSIZE_END
} RBEllipsizeMode;

typedef struct
{
	GtkLabel parent;

	RBEllipsizingLabelPrivate *priv;

	gboolean ellipsized;
} RBEllipsizingLabel;

typedef struct
{
	GtkLabelClass parent_class;
} RBEllipsizingLabelClass;

GtkType    rb_ellipsizing_label_get_type	(void);

GtkWidget *rb_ellipsizing_label_new		(const char *string);

void       rb_ellipsizing_label_set_mode        (RBEllipsizingLabel *label,
						 RBEllipsizeMode mode);

void       rb_ellipsizing_label_set_text	(RBEllipsizingLabel *label,
						 const char *string);

void       rb_ellipsizing_label_set_markup	(RBEllipsizingLabel *label,
						 const char *string);

gboolean   rb_ellipsizing_label_get_ellipsized  (RBEllipsizingLabel *label);

int		rb_ellipsizing_label_get_width	(RBEllipsizingLabel *label);
int	rb_ellipsizing_label_get_full_text_size (RBEllipsizingLabel *label);

G_END_DECLS

#endif /* RB_ELLIPSIZING_LABEL_H */
