/*
 *  Copyright (C) 2002 Colin Walters <walters@gnu.org>
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

#ifndef __RB_IRADIO_BACKEND_H
#define __RB_IRADIO_BACKEND_H

#include <gtk/gtktreestore.h>
#include "rb-node.h"

G_BEGIN_DECLS

typedef enum
{
	RB_IRADIO_BACKEND_COLUMN_PLAYING,
	RB_IRADIO_BACKEND_COLUMN_GENRE,
	RB_IRADIO_BACKEND_COLUMN_NAME,
	RB_IRADIO_BACKEND_COLUMN_QUALITY,
	RB_IRADIO_BACKEND_COLUMN_URL,
	RB_IRADIO_BACKEND_NUM_COLUMNS
} RBIRadioBackendColumn;

#define RB_TYPE_IRADIO_BACKEND         (rb_iradio_backend_get_type ())
#define RB_IRADIO_BACKEND(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_IRADIO_BACKEND, RBIRadioBackend))
#define RB_IRADIO_BACKEND_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_IRADIO_BACKEND, RBIRadioBackendClass))
#define RB_IS_IRADIO_BACKEND(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_IRADIO_BACKEND))
#define RB_IS_IRADIO_BACKEND_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_IRADIO_BACKEND))
#define RB_IRADIO_BACKEND_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_IRADIO_BACKEND, RBIRadioBackendClass))

typedef struct RBIRadioBackendPrivate RBIRadioBackendPrivate;

typedef struct
{
	GObject parent;

	RBIRadioBackendPrivate *priv;
} RBIRadioBackend;

typedef struct
{
	GObjectClass parent;

	/* Signals */
	void (*genre_added)(const char *genre);
	void (*changed)();
	void (*done_loading)(void);
} RBIRadioBackendClass;

GType   rb_iradio_backend_get_type			(void);

void    rb_iradio_backend_load				(RBIRadioBackend *backend);

void    rb_iradio_backend_remove_node			(RBIRadioBackend *backend,
							 RBNode *node);

void    rb_iradio_backend_release_brakes		(RBIRadioBackend *backend);

RBNode *rb_iradio_backend_get_all_genres		(RBIRadioBackend *backend);
RBNode *rb_iradio_backend_get_all_stations		(RBIRadioBackend *backend);

RBNode *rb_iradio_backend_get_genre_by_name		(RBIRadioBackend *backend,
							 const char *genre);

RBNode *rb_iradio_backend_get_station_by_location	(RBIRadioBackend *backend,
							 const char *location);

int     rb_iradio_backend_get_genre_count		(RBIRadioBackend *backend);
int     rb_iradio_backend_get_station_count		(RBIRadioBackend *backend);

void    rb_iradio_backend_add_station_from_uri		(RBIRadioBackend *backend,
							 const char *uri);
void	rb_iradio_backend_add_station_full		(RBIRadioBackend *backend,
							 GList *locations,
							 const char *name,
							 const char *genre);
GList  *rb_iradio_backend_get_genre_names		(RBIRadioBackend *backend);

     
G_END_DECLS

#endif /* __RB_IRADIO_BACKEND_H */
