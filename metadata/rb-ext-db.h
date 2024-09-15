/*
 *  Copyright (C) 2011 Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#ifndef RB_EXT_DB_H
#define RB_EXT_DB_H

#include <glib-object.h>

#include <metadata/rb-ext-db-key.h>

G_BEGIN_DECLS

typedef struct _RBExtDB RBExtDB;
typedef struct _RBExtDBClass RBExtDBClass;
typedef struct _RBExtDBPrivate RBExtDBPrivate;

#define RB_TYPE_EXT_DB      	(rb_ext_db_get_type ())
#define RB_EXT_DB(o)        	(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_EXT_DB, RBExtDB))
#define RB_EXT_DB_CLASS(k)  	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_EXT_DB, RBExtDBClass))
#define RB_IS_EXT_DB(o)     	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_EXT_DB))
#define RB_IS_EXT_DB_CLASS(k) 	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_EXT_DB))
#define RB_EXT_DB_GET_CLASS(o) 	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_EXT_DB, RBExtDBClass))

/* these are in priority order - higher is preferred to lower */
typedef enum {
	RB_EXT_DB_SOURCE_NONE,		/* nothing */
	RB_EXT_DB_SOURCE_SEARCH,	/* found by external search */
	RB_EXT_DB_SOURCE_EMBEDDED,	/* embedded in media itself */
	RB_EXT_DB_SOURCE_USER,		/* provided by user (eg image in same dir) */
	RB_EXT_DB_SOURCE_USER_EXPLICIT,	/* provided explicitly by user */
} RBExtDBSourceType;

GType		rb_ext_db_source_type_get_type (void);
#define RB_TYPE_EXT_DB_SOURCE_TYPE (rb_ext_db_source_type_get_type ())


struct _RBExtDB
{
	GObject parent;

	RBExtDBPrivate *priv;
};

struct _RBExtDBClass
{
	GObjectClass parent;

	/* requestor signals */
	void		(*added)	(RBExtDB *store,
					 RBExtDBKey *key,
					 const char *filename,
					 GValue *data);

	/* provider signals */
	gboolean	(*request)	(RBExtDB *store,
					 RBExtDBKey *key,
					 guint64 last_time);

	/* data format conversion signals */
	GValue *	(*store)	(RBExtDB *store,
					 GValue *data);
	GValue *	(*load)		(RBExtDB *store,
					 GValue *data);
};

typedef void (*RBExtDBRequestCallback) (RBExtDBKey *key, RBExtDBKey *store_key, const char *filename, GValue *data, gpointer user_data);

GType			rb_ext_db_get_type 		(void);

RBExtDB *		rb_ext_db_new			(const char *name);

/* for requestors */
char *			rb_ext_db_lookup		(RBExtDB *store,
							 RBExtDBKey *key,
							 RBExtDBKey **store_key);

gboolean		rb_ext_db_request		(RBExtDB *store,
							 RBExtDBKey *key,
							 RBExtDBRequestCallback callback,
							 gpointer user_data,
							 GDestroyNotify destroy);
void			rb_ext_db_cancel_requests	(RBExtDB *store,
							 RBExtDBRequestCallback callback,
							 gpointer user_data);

/* for providers */
void			rb_ext_db_store_uri		(RBExtDB *store,
							 RBExtDBKey *key,
							 RBExtDBSourceType source_type,
							 const char *uri);

void			rb_ext_db_store			(RBExtDB *store,
							 RBExtDBKey *key,
							 RBExtDBSourceType source_type,
							 GValue *data);

void			rb_ext_db_store_raw		(RBExtDB *store,
							 RBExtDBKey *key,
							 RBExtDBSourceType source_type,
							 GValue *data);

void			rb_ext_db_delete		(RBExtDB *store,
							 RBExtDBKey *key);

G_END_DECLS

#endif /* RB_EXT_DB_H */
