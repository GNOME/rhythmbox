/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007 Christophe Fergeau <teuf@gnome.org>
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

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "rb-debug.h"
#include "rhythmdb.h"

#include "rb-audioscrobbler-entry.h"
#include "rb-audioscrobbler-radio-track-entry-type.h"


void
rb_audioscrobbler_entry_init (AudioscrobblerEntry *entry)
{
	entry->artist = g_strdup ("");
	entry->album = g_strdup ("");
	entry->title = g_strdup ("");
	entry->length = 0;
	entry->play_time = 0;
	entry->mbid = g_strdup ("");
	entry->source = g_strdup ("P");
}

void
rb_audioscrobbler_entry_free (AudioscrobblerEntry *entry)
{
	g_free (entry->artist);
	g_free (entry->album);
	g_free (entry->title);
	g_free (entry->mbid);
	g_free (entry->source);

	g_free (entry);
}

void
rb_audioscrobbler_encoded_entry_free (AudioscrobblerEncodedEntry *entry)
{
	g_free (entry->artist);
	g_free (entry->album);
	g_free (entry->title);
	g_free (entry->mbid);
	g_free (entry->timestamp);
	g_free (entry->source);
	g_free (entry->track);

	g_free (entry);
}


AudioscrobblerEntry *
rb_audioscrobbler_entry_create (RhythmDBEntry *rb_entry, RBAudioscrobblerService *service)
{
	AudioscrobblerEntry *as_entry = g_new0 (AudioscrobblerEntry, 1);

	as_entry->title = rhythmdb_entry_dup_string (rb_entry, RHYTHMDB_PROP_TITLE);
	as_entry->track = rhythmdb_entry_get_ulong (rb_entry, RHYTHMDB_PROP_TRACK_NUMBER);
	as_entry->artist = rhythmdb_entry_dup_string (rb_entry, RHYTHMDB_PROP_ARTIST);
	as_entry->album = rhythmdb_entry_dup_string (rb_entry, RHYTHMDB_PROP_ALBUM);
	if (strcmp (as_entry->album, _("Unknown")) == 0) {
		g_free (as_entry->album);
		as_entry->album = g_strdup ("");
	}

	as_entry->length = rhythmdb_entry_get_ulong (rb_entry, RHYTHMDB_PROP_DURATION);
	as_entry->mbid = rhythmdb_entry_dup_string (rb_entry, RHYTHMDB_PROP_MUSICBRAINZ_TRACKID);
	if (strcmp (as_entry->mbid, _("Unknown")) == 0) {
		g_free (as_entry->mbid);
		as_entry->mbid = g_strdup ("");
	}

	/* identify the source type. Currently we use:
	 * L for an audioscrobbler-provided radio track when scrobbling to its own service
	 * E for an audioscrobbler-provided radio track when scrobbling to a different service
	 * P for everything else
	 * TODO: Use R or E in some cases instead of P
	 */
	if (rhythmdb_entry_get_entry_type (rb_entry) == RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK) {
		RBAudioscrobblerRadioTrackData *track_data;
		track_data = RHYTHMDB_ENTRY_GET_TYPE_DATA (rb_entry, RBAudioscrobblerRadioTrackData);

		/* only use L if we have an auth code,
		 * and the track is from the correct service (ie not for a Libre.fm track scrobbling to Last.fm)
		 */
		if (track_data->track_auth != NULL && track_data->service == service) {
			as_entry->source = g_strdup_printf ("L%s", track_data->track_auth);
		} else {
			as_entry->source = g_strdup ("E");
		}
	} else {
		as_entry->source = g_strdup ("P");
	}

	return as_entry;
}

AudioscrobblerEncodedEntry *
rb_audioscrobbler_entry_encode (AudioscrobblerEntry *entry)
{

	AudioscrobblerEncodedEntry *encoded;

	encoded = g_new0 (AudioscrobblerEncodedEntry, 1);
	
	encoded->artist = g_uri_escape_string (entry->artist, NULL, FALSE);
	encoded->title = g_uri_escape_string (entry->title, NULL, FALSE);
	encoded->album = g_uri_escape_string (entry->album, NULL, FALSE);
	encoded->track = g_strdup_printf ("%lu", entry->track);

	encoded->mbid = g_uri_escape_string (entry->mbid, NULL, FALSE);

	encoded->timestamp = g_strdup_printf("%ld", (long)entry->play_time);
	encoded->length = entry->length;
	encoded->source = g_strdup (entry->source);

	return encoded;
}

AudioscrobblerEntry*
rb_audioscrobbler_entry_load_from_string (const char *string)
{
	AudioscrobblerEntry *entry;
	int i = 0;
	char **breaks;

	entry = g_new0 (AudioscrobblerEntry, 1);
	rb_audioscrobbler_entry_init (entry);

	breaks = g_strsplit (string, "&", 6);

	for (i = 0; breaks[i] != NULL; i++) {
		char **breaks2 = g_strsplit (breaks[i], "=", 2);

		if (breaks2[0] != NULL && breaks2[1] != NULL) {
			if (g_str_has_prefix (breaks2[0], "a")) {
				g_free (entry->artist);
				entry->artist = g_uri_unescape_string (breaks2[1], NULL);
			}
			if (g_str_has_prefix (breaks2[0], "t")) {
				g_free (entry->title);
				entry->title = g_uri_unescape_string (breaks2[1], NULL);
			}
			if (g_str_has_prefix (breaks2[0], "b")) {
				g_free (entry->album);
				entry->album = g_uri_unescape_string (breaks2[1], NULL);
			}
			if (g_str_has_prefix (breaks2[0], "m")) {
				g_free (entry->mbid);
				entry->mbid = g_uri_unescape_string (breaks2[1], NULL);
			}
			if (g_str_has_prefix (breaks2[0], "l")) {
				entry->length = atoi (breaks2[1]);
			}
			/* 'I' here is for backwards compatibility with queue files
			 * saved while we were using the 1.1 protocol.  see bug 508895.
			 */
			if (g_str_has_prefix (breaks2[0], "i") ||
			    g_str_has_prefix (breaks2[0], "I")) {
				entry->play_time = strtol (breaks2[1], NULL, 10);
			}
		}

		g_strfreev (breaks2);
	}

	g_strfreev (breaks);

	if (strcmp (entry->artist, "") == 0 || strcmp (entry->title, "") == 0) {
		rb_audioscrobbler_entry_free (entry);
		entry = NULL;
	}

	return entry;
}

void
rb_audioscrobbler_entry_save_to_string (GString *string, AudioscrobblerEntry *entry)
{
	AudioscrobblerEncodedEntry *encoded;

	encoded = rb_audioscrobbler_entry_encode (entry);
	g_string_append_printf (string,
				"a=%s&t=%s&b=%s&m=%s&l=%d&i=%ld\n",
				encoded->artist,
				encoded->title,
				encoded->album,
				encoded->mbid,
				encoded->length,
				(long)entry->play_time);
	rb_audioscrobbler_encoded_entry_free (encoded);
}

void
rb_audioscrobbler_entry_debug (AudioscrobblerEntry *entry, int index)
{
	rb_debug ("%-3d  artist: %s", index, entry->artist);
	rb_debug ("      album: %s", entry->album);
	rb_debug ("      title: %s", entry->title);
	rb_debug ("     length: %d", entry->length);
	rb_debug ("   playtime: %ld", (long)entry->play_time);
}

