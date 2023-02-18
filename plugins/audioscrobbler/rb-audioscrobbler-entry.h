/*
 * Copyright (C) 2007 Christophe Fergeau <teuf@gnome.org>
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

#ifndef __RB_AUDIOSCROBBLER_ENTRY_H
#define __RB_AUDIOSCROBBLER_ENTRY_H

G_BEGIN_DECLS

#include "rhythmdb.h"
#include "rb-audioscrobbler-service.h"

typedef struct
{
	gchar *artist;
	gchar *album;
	gchar *title;
	guint length;
	gulong track;
	gchar *mbid;
	time_t play_time;
	gchar *source;
} AudioscrobblerEntry;

typedef struct
{
	gchar *artist;
	gchar *album;
	gchar *title;
	guint length;
	gchar *mbid;
	gchar *timestamp;
	gchar *source;
	gchar *track;
} AudioscrobblerEncodedEntry;


void	     			rb_audioscrobbler_entry_init (AudioscrobblerEntry *entry);
void	     			rb_audioscrobbler_entry_free (AudioscrobblerEntry *entry);
void          			rb_audioscrobbler_encoded_entry_free (AudioscrobblerEncodedEntry *entry);
AudioscrobblerEncodedEntry *	rb_audioscrobbler_entry_encode (AudioscrobblerEntry *entry);

AudioscrobblerEntry *		rb_audioscrobbler_entry_create (RhythmDBEntry *rb_entry, RBAudioscrobblerService *service);

AudioscrobblerEntry *		rb_audioscrobbler_entry_load_from_string (const char *string);
void				rb_audioscrobbler_entry_save_to_string (GString *string, AudioscrobblerEntry *entry);

void				rb_audioscrobbler_entry_debug (AudioscrobblerEntry *entry, int index);

G_END_DECLS

#endif /* __RB_AUDIOSCROBBLER_ENTRY_H */

