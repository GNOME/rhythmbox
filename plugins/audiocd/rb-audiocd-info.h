/*
 *  Copyright (C) 2012 Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_AUDIOCD_INFO_H
#define RB_AUDIOCD_INFO_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

typedef struct {
	gboolean is_audio;
	int track_num;
	int duration;		/* milliseconds? */
	char *artist;
	char *title;
} RBAudioCDTrack;

typedef struct {
	char *device;

	char *musicbrainz_disc_id;
	char *musicbrainz_full_disc_id;

	char *album;
	char *genre;
	char *album_artist;

	int num_tracks;
	RBAudioCDTrack *tracks;
} RBAudioCDInfo;

RBAudioCDInfo *		rb_audiocd_info_finish		(GAsyncResult *result,
							 GError **error);

void			rb_audiocd_info_get		(const char *device,
							 GCancellable *cancellable,
							 GAsyncReadyCallback callback,
							 gpointer user_data);

void			rb_audiocd_info_free		(RBAudioCDInfo *info);

#endif /* RB_AUDIOCD_INFO_H */
