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

#ifndef RB_MUSICBRAINZ_LOOKUP_H
#define RB_MUSICBRAINZ_LOOKUP_H

#include <glib.h>
#include <gio/gio.h>

G_BEGIN_DECLS

#define RB_MUSICBRAINZ_ATTR_ASIN			"asin"
#define RB_MUSICBRAINZ_ATTR_COUNTRY			"country"
#define RB_MUSICBRAINZ_ATTR_DATE			"date"
#define RB_MUSICBRAINZ_ATTR_TITLE			"title"
#define RB_MUSICBRAINZ_ATTR_ALBUM			"album"
#define RB_MUSICBRAINZ_ATTR_ALBUM_ID			"album-id"
#define RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST		"album-artist"
#define RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_ID		"album-artist-id"
#define RB_MUSICBRAINZ_ATTR_ALBUM_ARTIST_SORTNAME	"album-artist-sortname"
#define RB_MUSICBRAINZ_ATTR_DISC_ID			"disc-id"
#define RB_MUSICBRAINZ_ATTR_DISC_NUMBER			"disc-number"
#define RB_MUSICBRAINZ_ATTR_TRACK_COUNT			"track-count"
#define RB_MUSICBRAINZ_ATTR_TRACK_NUMBER		"track-number"
#define RB_MUSICBRAINZ_ATTR_DURATION			"duration"
#define RB_MUSICBRAINZ_ATTR_TRACK_ID			"track-id"
#define RB_MUSICBRAINZ_ATTR_TITLE			"title"
#define RB_MUSICBRAINZ_ATTR_ARTIST			"artist"
#define RB_MUSICBRAINZ_ATTR_ARTIST_ID			"artist-id"
#define RB_MUSICBRAINZ_ATTR_ARTIST_SORTNAME		"artist-sortname"
#define RB_MUSICBRAINZ_ATTR_RELATION_TYPE		"relation-type"
#define RB_MUSICBRAINZ_ATTR_RELATION_TARGET		"relation-target"
#define RB_MUSICBRAINZ_ATTR_WORK_ID			"work-id"
#define RB_MUSICBRAINZ_ATTR_WORK_TITLE			"work-title"

typedef enum
{
	RB_MUSICBRAINZ_ERROR_NOT_FOUND,
	RB_MUSICBRAINZ_ERROR_NETWORK,
	RB_MUSICBRAINZ_ERROR_SERVER
} RBMusicBrainzError;

GType rb_musicbrainz_error_get_type (void);
GQuark rb_musicbrainz_error_quark (void);
#define RB_TYPE_MUSICBRAINZ_ERROR (rb_musicbrainz_error_get_type())
#define RB_MUSICBRAINZ_ERROR (rb_musicbrainz_error_quark())

typedef struct _RBMusicBrainzData RBMusicBrainzData;

void			rb_musicbrainz_data_free		(RBMusicBrainzData *data);
const char *		rb_musicbrainz_data_get_data_type	(RBMusicBrainzData *data);

GList *			rb_musicbrainz_data_get_attr_names	(RBMusicBrainzData *data);
GList *			rb_musicbrainz_data_get_attr_values	(RBMusicBrainzData *data,
								 const char *attr);
const char *		rb_musicbrainz_data_get_attr_value	(RBMusicBrainzData *data,
								 const char *attr);

RBMusicBrainzData *	rb_musicbrainz_data_find_child		(RBMusicBrainzData *data,
								 const char *attr,
								 const char *value);

GList *			rb_musicbrainz_data_get_children	(RBMusicBrainzData *data);


RBMusicBrainzData *	rb_musicbrainz_data_parse		(const char *data,
								 gssize len,
								 GError **error);

void			rb_musicbrainz_lookup			(const char *entity,
								 const char *entity_id,
								 const char **includes,
								 GCancellable *cancellable,
								 GAsyncReadyCallback callback,
								 gpointer user_data);

RBMusicBrainzData *	rb_musicbrainz_lookup_finish		(GAsyncResult *result,
								 GError **error);

char *			rb_musicbrainz_create_submit_url	(const char *disc_id,
								 const char *full_disc_id);


G_END_DECLS

#endif /* RB_MUSICBRAINZ_LOOKUP_H */
