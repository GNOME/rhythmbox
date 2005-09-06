/*
 *  Header for DAAP (iTunes Music Sharing) hashing, connection
 *
 *  Copyright (C) 2004,2005 Charles Schmidt <cschmidt2@emich.edu>
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

#ifndef __RB_DAAP_CONNECTION_H
#define __RB_DAAP_CONNECTIONH

#include <glib.h>
#include <glib-object.h>
#include "rhythmdb.h"

G_BEGIN_DECLS

typedef struct _RBDAAPConnection RBDAAPConnection;

typedef struct {
	gchar *name;
	gint id;
	GList *uris;
} RBDAAPPlaylist;

RBDAAPConnection * 
rb_daap_connection_new (const gchar *host,
		        gint port,
			RhythmDB *db,
			RhythmDBEntryType type);

gchar * 
rb_daap_connection_get_headers (RBDAAPConnection *connection,
				const gchar *uri,
				gint64 bytes);

GSList * 
rb_daap_connection_get_playlists (RBDAAPConnection *connection);

void 
rb_daap_connection_destroy (RBDAAPConnection *connection);


G_END_DECLS

#endif /* __RB_DAAP_CONNECTION_H */

