/*
 *  Copyright (C) 2002 Colin Walters <walters@debian.org>
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

#ifndef __RB_NODE_STATION_H
#define __RB_NODE_STATION_H

#include "library/rb-node.h"
#include "iradio/rb-iradio-backend.h"

G_BEGIN_DECLS

enum
{
	/* Must be one more than the last RBNode property */
	RB_NODE_STATION_PROP_SOURCE = 24,
};

#define RB_TYPE_NODE_STATION         (rb_node_station_get_type ())
#define RB_NODE_STATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_NODE_STATION, RBNodeStation))
#define RB_NODE_STATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_NODE_STATION, RBNodeStationClass))
#define RB_IS_NODE_STATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_NODE_STATION))
#define RB_IS_NODE_STATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_NODE_STATION))
#define RB_NODE_STATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_NODE_STATION, RBNodeStationClass))

typedef struct RBNodeStationPrivate RBNodeStationPrivate;

typedef struct
{
	RBNode parent;
} RBNodeStation;

typedef struct
{
	RBNodeClass parent;
} RBNodeStationClass;

GType       rb_node_station_get_type              (void);

RBNodeStation *rb_node_station_new                (GList *locations, const char *name,
						   const char *genre, const char *source,
						   RBIRadioBackend *iradio_backend);

/* convenience property wrappers: */
RBNode     *rb_node_station_get_genre             (RBNodeStation *song);
gboolean    rb_node_station_has_genre             (RBNodeStation *song,
						   RBNode *genre,
						   RBIRadioBackend *backend);

G_END_DECLS

#endif /* __RB_NODE_STATION_H */
