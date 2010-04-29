/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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

#include "config.h"

#include "rhythmdb-entry-type.h"
#include "rhythmdb-private.h"
#include "rb-util.h"

static RhythmDBEntryType *song_entry_type = NULL;
static RhythmDBEntryType *error_entry_type = NULL;
static RhythmDBEntryType *ignore_entry_type = NULL;

static void
song_sync_metadata (RhythmDBEntryType *entry_type,
		    RhythmDBEntry *entry,
		    GSList *changes,
		    GError **error)
{
	RhythmDB *db;

	g_object_get (entry_type, "db", &db, NULL);
	rhythmdb_entry_write_metadata_changes (db, entry, changes, error);
	g_object_unref (db);
}


static gboolean
song_can_sync_metadata (RhythmDBEntryType *entry_type,
			RhythmDBEntry *entry)
{
	const char *mimetype;
	gboolean can_sync;
	RhythmDB *db;

	g_object_get (entry_type, "db", &db, NULL);

	mimetype = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	can_sync = rb_metadata_can_save (db->priv->metadata, mimetype);

	g_object_unref (db);
	return can_sync;
}

/**
 * rhythmdb_get_song_entry_type:
 *
 * Returns the #RhythmDBEntryType for normal songs.
 *
 * Return value: (transfer none): the entry type for normal songs
 */
RhythmDBEntryType *
rhythmdb_get_song_entry_type (void)
{
	return song_entry_type;
}

/**
 * rhythmdb_get_ignore_entry_type:
 *
 * Returns the #RhythmDBEntryType for ignored files
 *
 * Return value: (transfer none): the entry type for ignored files
 */
RhythmDBEntryType *
rhythmdb_get_ignore_entry_type (void)
{
	return ignore_entry_type;
}

/**
 * rhythmdb_get_error_entry_type:
 *
 * Returns the #RhythmDBEntryType for import errors
 *
 * Return value: (transfer none): the entry type for import errors
 */
RhythmDBEntryType *
rhythmdb_get_error_entry_type (void)
{
	return error_entry_type;
}



void
rhythmdb_register_song_entry_types (RhythmDB *db)
{
	g_assert (song_entry_type == NULL);
	g_assert (error_entry_type == NULL);
	g_assert (ignore_entry_type == NULL);

	song_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
					"db", db,
					"name", "song",
					"save-to-disk", TRUE,
					"has-playlists", TRUE,
					NULL);
	song_entry_type->can_sync_metadata = song_can_sync_metadata;
	song_entry_type->sync_metadata = song_sync_metadata;

	ignore_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
					  "db", db,
					  "name", "ignore",
					  "save-to-disk", TRUE,
					  "category", RHYTHMDB_ENTRY_VIRTUAL,
					  NULL);
	ignore_entry_type->get_playback_uri = (RhythmDBEntryTypeStringFunc) rb_null_function;

	error_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
					 "db", db,
					 "name", "import-error",
					 "category", RHYTHMDB_ENTRY_VIRTUAL,
					 NULL);
	error_entry_type->get_playback_uri = (RhythmDBEntryTypeStringFunc) rb_null_function;
	error_entry_type->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	error_entry_type->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;


	rhythmdb_register_entry_type (db, song_entry_type);
	rhythmdb_register_entry_type (db, error_entry_type);
	rhythmdb_register_entry_type (db, ignore_entry_type);
}
