/* 
 *  arch-tag: Header for search entry/browse toggle container
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtkhbox.h>
#include <bonobo/bonobo-ui-component.h>
#include <monkey-media.h>

#include "rb-source.h"

#ifndef __RB_SOURCE_HEADER_H
#define __RB_SOURCE_HEADER_H

G_BEGIN_DECLS

#define RB_TYPE_SOURCE_HEADER         (rb_source_header_get_type ())
#define RB_SOURCE_HEADER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SOURCE_HEADER, RBSourceHeader))
#define RB_SOURCE_HEADER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SOURCE_HEADER, RBSourceHeaderClass))
#define RB_IS_SOURCE_HEADER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SOURCE_HEADER))
#define RB_IS_SOURCE_HEADER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SOURCE_HEADER))
#define RB_SOURCE_HEADER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SOURCE_HEADER, RBSourceHeaderClass))

typedef struct RBSourceHeaderPrivate RBSourceHeaderPrivate;

typedef struct
{
	GtkTable parent;

	RBSourceHeaderPrivate *priv;
} RBSourceHeader;

typedef struct
{
	GtkTableClass parent_class;
} RBSourceHeaderClass;

GType			rb_source_header_get_type	(void);

RBSourceHeader *	rb_source_header_new		(BonoboUIComponent *component);

void			rb_source_header_set_source	(RBSourceHeader *header,
							 RBSource *player);

RBSource *		rb_source_header_get_source	(RBSourceHeader *shell_player);

void			rb_source_header_clear_search	(RBSourceHeader *header);
void			rb_source_header_sync_control_state (RBSourceHeader *header);


G_END_DECLS

#endif /* __RB_SOURCE_HEADER_H */
