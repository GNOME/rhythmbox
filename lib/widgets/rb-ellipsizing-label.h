/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */

/* eel-ellipsizing-label.h: Subclass of GtkLabel that ellipsizes the text.

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

#define GUL_TYPE_ELLIPSIZING_LABEL            (rb_ellipsizing_label_get_type ())
#define RB_ELLIPSIZING_LABEL(obj)            (GTK_CHECK_CAST ((obj), GUL_TYPE_ELLIPSIZING_LABEL, RBEllipsizingLabel))
#define RB_ELLIPSIZING_LABEL_CLASS(klass)    (GTK_CHECK_CLASS_CAST ((klass), GUL_TYPE_ELLIPSIZING_LABEL, RBEllipsizingLabelClass))
#define GUL_IS_ELLIPSIZING_LABEL(obj)         (GTK_CHECK_TYPE ((obj), GUL_TYPE_ELLIPSIZING_LABEL))
#define GUL_IS_ELLIPSIZING_LABEL_CLASS(klass) (GTK_CHECK_CLASS_TYPE ((klass), GUL_TYPE_ELLIPSIZING_LABEL))

typedef struct RBEllipsizingLabel	      RBEllipsizingLabel;
typedef struct RBEllipsizingLabelClass	      RBEllipsizingLabelClass;
typedef struct RBEllipsizingLabelDetails     RBEllipsizingLabelDetails;

struct RBEllipsizingLabel {
	GtkLabel parent;
	RBEllipsizingLabelDetails *details;
};

struct RBEllipsizingLabelClass {
	GtkLabelClass parent_class;
};

GtkType    rb_ellipsizing_label_get_type 	(void);
GtkWidget *rb_ellipsizing_label_new      	(const char          *string);
void       rb_ellipsizing_label_set_text 	(RBEllipsizingLabel *label,
					  	 const char          *string);
void       rb_ellipsizing_label_set_markup 	(RBEllipsizingLabel *label,
					    	 const char          *string);


#endif /* RB_ELLIPSIZING_LABEL_H */
