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


typedef struct {
	gchar *name;
	gint id;
	GList *uris;
} RBDAAPPlaylist;


#define RB_TYPE_DAAP_CONNECTION		(rb_daap_connection_get_type ())
#define RB_DAAP_CONNECTION(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DAAP_CONNECTION, RBDAAPConnection))
#define RB_DAAP_CONNECTION_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_DAAP_CONNECTION, RBDAAPConnectionClass))
#define RB_IS_DAAP_CONNECTION(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DAAP_CONNECTION))
#define RB_IS_DAAP_CONNECTION_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_DAAP_CONNECTION))
#define RB_DAAP_CONNECTION_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_DAAP_CONNECTION, RBDAAPConnectionClass))

typedef struct RBDAAPConnectionPrivate RBDAAPConnectionPrivate;

typedef struct {
	GObject parent;
	RBDAAPConnectionPrivate *priv;
} RBDAAPConnection;

typedef struct {
	GObjectClass parent;
} RBDAAPConnectionClass;


/* hmm, maybe should give more error information? */
typedef gboolean (*RBDAAPConnectionCallback) (RBDAAPConnection *connection,
					      gboolean result,
					      gpointer user_data);

RBDAAPConnection * 
rb_daap_connection_new (const gchar *name,
			const gchar *host,
		        gint port,
			gboolean password_protected,
			RhythmDB *db,
			RhythmDBEntryType type,
			RBDAAPConnectionCallback callback,
			gpointer user_data);

/* will cause an assertion failure if the login has not completed yet (probably should FIXME) */
void
rb_daap_connection_logout (RBDAAPConnection *connection,
			   RBDAAPConnectionCallback callback,
			   gpointer user_data);

gchar * 
rb_daap_connection_get_headers (RBDAAPConnection *connection,
				const gchar *uri,
				gint64 bytes);

GSList * 
rb_daap_connection_get_playlists (RBDAAPConnection *connection);

GType 
rb_daap_connection_get_type (void);

G_END_DECLS

#endif /* __RB_DAAP_CONNECTION_H */

