/*
 *  arch-tag: Implementation of main song information database object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <libgnome/gnome-init.h>
#include <libxml/tree.h>
#include <gtk/gtkmain.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <monkey-media.h>
#include <unistd.h>
#include <string.h>

#include "rb-library.h"
#include "rhythmdb-legacy.h"
#include "rb-library-main-thread.h"
#include "rb-string-helpers.h"
#include "rb-thread-helpers.h"
#include "rb-library-action.h"
#include "rb-file-monitor.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-marshal.h"
#include "rb-file-helpers.h"

typedef struct 
{
	GValue track_number_val;
	GValue file_size_val;
	GValue title_val;
	GValue duration_val;
	GValue mtime_val;
	GValue genre_val;
	GValue artist_val;
	GValue album_val;
} RBLibraryEntryUpdateData;

static void rb_library_class_init (RBLibraryClass *klass);
static void rb_library_init (RBLibrary *library);
static void rb_library_finalize (GObject *object);
static void rb_library_set_property (GObject *object,
				     guint prop_id,
				     const GValue *value,
				     GParamSpec *pspec);
static void rb_library_get_property (GObject *object,
				     guint prop_id,
				     GValue *value,
				     GParamSpec *pspec);
static void synchronize_entry_with_data (RBLibrary *library, RhythmDBEntry *entry, RBLibraryEntryUpdateData *data);
static void rb_library_entry_restored_cb (RhythmDB *db, RhythmDBEntry *entry,
					  RBLibrary *library);
enum RBLibraryState
{
	LIBRARY_STATE_NONE,
	LIBRARY_STATE_INITIAL_REFRESH,
};

struct RBLibraryPrivate
{
	enum RBLibraryState state;

	RBLibraryMainThread *main_thread;

	GMutex *walker_mutex;

	RhythmDB *db;

	GHashTable *legacy_id_map;

	RBAtomic refresh_count;
	RBAtomic total_count;

	gboolean in_shutdown;

	GAsyncQueue *main_queue;
	GAsyncQueue *add_queue;
};

enum
{
	PROP_0,
	PROP_DB,
};

enum
{
	ERROR,
	LEGACY_LOAD_COMPLETE,
	LAST_SIGNAL,
};

static GObjectClass *parent_class = NULL;

static guint rb_library_signals[LAST_SIGNAL] = { 0 };

GType
rb_library_get_type (void)
{
	static GType rb_library_type = 0;

	if (rb_library_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBLibraryClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_library_class_init,
			NULL,
			NULL,
			sizeof (RBLibrary),
			0,
			(GInstanceInitFunc) rb_library_init
		};

		rb_library_type = g_type_register_static (G_TYPE_OBJECT,
						          "RBLibrary",
						          &our_info, 0);
	}

	return rb_library_type;
}

static void
rb_library_class_init (RBLibraryClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_library_finalize;
	object_class->set_property = rb_library_set_property;
	object_class->get_property = rb_library_get_property;

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB database",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	rb_library_signals[ERROR] =
		g_signal_new ("error",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryClass, error),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_STRING,
			      G_TYPE_NONE,
			      2,
			      G_TYPE_STRING,
			      G_TYPE_STRING);
	rb_library_signals[LEGACY_LOAD_COMPLETE] =
		g_signal_new ("legacy-load-complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBLibraryClass, legacy_load_complete),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__VOID,
			      G_TYPE_NONE,
			      0);
}

static void
rb_library_init (RBLibrary *library)
{
	library->priv = g_new0 (RBLibraryPrivate, 1);
	library->priv->main_queue = g_async_queue_new ();
	library->priv->add_queue = g_async_queue_new ();
	library->priv->legacy_id_map = g_hash_table_new (NULL, NULL);
}

static void
rb_library_pass_on_error (RBLibraryMainThread *thread,
			  const char *uri, const char *error,
			  RBLibrary *library)
{
	rb_debug ("passing on signal");
	g_signal_emit (G_OBJECT (library), rb_library_signals[ERROR], 0,
		       uri, error);
}

void
rb_library_release_brakes (RBLibrary *library)
{
	rb_debug ("releasing brakes");

	rb_debug ("library: kicking off main thread");
	library->priv->main_thread = rb_library_main_thread_new (library);

	g_signal_connect (G_OBJECT (library->priv->main_thread), "error",
			  G_CALLBACK (rb_library_pass_on_error), library);

}

static gboolean
queue_is_empty (GAsyncQueue *queue)
{
	return g_async_queue_length (queue) <= 0;
}

GAsyncQueue *
rb_library_get_main_queue (RBLibrary *library)
{
	return library->priv->main_queue;
}

GAsyncQueue *
rb_library_get_add_queue (RBLibrary *library)
{
	return library->priv->add_queue;
}

/* We don't particularly care about race conditions here.  This function
 * is just supposed to give some feedback about whether the library
 * is busy or not, it doesn't have to be precise.
 */
gboolean
rb_library_is_adding (RBLibrary *library)
{
	return !queue_is_empty (library->priv->add_queue);
}

gboolean
rb_library_is_refreshing (RBLibrary *library)
{
	return !queue_is_empty (library->priv->main_queue);
}

gboolean
rb_library_is_idle (RBLibrary *library)
{
	return !rb_library_is_adding (library)
		&& !rb_library_is_refreshing (library);
}

void
rb_library_shutdown (RBLibrary *library)
{
	rb_debug ("Shuuut it dooooown!");

	library->priv->in_shutdown = TRUE;

	g_object_unref (G_OBJECT (library->priv->main_thread));
}

static void
rb_library_finalize (GObject *object)
{
	RBLibrary *library;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_LIBRARY (object));

	library = RB_LIBRARY (object);

	g_return_if_fail (library->priv != NULL);

	rb_debug ("library: finalizing");

	g_free (library->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
	rb_debug ("library finalization complete");
}

static void
rb_library_set_property (GObject *object,
			 guint prop_id,
			 const GValue *value,
			 GParamSpec *pspec)
{
	RBLibrary *library = RB_LIBRARY (object);

	switch (prop_id)
	{
	case PROP_DB:
		library->priv->db = g_value_get_object (value);
		g_signal_connect_object (G_OBJECT (library->priv->db),
					 "entry_restored",
					 G_CALLBACK (rb_library_entry_restored_cb),
        				 library, 0);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_library_get_property (GObject *object,
			 guint prop_id,
			 GValue *value,
			 GParamSpec *pspec)
{
	RBLibrary *library = RB_LIBRARY (object);

	switch (prop_id)
	{
	case PROP_DB:
		g_value_set_object (value, library->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}


RBLibrary *
rb_library_new (RhythmDB *db)
{
	RBLibrary *library;

	library = RB_LIBRARY (g_object_new (RB_TYPE_LIBRARY, "db", db, NULL));

	g_return_val_if_fail (library->priv != NULL, NULL);

	return library;
}

double
rb_library_get_progress (RBLibrary *library)
{
	double total;
	double refresh_count;

	if (queue_is_empty (library->priv->main_queue))
		return -1.0;
	
	rb_atomic_inc (&library->priv->refresh_count);
	refresh_count = (double) rb_atomic_dec (&library->priv->refresh_count);
	
	/* RHYTHMDB FIXME */
	rb_atomic_inc (&library->priv->total_count);
	total = (double) rb_atomic_dec (&library->priv->total_count);
	
	return (refresh_count / total);
}

static RBLibraryEntryUpdateData *
rb_library_read_metadata (const char *location, GError **error)
{
	RBLibraryEntryUpdateData *data = g_new0 (RBLibraryEntryUpdateData, 1);
	MonkeyMediaStreamInfo *info;
	GnomeVFSFileInfo *vfsinfo;

	info = monkey_media_stream_info_new (location, error);
	if (G_UNLIKELY (info == NULL)) {
		g_free (data);
		return NULL;
	}

	/* track number */
	if (monkey_media_stream_info_get_value (info,
				                MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					        0, &data->track_number_val) == FALSE) {
		g_value_init (&data->track_number_val, G_TYPE_INT);
		g_value_set_int (&data->track_number_val, -1);
	}

	/* duration */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    0, &data->duration_val);

	/* filesize */
	monkey_media_stream_info_get_value (info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    0, &data->file_size_val);

	/* title */
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    0, &data->title_val);
	if (*(g_value_get_string (&data->title_val)) == '\0') {
		GnomeVFSURI *vfsuri;
		char *fname;

		vfsuri = gnome_vfs_uri_new (location);
		fname = gnome_vfs_uri_extract_short_name (vfsuri);
		g_value_set_string_take_ownership (&data->title_val, fname);
		gnome_vfs_uri_unref (vfsuri);
	}

	vfsinfo = gnome_vfs_file_info_new ();

	gnome_vfs_get_file_info (location, vfsinfo, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	/* mtime */
	g_value_init (&data->mtime_val, G_TYPE_LONG);
	g_value_set_long (&data->mtime_val, vfsinfo->mtime);

	gnome_vfs_file_info_unref (vfsinfo);

	/* genre */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    0,
				            &data->genre_val);

	/* artist */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    0,
				            &data->artist_val);

	/* album */
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    0,
				            &data->album_val);
	g_object_unref (G_OBJECT (info));
	return data;
}

/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
void
rb_library_add_uri_async (RBLibrary *library, const char *uri)
{
	RBLibraryAction *action = rb_library_action_new (RB_LIBRARY_ACTION_ADD_FILE, uri);
	rb_debug ("queueing ADD_FILE for %s", uri);
	g_async_queue_push (library->priv->add_queue, action);
}

/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
void
rb_library_add_uri (RBLibrary *library, const char *uri, GError **error)
{
	RhythmDBEntry *entry;
	char *realuri;
	RBLibraryEntryUpdateData *metadata;

	realuri = rb_uri_resolve_symlink (uri);

	rhythmdb_read_lock (library->priv->db);

	entry = rhythmdb_entry_lookup_by_location (library->priv->db, realuri);

	rhythmdb_read_unlock (library->priv->db);

	if (entry) {
		rb_debug ("location \"%s\" already in library", realuri);
		return;
	}

	metadata = rb_library_read_metadata (uri, error);
	if (!metadata) {
		rb_debug ("failed to read data from \"%s\"", uri);
		return;
	}

	rhythmdb_write_lock (library->priv->db);

	entry = rhythmdb_entry_lookup_by_location (library->priv->db, realuri);

	if (entry) {
		rb_debug ("location \"%s\" already in library", realuri);
		rhythmdb_write_unlock (library->priv->db);
		return;
	}

	entry = rhythmdb_entry_new (library->priv->db, RHYTHMDB_ENTRY_TYPE_SONG, realuri);
	synchronize_entry_with_data (library, entry, metadata);
	g_free (metadata);

	rhythmdb_write_unlock (library->priv->db);

	rb_file_monitor_add (rb_file_monitor_get (), uri);
}

static void
rb_library_entry_restored_cb (RhythmDB *db, RhythmDBEntry *entry,
			      RBLibrary *library)
{
	RhythmDBEntryType type;
	type = rhythmdb_entry_get_int (db, entry, RHYTHMDB_PROP_TYPE);
	if (type == RHYTHMDB_ENTRY_TYPE_SONG) {
		rb_atomic_inc (&library->priv->total_count);
		rhythmdb_entry_ref_unlocked (db, entry);
		g_async_queue_push (library->priv->main_queue, entry);
	}
}

/* MULTI-THREAD ENTRYPOINT
 * Locks required: None
 */
void
rb_library_update_entry (RBLibrary *library, RhythmDBEntry *entry, GError **error)
{
	char *location;
	RBLibraryEntryUpdateData *metadata;

	rb_atomic_inc (&library->priv->refresh_count);

	rhythmdb_read_lock (library->priv->db);
	location = rhythmdb_entry_get_string (library->priv->db, entry, RHYTHMDB_PROP_LOCATION);
	rhythmdb_read_unlock (library->priv->db);
	
	if (rb_uri_exists (location) == FALSE) {
		rb_debug ("song \"%s\" was deleted", location);
		rhythmdb_entry_delete (library->priv->db, entry);
		goto out;
	}

	metadata = rb_library_read_metadata (location, error);
	if (!metadata) {
		rb_debug ("failed to read data from \"%s\"", location);
		goto out;
	}

	rhythmdb_write_lock (library->priv->db);
	
	rb_debug ("updating existing entry \"%s\"", location);
	synchronize_entry_with_data (library, entry, metadata);
	g_free (metadata);
	rhythmdb_entry_unref_unlocked (library->priv->db, entry);

	rhythmdb_write_unlock (library->priv->db);

	if (!(error && *error))
		rb_file_monitor_add (rb_file_monitor_get (), location);
out:
	g_free (location);
}

/*
 * Locks required: None
 */
static void
synchronize_entry_with_data (RBLibrary *library, RhythmDBEntry *entry, RBLibraryEntryUpdateData *data)
{
	rb_debug ("synchronizing entry %p", entry);

	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_TRACK_NUMBER, &data->track_number_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_DURATION, &data->duration_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_FILE_SIZE, &data->file_size_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_TITLE, &data->title_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_MTIME, &data->mtime_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_GENRE, &data->genre_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_ARTIST, &data->artist_val);
	rhythmdb_entry_set (library->priv->db, entry, RHYTHMDB_PROP_ALBUM, &data->album_val);
}

char *
rb_library_get_status (RBLibrary *library)
{
	char *ret = NULL;

	if (rb_library_is_adding (library))
		ret = g_strdup_printf ("<b>%s</b>", _("Loading songs..."));
	else if (rb_library_is_refreshing (library))
		ret = g_strdup_printf ("<b>%s</b>", _("Refreshing songs..."));

	return ret;
}

char *
rb_library_compute_status_normal (gint n_songs, glong duration, GnomeVFSFileSize size)
{
	float days;
	long hours, minutes, seconds;
	char *songcount;
	char *time;
	char *size_str;
	char *ret;

	songcount = g_strdup_printf (ngettext ("%d song", "%d songs", n_songs), n_songs);

	days    = (float) duration / (float) (60 * 60 * 24); 
	hours   = duration / (60 * 60);
	minutes = duration / 60 - hours * 60;
	seconds = duration % 60;

	if (days >= 1.0) {
		time = g_strdup_printf ("%.1f days", days);
	} else {
		const char *minutefmt = ngettext ("%ld minute", "%ld minutes", minutes);
		if (hours >= 1) {		
			const char *hourfmt = ngettext ("%ld hour", "%ld hours", hours);
			char *fmt = g_strdup_printf (_("%s and %s"), hourfmt, minutefmt);
			time = g_strdup_printf (fmt, hours, minutes);
			g_free (fmt);
		} else 
			time = g_strdup_printf (minutefmt, minutes);
	}
	size_str = gnome_vfs_format_file_size_for_display (size);
	ret = g_strdup_printf ("%s, %s, %s", songcount, time, size_str);
	g_free (songcount);
	g_free (time);
	g_free (size_str);

	return ret;
}

typedef struct
{
	RhythmDB *db;
	RBLibrary *library;
	char *libname;
} RBLibraryLegacyLoadData;

RhythmDBEntry *
rb_library_legacy_id_to_entry (RBLibrary *library, guint id)
{
	return g_hash_table_lookup (library->priv->legacy_id_map, GINT_TO_POINTER (id));
}

static gboolean
emit_legacy_load_complete (RBLibrary *library)
{
	g_signal_emit (G_OBJECT (library), rb_library_signals[LEGACY_LOAD_COMPLETE], 0);
	g_hash_table_destroy (library->priv->legacy_id_map);
	g_object_unref (G_OBJECT (library));
	return FALSE;
}

gpointer
legacy_load_thread_main (RBLibraryLegacyLoadData *data)
{
	xmlDocPtr doc;
	xmlNodePtr root, child;
	guint id;

	doc = xmlParseFile (data->libname);

	if (doc == NULL) {
		g_object_unref (G_OBJECT (data->library));
		goto free_exit;
	}

	rb_debug ("parsing entries");
	root = xmlDocGetRootElement (doc);
	for (child = root->children; child != NULL; child = child->next) {
		RhythmDBEntry *entry = 
			rhythmdb_legacy_parse_rbnode (data->db, RHYTHMDB_ENTRY_TYPE_SONG, child,
						      &id);

		if (id > 0)
			g_hash_table_insert (data->library->priv->legacy_id_map, GINT_TO_POINTER (id),
					     entry);
	}
	xmlFreeDoc (doc);

	/* steals the library ref */
	g_idle_add ((GSourceFunc) emit_legacy_load_complete, data->library);
	rb_debug ("legacy load thread exiting");
free_exit:
	g_object_unref (G_OBJECT (data->db));
	g_free (data->libname);
	g_free (data);
	g_thread_exit (NULL);
	return NULL;
}


void
rb_library_load_legacy (RBLibrary *library)
{
	RBLibraryLegacyLoadData *data;
	char *libname = g_build_filename (rb_dot_dir (), "library-2.1.xml", NULL);

	if (!g_file_test (libname, G_FILE_TEST_EXISTS)) {
		g_free (libname);
		return;
	}

	data = g_new0 (RBLibraryLegacyLoadData, 1);
	g_object_get (G_OBJECT (library), "db", &data->db, NULL);
	g_object_ref (G_OBJECT (data->db));
	data->libname = libname;
	data->library = library;
	g_object_ref (G_OBJECT (data->library));
	
	rb_debug ("kicking off library legacy loading thread");
	g_thread_create ((GThreadFunc) legacy_load_thread_main, data, FALSE, NULL);
}

