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
static RhythmDBEntryType *podcast_search_entry_type = NULL;

/* podcast post entry type class */

typedef struct _RhythmDBEntryType RBPodcastPostEntryType;
typedef struct _RhythmDBEntryTypeClass RBPodcastPostEntryTypeClass;

static void rb_podcast_post_entry_type_class_init (RBPodcastPostEntryTypeClass *klass);
static void rb_podcast_post_entry_type_init (RBPodcastPostEntryType *etype);
GType rb_podcast_post_entry_type_get_type (void);

G_DEFINE_TYPE (RBPodcastPostEntryType, rb_podcast_post_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

/* podcast feed entry type class */

typedef struct _RhythmDBEntryType RBPodcastFeedEntryType;
typedef struct _RhythmDBEntryTypeClass RBPodcastFeedEntryTypeClass;

static void rb_podcast_feed_entry_type_class_init (RBPodcastFeedEntryTypeClass *klass);
static void rb_podcast_feed_entry_type_init (RBPodcastFeedEntryType *etype);
GType rb_podcast_feed_entry_type_get_type (void);

G_DEFINE_TYPE (RBPodcastFeedEntryType, rb_podcast_feed_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

/* podcast search entry type class */

typedef struct _RhythmDBEntryType RBPodcastSearchEntryType;
typedef struct _RhythmDBEntryTypeClass RBPodcastSearchEntryTypeClass;

static void rb_podcast_search_entry_type_class_init (RBPodcastSearchEntryTypeClass *klass);
static void rb_podcast_search_entry_type_init (RBPodcastSearchEntryType *etype);
GType rb_podcast_search_entry_type_get_type (void);

G_DEFINE_TYPE (RBPodcastSearchEntryType, rb_podcast_search_entry_type, RHYTHMDB_TYPE_ENTRY_TYPE);

static void
podcast_post_create (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	RBRefString *empty = rb_refstring_new ("");
	podcast->description = rb_refstring_ref (empty);
	podcast->subtitle = rb_refstring_ref (empty);
	podcast->lang = rb_refstring_ref (empty);
	podcast->copyright = rb_refstring_ref (empty);
	podcast->image = rb_refstring_ref (empty);
	podcast->guid = NULL;
	rb_refstring_unref (empty);
}

static void
podcast_data_destroy (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	RhythmDBPodcastFields *podcast = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RhythmDBPodcastFields);
	rb_refstring_unref (podcast->description);
	rb_refstring_unref (podcast->subtitle);
	rb_refstring_unref (podcast->lang);
	rb_refstring_unref (podcast->copyright);
	rb_refstring_unref (podcast->image);
	if (podcast->guid != NULL)
		rb_refstring_unref (podcast->guid);
}

static RBExtDBKey *
podcast_feed_create_ext_db_key (RhythmDBEntryType *etype, RhythmDBEntry *entry, RhythmDBPropType prop)
{
	RBExtDBKey *key;
	const char *uri;

	/* match on feed url */
	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	key = rb_ext_db_key_create_lookup ("subtitle", uri);

	rb_ext_db_key_add_info (key, "location", uri);
	return key;
}

static RBExtDBKey *
podcast_post_create_ext_db_key (RhythmDBEntryType *etype, RhythmDBEntry *entry, RhythmDBPropType prop)
{
	RBExtDBKey *key;

	/* match on feed url and optionally the entry guid */
	key = rb_ext_db_key_create_lookup ("subtitle", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_SUBTITLE));
	rb_ext_db_key_add_field (key, "podcast-guid", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_PODCAST_GUID));
	rb_ext_db_key_add_field (key, "podcast-guid", NULL);

	rb_ext_db_key_add_info (key, "location", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	return key;
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

static void
rb_podcast_post_entry_type_class_init (RBPodcastPostEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);

	etype_class->entry_created = podcast_post_create;
	etype_class->destroy_entry = podcast_data_destroy;
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	etype_class->create_ext_db_key = podcast_post_create_ext_db_key;
}

static void
rb_podcast_post_entry_type_init (RBPodcastPostEntryType *etype)
{
}

/**
 * rhythmdb_get_feed_entry_type:
 *
 * Returns the #RhythmDBEntryType for podcast feeds 
 *
 * Return value: (transfer none): the entry type for podcast feeds
 */
RhythmDBEntryType *
rb_podcast_get_feed_entry_type (void)
{
	return podcast_feed_entry_type;
}

static void
rb_podcast_feed_entry_type_class_init (RBPodcastFeedEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);

	etype_class->entry_created = podcast_post_create;
	etype_class->destroy_entry = podcast_data_destroy;
	etype_class->get_playback_uri = (RhythmDBEntryTypeStringFunc) rb_null_function;
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
	etype_class->create_ext_db_key = podcast_feed_create_ext_db_key;
}

static void
rb_podcast_feed_entry_type_init (RBPodcastFeedEntryType *etype)
{
}

/**
 * rhythmdb_get_search_entry_type:
 *
 * Returns the #RhythmDBEntryType for search result podcast episodes
 *
 * Return value: (transfer none): the entry type for search result podcast episodes
 */
RhythmDBEntryType *
rb_podcast_get_search_entry_type (void)
{
	return podcast_search_entry_type;
}

static void
rb_podcast_search_entry_type_class_init (RBPodcastSearchEntryTypeClass *klass)
{
	RhythmDBEntryTypeClass *etype_class = RHYTHMDB_ENTRY_TYPE_CLASS (klass);

	etype_class->entry_created = podcast_post_create;
	etype_class->destroy_entry = podcast_data_destroy;
	etype_class->can_sync_metadata = (RhythmDBEntryTypeBooleanFunc) rb_true_function;
	etype_class->sync_metadata = (RhythmDBEntryTypeSyncFunc) rb_null_function;
}

static void
rb_podcast_search_entry_type_init (RBPodcastSearchEntryType *etype)
{
}


void
rb_podcast_register_entry_types (RhythmDB *db)
{
	g_assert (podcast_post_entry_type == NULL);
	g_assert (podcast_feed_entry_type == NULL);

	podcast_post_entry_type = g_object_new (rb_podcast_post_entry_type_get_type (),
						"db", db,
						"name", "podcast-post",
						"save-to-disk", TRUE,
						"category", RHYTHMDB_ENTRY_NORMAL,
						"type-data-size", sizeof (RhythmDBPodcastFields),
						NULL);
	rhythmdb_register_entry_type (db, podcast_post_entry_type);

	podcast_feed_entry_type = g_object_new (rb_podcast_feed_entry_type_get_type (),
						"db", db,
						"name", "podcast-feed",
						"save-to-disk", TRUE,
						"category", RHYTHMDB_ENTRY_CONTAINER,
						"type-data-size", sizeof (RhythmDBPodcastFields),
						NULL);
	rhythmdb_register_entry_type (db, podcast_feed_entry_type);

	podcast_search_entry_type = g_object_new (rb_podcast_search_entry_type_get_type (),
						"db", db,
						"name", "podcast-search",
						"save-to-disk", FALSE,
						"category", RHYTHMDB_ENTRY_NORMAL,
						"type-data-size", sizeof (RhythmDBPodcastFields),
						NULL);
	rhythmdb_register_entry_type (db, podcast_search_entry_type);
}
