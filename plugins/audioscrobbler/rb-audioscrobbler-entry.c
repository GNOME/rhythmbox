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

#define __EXTENSIONS__

#include "config.h"

#include <string.h>
#include <time.h>

#include <glib.h>
#include <glib/gi18n.h>

#include "rb-debug.h"
#include "rhythmdb.h"
#include <libsoup/soup.h>

#include "rb-audioscrobbler-entry.h"


#define SCROBBLER_DATE_FORMAT "%Y%%2D%m%%2D%d%%20%H%%3A%M%%3A%S"


void
rb_audioscrobbler_entry_init (AudioscrobblerEntry *entry)
{
	entry->artist = g_strdup ("");
	entry->album = g_strdup ("");
	entry->title = g_strdup ("");
	entry->length = 0;
	entry->play_time = 0;
	entry->mbid = g_strdup ("");
}

void
rb_audioscrobbler_entry_free (AudioscrobblerEntry *entry)
{
	g_free (entry->artist);
	g_free (entry->album);
	g_free (entry->title);
	g_free (entry->mbid);

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

	g_free (entry);
}


AudioscrobblerEntry *
rb_audioscrobbler_entry_create (RhythmDBEntry *rb_entry)
{
	AudioscrobblerEntry *as_entry = g_new0 (AudioscrobblerEntry, 1);

	as_entry->title = rhythmdb_entry_dup_string (rb_entry,
						     RHYTHMDB_PROP_TITLE);
	as_entry->artist = rhythmdb_entry_dup_string (rb_entry,
						      RHYTHMDB_PROP_ARTIST);
	as_entry->album = rhythmdb_entry_dup_string (rb_entry,
						     RHYTHMDB_PROP_ALBUM);
	if (strcmp (as_entry->album, _("Unknown")) == 0) {
		g_free (as_entry->album);
		as_entry->album = g_strdup ("");
	}
	as_entry->length = rhythmdb_entry_get_ulong (rb_entry,
						     RHYTHMDB_PROP_DURATION);
	as_entry->mbid = rhythmdb_entry_dup_string (rb_entry,
						    RHYTHMDB_PROP_MUSICBRAINZ_TRACKID);

	return as_entry;
}

AudioscrobblerEncodedEntry *
rb_audioscrobbler_entry_encode (AudioscrobblerEntry *entry)
{

	AudioscrobblerEncodedEntry *encoded;

	encoded = g_new0 (AudioscrobblerEncodedEntry, 1);
	
	encoded->artist = soup_uri_encode (entry->artist, 
					   EXTRA_URI_ENCODE_CHARS);
	encoded->title = soup_uri_encode (entry->title,
					  EXTRA_URI_ENCODE_CHARS);
	encoded->album = soup_uri_encode (entry->album, 
					  EXTRA_URI_ENCODE_CHARS);
	encoded->mbid = soup_uri_encode (entry->mbid, 
					 EXTRA_URI_ENCODE_CHARS);
	encoded->timestamp = g_new0 (gchar, 30);
	strftime (encoded->timestamp, 30, SCROBBLER_DATE_FORMAT, 
		  gmtime (&entry->play_time));

	encoded->length = entry->length;

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
				entry->artist = soup_uri_decode (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "t")) {
				g_free (entry->title);
				entry->title = soup_uri_decode (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "b")) {
				g_free (entry->album);
				entry->album = soup_uri_decode (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "m")) {
				g_free (entry->mbid);
				entry->mbid = soup_uri_decode (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "l")) {
				entry->length = atoi (breaks2[1]);
			}
			if (g_str_has_prefix (breaks2[0], "i")) {
				struct tm tm;
				strptime (breaks2[1], SCROBBLER_DATE_FORMAT, 
					  &tm);
				entry->play_time = mktime (&tm);
			}
			/* slight format extension: time_t */
			if (g_str_has_prefix (breaks2[0], "I")) {		
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
				"a=%s&t=%s&b=%s&m=%s&l=%d&I=%ld\n",
				encoded->artist,
				encoded->title,
				encoded->album,
				encoded->mbid,
				encoded->length,
				entry->play_time);
	rb_audioscrobbler_encoded_entry_free (encoded);
}

void
rb_audioscrobbler_entry_debug (AudioscrobblerEntry *entry, int index)
{
	char timestamp[30];
	rb_debug ("%-3d  artist: %s", index, entry->artist);
	rb_debug ("      album: %s", entry->album);
	rb_debug ("      title: %s", entry->title);
	rb_debug ("     length: %d", entry->length);
	rb_debug ("   playtime: %ld", entry->play_time);
	strftime (timestamp, 30, SCROBBLER_DATE_FORMAT, 
		  gmtime (&entry->play_time));
	rb_debug ("  timestamp: %s", timestamp);
}

