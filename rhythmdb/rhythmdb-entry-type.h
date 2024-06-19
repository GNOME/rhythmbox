/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RHYTHMDB_ENTRY_TYPE_H
#define RHYTHMDB_ENTRY_TYPE_H

#include <glib.h>
#include <glib-object.h>
#include <gio/gio.h>

#include <rhythmdb/rhythmdb-entry.h>
#include <metadata/rb-ext-db-key.h>

G_BEGIN_DECLS

/* entry type category */

typedef enum _RhythmDBPropType RhythmDBPropType;

GType rhythmdb_entry_category_get_type (void);
#define RHYTHMDB_TYPE_ENTRY_CATEGORY (rhythmdb_entry_category_get_type ())
typedef enum {
	RHYTHMDB_ENTRY_NORMAL,
	RHYTHMDB_ENTRY_STREAM,
	RHYTHMDB_ENTRY_CONTAINER,
	RHYTHMDB_ENTRY_VIRTUAL
} RhythmDBEntryCategory;

/* entry availability events */

GType rhythmdb_entry_availability_get_type (void);
#define RHYTHMDB_TYPE_ENTRY_AVAILABILITY (rhythmdb_entry_availability_get_type ())
typedef enum {
	RHYTHMDB_ENTRY_AVAIL_CHECKED,
	RHYTHMDB_ENTRY_AVAIL_MOUNTED,
	RHYTHMDB_ENTRY_AVAIL_UNMOUNTED,
	RHYTHMDB_ENTRY_AVAIL_NOT_FOUND
} RhythmDBEntryAvailability;

/* entry type */

typedef struct _RhythmDBEntryType RhythmDBEntryType;
typedef struct _RhythmDBEntryTypeClass RhythmDBEntryTypeClass;
typedef struct _RhythmDBEntryTypePrivate RhythmDBEntryTypePrivate;

#define RHYTHMDB_TYPE_ENTRY_TYPE      (rhythmdb_entry_type_get_type ())
#define RHYTHMDB_ENTRY_TYPE(o)        (G_TYPE_CHECK_INSTANCE_CAST ((o), RHYTHMDB_TYPE_ENTRY_TYPE, RhythmDBEntryType))
#define RHYTHMDB_ENTRY_TYPE_CLASS(k)  (G_TYPE_CHECK_CLASS_CAST((k), RHYTHMDB_TYPE_ENTRY_TYPE, RhythmDBEntryTypeClass))
#define RHYTHMDB_IS_ENTRY_TYPE(o)     (G_TYPE_CHECK_INSTANCE_TYPE ((o), RHYTHMDB_TYPE_ENTRY_TYPE))
#define RHYTHMDB_IS_ENTRY_TYPE_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), RHYTHMDB_TYPE_ENTRY_TYPE))
#define RHYTHMDB_ENTRY_TYPE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RHYTHMDB_TYPE_ENTRY_TYPE, RhythmDBEntryTypeClass))

typedef char *(*RhythmDBEntryTypeStringFunc) (RhythmDBEntryType *entry_type, RhythmDBEntry *entry);
typedef gboolean (*RhythmDBEntryTypeBooleanFunc) (RhythmDBEntryType *entry_type, RhythmDBEntry *entry);
typedef void (*RhythmDBEntryTypeSyncFunc) (RhythmDBEntryType *entry_type, RhythmDBEntry *entry, GSList *changes, GError **error);

struct _RhythmDBEntryType {
	GObject parent;

	RhythmDBEntryTypePrivate *priv;
};

struct _RhythmDBEntryTypeClass {
	GObjectClass parent_class;

	/* methods */
	void		(*entry_created) (RhythmDBEntryType *etype, RhythmDBEntry *entry);
	void		(*destroy_entry) (RhythmDBEntryType *etype, RhythmDBEntry *entry);

	char *		(*get_playback_uri) (RhythmDBEntryType *etype, RhythmDBEntry *entry);
	void		(*update_availability) (RhythmDBEntryType *etype, RhythmDBEntry *entry, RhythmDBEntryAvailability avail);

	gboolean	(*can_sync_metadata) (RhythmDBEntryType *etype, RhythmDBEntry *entry);
	void		(*sync_metadata) (RhythmDBEntryType *etype, RhythmDBEntry *entry, GSList *changes, GError **error);

	char *		(*uri_to_cache_key) (RhythmDBEntryType *etype, const char *uri);
	char *		(*cache_key_to_uri) (RhythmDBEntryType *etype, const char *key);

	RBExtDBKey *	(*create_ext_db_key) (RhythmDBEntryType *etype, RhythmDBEntry *entry, RhythmDBPropType prop);
};

GType		rhythmdb_entry_type_get_type (void);

const char *	rhythmdb_entry_type_get_name (RhythmDBEntryType *etype);

char *		rhythmdb_entry_get_playback_uri (RhythmDBEntry *entry);
void		rhythmdb_entry_update_availability (RhythmDBEntry *entry, RhythmDBEntryAvailability avail);
void 		rhythmdb_entry_created (RhythmDBEntry *entry);
void 		rhythmdb_entry_pre_destroy (RhythmDBEntry *entry);
gboolean 	rhythmdb_entry_can_sync_metadata (RhythmDBEntry *entry);
void 		rhythmdb_entry_sync_metadata (RhythmDBEntry *entry, GSList *changes, GError **error);

gboolean	rhythmdb_entry_type_fetch_metadata (RhythmDBEntryType *etype, const char *uri, GArray *metadata);
void		rhythmdb_entry_cache_metadata (RhythmDBEntry *entry);
void		rhythmdb_entry_apply_cached_metadata (RhythmDBEntry *entry, GArray *metadata);

void		rhythmdb_entry_type_purge_metadata_cache (RhythmDBEntryType *etype, const char *prefix, guint64 max_age);

RBExtDBKey *	rhythmdb_entry_create_ext_db_key (RhythmDBEntry *entry, RhythmDBPropType prop);

/* predefined entry types -- these mostly need to die */

#define RHYTHMDB_ENTRY_TYPE_SONG (rhythmdb_get_song_entry_type ())
#define RHYTHMDB_ENTRY_TYPE_IMPORT_ERROR (rhythmdb_get_error_entry_type ())
#define RHYTHMDB_ENTRY_TYPE_IGNORE (rhythmdb_get_ignore_entry_type ())

RhythmDBEntryType *rhythmdb_get_song_entry_type          (void);
RhythmDBEntryType *rhythmdb_get_error_entry_type	 (void);
RhythmDBEntryType *rhythmdb_get_ignore_entry_type        (void);

G_END_DECLS

#endif
