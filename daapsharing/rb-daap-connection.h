/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Header for DAAP (iTunes Music Sharing) hashing, connection
 *
 *  Copyright (C) 2004-2005 Charles Schmidt <cschmidt2@emich.edu>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_DAAP_CONNECTION_H
#define __RB_DAAP_CONNECTION_H

#include <glib.h>
#include <glib-object.h>
#include "rhythmdb.h"

G_BEGIN_DECLS

typedef struct {
	char  *name;
	int    id;
	GList *uris;
} RBDAAPPlaylist;


#define RB_TYPE_DAAP_CONNECTION		(rb_daap_connection_get_type ())
#define RB_DAAP_CONNECTION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_CONNECTION, RBDAAPConnection))
#define RB_DAAP_CONNECTION_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_CONNECTION, RBDAAPConnectionClass))
#define RB_IS_DAAP_CONNECTION(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_CONNECTION))
#define RB_IS_DAAP_CONNECTION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_CONNECTION))
#define RB_DAAP_CONNECTION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_CONNECTION, RBDAAPConnectionClass))

typedef struct RBDAAPConnectionPrivate RBDAAPConnectionPrivate;

typedef enum {
	DAAP_GET_INFO = 0,
	DAAP_GET_PASSWORD,
	DAAP_LOGIN,
	DAAP_GET_REVISION_NUMBER,
	DAAP_GET_DB_INFO,
	DAAP_GET_SONGS,
	DAAP_GET_PLAYLISTS,
	DAAP_GET_PLAYLIST_ENTRIES,
	DAAP_LOGOUT,
	DAAP_DONE
} RBDAAPConnectionState;

typedef struct {
	GObject parent;
	RBDAAPConnectionPrivate *priv;
} RBDAAPConnection;

typedef struct {
	GObjectClass parent;

	void   (* connected)      (RBDAAPConnection     *connection);
	void   (* disconnected)   (RBDAAPConnection     *connection); 

	char * (* authenticate)   (RBDAAPConnection     *connection,
				   const char           *name);
	void   (* connecting)     (RBDAAPConnection     *connection,
				   RBDAAPConnectionState state,
				   float		 progress);

	void   (* operation_done) (RBDAAPConnection     *connection);

} RBDAAPConnectionClass;


/* hmm, maybe should give more error information? */
typedef gboolean (* RBDAAPConnectionCallback)  (RBDAAPConnection *connection,
						gboolean          result,
						const char       *reason,
						gpointer          user_data);

GType              rb_daap_connection_get_type        (void);

RBDAAPConnection * rb_daap_connection_new             (const char              *name,
						       const char              *host,
						       int                      port,
						       gboolean                 password_protected,
						       RhythmDB                *db,
						       RhythmDBEntryType        type);

gboolean           rb_daap_connection_is_connected    (RBDAAPConnection        *connection);
void               rb_daap_connection_connect         (RBDAAPConnection        *connection,
						       RBDAAPConnectionCallback callback,
						       gpointer                 user_data);
void               rb_daap_connection_disconnect      (RBDAAPConnection        *connection,
						       RBDAAPConnectionCallback callback,
						       gpointer                 user_data);

char *             rb_daap_connection_get_headers     (RBDAAPConnection         *connection,
						       const char               *uri,
						       gint64                    bytes);

GSList *           rb_daap_connection_get_playlists   (RBDAAPConnection         *connection);

G_END_DECLS

#endif /* __RB_DAAP_CONNECTION_H */

