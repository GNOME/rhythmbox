/*
 *  arch-tag: Header for GtkLabel subclass that acts as a hyperlink
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_LINK_H
#define __RB_LINK_H

#include <gtk/gtkeventbox.h>

G_BEGIN_DECLS

#define RB_TYPE_LINK         (rb_link_get_type ())
#define RB_LINK(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LINK, RBLink))
#define RB_LINK_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LINK, RBLinkClass))
#define RB_IS_LINK(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LINK))
#define RB_IS_LINK_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LINK))
#define RB_LINK_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LINK, RBLinkClass))

typedef struct RBLinkPrivate RBLinkPrivate;

typedef struct
{
	GtkEventBox parent;

	RBLinkPrivate *priv;
} RBLink;

typedef struct
{
	GtkEventBoxClass parent;
} RBLinkClass;

GType   rb_link_get_type (void);

RBLink *rb_link_new      (void);

void    rb_link_set      (RBLink *link,
		          const char *text,
			  const char *tooltip,
		          const char *url);
gboolean rb_link_get_ellipsized (RBLink *link);
int rb_link_get_full_text_size (RBLink *link);


G_END_DECLS

#endif /* __RB_LINK_H */
