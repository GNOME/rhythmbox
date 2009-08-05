/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of generic audio player source object
 *
 *  Copyright (C) 2004 James Livingston  <doclivingston@gmail.com>
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

#include <gtk/gtk.h>
#include <glib/gi18n.h>

#include <totem-pl-parser.h>

#include "mediaplayerid.h"

#include "eel-gconf-extensions.h"
#include "rb-generic-player-source.h"
#include "rb-generic-player-playlist-source.h"
#include "rb-removable-media-manager.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"
#include "rb-dialog.h"
#include "rb-plugin.h"
#include "rhythmdb-import-job.h"
#include "rb-import-errors-source.h"

static GObject *impl_constructor (GType type,
				  guint n_construct_properties,
				  GObjectConstructParam *construct_properties);
static void impl_dispose (GObject *object);
static void impl_finalize (GObject *object);
static void impl_set_property (GObject *object,
			       guint prop_id,
			       const GValue *value,
			       GParamSpec *pspec);
static void impl_get_property (GObject *object,
			       guint prop_id,
			       GValue *value,
			       GParamSpec *pspec);

static void load_songs (RBGenericPlayerSource *source);

static gboolean impl_show_popup (RBSource *source);
static void impl_delete_thyself (RBSource *source);
static gboolean impl_can_move_to_trash (RBSource *source);
static gboolean impl_can_paste (RBSource *source);
static gboolean impl_can_delete (RBSource *source);
static void impl_delete (RBSource *source);
static void impl_move_to_trash (RBSource *source);
static void impl_get_status (RBSource *source, char **text, char **progress_text, float *progress);

static GList* impl_get_mime_types (RBRemovableMediaSource *source);
static char* impl_build_dest_uri (RBRemovableMediaSource *source,
				  RhythmDBEntry *entry,
				  const char *mimetype,
				  const char *extension);

static char *default_get_mount_path (RBGenericPlayerSource *source);
static void default_load_playlists (RBGenericPlayerSource *source);
static char * default_uri_from_playlist_uri (RBGenericPlayerSource *source,
					     const char *uri);
static char * default_uri_to_playlist_uri (RBGenericPlayerSource *source,
					   const char *uri);

enum
{
	PROP_0,
	PROP_IGNORE_ENTRY_TYPE,
	PROP_ERROR_ENTRY_TYPE,
	PROP_DEVICE_INFO
};

typedef struct
{
	RhythmDB *db;

	RhythmDBImportJob *import_job;
	gint load_playlists_id;
	GList *playlists;
	RBSource *import_errors;

	char *mount_path;

	/* entry types */
	RhythmDBEntryType ignore_type;
	RhythmDBEntryType error_type;

	/* information derived from volume */
	gboolean read_only;

	MPIDDevice *device_info;

} RBGenericPlayerSourcePrivate;

RB_PLUGIN_DEFINE_TYPE(RBGenericPlayerSource, rb_generic_player_source, RB_TYPE_REMOVABLE_MEDIA_SOURCE)
#define GENERIC_PLAYER_SOURCE_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_GENERIC_PLAYER_SOURCE, RBGenericPlayerSourcePrivate))

static void
rb_generic_player_source_class_init (RBGenericPlayerSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBRemovableMediaSourceClass *rms_class = RB_REMOVABLE_MEDIA_SOURCE_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->constructor = impl_constructor;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	source_class->impl_show_popup = impl_show_popup;
	source_class->impl_delete_thyself = impl_delete_thyself;
	source_class->impl_can_delete = impl_can_delete;
	source_class->impl_delete = impl_delete;
	source_class->impl_can_move_to_trash = impl_can_move_to_trash;
	source_class->impl_move_to_trash = impl_move_to_trash;
	source_class->impl_can_paste = impl_can_paste;
	source_class->impl_get_status = impl_get_status;

	rms_class->impl_build_dest_uri = impl_build_dest_uri;
	rms_class->impl_get_mime_types = impl_get_mime_types;
	rms_class->impl_should_paste = rb_removable_media_source_should_paste_no_duplicate;

	klass->impl_get_mount_path = default_get_mount_path;
	klass->impl_load_playlists = default_load_playlists;
	klass->impl_uri_from_playlist_uri = default_uri_from_playlist_uri;
	klass->impl_uri_to_playlist_uri = default_uri_to_playlist_uri;

	g_object_class_install_property (object_class,
					 PROP_ERROR_ENTRY_TYPE,
					 g_param_spec_boxed ("error-entry-type",
							     "Error entry type",
							     "Entry type to use for import error entries added by this source",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IGNORE_ENTRY_TYPE,
					 g_param_spec_boxed ("ignore-entry-type",
							     "Ignore entry type",
							     "Entry type to use for ignore entries added by this source",
							     RHYTHMDB_TYPE_ENTRY_TYPE,
							     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_DEVICE_INFO,
					 g_param_spec_object ("device-info",
							      "device info",
							      "device information object",
							      MPID_TYPE_DEVICE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));



	g_type_class_add_private (klass, sizeof (RBGenericPlayerSourcePrivate));
}

static void
rb_generic_player_source_init (RBGenericPlayerSource *source)
{

}

static GObject *
impl_constructor (GType type,
		  guint n_construct_properties,
		  GObjectConstructParam *construct_properties)
{
	RBGenericPlayerSource *source;
	RBGenericPlayerSourcePrivate *priv;
	GMount *mount;
	char **playlist_formats;
	char *mount_name;
	RBShell *shell;
	GFile *root;
	GFileInfo *info;
	GError *error = NULL;

	source = RB_GENERIC_PLAYER_SOURCE (G_OBJECT_CLASS (rb_generic_player_source_parent_class)->
					   constructor (type, n_construct_properties, construct_properties));

	priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	g_object_get (source, "shell", &shell, NULL);

	g_object_get (shell, "db", &priv->db, NULL);
	
	priv->import_errors = rb_import_errors_source_new (shell, priv->error_type);

	g_object_unref (shell);

	g_object_get (source, "mount", &mount, NULL);

	root = g_mount_get_root (mount);
	mount_name = g_mount_get_name (mount);

	info = g_file_query_filesystem_info (root, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY, NULL, &error);
	if (error != NULL) {
		rb_debug ("error querying filesystem info for %s: %s", mount_name, error->message);
		g_error_free (error);
		priv->read_only = FALSE;
	} else {
		priv->read_only = g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_FILESYSTEM_READONLY);
		g_object_unref (info);
	}

	g_free (mount_name);
	g_object_unref (root);
	g_object_unref (mount);

	g_object_get (priv->device_info, "playlist-formats", &playlist_formats, NULL);
	if (playlist_formats != NULL && g_strv_length (playlist_formats) > 0) {
		RhythmDBEntryType entry_type;

		g_object_get (source, "entry-type", &entry_type, NULL);
		entry_type->has_playlists = TRUE;
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	}
	g_strfreev (playlist_formats);

	load_songs (source);

	return G_OBJECT (source);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IGNORE_ENTRY_TYPE:
		priv->ignore_type = g_value_get_boxed (value);
		break;
	case PROP_ERROR_ENTRY_TYPE:
		priv->error_type = g_value_get_boxed (value);
		break;
	case PROP_DEVICE_INFO:
		priv->device_info = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IGNORE_ENTRY_TYPE:
		g_value_set_boxed (value, priv->ignore_type);
		break;
	case PROP_ERROR_ENTRY_TYPE:
		g_value_set_boxed (value, priv->error_type);
		break;
	case PROP_DEVICE_INFO:
		g_value_set_object (value, priv->device_info);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);

	if (priv->load_playlists_id != 0) {
		g_source_remove (priv->load_playlists_id);
		priv->load_playlists_id = 0;
	}

	if (priv->db != NULL) {
		if (priv->ignore_type != RHYTHMDB_ENTRY_TYPE_INVALID) {
			rhythmdb_entry_delete_by_type (priv->db, priv->ignore_type);
			g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, priv->ignore_type);
			priv->ignore_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		}
		if (priv->error_type != RHYTHMDB_ENTRY_TYPE_INVALID) {
			rhythmdb_entry_delete_by_type (priv->db, priv->error_type);
			g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, priv->error_type);
			priv->error_type = RHYTHMDB_ENTRY_TYPE_INVALID;
		}

		g_object_unref (priv->db);
		priv->db = NULL;
	}

	if (priv->import_job != NULL) {
		rhythmdb_import_job_cancel (priv->import_job);
		g_object_unref (priv->import_job);
		priv->import_job = NULL;
	}

	if (priv->device_info != NULL) {
		g_object_unref (priv->device_info);
		priv->device_info = NULL;
	}

	G_OBJECT_CLASS (rb_generic_player_source_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBGenericPlayerSourcePrivate *priv;

	g_return_if_fail (RB_IS_GENERIC_PLAYER_SOURCE (object));
	priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (object);
}

RBRemovableMediaSource *
rb_generic_player_source_new (RBShell *shell, GMount *mount, MPIDDevice *device_info)
{
	RBGenericPlayerSource *source;
	RhythmDBEntryType entry_type;
	RhythmDBEntryType error_type;
	RhythmDBEntryType ignore_type;
	RhythmDB *db;
	GVolume *volume;
	char *name;
	char *path;

	volume = g_mount_get_volume (mount);

	g_object_get (shell, "db", &db, NULL);
	path = g_volume_get_identifier (volume, G_VOLUME_IDENTIFIER_KIND_UNIX_DEVICE);

	name = g_strdup_printf ("generic audio player: %s", path);
	entry_type = rhythmdb_entry_register_type (db, name);
	g_free (name);

	name = g_strdup_printf ("generic audio player (ignore): %s", path);
	ignore_type = rhythmdb_entry_register_type (db, name);
	g_free (name);

	name = g_strdup_printf ("generic audio player (errors): %s", path);
	error_type = rhythmdb_entry_register_type (db, name);
	g_free (name);

	g_object_unref (db);
	g_object_unref (volume);
	g_free (path);

	source = RB_GENERIC_PLAYER_SOURCE (g_object_new (RB_TYPE_GENERIC_PLAYER_SOURCE,
							 "entry-type", entry_type,
							 "ignore-entry-type", ignore_type,
							 "error-entry-type", error_type,
							 "mount", mount,
							 "shell", shell,
							 "source-group", RB_SOURCE_GROUP_DEVICES,
							 "device-info", device_info,
							 NULL));

	rb_shell_register_entry_type_for_source (shell, RB_SOURCE (source), entry_type);

	return RB_REMOVABLE_MEDIA_SOURCE (source);
}

static void
impl_delete_thyself (RBSource *source)
{
	GList *pl;
	GList *p;
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	/* take a copy of the list first, as playlist_deleted_cb modifies priv->playlists */
	pl = g_list_copy (priv->playlists);
	for (p = pl; p != NULL; p = p->next) {
		RBSource *playlist = RB_SOURCE (p->data);
		rb_source_delete_thyself (playlist);
	}
	g_list_free (priv->playlists);
	g_list_free (pl);
	priv->playlists = NULL;

	if (priv->import_errors != NULL) {
		rb_source_delete_thyself (priv->import_errors);
		priv->import_errors = NULL;
	}

	RB_SOURCE_CLASS (rb_generic_player_source_parent_class)->impl_delete_thyself (source);
}

static void
import_complete_cb (RhythmDBImportJob *job, int total, RBGenericPlayerSource *source)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	RBShell *shell;

	GDK_THREADS_ENTER ();
	
	g_object_get (source, "shell", &shell, NULL);
	rb_shell_append_source (shell, priv->import_errors, RB_SOURCE (source));
	g_object_unref (shell);

	if (klass->impl_load_playlists)
		klass->impl_load_playlists (source);

	g_object_unref (priv->import_job);
	priv->import_job = NULL;
	
	rb_source_notify_status_changed (RB_SOURCE (source));

	GDK_THREADS_LEAVE ();
}

static void
import_status_changed_cb (RhythmDBImportJob *job, int total, int imported, RBGenericPlayerSource *source)
{
	rb_source_notify_status_changed (RB_SOURCE (source));
}

static void
load_songs (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	RhythmDBEntryType entry_type;
	char **audio_folders;
	char *mount_path;

	mount_path = rb_generic_player_source_get_mount_path (source);
	g_object_get (source, "entry-type", &entry_type, NULL);

	/* if we have a set of folders on the device containing audio files,
	 * load only those folders, otherwise add the whole volume.
	 */
	priv->import_job = rhythmdb_import_job_new (priv->db, entry_type, priv->ignore_type, priv->error_type);

	g_signal_connect_object (priv->import_job, "complete", G_CALLBACK (import_complete_cb), source, 0);
	g_signal_connect_object (priv->import_job, "status-changed", G_CALLBACK (import_status_changed_cb), source, 0);

	g_object_get (priv->device_info, "audio-folders", &audio_folders, NULL);
	if (audio_folders != NULL && g_strv_length (audio_folders) > 0) {
		int af;
		for (af=0; audio_folders[af] != NULL; af++) {
			char *path;
			path = rb_uri_append_path (mount_path, audio_folders[af]);
			rb_debug ("loading songs from device audio folder %s", path);
			rhythmdb_import_job_add_uri (priv->import_job, path);
			g_free (path);
		}
	} else {
		rb_debug ("loading songs from device mount path %s", mount_path);
		rhythmdb_import_job_add_uri (priv->import_job, mount_path);
	}
	g_strfreev (audio_folders);

	rhythmdb_import_job_start (priv->import_job);

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	g_free (mount_path);
}

char *
rb_generic_player_source_get_mount_path (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_get_mount_path (source);
}

static char *
default_get_mount_path (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	if (priv->mount_path == NULL) {
		GMount *mount;
		GFile *root;

		g_object_get (source, "mount", &mount, NULL);

		root = g_mount_get_root (mount);
		if (root != NULL) {
			priv->mount_path = g_file_get_uri (root);
			g_object_unref (root);
		}

		g_object_unref (mount);
	}

	return g_strdup (priv->mount_path);
}

gboolean
rb_generic_player_is_mount_player (GMount *mount, MPIDDevice *device_info)
{
	char **protocols;
	gboolean result = FALSE;
	int i;

	/* claim anything with 'storage' as an access protocol */
	g_object_get (device_info, "access-protocols", &protocols, NULL);
	if (protocols != NULL) {
		for (i = 0; protocols[i] != NULL; i++) {
			if (g_str_equal (protocols[i], "storage")) {
				result = TRUE;
				break;
			}
		}
		g_strfreev (protocols);
	}

	return result;
}

static gboolean
impl_show_popup (RBSource *source)
{
	_rb_source_show_popup (RB_SOURCE (source), "/GenericPlayerSourcePopup");
	return TRUE;
}

gboolean
rb_generic_player_source_can_trash_entries (RBGenericPlayerSource *source,
					    GList *entries)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *tem;
	gboolean ret;

	if (priv->read_only != FALSE)
		return FALSE;

	ret = FALSE;

	for (tem = entries; tem != NULL; tem = tem->next) {
		RhythmDBEntry *entry;
		const char *uri;
		GFile *file;
		GFileInfo *info;

		entry = tem->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		file = g_file_new_for_uri (uri);
		info = g_file_query_info (file,
					  G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH,
					  G_FILE_QUERY_INFO_NONE,
					  NULL,
					  NULL);
		g_object_unref (file);
		if (info == NULL) {
			ret = FALSE;
			break;
		}
		ret = g_file_info_get_attribute_boolean (info, G_FILE_ATTRIBUTE_ACCESS_CAN_TRASH);
		g_object_unref (info);
		if (ret == FALSE)
			break;
	}

	return ret;
}

static gboolean
impl_can_move_to_trash (RBSource *source)
{
	RBEntryView *view;
	GList *sel;
	gboolean ret;


	view = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (view);

	ret = rb_generic_player_source_can_trash_entries (RB_GENERIC_PLAYER_SOURCE (source),
							  sel);

	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);

	return ret;
}

static void
impl_get_status (RBSource *source, char **text, char **progress_text, float *progress)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	/* get default status text first */
	RB_SOURCE_CLASS (rb_generic_player_source_parent_class)->impl_get_status (source, text, progress_text, progress);

	/* override with bits of import status */
	if (priv->import_job != NULL) {
		int total;
		int imported;

		total = rhythmdb_import_job_get_total (priv->import_job);
		imported = rhythmdb_import_job_get_imported (priv->import_job);

		g_free (*progress_text);
		*progress_text = g_strdup_printf (_("Importing (%d/%d)"), imported, total);
		*progress = ((float)imported / (float)total);
	}
}

/* code for playlist loading */

static void
playlist_deleted_cb (RBSource *playlist, RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *p;

	p = g_list_find (priv->playlists, playlist);
	if (p != NULL) {
		priv->playlists = g_list_delete_link (priv->playlists, p);
		g_object_unref (playlist);
	}
}

void
rb_generic_player_source_add_playlist (RBGenericPlayerSource *source,
				       RBShell *shell,
				       RBSource *playlist)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	g_object_ref (playlist);
	priv->playlists = g_list_prepend (priv->playlists, playlist);

	g_signal_connect_object (playlist, "deleted", G_CALLBACK (playlist_deleted_cb), source, 0);

	rb_shell_append_source (shell, playlist, RB_SOURCE (source));
}



static char *
default_uri_from_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	char *mount_uri;
	char *full_uri;

	mount_uri = rb_generic_player_source_get_mount_path (source);
	if (g_str_has_prefix (uri, mount_uri)) {
		return g_strdup (uri);
	}

	full_uri = rb_uri_append_uri (mount_uri, uri);
	g_free (mount_uri);

	rb_debug ("%s => %s", uri, full_uri);
	return full_uri;
}

static char *
default_uri_to_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	char *mount_uri;
	char *playlist_uri;

	mount_uri = rb_generic_player_source_get_mount_path (source);
	if (g_str_has_prefix (uri, mount_uri) == FALSE) {
		rb_debug ("uri %s is not under device mount uri %s", uri, mount_uri);
		return NULL;
	}

	playlist_uri = g_strdup_printf ("file://%s", uri + strlen (mount_uri));
	return playlist_uri;
}

char *
rb_generic_player_source_uri_from_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_uri_from_playlist_uri (source, uri);
}

char *
rb_generic_player_source_uri_to_playlist_uri (RBGenericPlayerSource *source, const char *uri)
{
	RBGenericPlayerSourceClass *klass = RB_GENERIC_PLAYER_SOURCE_GET_CLASS (source);

	return klass->impl_uri_to_playlist_uri (source, uri);
}

static void
load_playlist_file (RBGenericPlayerSource *source,
		    const char *playlist_path,
		    const char *rel_path)
{
	RhythmDBEntryType entry_type;
	RBGenericPlayerPlaylistSource *playlist;
	RBShell *shell;
	char *mount_path;

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);

	mount_path = rb_generic_player_source_get_mount_path (source);
	rb_debug ("loading playlist %s", playlist_path);
	playlist = RB_GENERIC_PLAYER_PLAYLIST_SOURCE (
			rb_generic_player_playlist_source_new (shell,
							       source,
							       playlist_path,
							       mount_path,
							       entry_type));

	if (playlist != NULL) {
		rb_generic_player_source_add_playlist (source, shell, RB_SOURCE (playlist));
	}

	g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);
	g_object_unref (shell);
	g_free (mount_path);
}

static gboolean
visit_playlist_dirs (GFile *file,
		     gboolean dir,
		     RBGenericPlayerSource *source)
{
	char *basename;
	char *uri;
	RhythmDBEntry *entry;
	RhythmDBEntryType entry_type;
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	if (dir) {
		return TRUE;
	}

	/* check if we've already got an entry 
	 * for this file, just to save some i/o.
	 */
	uri = g_file_get_uri (file);
	entry = rhythmdb_entry_lookup_by_location (priv->db, uri);
	g_free (uri);
	if (entry != NULL) {
		gboolean is_song;

		is_song = FALSE;

		g_object_get (source, "entry-type", &entry_type, NULL);
		is_song = (rhythmdb_entry_get_entry_type (entry) == entry_type);
		g_boxed_free (RHYTHMDB_TYPE_ENTRY_TYPE, entry_type);

		if (is_song) {
			rb_debug ("%s was loaded as a song",
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			return TRUE;
		}
	}

	basename = g_file_get_basename (file);
	if (strcmp (basename, ".is_audio_player") != 0) {
		char *playlist_path;
		playlist_path = g_file_get_path (file);
		load_playlist_file (source, playlist_path, basename);
		g_free (playlist_path);
	}

	g_free (basename);

	return TRUE;
}

static void
default_load_playlists (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	char *mount_path;
	char *playlist_path;
	char *full_playlist_path;
	char **playlist_formats;

	mount_path = rb_generic_player_source_get_mount_path (source);

	g_object_get (priv->device_info, "playlist-path", &playlist_path, NULL);
	if (playlist_path) {

		/* If the device only supports a single playlist, just load that */
		if (g_str_has_suffix (playlist_path, ".m3u") ||
		    g_str_has_suffix (playlist_path, ".pls")) {
			full_playlist_path = rb_uri_append_path (mount_path, playlist_path);
			if (rb_uri_exists (full_playlist_path)) {
				load_playlist_file (source, full_playlist_path, playlist_path);
			}

			g_free (full_playlist_path);
			g_free (playlist_path);
			return;
		}

		/* Otherwise, limit the search to the device's playlist folder */
		if (g_str_has_suffix (playlist_path, "/%File")) {
			playlist_path[strlen (playlist_path) - strlen("/%File")] = '\0';
		}
		full_playlist_path = rb_uri_append_path (mount_path, playlist_path);
		rb_debug ("constructed playlist search path %s", full_playlist_path);
	} else {
		full_playlist_path = g_strdup (mount_path);
	}

	/* only try to load playlists if the device has at least one playlist format */
	g_object_get (priv->device_info, "playlist-formats", &playlist_formats, NULL);
	if (playlist_formats != NULL && g_strv_length (playlist_formats) > 0) {
		rb_debug ("searching for playlists in %s", playlist_path);
		rb_uri_handle_recursively (full_playlist_path,
					   NULL,
					   (RBUriRecurseFunc) visit_playlist_dirs,
					   source);
	}
	g_strfreev (playlist_formats);

	g_free (playlist_path);
	g_free (full_playlist_path);
	g_free (mount_path);
}

static gboolean
impl_can_paste (RBSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	return (priv->read_only == FALSE);
}

static gboolean
impl_can_delete (RBSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);

	return (priv->read_only == FALSE);
}

static gboolean
can_delete_directory (RBGenericPlayerSource *source, GFile *dir)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	gboolean result;
	GMount *mount;
	GFile *root;
	char **audio_folders;
	int i;

	g_object_get (source, "mount", &mount, NULL);
	root = g_mount_get_root (mount);
	g_object_unref (mount);

	/* can't delete the root dir */
	if (g_file_equal (dir, root)) {
		rb_debug ("refusing to delete device root dir");
		g_object_unref (root);
		return FALSE;
	}

	/* can't delete the device's audio folders */
	result = TRUE;
	g_object_get (priv->device_info, "audio-folders", &audio_folders, NULL);
	if (audio_folders != NULL && g_strv_length (audio_folders) > 0) {
		for (i = 0; audio_folders[i] != NULL; i++) {
			GFile *check;

			check = g_file_resolve_relative_path (root, audio_folders[i]);
			if (g_file_equal (dir, check)) {
				rb_debug ("refusing to delete device audio folder %s", audio_folders[i]);
				result = FALSE;
			}
			g_object_unref (check);
		}
	}
	g_strfreev (audio_folders);

	/* can delete anything else */
	g_object_unref (root);
	return result;
}

void
rb_generic_player_source_trash_or_delete_entries (RBGenericPlayerSource *source,
						  GList *entries,
						  gboolean _delete)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *tem;

	if (priv->read_only != FALSE)
		return;

	for (tem = entries; tem != NULL; tem = tem->next) {
		RhythmDBEntry *entry;
		const char *uri;
		GFile *file;
		GFile *dir;

		entry = tem->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		file = g_file_new_for_uri (uri);
		if (_delete)
			g_file_delete (file, NULL, NULL);
		else
			g_file_trash (file, NULL, NULL);

		/* now walk up the directory structure and delete empty dirs
		 * until we reach the root or one of the device's audio folders.
		 */
		dir = g_file_get_parent (file);
		while (can_delete_directory (source, dir)) {
			GFile *parent;
			char *path;

			path = g_file_get_path (dir);
			rb_debug ("trying to delete %s", path);
			g_free (path);

			if (g_file_delete (dir, NULL, NULL) == FALSE) {
				break;
			}

			parent = g_file_get_parent (dir);
			if (parent == NULL) {
				break;
			}
			g_object_unref (dir);
			dir = parent;
		}

		g_object_unref (dir);
		g_object_unref (file);

		rhythmdb_entry_delete (priv->db, entry);
	}

	rhythmdb_commit (priv->db);
}

static void
impl_move_to_trash (RBSource *source)
{
	RBEntryView *view;
	GList *sel;

	view = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (view);

	rb_generic_player_source_trash_or_delete_entries (RB_GENERIC_PLAYER_SOURCE (source),
							  sel,
							  FALSE);
	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static void
impl_delete (RBSource *source)
{
	RBEntryView *view;
	GList *sel;

	view = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (view);

	rb_generic_player_source_trash_or_delete_entries (RB_GENERIC_PLAYER_SOURCE (source),
							  sel,
							  TRUE);
	g_list_foreach (sel, (GFunc)rhythmdb_entry_unref, NULL);
	g_list_free (sel);
}

static char *
sanitize_path (const char *str)
{
	char *res = NULL;
	char *s;

	/* Skip leading periods, otherwise files disappear... */
	while (*str == '.')
		str++;

	s = g_strdup (str);
	g_strdelimit (s, "/", '-');
	res = g_uri_escape_string (s, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT, TRUE);
	g_free (s);
	return res;
}

static GList *
impl_get_mime_types (RBRemovableMediaSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	GList *list = NULL;
	char **output_formats;
	char **mime;

	g_object_get (priv->device_info, "output-formats", &output_formats, NULL);
	for (mime = output_formats; mime && *mime != NULL; mime++) {
		list = g_list_prepend (list, g_strdup (*mime));
	}
	g_strfreev (output_formats);
	return g_list_reverse (list);
}

static char *
impl_build_dest_uri (RBRemovableMediaSource *source,
		     RhythmDBEntry *entry,
		     const char *mimetype,
		     const char *extension)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	char *artist, *album, *title;
	gulong track_number, disc_number;
	const char *folders;
	char **audio_folders;
	char *mount_path;
	char *number;
	char *file = NULL;
	char *path;
	char *ext;

	rb_debug ("building dest uri for entry at %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

	if (extension != NULL) {
		ext = g_strconcat (".", extension, NULL);
	} else {
		ext = g_strdup ("");
	}

	artist = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	album = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	title = sanitize_path (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));

	/* we really do need to fix this so untagged entries actually have NULL rather than
	 * a translated string.
	 */
	if (strcmp (artist, _("Unknown")) == 0 && strcmp (album, _("Unknown")) == 0 &&
	    g_str_has_suffix (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION), title)) {
		/* file isn't tagged, so just use the filename as-is, replacing the extension */
		char *p;

		p = g_utf8_strrchr (title, -1, '.');
		if (p != NULL) {
			*p = '\0';
		}
		file = g_strdup_printf ("%s%s", title, ext);
	}

	if (file == NULL) {
		int folder_depth;

		track_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
		disc_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
		if (disc_number > 0)
			number = g_strdup_printf ("%.02u.%.02u", (guint)disc_number, (guint)track_number);
		else
			number = g_strdup_printf ("%.02u", (guint)track_number);

		g_object_get (priv->device_info, "folder-depth", &folder_depth, NULL);
		switch (folder_depth) {
		case 0:
			/* artist - album - number - title */
			file = g_strdup_printf ("%s - %s - %s - %s%s",
						artist, album, number, title, ext);
			break;

		case 1:
			/* artist - album/number - title */
			file = g_strdup_printf ("%s - %s" G_DIR_SEPARATOR_S "%s - %s%s",
						artist, album, number, title, ext);
			break;

		default: /* use this for players that don't care */
		case 2:
			/* artist/album/number - title */
			file = g_strdup_printf ("%s" G_DIR_SEPARATOR_S "%s" G_DIR_SEPARATOR_S "%s - %s%s",
						artist, album, number, title, ext);
			break;
		}
		g_free (number);
	}

	g_free (artist);
	g_free (album);
	g_free (title);
	g_free (ext);

	if (file == NULL)
		return NULL;

	g_object_get (priv->device_info, "audio-folders", &audio_folders, NULL);
	if (audio_folders != NULL && g_strv_length (audio_folders) > 0) {
		folders = g_strdup (audio_folders[0]);
	} else {
		folders = "";
	}
	g_strfreev (audio_folders);

	mount_path = rb_generic_player_source_get_mount_path (RB_GENERIC_PLAYER_SOURCE (source));
	path = g_build_filename (mount_path, folders, file, NULL);
	g_free (file);
	g_free (mount_path);

	/* TODO: check for duplicates, or just overwrite by default? */
	rb_debug ("dest file is %s", path);
	return path;
}

static gboolean
strv_contains (char **strv, const char *s)
{
	int i;
	for (i = 0; strv[i] != NULL; i++) {
		if (g_str_equal (strv[i], s))
			return TRUE;
	}
	return FALSE;
}

void
rb_generic_player_source_set_supported_formats (RBGenericPlayerSource *source, TotemPlParser *parser)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	char **playlist_formats;
	const char *check[] = { "audio/x-mpegurl", "audio/x-scpls", "audio/x-iriver-pla" };

	g_object_get (priv->device_info, "playlist-formats", &playlist_formats, NULL);
	if (playlist_formats != NULL && g_strv_length (playlist_formats) > 0) {
		int i;
		for (i = 0; i < G_N_ELEMENTS (check); i++) {
			if (strv_contains (playlist_formats, check[i])) {
				totem_pl_parser_add_ignored_mimetype (parser, check[i]);
			}
		}
	}
	g_strfreev (playlist_formats);

	totem_pl_parser_add_ignored_mimetype (parser, "x-directory/normal");
}

TotemPlParserType
rb_generic_player_source_get_playlist_format (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	TotemPlParserType result;
	char **playlist_formats;

	g_object_get (priv->device_info, "playlist-formats", &playlist_formats, NULL);

	if (playlist_formats == NULL || g_strv_length (playlist_formats) == 0 || strv_contains (playlist_formats, "audio/x-scpls")) {
		result = TOTEM_PL_PARSER_PLS;
	} else if (strv_contains (playlist_formats, "audio/x-mpegurl")) {
		result = TOTEM_PL_PARSER_M3U;
	} else if (strv_contains (playlist_formats, "audio/x-iriver-pla")) {
		result = TOTEM_PL_PARSER_IRIVER_PLA;
	} else {
		/* now what? */
		result = TOTEM_PL_PARSER_PLS;
	}

	g_strfreev (playlist_formats);
	return result;
}

char *
rb_generic_player_source_get_playlist_path (RBGenericPlayerSource *source)
{
	RBGenericPlayerSourcePrivate *priv = GENERIC_PLAYER_SOURCE_GET_PRIVATE (source);
	char *path;

	g_object_get (priv->device_info, "playlist-path", &path, NULL);
	return path;
}

