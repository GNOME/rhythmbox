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

#include "rb-podcast-entry-types.h"
#include "rhythmdb-private.h"		/* for RhythmDBPodcastFields, which should move in here somehow */
#include "rhythmdb.h"
#include "rb-util.h"

static RhythmDBEntryType *podcast_post_entry_type = NULL;
static RhythmDBEntryType *podcast_feed_entry_type = NULL;

static char *
podcast_get_playback_uri (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	if (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MOUNTPOINT) != NULL) {
		return rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_LOCATION);
	}
	return NULL;
}

static void
podcast_post_create (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	RBRefString *empty = rb_refstring_new ("");
	podcast->description = rb_refstring_ref (empty);
	podcast->subtitle = rb_refstring_ref (empty);
	podcast->summary = rb_refstring_ref (empty);
	podcast->lang = rb_refstring_ref (empty);
	podcast->copyright = rb_refstring_ref (empty);
	podcast->image = rb_refstring_ref (empty);
	rb_refstring_unref (empty);
}

static void
podcast_data_destroy (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	rb_refstring_unref (podcast->description);
	rb_refstring_unref (podcast->subtitle);
	rb_refstring_unref (podcast->summary);
	rb_refstring_unref (podcast->lang);
	rb_refstring_unref (podcast->copyright);
	rb_refstring_unref (podcast->image);
}

/**
 * rb_podcast_get_post_entry_type:
 *
 * Returns the #RhythmDBEntryType for podcast posts
 *
 * Return value: (transfer none): the entry type for podcast posts
 */
RhythmDBEntryType *
rb_podcast_get_post_entry_type (void)
{
	return podcast_post_entry_type;
}

/**
 * rhythmdb_get_ignore_entry_type:
 *
 * Returns the #RhythmDBEntryType for ignored files
 *
 * Return value: (transfer none): the entry type for ignored files
 */
RhythmDBEntryType *
rb_podcast_get_feed_entry_type (void)
{
	return podcast_feed_entry_type;
}



void
rb_podcast_register_entry_types (RhythmDB *db)
{
	g_assert (podcast_post_entry_type == NULL);
	g_assert (podcast_feed_entry_type == NULL);

	podcast_post_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
						"db", db,
						"name", "podcast-post",
						"save-to-disk", TRUE,
						"category", RHYTHMDB_ENTRY_NORMAL,
						"type-data-size", sizeof (RhythmDBPodcastFields),
						NULL);
	podcast_post_entry_type->entry_created = podcast_post_create;
	podcast_post_entry_type->destroy_entry = podcast_data_destroy;
	podcast_post_entry_type->get_playback_uri = podcast_get_playback_uri;
	podcast_post_entry_type->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	podcast_post_entry_type->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	rhythmdb_register_entry_type (db, podcast_post_entry_type);

	podcast_feed_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
						"db", db,
						"name", "podcast-feed",
						"save-to-disk", TRUE,
						"category", RHYTHMDB_ENTRY_CONTAINER,
						"type-data-size", sizeof (RhythmDBPodcastFields),
						NULL);
	podcast_feed_entry_type->entry_created = podcast_post_create;
	podcast_feed_entry_type->destroy_entry = podcast_data_destroy;
	podcast_feed_entry_type->get_playback_uri = (RhythmDBEntryTypeStringFunc) rb_null_function;
	podcast_feed_entry_type->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	podcast_feed_entry_type->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	rhythmdb_register_entry_type (db, podcast_feed_entry_type);
}
