/*
 *  Database record class for DAAP sharing
 *
 *  Copyright (C) 2008 W. Michael Petullo <mike@flyn.org>
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

#include <sys/stat.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "rhythmdb.h"
#include "rb-daap-record.h"

struct RBDAAPRecordPrivate {
	guint64 filesize;
	char *location;
	char *format;	 /* Format, possibly after transcoding. */
	char *real_format;
	char *title;
	char *album;
	char *artist;
	char *genre;
	gboolean has_video;
	gint mediakind;
	gint rating;
	int duration;
	int track;
	int year;
	int firstseen;
	int mtime;
	int disc;
	int bitrate;
	char *sort_artist;
	char *sort_album;
	gint64 albumid;
};

enum {
	PROP_0,
	PROP_LOCATION,
	PROP_TITLE,
	PROP_RATING,
	PROP_FILESIZE,
	PROP_ALBUM,
	PROP_ARTIST,
	PROP_GENRE,
	PROP_MEDIAKIND,
	PROP_FORMAT,
	PROP_DURATION,
	PROP_TRACK,
	PROP_YEAR,
	PROP_FIRSTSEEN,
	PROP_MTIME,
	PROP_DISC,
	PROP_BITRATE,
	PROP_HAS_VIDEO,
	PROP_REAL_FORMAT,
	PROP_ARTIST_SORT_NAME,
	PROP_ALBUM_SORT_NAME,
	PROP_ALBUM_ID
};

static void rb_daap_record_finalize (GObject *object);

static void
rb_daap_record_set_property (GObject *object,
				guint prop_id,
				const GValue *value,
				GParamSpec *pspec)
{
	RBDAAPRecord *record = RB_DAAP_RECORD (object);

	switch (prop_id) {
		case PROP_LOCATION:
			g_free (record->priv->location);
			record->priv->location = g_value_dup_string (value);
			break;
		case PROP_TITLE:
			g_free (record->priv->title);
			record->priv->title = g_value_dup_string (value);
			break;
		case PROP_ALBUM:
			g_free (record->priv->album);
			record->priv->album = g_value_dup_string (value);
			break;
		case PROP_ALBUM_ID:
			record->priv->albumid = g_value_get_int64 (value);
			break;
		case PROP_ARTIST:
			g_free (record->priv->artist);
			record->priv->artist = g_value_dup_string (value);
			break;
		case PROP_GENRE:
			g_free (record->priv->genre);
			record->priv->genre = g_value_dup_string (value);
			break;
		case PROP_MEDIAKIND:
			record->priv->mediakind = g_value_get_enum (value);
			break;
		case PROP_FORMAT:
			g_free (record->priv->format);
			record->priv->format = g_value_dup_string (value);
			break;
		case PROP_RATING:
			record->priv->rating = g_value_get_int (value);
			break;
		case PROP_FILESIZE:
			record->priv->filesize = g_value_get_uint64 (value);
			break;
		case PROP_DURATION:
			record->priv->duration = g_value_get_int (value);
			break;
		case PROP_TRACK:
			record->priv->track = g_value_get_int (value);
			break;
		case PROP_YEAR:
			record->priv->year = g_value_get_int (value);
			break;
		case PROP_FIRSTSEEN:
			record->priv->firstseen = g_value_get_int (value);
			break;
		case PROP_MTIME:
			record->priv->mtime = g_value_get_int (value);
			break;
		case PROP_DISC:
			record->priv->disc = g_value_get_int (value);
			break;
		case PROP_BITRATE:
			record->priv->bitrate = g_value_get_int (value);
			break;
		case PROP_HAS_VIDEO:
			record->priv->has_video = g_value_get_boolean (value);
			break;
		case PROP_REAL_FORMAT:
			g_free (record->priv->real_format);
			record->priv->real_format = g_value_dup_string (value);
			break;
		case PROP_ARTIST_SORT_NAME:
			g_free (record->priv->sort_artist);
			record->priv->sort_artist = g_value_dup_string (value);
			break;
		case PROP_ALBUM_SORT_NAME:
			g_free (record->priv->sort_album);
			record->priv->sort_album = g_value_dup_string (value);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_record_get_property (GObject *object,
				guint prop_id,
				GValue *value,
				GParamSpec *pspec)
{
	RBDAAPRecord *record = RB_DAAP_RECORD (object);

	switch (prop_id) {
		case PROP_LOCATION:
			g_value_set_string (value, record->priv->location);
			break;
		case PROP_TITLE:
			g_value_set_string (value, record->priv->title);
			break;
		case PROP_ALBUM:
			g_value_set_string (value, record->priv->album);
			break;
		case PROP_ALBUM_ID:
			g_value_set_int64 (value, record->priv->albumid);
			break;
		case PROP_ARTIST:
			g_value_set_string (value, record->priv->artist);
			break;
		case PROP_GENRE:
			g_value_set_string (value, record->priv->genre);
			break;
		case PROP_MEDIAKIND:
			g_value_set_enum (value, record->priv->mediakind);
			break;
		case PROP_FORMAT:
			g_value_set_string (value, record->priv->format);
			break;
		case PROP_RATING:
			g_value_set_int (value, record->priv->rating);
			break;
		case PROP_FILESIZE:
			g_value_set_uint64 (value, record->priv->filesize);
			break;
		case PROP_DURATION:
			g_value_set_int (value, record->priv->duration);
			break;
		case PROP_TRACK:
			g_value_set_int (value, record->priv->track);
			break;
		case PROP_YEAR:
			g_value_set_int (value, record->priv->year);
			break;
		case PROP_FIRSTSEEN:
			g_value_set_int (value, record->priv->firstseen);
			break;
		case PROP_MTIME:
			g_value_set_int (value, record->priv->mtime);
			break;
		case PROP_DISC:
			g_value_set_int (value, record->priv->disc);
			break;
		case PROP_BITRATE:
			g_value_set_int (value, record->priv->bitrate);
			break;
		case PROP_HAS_VIDEO:
			g_value_set_boolean (value, record->priv->has_video);
			break;
		case PROP_REAL_FORMAT:
			g_value_set_string (value, record->priv->real_format);
			break;
		case PROP_ARTIST_SORT_NAME:
			g_value_set_string (value, record->priv->sort_artist);
			break;
		case PROP_ALBUM_SORT_NAME:
			g_value_set_string (value, record->priv->sort_album);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

GInputStream *
rb_daap_record_read (DmapAvRecord *record, GError **error)
{
	GFile *file;
	GInputStream *fnval = NULL;

	file = g_file_new_for_uri (RB_DAAP_RECORD (record)->priv->location);
	fnval = G_INPUT_STREAM (g_file_read (file, NULL, error));

	g_object_unref (file);

	return fnval;
}

static void
rb_daap_record_init (RBDAAPRecord *record)
{
	record->priv = RB_DAAP_RECORD_GET_PRIVATE (record);

        record->priv->location		= NULL;
        record->priv->format		= NULL;
        record->priv->real_format	= NULL;
        record->priv->title		= NULL;
        record->priv->album		= NULL;
        record->priv->artist		= NULL;
        record->priv->genre		= NULL;
        record->priv->sort_artist	= NULL;
        record->priv->sort_album	= NULL;
	record->priv->filesize		= 0;
        record->priv->mediakind		= 0;
        record->priv->rating		= 0;
        record->priv->duration		= 0;
        record->priv->track		= 0;
        record->priv->year		= 0;
        record->priv->firstseen		= 0;
        record->priv->mtime		= 0;
        record->priv->disc		= 0;
        record->priv->bitrate		= 0;
        record->priv->has_video		= FALSE;
}

static void
rb_daap_record_class_init (RBDAAPRecordClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);

	g_type_class_add_private (klass, sizeof (RBDAAPRecordPrivate));

	gobject_class->set_property = rb_daap_record_set_property;
	gobject_class->get_property = rb_daap_record_get_property;
	gobject_class->finalize     = rb_daap_record_finalize;

	g_object_class_override_property (gobject_class, PROP_LOCATION, "location");
	g_object_class_override_property (gobject_class, PROP_TITLE, "title");
	g_object_class_override_property (gobject_class, PROP_ALBUM, "songalbum");
	g_object_class_override_property (gobject_class, PROP_ARTIST, "songartist");
	g_object_class_override_property (gobject_class, PROP_GENRE, "songgenre");
	g_object_class_override_property (gobject_class, PROP_MEDIAKIND, "mediakind");
	g_object_class_override_property (gobject_class, PROP_FORMAT, "format");
	g_object_class_override_property (gobject_class, PROP_RATING, "rating");
	g_object_class_override_property (gobject_class, PROP_FILESIZE, "filesize");
	g_object_class_override_property (gobject_class, PROP_DURATION, "duration");
	g_object_class_override_property (gobject_class, PROP_TRACK, "track");
	g_object_class_override_property (gobject_class, PROP_YEAR, "year");
	g_object_class_override_property (gobject_class, PROP_FIRSTSEEN, "firstseen");
	g_object_class_override_property (gobject_class, PROP_MTIME, "mtime");
	g_object_class_override_property (gobject_class, PROP_DISC, "disc");
	g_object_class_override_property (gobject_class, PROP_BITRATE, "bitrate");
	g_object_class_override_property (gobject_class, PROP_HAS_VIDEO, "has-video");
	g_object_class_override_property (gobject_class, PROP_ARTIST_SORT_NAME, "sort_artist");
	g_object_class_override_property (gobject_class, PROP_ALBUM_SORT_NAME, "sort_album");
	g_object_class_override_property (gobject_class, PROP_ALBUM_ID, "songalbumid");

	g_object_class_install_property (gobject_class, PROP_REAL_FORMAT,
	                        g_param_spec_string ("real-format",
			     "Real format of song data",
			     "Real format of song data",
			       NULL,
			    G_PARAM_READWRITE));
}

static void
rb_daap_record_class_finalize (RBDAAPRecordClass *klass)
{
}

static void
rb_daap_record_daap_iface_init (gpointer iface, gpointer data)
{
	DmapAvRecordInterface *daap_record = iface;

	g_assert (G_TYPE_FROM_INTERFACE (daap_record) == DMAP_TYPE_AV_RECORD);

	daap_record->read = rb_daap_record_read;
}

static void
rb_daap_record_dmap_iface_init (gpointer iface, gpointer data)
{
	DmapRecordInterface *dmap_record = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dmap_record) == DMAP_TYPE_RECORD);
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBDAAPRecord,
				rb_daap_record,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_AV_RECORD, rb_daap_record_daap_iface_init)
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_RECORD, rb_daap_record_dmap_iface_init))

static void
rb_daap_record_finalize (GObject *object)
{
	RBDAAPRecord *record = RB_DAAP_RECORD (object);

        g_free (record->priv->location);
	g_free (record->priv->title);
	g_free (record->priv->format);
	g_free (record->priv->album);
	g_free (record->priv->artist);
	g_free (record->priv->genre);
	g_free (record->priv->real_format);

	G_OBJECT_CLASS (rb_daap_record_parent_class)->finalize (object);
}

RBDAAPRecord *
rb_daap_record_new (RhythmDBEntry *entry)
{
	RBDAAPRecord *record = NULL;
	record = RB_DAAP_RECORD (g_object_new (RB_TYPE_DAAP_RECORD, NULL));

	/* When browsing, entry will be NULL because we will pull
	 * the metadata from the DAAP query. When sharing, entry will
	 * point to an existing entry from the Rhythmbox DB.
	 */
	if (entry) {
		gchar *ext;

		record->priv->filesize = rhythmdb_entry_get_uint64
						(entry, RHYTHMDB_PROP_FILE_SIZE);

		record->priv->location = rhythmdb_entry_dup_string
						(entry, RHYTHMDB_PROP_LOCATION);

		record->priv->title    = rhythmdb_entry_dup_string
						(entry, RHYTHMDB_PROP_TITLE);

		record->priv->artist   = rhythmdb_entry_dup_string
						(entry, RHYTHMDB_PROP_ARTIST);

		record->priv->album    = rhythmdb_entry_dup_string
						(entry, RHYTHMDB_PROP_ALBUM);

		/* Since we don't support album id's on Rhythmbox, "emulate" it */
		record->priv->albumid  = (gintptr) rhythmdb_entry_get_refstring
						(entry, RHYTHMDB_PROP_ALBUM);

		record->priv->genre    = rhythmdb_entry_dup_string
						(entry, RHYTHMDB_PROP_GENRE);

		/* FIXME: Support transcoding: */
		/* FIXME: we should use RHYTHMDB_PROP_MEDIA_TYPE instead */
		ext = strrchr (record->priv->location, '.');
		if (ext == NULL) {
			ext = "mp3";
		} else {
			ext++;
		}
		record->priv->mediakind = DMAP_MEDIA_KIND_MUSIC;
		record->priv->real_format = g_strdup (ext);
		record->priv->format = g_strdup (record->priv->real_format);

		record->priv->track    = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_TRACK_NUMBER);

		record->priv->duration = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_DURATION);

		record->priv->rating   = (gint) rhythmdb_entry_get_double
						(entry, RHYTHMDB_PROP_RATING);

		record->priv->year     = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_YEAR);

		record->priv->firstseen = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_FIRST_SEEN);

		record->priv->mtime     = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_MTIME);

		record->priv->disc      = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_DISC_NUMBER);

		record->priv->bitrate   = rhythmdb_entry_get_ulong
						(entry, RHYTHMDB_PROP_BITRATE);
	}

	return record;
}

void
_rb_daap_record_register_type (GTypeModule *module)
{
	rb_daap_record_register_type (module);
}
