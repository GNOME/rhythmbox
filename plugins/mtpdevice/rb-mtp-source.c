/*
 *  arch-tag: Implementation of mtp source object
 *
 *  Copyright (C) 2006 Peter Grundström  <pete@openfestis.org>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <config.h>

#include <string.h>
#include <gtk/gtktreeview.h>
#include <libgnome/gnome-i18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-volume.h>
#include <libgnomevfs/gnome-vfs-volume-monitor.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"
#include "rb-plugin.h"
#include "rb-removable-media-manager.h"
#include "rb-static-playlist-source.h"
#include "rb-util.h"
#include "rb-refstring.h"
#include "rhythmdb.h"
#include "rb-encoder.h"

#include "rb-mtp-source.h"

static GObject *rb_mtp_source_constructor (GType type,
					   guint n_construct_properties,
					   GObjectConstructParam *construct_properties);
static void rb_mtp_source_finalize (GObject *object);


static void rb_mtp_source_load_tracks (RBMtpSource*);
static gboolean rb_mtp_source_transfer_track_to_disk (LIBMTP_mtpdevice_t *device,
						      LIBMTP_track_t *track,
						      const char *uri);
static char* rb_mtp_source_get_playback_uri (RhythmDBEntry *entry,
					     gpointer data);

static void impl_delete_thyself (RBSource *source);
static void impl_delete (RBSource *asource);
static gboolean impl_show_popup (RBSource *source);
static GList* impl_get_ui_actions (RBSource *source);
static GList* impl_copy (RBSource *source);

static GList * impl_get_mime_types (RBRemovableMediaSource *source);
static gboolean impl_track_added (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *dest,
				  const char *mimetype);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static RhythmDB * get_db_for_source (RBMtpSource *source);

typedef struct
{
	LIBMTP_mtpdevice_t *device;
	GHashTable *entry_map;
	char *udi;
} RBMtpSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBMtpSource,
		       rb_mtp_source,
		       RB_TYPE_REMOVABLE_MEDIA_SOURCE)

#define MTP_SOURCE_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_MTP_SOURCE, RBMtpSourcePrivate))

static void
rb_mtp_source_class_init (RBMtpSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);

	object_class->constructor = rb_mtp_source_constructor;
	object_class->finalize = rb_mtp_source_finalize;

	source_class->impl_can_rename = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_delete = (RBSourceFeatureFunc) rb_true_function;
#ifdef ENABLE_TRACK_TRANSFER
	source_class->impl_can_paste = (RBSourceFeatureFunc) rb_true_function;
#endif
	source_class->impl_can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->impl_can_copy = (RBSourceFeatureFunc) rb_true_function;
	source_class->impl_can_cut = (RBSourceFeatureFunc) rb_false_function;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_get_ui_actions = impl_get_ui_actions;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_delete = impl_delete;
	source_class->impl_copy = impl_copy;

	rms_class->impl_track_added = impl_track_added;
	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;

	g_type_class_add_private (klass, sizeof (RBMtpSourcePrivate));
}

static void
rb_mtp_source_name_changed_cb (RBMtpSource *source,
			       GParamSpec *spec,
			       gpointer data)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	if (priv->device) {
		char *name = NULL;

		g_object_get (source, "name", &name, NULL);
		if (LIBMTP_Set_Friendlyname (priv->device, name) != 0) {
			rb_debug ("Set friendly name failed");
		}
		g_free (name);
	}
}

static void
rb_mtp_source_init (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	priv->entry_map = g_hash_table_new (g_direct_hash,
					    g_direct_equal);

	g_signal_connect (G_OBJECT (source), "notify::name",
			  (GCallback)rb_mtp_source_name_changed_cb, NULL);
}

static GObject *
rb_mtp_source_constructor (GType type, guint n_construct_properties,
			   GObjectConstructParam *construct_properties)
{
	RBMtpSource *source;
	RBEntryView *tracks;
	GtkIconTheme *theme;
	GdkPixbuf *pixbuf;
	gint size;

	source = RB_MTP_SOURCE (G_OBJECT_CLASS (rb_mtp_source_parent_class)->
				constructor (type, n_construct_properties, construct_properties));

	tracks = rb_source_get_entry_view (RB_SOURCE (source));
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_RATING, FALSE);
	rb_entry_view_append_column (tracks, RB_ENTRY_VIEW_COL_LAST_PLAYED, FALSE);

	/* icon */
	theme = gtk_icon_theme_get_default ();
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
	pixbuf = gtk_icon_theme_load_icon (theme, "multimedia-player", size, 0, NULL);

	rb_source_set_pixbuf (RB_SOURCE (source), pixbuf);
	g_object_unref (pixbuf);

	return G_OBJECT (source);
}

static void
rb_mtp_source_finalize (GObject *object)
{
	G_OBJECT_CLASS (rb_mtp_source_parent_class)->finalize (object);
}

RBBrowserSource *
rb_mtp_source_new (RBShell *shell,
		   LIBMTP_mtpdevice_t *device,
		   const char *udi)
{
	RBMtpSource *source = NULL;
	RhythmDBEntryType entry_type;
	RhythmDB *db = NULL;
	RBMtpSourcePrivate *priv = NULL;
	char *name = NULL;

	g_object_get (shell, "db", &db, NULL);
	name = g_strdup_printf ("MTP-%s", LIBMTP_Get_Serialnumber (device));

	entry_type = rhythmdb_entry_register_type (db, name);
	entry_type->save_to_disk = FALSE;
	entry_type->category = RHYTHMDB_ENTRY_NORMAL;
	entry_type->get_playback_uri = (RhythmDBEntryStringFunc)rb_mtp_source_get_playback_uri;

	g_free (name);
	g_object_unref (db);

	source = RB_MTP_SOURCE (g_object_new (RB_TYPE_MTP_SOURCE,
					      "entry-type", entry_type,
					      "shell", shell,
					      "visibility", TRUE,
					      "volume", NULL,
					      "source-group", RB_SOURCE_GROUP_DEVICES,
					      NULL));

	entry_type->get_playback_uri_data = source;

	priv = MTP_SOURCE_GET_PRIVATE (source);
	priv->device = device;
	priv->udi = g_strdup (udi);

	rb_mtp_source_load_tracks (source);

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_BROWSER_SOURCE (source);
}

static void
entry_set_string_prop (RhythmDB *db,
		       RhythmDBEntry *entry,
		       RhythmDBPropType propid,
		       const char *str)
{
	GValue value = {0,};

	if (!str)
		str = _("Unknown");

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_static_string (&value, str);
	rhythmdb_entry_set (RHYTHMDB (db), entry, propid, &value);
	g_value_unset (&value);
}

static void
add_mtp_track_to_db (RBMtpSource *source,
		     LIBMTP_track_t *track)
{
	RhythmDBEntry *entry = NULL;
	RhythmDBEntryType entry_type;
	RhythmDB *db = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	db = get_db_for_source (source);

	/* Set URI */
	g_object_get (G_OBJECT (source), "entry-type", &entry_type, NULL);
	name = g_strdup_printf ("rb-mtp-%i", track->item_id);
	entry = rhythmdb_entry_new (RHYTHMDB (db), entry_type, name);
	g_free (name);

	if (entry == NULL) {
		rb_debug ("cannot create entry %i", track->item_id);
		g_object_unref (G_OBJECT (db));
		return;
	}

	/* Set track number */
	if (track->tracknumber != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->tracknumber);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_TRACK_NUMBER,
				    &value);
		g_value_unset (&value);
	}

	/* Set length */
	if (track->duration != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_ULONG);
		g_value_set_ulong (&value, track->duration/1000);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_DURATION,
				    &value);
		g_value_unset (&value);
	}

	/* Set file size */
	if (track->filesize != 0) {
		GValue value = {0, };
		g_value_init (&value, G_TYPE_UINT64);
		g_value_set_uint64 (&value, track->filesize);
		rhythmdb_entry_set (RHYTHMDB (db), entry,
				    RHYTHMDB_PROP_FILE_SIZE,
				    &value);
		g_value_unset (&value);
	}

	/* Set title */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_TITLE, track->title);

	/* Set album, artist and genre from MTP */
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ARTIST, track->artist);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_ALBUM, track->album);
	entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_GENRE, track->genre);

	g_hash_table_insert (priv->entry_map, entry, track);
	rhythmdb_commit (RHYTHMDB (db));

	g_object_unref (G_OBJECT (db));
}

static gboolean
load_mtp_db_idle_cb (RBMtpSource* source)
{
	RhythmDB *db = NULL;
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	LIBMTP_track_t *tracks = NULL;

	db = get_db_for_source (source);

	g_assert (db != NULL);

	tracks = LIBMTP_Get_Tracklisting (priv->device);
	if (tracks != NULL) {
		LIBMTP_track_t *track, *tmp = NULL;
		for (track = tracks; track != NULL; track = track->next) {
			add_mtp_track_to_db (source, track);
			tmp = track;
		}
	} else {
		rb_debug ("No tracks");
	}

	g_object_unref (G_OBJECT (db));
	return FALSE;
}

static void
rb_mtp_source_load_tracks (RBMtpSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *name = NULL;

	if ((priv->device != NULL) && (priv->entry_map != NULL)) {
		name = LIBMTP_Get_Friendlyname (priv->device);
		if (name == NULL) {
			name = LIBMTP_Get_Modelname (priv->device);
		}
		if (name == NULL) {
			name = g_strdup (_("Digital Audio Player"));
		}

		g_object_set (RB_SOURCE (source),
			      "name", name,
			      NULL);
		g_idle_add ((GSourceFunc)load_mtp_db_idle_cb, source);
	}
	g_free (name);
}

static gboolean
destroy_entry_map_pair (RhythmDBEntry *entry, LIBMTP_track_t *track, RhythmDB *db)
{
	LIBMTP_destroy_track_t (track);
	rhythmdb_entry_delete (db, entry);
	return TRUE;
}

static void
impl_delete_thyself (RBSource *asource)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (asource);
	RhythmDB *db = get_db_for_source (RB_MTP_SOURCE (asource));

	if (priv->entry_map) {
		g_hash_table_foreach_remove (priv->entry_map, (GHRFunc)destroy_entry_map_pair, db);
		g_hash_table_destroy (priv->entry_map);
		rhythmdb_commit (db);
		priv->entry_map = NULL;
	}

	g_object_unref (db);

	LIBMTP_Release_Device (priv->device);
	priv->device = NULL;

	if (priv->udi != NULL) {
		g_free (priv->udi);
		priv->udi = NULL;
	}

	RB_SOURCE_CLASS (rb_mtp_source_parent_class)->impl_delete_thyself (asource);
}

static char *
gdate_to_char (GDate* date)
{
	return g_strdup_printf ("%04i%02i%02iT0000.0",
				g_date_get_year (date),
				g_date_get_month (date),
				g_date_get_day (date));
}

static LIBMTP_filetype_t
mimetype_to_filetype (const char *mimetype)
{
	if (!strcmp (mimetype, "audio/mpeg") || !strcmp (mimetype, "application/x-id3")) {
		return LIBMTP_FILETYPE_MP3;
	}  else if (!strcmp (mimetype, "audio/x-wav")) {
		return  LIBMTP_FILETYPE_WAV;
	} else if (!strcmp (mimetype, "application/ogg")) {
		return LIBMTP_FILETYPE_OGG;
	} else if (!strcmp (mimetype, "audio/mp4")) {
		return LIBMTP_FILETYPE_MP4;
	} else if (!strcmp (mimetype, "audio/x-ms-wma") || !strcmp (mimetype, "audio/x-ms-asf")) {
		return LIBMTP_FILETYPE_WMA;
	} else {
		rb_debug ("\"%s\" is not a supported mimetype", mimetype);
		return LIBMTP_FILETYPE_UNKNOWN;
	}
}

static const char*
filetype_to_mimetype (LIBMTP_filetype_t filetype)
{
	if (filetype == LIBMTP_FILETYPE_WAV) {
		return "audio/x-wav";
	} else if (filetype == LIBMTP_FILETYPE_MP3) {
		return "audio/mpeg";
	} else if (filetype == LIBMTP_FILETYPE_WMA) {
		return "audio/x-ms-wma";
	} else if (filetype == LIBMTP_FILETYPE_OGG) {
		return "application/ogg";
	} else if (filetype == LIBMTP_FILETYPE_MP4) {
		return "audio/mp4";
	} else if (filetype == LIBMTP_FILETYPE_WMV) {
		return "audio/x-ms-wmv";
	} else {
		return NULL;
	}
}

static void
impl_delete (RBSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	GList *sel = NULL;
	GList *tem = NULL;
	RBEntryView *tracks = NULL;
	RhythmDB *db = NULL;
	RhythmDBEntry *entry = NULL;
	const gchar *uri = NULL;
	LIBMTP_track_t *track = NULL;
	int ret = -1;

	db = get_db_for_source (RB_MTP_SOURCE (source));

	tracks = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (tracks);
	for (tem = sel; tem != NULL; tem = tem->next) {

		entry = (RhythmDBEntry *)tem->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		track = g_hash_table_lookup (priv->entry_map, entry);
		if (track == NULL) {
			rb_debug ("Couldn't find track on mtp-device! (%s)\n", uri);
			continue;
		}

		ret = LIBMTP_Delete_Object (priv->device, track->item_id);
		if (ret == 0) {
			g_hash_table_remove (priv->entry_map, entry);
			LIBMTP_destroy_track_t (track);
			rhythmdb_entry_delete (db, entry);
		} else {
			rb_debug ("Delete track failed");
		}
	}
	rhythmdb_commit (db);

	g_list_free (sel);
	g_list_free (tem);
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/MTPSourcePopup");
	return TRUE;
}

static GList *
impl_get_ui_actions (RBSource *source)
{
	GList *actions = NULL;

	actions = g_list_prepend (actions, g_strdup ("RemovableSourceEject"));

	return actions;
}

gboolean
rb_mtp_source_is_udi (RBMtpSource *source,
		      const char *udi)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);

	return (strcmp (udi, priv->udi) == 0);
}

/*Borowed from rb-playlist-source-recorder*/
static gboolean
check_dir_has_space (const char *path,
		     guint64 bytes_needed)
{
	GnomeVFSResult result;
	GnomeVFSURI *dir_uri = NULL;
	GnomeVFSFileSize free_bytes;

	if (!g_file_test (path, G_FILE_TEST_IS_DIR))
		return FALSE;

	dir_uri = gnome_vfs_uri_new (path);
	if (dir_uri == NULL) {
		rb_debug ("Cannot get free space at %s\n", path);
		return FALSE;
	}

	result = gnome_vfs_get_volume_free_space (dir_uri, &free_bytes);
	gnome_vfs_uri_unref (dir_uri);

	if (result != GNOME_VFS_OK) {
		rb_debug ("Cannot get free space at %s\n", path);
		return FALSE;
	}

	if (bytes_needed >= free_bytes) {
		return FALSE;
	}

	return TRUE;
}

static gboolean
rb_mtp_source_transfer_track_to_disk (LIBMTP_mtpdevice_t *device,
				      LIBMTP_track_t *track,
				      const char *uri)
{
	int ret = -1;
	GnomeVFSURI* guri = NULL;

	if (device == NULL || track == NULL || strlen (uri) == 0) {
		return FALSE;
	}

	guri = gnome_vfs_uri_new (uri);
	if (!check_dir_has_space (gnome_vfs_uri_get_path (gnome_vfs_uri_get_parent (guri)), track->filesize)) {
		gnome_vfs_uri_unref (guri);
		return FALSE;
	}
	gnome_vfs_uri_unref (guri);

	ret = LIBMTP_Get_Track_To_File (device, track->item_id, uri, NULL, NULL);

	if (ret == 0) {
		return TRUE;
	} else {
		return FALSE;
	}
}

static char *
rb_mtp_source_get_playback_uri (RhythmDBEntry *entry, gpointer data)
{
	RBMtpSourcePrivate *priv;
	LIBMTP_track_t *track;
	char *uri = NULL;
	char *s = NULL;

	priv = MTP_SOURCE_GET_PRIVATE (data);

	track = g_hash_table_lookup (priv->entry_map, entry);
	uri = g_strdup_printf ("%s/%s-%s", g_get_tmp_dir (),
			       rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST),
			       rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE));

	if (rb_mtp_source_transfer_track_to_disk (priv->device, track, uri) == TRUE) {
		s = g_strdup_printf ("file://%s", uri);
		g_free (uri);
		return s;
	} else {
		g_free (uri);
		return NULL;
	}
}

static GList *
impl_copy (RBSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (RB_MTP_SOURCE (source));
	RhythmDB *db;
	GList *selected_entries;
	GList *iter;
	GList *copy_entries;
	int ret = -1;
	const char *uri = NULL;
	LIBMTP_track_t *track = NULL;

	db = get_db_for_source (RB_MTP_SOURCE (source));

	copy_entries = NULL;
	selected_entries = rb_entry_view_get_selected_entries (rb_source_get_entry_view (source));
	for (iter = selected_entries; iter != NULL; iter = g_list_next (iter)) {
		RhythmDBEntry *entry;

		entry = (RhythmDBEntry *)iter->data;
		track = g_hash_table_lookup (priv->entry_map, entry);

		if (track == NULL)
			continue;

		uri = g_strdup_printf ("%s/%s", g_get_tmp_dir (), track->filename);
		ret = rb_mtp_source_transfer_track_to_disk (priv->device, track, uri);

		if (ret == 0) {
			entry_set_string_prop (RHYTHMDB (db), entry, RHYTHMDB_PROP_LOCATION, g_strdup_printf ("file://%s", uri));
			copy_entries = g_list_prepend (copy_entries, entry);
		}
	}

	g_list_free (selected_entries);
	g_object_unref (G_OBJECT (db));

	return copy_entries;
}

static RhythmDB *
get_db_for_source (RBMtpSource *source)
{
	RBShell *shell = NULL;
	RhythmDB *db = NULL;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	return db;
}

static LIBMTP_track_t *
transfer_track (RBMtpSource *source,
		LIBMTP_mtpdevice_t *device,
		RhythmDBEntry *entry,
		const char *filename,
		const char *mimetype)
{
	LIBMTP_track_t *trackmeta = LIBMTP_new_track_t ();
	GDate d;

	trackmeta->title = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE);
	trackmeta->album = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ALBUM);
	trackmeta->artist = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST);
	trackmeta->genre = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_GENRE);
	trackmeta->filename = g_path_get_basename (filename);

	if (rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE) > 0) { /* Entries without a date returns 0, g_date_set_julian don't accept that */
		g_date_set_julian (&d, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE));
		trackmeta->date	= gdate_to_char (&d);
	}
	trackmeta->tracknumber = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	trackmeta->duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION) * 1000;
	trackmeta->filesize = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
	if (mimetype == NULL) {
		trackmeta->filetype = mimetype_to_filetype (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE));
	} else {
		trackmeta->filetype = mimetype_to_filetype (mimetype);
	}

	if (LIBMTP_Send_Track_From_File (device, filename, trackmeta, NULL, NULL, 0) != 0) {
		LIBMTP_destroy_track_t (trackmeta);
		rb_debug ("Tracktransfer failed\n");
		return NULL;
	}

	return trackmeta;
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	guint16 *types = NULL;
	guint16 num_devices = 0;
	int i = 0;
	GList *list = NULL;

	if (LIBMTP_Get_Supported_Filetypes (priv->device, &types, &num_devices) == 0) {
		for (i = 0; i < num_devices; i++) {
			const char *mime = filetype_to_mimetype (types[i]);

			if (mime != NULL) {
				list = g_list_prepend (list, g_strdup (mime));
			}
		}
	} else {
		rb_debug ("Get supported filetypes failed");
	}

	return list;
}

static gboolean
impl_track_added (RBRemovableMediaSource *isource,
		  RhythmDBEntry *entry,
		  const char *dest,
		  const char *mimetype)
{
	RBMtpSource *source = RB_MTP_SOURCE (isource);
	RBMtpSourcePrivate *priv = MTP_SOURCE_GET_PRIVATE (source);
	char *filename = NULL;
	LIBMTP_track_t *track = NULL;

	filename = g_filename_from_uri (dest, NULL, NULL);
	track = transfer_track (source, priv->device, entry, filename, mimetype);
	gnome_vfs_unlink (filename);
	g_free (filename);

	if (track != NULL) {
		/*request_artwork (isource, entry, song);*/
		add_mtp_track_to_db (source, track);
	}

	return FALSE;
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	char* file = g_strdup_printf ("%s/%s-%s.%s", g_get_tmp_dir (),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_ARTIST),
				      rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_TITLE),
				      extension);
	char* uri = g_filename_to_uri (file, NULL, NULL);
	g_free (file);
	return uri;
}

