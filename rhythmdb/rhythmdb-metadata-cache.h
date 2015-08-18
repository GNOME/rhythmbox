/*
 *  Copyright (C) 2015  Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RHYTHMDB_METADATA_CACHE_H
#define RHYTHMDB_METADATA_CACHE_H

#include <glib.h>
#include <glib-object.h>

#include <rhythmdb/rhythmdb.h>

G_BEGIN_DECLS

#define RHYTHMDB_TYPE_METADATA_CACHE		(rhythmdb_metadata_cache_get_type ())
#define RHYTHMDB_METADATA_CACHE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_METADATA_CACHE, RhythmDBMetadataCache))
#define RHYTHMDB_METADATA_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE_METADATA_CACHE, RhythmDBMetadataCacheClass))
#define RHYTHMDB_IS_METADATA_CACHE(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_METADATA_CACHE))
#define RHYTHMDB_IS_METADATA_CACHE_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_METADATA_CACHE))
#define RHYTHMDB_METADATA_CACHE_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_METADATA_CACHE, RhythmDBMetadataCacheClass))

typedef struct _RhythmDBMetadataCache 		RhythmDBMetadataCache;
typedef struct _RhythmDBMetadataCacheClass 	RhythmDBMetadataCacheClass;
typedef struct _RhythmDBMetadataCachePrivate 	RhythmDBMetadataCachePrivate;

struct _RhythmDBMetadataCache
{
	GObject parent;
	RhythmDBMetadataCachePrivate *priv;
};

struct _RhythmDBMetadataCacheClass
{
	GObjectClass parent_class;
};

typedef gboolean (*RhythmDBMetadataCacheValidFunc) (const char *key, gpointer data);

GType		rhythmdb_metadata_cache_get_type		(void);

RhythmDBMetadataCache *rhythmdb_metadata_cache_get	(RhythmDB *db,
							 const char *cache_name);

gboolean	rhythmdb_metadata_cache_load		(RhythmDBMetadataCache *cache,
							 const char *key,
							 GArray *metadata);

void		rhythmdb_metadata_cache_store		(RhythmDBMetadataCache *cache,
							 const char *key,
							 RhythmDBEntry *entry);

void		rhythmdb_metadata_cache_purge		(RhythmDBMetadataCache *cache,
							 const char *prefix,
							 gulong age,
							 RhythmDBMetadataCacheValidFunc cb,
							 gpointer cb_data,
							 GDestroyNotify cb_data_destroy);

G_END_DECLS

#endif /* RHYTHMDB_METADATA_CACHE_H */
