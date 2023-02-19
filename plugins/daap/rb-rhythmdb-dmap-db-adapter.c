/*
 *  Database adapter class for DMAP sharing
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

#include <config.h>

#include "rhythmdb.h"
#include "rb-rhythmdb-dmap-db-adapter.h"
#include "rb-daap-record.h"

#include <glib/gi18n.h>
#include <libdmapsharing/dmap.h>

struct RBRhythmDBDMAPDbAdapterPrivate {
	RhythmDB *db;
	RhythmDBEntryType *entry_type;
};

typedef struct ForeachAdapterData {
	gpointer data;
	DmapIdRecordFunc func;
} ForeachAdapterData;

static DmapRecord *
rb_rhythmdb_dmap_db_adapter_lookup_by_id (const DmapDb *db, guint id)
{
	RhythmDBEntry *entry;

	g_assert (RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db != NULL);

	entry = rhythmdb_entry_lookup_by_id (
			RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db,
			id);

	return DMAP_RECORD (rb_daap_record_new (entry));
}

static void
foreach_adapter (RhythmDBEntry *entry, gpointer data)
{
	gulong id;
	DmapRecord *record;
	ForeachAdapterData *foreach_adapter_data;
	char *playback_uri;

	if (rhythmdb_entry_get_boolean (entry, RHYTHMDB_PROP_HIDDEN))
		return;

	playback_uri = rhythmdb_entry_get_playback_uri (entry);
	if (playback_uri == NULL)
		return;

	id = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID);
	foreach_adapter_data = data;
	record = DMAP_RECORD (rb_daap_record_new (entry));

	foreach_adapter_data->func (id,
				    record,
				    foreach_adapter_data->data);

	g_free (playback_uri);
	g_object_unref (record);
}

static void
rb_rhythmdb_dmap_db_adapter_foreach (const DmapDb *db,
                                     DmapIdRecordFunc func,
                                     gpointer data)
{
	ForeachAdapterData *foreach_adapter_data;

	g_assert (RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db != NULL);

	foreach_adapter_data = g_new (ForeachAdapterData, 1);
	foreach_adapter_data->data = data;
	foreach_adapter_data->func = func;

	rhythmdb_entry_foreach_by_type (RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db,
					RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->entry_type,
				        foreach_adapter,
				        foreach_adapter_data);

	g_free (foreach_adapter_data);
}

static gint64
rb_rhythmdb_dmap_db_adapter_count (const DmapDb *db)
{
	g_assert (RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db != NULL);
	return rhythmdb_entry_count_by_type (
			RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->db,
			RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv->entry_type);
}

static void
entry_set_string_prop (RhythmDB        *db,
                       RhythmDBEntry   *entry,
                       RhythmDBPropType propid,
                       const char      *str)
{
        GValue value = {0,};
        const gchar *tmp;

        if (str == NULL || *str == '\0' || !g_utf8_validate (str, -1, NULL)) {
                tmp = _("Unknown");
        } else {
                tmp = str;
        }

        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, tmp);
        rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
        g_value_unset (&value);
}

static guint
rb_rhythmdb_dmap_db_adapter_add (DmapDb *db, DmapRecord *record, GError **error)
{
	gchar *uri = NULL;
	const gchar *title = NULL;
	const gchar *album = NULL;
	const gchar *artist = NULL;
	const gchar *format = NULL;
	const gchar *genre = NULL;
	gint length = 0;
	gint track = 0;
	gint disc = 0;
	gint year = 0;
	gint filesize = 0;
	gint bitrate = 0;
	GValue value = { 0, };
	RhythmDBEntry *entry = NULL;
	RBRhythmDBDMAPDbAdapterPrivate *priv = RB_RHYTHMDB_DMAP_DB_ADAPTER (db)->priv;

	g_assert (priv->db != NULL);

	g_object_get (record,
		     "location", &uri,
		     "year", &year,
                     "track", &track,
                     "disc", &disc,
                     "bitrate", &bitrate,
                     "duration", &length,
                     "filesize", &filesize,
		     "format", &format,
                     "title", &title,
                     "songalbum", &album,
                     "songartist", &artist,
                     "songgenre", &genre,
		      NULL);

	entry = rhythmdb_entry_new (priv->db, priv->entry_type, uri);

	if (entry == NULL) {
		g_warning ("cannot create entry for daap track %s", uri);
		return FALSE;
	}

	/* year */
	if (year != 0) {
		GDate date;
		gulong julian;

		/* create dummy date with given year */
		g_date_set_dmy (&date, 1, G_DATE_JANUARY, year);
		julian = g_date_get_julian (&date);

		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value,julian);
		rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DATE, &value);
		g_value_unset (&value);
	}

	/* track number */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value,(gulong)track);
	rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &value);
	g_value_unset (&value);

	/* disc number */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value,(gulong)disc);
	rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DISC_NUMBER, &value);
	g_value_unset (&value);

	/* bitrate */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value,(gulong)bitrate);
	rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_BITRATE, &value);
	g_value_unset (&value);

	/* length */
	g_value_init (&value, G_TYPE_ULONG);
	g_value_set_ulong (&value,(gulong)length);
	rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_DURATION, &value);
	g_value_unset (&value);

	/* file size */
	g_value_init (&value, G_TYPE_UINT64);
	g_value_set_uint64(&value,(gint64)filesize);
	rhythmdb_entry_set (priv->db, entry, RHYTHMDB_PROP_FILE_SIZE, &value);
	g_value_unset (&value);

	/* title */
	entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_TITLE, title);

	/* album */
	entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ALBUM, album);

	/* artist */
	entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_ARTIST, artist);

	/* genre */
	entry_set_string_prop (priv->db, entry, RHYTHMDB_PROP_GENRE, genre);

	rhythmdb_commit (priv->db);

	return rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_ENTRY_ID);
}

static void
rb_rhythmdb_dmap_db_adapter_init (RBRhythmDBDMAPDbAdapter *db)
{
	db->priv = RB_RHYTHMDB_DMAP_DB_ADAPTER_GET_PRIVATE (db);
}

static void
rb_rhythmdb_dmap_db_adapter_class_init (RBRhythmDBDMAPDbAdapterClass *klass)
{
	g_type_class_add_private (klass, sizeof (RBRhythmDBDMAPDbAdapterPrivate));
}

static void
rb_rhythmdb_dmap_db_adapter_class_finalize (RBRhythmDBDMAPDbAdapterClass *klass)
{
}

static void
rb_rhythmdb_dmap_db_adapter_interface_init (gpointer iface, gpointer data)
{
	DmapDbInterface *dmap_db = iface;

	g_assert (G_TYPE_FROM_INTERFACE (dmap_db) == DMAP_TYPE_DB);

	dmap_db->add = rb_rhythmdb_dmap_db_adapter_add;
	dmap_db->lookup_by_id = rb_rhythmdb_dmap_db_adapter_lookup_by_id;
	dmap_db->foreach = rb_rhythmdb_dmap_db_adapter_foreach;
	dmap_db->count = rb_rhythmdb_dmap_db_adapter_count;
}

G_DEFINE_DYNAMIC_TYPE_EXTENDED (RBRhythmDBDMAPDbAdapter,
				rb_rhythmdb_dmap_db_adapter,
				G_TYPE_OBJECT,
				0,
				G_IMPLEMENT_INTERFACE_DYNAMIC (DMAP_TYPE_DB,
							       rb_rhythmdb_dmap_db_adapter_interface_init))

RBRhythmDBDMAPDbAdapter *
rb_rhythmdb_dmap_db_adapter_new (RhythmDB *rdb, RhythmDBEntryType *entry_type)
{
	RBRhythmDBDMAPDbAdapter *db;

	db = RB_RHYTHMDB_DMAP_DB_ADAPTER (g_object_new (RB_TYPE_DMAP_DB_ADAPTER,
					       NULL));

	db->priv->db = rdb;
	db->priv->entry_type = entry_type;

	return db;
}

void
_rb_rhythmdb_dmap_db_adapter_register_type (GTypeModule *module)
{
	rb_rhythmdb_dmap_db_adapter_register_type (module);
}
