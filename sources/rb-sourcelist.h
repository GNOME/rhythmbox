/*
 * arch-tag: Header for main "Sources" display widget
 *
 * Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 */

#ifndef __RB_SOURCELIST_H
#define __RB_SOURCELIST_H

#include <gtk/gtkscrolledwindow.h>

#include "rb-source.h"
#include "rb-sourcelist-model.h"

G_BEGIN_DECLS

#define RB_TYPE_SOURCELIST	      (rb_sourcelist_get_type ())
#define RB_SOURCELIST(obj)	      (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_SOURCELIST, RBSourceList))
#define RB_SOURCELIST_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_SOURCELIST, RBSourceListClass))
#define RB_IS_SOURCELIST(obj)	      (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_SOURCELIST))
#define RB_IS_SOURCELIST_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((obj), RB_TYPE_SOURCELIST))
#define RB_SOURCELIST_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_SOURCELIST, RBSourceListClass))

typedef struct RBSourceListPriv RBSourceListPriv;

typedef struct RBSourceList
{
	GtkScrolledWindow parent;

	RBSourceListPriv *priv;
} RBSourceList;

typedef struct RBSourceListClass
{
	GtkScrolledWindowClass parent_class;

	/* signals */
	void (*selected) (RBSourceList *list, RBSource *source);

	void (*drop_received) (RBSourceList *list, RBSource *target, GtkSelectionData *data);

	void (*source_activated) (RBSourceList *list, RBSource *target);

	gboolean (*show_popup) (RBSourceList *list, RBSource *target);
} RBSourceListClass;

GType		rb_sourcelist_get_type		(void);

GtkWidget *	rb_sourcelist_new		(void);

void		rb_sourcelist_append		(RBSourceList *sourcelist,
						 RBSource *source);

void		rb_sourcelist_edit_source_name	(RBSourceList *sourcelist,
						 RBSource *source);

void		rb_sourcelist_remove		(RBSourceList *sourcelist,
						 RBSource *source);

void		rb_sourcelist_select		(RBSourceList *sourcelist,
						 RBSource *source);

void		rb_sourcelist_set_dnd_targets	(RBSourceList *sourcelist,
						 const GtkTargetEntry *targets,
						 int n_targets);

G_END_DECLS

#endif /* __RB_SOURCELIST_H */
