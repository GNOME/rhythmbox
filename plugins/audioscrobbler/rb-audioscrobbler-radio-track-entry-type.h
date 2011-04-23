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

#ifndef __RB_AUDIOSCROBBLER_RADIO_TRACK_ENTRY_TYPE_H
#define __RB_AUDIOSCROBBLER_RADIO_TRACK_ENTRY_TYPE_H

#include "rhythmdb-entry-type.h"
#include "rhythmdb.h"
#include "rb-audioscrobbler-service.h"

G_BEGIN_DECLS

typedef struct
{
	char *image_url;
	char *track_auth;
	char *download_url;
	RBAudioscrobblerService *service;
} RBAudioscrobblerRadioTrackData;

#define RHYTHMDB_ENTRY_TYPE_AUDIOSCROBBLER_RADIO_TRACK (rb_audioscrobbler_radio_track_get_entry_type ())
RhythmDBEntryType *rb_audioscrobbler_radio_track_get_entry_type (void);

void rb_audioscrobbler_radio_track_register_entry_type (RhythmDB *db);

void _rb_audioscrobbler_radio_track_entry_type_register_type (GTypeModule *module);

G_END_DECLS

#endif /* #ifndef __RB_AUDIOSCROBBLER_RADIO_TRACK_ENTRY_TYPE_H */
