/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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
 *  $Id$
 */

#ifndef __RB_NODE_SEARCH_H
#define __RB_NODE_SEARCH_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_NODE_SEARCH            (rb_node_search_get_type ())
#define RB_NODE_SEARCH(obj)	       (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_NODE_SEARCH, RBNodeSearch))
#define RB_NODE_SEARCH_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_NODE_SEARCH, RBNodeSearchClass))
#define RB_IS_NODE_SEARCH(obj)	       (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_NODE_SEARCH))
#define RB_IS_NODE_SEARCH_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_NODE_SEARCH))
#define RB_NODE_SEARCH_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_NODE_SEARCH, RBNodeSearchClass))

typedef struct _RBNodeSearch        	RBNodeSearch;
typedef struct _RBNodeSearchClass 	RBNodeSearchClass;

typedef struct _RBNodeSearchPrivate 	RBNodeSearchPrivate;

struct _RBNodeSearch
{
	GObject base;
	
	RBNodeSearchPrivate *priv;
};

struct _RBNodeSearchClass
{
	GObjectClass parent_class;
};

GType         rb_node_search_get_type          (void) G_GNUC_CONST;

RBNodeSearch *rb_node_search_new               (void);

void          rb_node_search_add_song          (RBNodeSearch *search, RBNode *node);

const GSList *rb_node_search_run_search        (RBNodeSearch *search, const char *search_string);

G_END_DECLS

#endif /* __RB_NODE_SEARCH_H */
