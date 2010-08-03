/*
 * rb-audioscrobbler-radio-track-entry.c
 *
 * Copyright (C) 2010 Jamie Nicol <jamie@thenicols.net>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include "rb-audioscrobbler-radio-track-entry-type.h"

static RhythmDBEntryType *radio_track_entry_type = NULL;

static void
track_data_destroy (RhythmDBEntryType *entry_type, RhythmDBEntry *entry)
{
	RBAudioscrobblerRadioTrackData *data;
	data = RHYTHMDB_ENTRY_GET_TYPE_DATA (entry, RBAudioscrobblerRadioTrackData);

	g_free (data->image_url);
	g_free (data->track_auth);
	g_free (data->download_url);
}

RhythmDBEntryType *
rb_audioscrobbler_radio_track_get_entry_type (void)
{
	return radio_track_entry_type;
}

void
rb_audioscrobbler_radio_track_register_entry_type (RhythmDB *db)
{
	g_assert (radio_track_entry_type == NULL);

	radio_track_entry_type = g_object_new (RHYTHMDB_TYPE_ENTRY_TYPE,
	                                       "db", db,
	                                       "name", "audioscrobbler-radio-track",
	                                       "save-to-disk", FALSE,
	                                       "category", RHYTHMDB_ENTRY_NORMAL,
	                                       "type-data-size", sizeof (RBAudioscrobblerRadioTrackData),
	                                       NULL);

	radio_track_entry_type->destroy_entry = track_data_destroy;
	rhythmdb_register_entry_type (db, radio_track_entry_type);
}
