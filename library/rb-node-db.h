/*
 *  arch-tag: Header for node ID database object
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

#ifndef RB_NODE_DB_H
#define RB_NODE_DB_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_NODE_DB	  (rb_node_db_get_type ())
#define RB_NODE_DB(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_DB, RBNodeDb))
#define RB_NODE_DB_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_DB, RBNodeDbClass))
#define RB_IS_NODE_DB(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_DB))
#define RB_IS_NODE_DB_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_DB))
#define RB_NODE_DB_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_DB, RBNodeDbClass))

typedef struct RBNodeDb RBNodeDb;
typedef struct RBNodeDbPrivate RBNodeDbPrivate;

struct RBNodeDb
{
	GObject parent;

	RBNodeDbPrivate *priv;
};

typedef struct
{
	GObjectClass parent;

} RBNodeDbClass;

#include "rb-node.h"

GType         rb_node_db_get_type		(void);

RBNodeDb   *rb_node_db_get_by_name		(const char *name);

RBNodeDb   *rb_node_db_new			(const char *name);

const char   *rb_node_db_get_name		(RBNodeDb *db);

void		rb_node_db_lock			(RBNodeDb *db);

void		rb_node_db_unlock		(RBNodeDb *db);

RBNode     *rb_node_db_get_node_from_id	(RBNodeDb *db,
						 long id);

long	      _rb_node_db_new_id		(RBNodeDb *db);

void	      _rb_node_db_add_id		(RBNodeDb *db,
						 long id,
						 RBNode *node);

void	      _rb_node_db_remove_id		(RBNodeDb *db,
						 long id);

G_END_DECLS

#endif /* __RB_NODE_DB_H */
