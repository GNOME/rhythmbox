/*
 *  Copyright (C) 2015 Jonathan Matthew <jonathan@d14n.org>
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

#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include <gudev/gudev.h>

#include "mediaplayerid.h"

#include "rb-android-source.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-file-helpers.h"
#include "rhythmdb.h"
#include "rb-builder-helpers.h"
#include "rb-application.h"
#include "rb-transfer-target.h"
#include "rb-device-source.h"
#include "rb-sync-settings.h"
#include "rb-import-errors-source.h"
#include "rb-gst-media-types.h"
#include "rb-task-list.h"
#include "rb-encoder.h"
#include "rb-dialog.h"

static void rb_android_device_source_init (RBDeviceSourceInterface *interface);
static void rb_android_transfer_target_init (RBTransferTargetInterface *interface);

static void find_music_dirs (RBAndroidSource *source);
static void rescan_music_dirs (RBAndroidSource *source);
static void update_free_space_next (RBAndroidSource *source);

enum
{
	PROP_0,
	PROP_VOLUME,
	PROP_MOUNT_ROOT,
	PROP_IGNORE_ENTRY_TYPE,
	PROP_ERROR_ENTRY_TYPE,
	PROP_DEVICE_INFO,
	PROP_DEVICE_SERIAL,
	PROP_GUDEV_DEVICE
};

typedef struct
{
	RhythmDB *db;

	gboolean loaded;
	RhythmDBImportJob *import_job;
	RBSource *import_errors;
	GCancellable *cancel;
	GQueue to_scan;
	int scanned;

	RhythmDBEntryType *ignore_type;
	RhythmDBEntryType *error_type;

	MPIDDevice *device_info;
	GUdevDevice *gudev_device;
	GVolume *volume;
	GObject *mount_root;
	gboolean ejecting;

	GList *storage;
	guint64 storage_capacity;
	guint64 storage_free_space;
	GList *query_storage;
	guint64 storage_free_space_next;
	guint64 storage_capacity_next;
	guint rescan_id;

	GtkWidget *grid;
	GtkWidget *info_bar;
} RBAndroidSourcePrivate;

G_DEFINE_DYNAMIC_TYPE_EXTENDED (
	RBAndroidSource,
	rb_android_source,
	RB_TYPE_MEDIA_PLAYER_SOURCE,
	0,
	G_IMPLEMENT_INTERFACE_DYNAMIC (RB_TYPE_DEVICE_SOURCE, rb_android_device_source_init)
	G_IMPLEMENT_INTERFACE_DYNAMIC (RB_TYPE_TRANSFER_TARGET, rb_android_transfer_target_init))

#define GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_ANDROID_SOURCE, RBAndroidSourcePrivate))

static void
free_space_cb (GObject *obj, GAsyncResult *res, gpointer data)
{
	RBAndroidSource *source = RB_ANDROID_SOURCE (data);
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);
	GFileInfo *info;
	GError *error = NULL;

	info = g_file_query_filesystem_info_finish (G_FILE (obj), res, &error);
	if (info == NULL) {
		rb_debug ("error querying filesystem free space: %s", error->message);
		g_clear_error (&error);
	} else {
		priv->storage_free_space_next += g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_FREE);
		priv->storage_capacity_next += g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_FILESYSTEM_SIZE);
		rb_debug ("capacity: %" G_GUINT64_FORMAT ", free space: %" G_GUINT64_FORMAT,
		          priv->storage_capacity_next, priv->storage_free_space_next);
	}

	priv->query_storage = priv->query_storage->next;
	if (priv->query_storage != NULL) {
		update_free_space_next (source);
	} else {
		priv->storage_free_space = priv->storage_free_space_next;
		priv->storage_capacity = priv->storage_capacity_next;
	}
}

static void
update_free_space_next (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);
	GFile *file;
	const char *attrs = G_FILE_ATTRIBUTE_FILESYSTEM_FREE "," G_FILE_ATTRIBUTE_FILESYSTEM_SIZE;

	file = G_FILE (priv->query_storage->data);
	g_file_query_filesystem_info_async (file, attrs, G_PRIORITY_DEFAULT, NULL, free_space_cb, source);
}

static void
update_free_space (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);

	if (priv->query_storage != NULL) {
		rb_debug ("already updating free space");
		return;
	}

	if (priv->storage == NULL) {
		rb_debug ("no storage to query");
		return;
	}

	priv->storage_free_space_next = 0;
	priv->storage_capacity_next = 0;
	priv->query_storage = priv->storage;
	update_free_space_next (source);
}


static void
music_dirs_done (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);

	if (priv->scanned > 1) {
		gtk_widget_hide (priv->info_bar);
		rhythmdb_import_job_start (priv->import_job);

		if (priv->rescan_id != 0) {
			g_source_remove (priv->rescan_id);
		}

		if (priv->storage != NULL) {
			rb_debug ("finished checking for music dirs");
			update_free_space (source);
		} else {
			rb_debug ("no music dirs found (%d)", priv->scanned);
		}
	} else {
		GtkWidget *label;

		rb_debug ("no storage areas found");
		if (gtk_widget_get_visible (priv->info_bar) == FALSE) {
			label = gtk_label_new (_("No storage areas found on this device. You may need to unlock it and change it to File Transfer mode."));
			gtk_container_add (GTK_CONTAINER (gtk_info_bar_get_content_area (GTK_INFO_BAR (priv->info_bar))), label);
			gtk_info_bar_set_message_type (GTK_INFO_BAR (priv->info_bar), GTK_MESSAGE_INFO);
			gtk_widget_show_all (priv->info_bar);

			/* more or less */
			g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);
		}
		if (priv->rescan_id == 0)
			priv->rescan_id = g_timeout_add_seconds (5, (GSourceFunc) rescan_music_dirs, source);
	}
}


static void
enum_files_cb (GObject *obj, GAsyncResult *result, gpointer data)
{
	RBAndroidSource *source;
	RBAndroidSourcePrivate *priv;
	GFileEnumerator *e = G_FILE_ENUMERATOR (obj);
	GError *error = NULL;
	GFileInfo *info;
	GList *files;
	GList *l;

	files = g_file_enumerator_next_files_finish (e, result, &error);
	if (error != NULL) {
		rb_debug ("error listing files: %s", error->message);
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			music_dirs_done (RB_ANDROID_SOURCE (data));
		}
		g_clear_error (&error);
		return;
	}

	source = RB_ANDROID_SOURCE (data);
	priv = GET_PRIVATE(source);

	if (files == NULL) {
		priv->scanned++;
		g_object_unref (e);
		find_music_dirs (source);
		return;
	}

	for (l = files; l != NULL; l = l->next) {
		guint32 filetype;
		info = (GFileInfo *)l->data;

		filetype = g_file_info_get_attribute_uint32 (info, G_FILE_ATTRIBUTE_STANDARD_TYPE);
		if (filetype == G_FILE_TYPE_DIRECTORY) {
			GFile *dir;
			if (priv->scanned == 0) {

				rb_debug ("got storage container %s", g_file_info_get_name (info));
				dir = g_file_get_child (g_file_enumerator_get_container (e), g_file_info_get_name (info));
				g_queue_push_tail (&priv->to_scan, dir);
			} else if (g_ascii_strcasecmp (g_file_info_get_name (info), "music") == 0) {
				GFile *storage;
				char *uri;

				storage = g_file_enumerator_get_container (e);
				dir = g_file_get_child (storage, g_file_info_get_name (info));
				uri = g_file_get_uri (dir);
				rb_debug ("music dir found at %s", uri);

				/* keep the container around for space/capacity calculation */
				priv->storage = g_list_append (priv->storage, dir);

				rhythmdb_import_job_add_uri (priv->import_job, uri);
				g_free (uri);
			}
		}

		g_object_unref (info);
	}

	g_list_free (files);

	g_file_enumerator_next_files_async (G_FILE_ENUMERATOR (obj), 64, G_PRIORITY_DEFAULT, priv->cancel, enum_files_cb, source);
}

static void
enum_child_cb (GObject *obj, GAsyncResult *result, gpointer data)
{
	RBAndroidSource *source;
	RBAndroidSourcePrivate *priv;
	GFileEnumerator *e;
	GError *error = NULL;

	e = g_file_enumerate_children_finish (G_FILE (obj), result, &error);
	if (e == NULL) {
		rb_debug ("enum error: %s", error->message);
		if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED)) {
			music_dirs_done (RB_ANDROID_SOURCE (data));
		}
		g_clear_error (&error);
		return;
	}

	source = RB_ANDROID_SOURCE (data);
	priv = GET_PRIVATE(source);
	g_file_enumerator_next_files_async (e, 64, G_PRIORITY_DEFAULT, priv->cancel, enum_files_cb, source);
}

static void
find_music_dirs (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);
	const char *attrs =
		G_FILE_ATTRIBUTE_STANDARD_NAME ","
		G_FILE_ATTRIBUTE_STANDARD_TYPE;

	gpointer dir;

	dir = g_queue_pop_head (&priv->to_scan);
	if (dir == NULL) {
		music_dirs_done (source);
		return;
	}

	rb_debug ("scanning %s", g_file_get_uri (G_FILE (dir)));
	g_file_enumerate_children_async (G_FILE (dir),
					 attrs,
					 G_FILE_QUERY_INFO_NONE,
					 G_PRIORITY_DEFAULT,
					 priv->cancel,
					 enum_child_cb,
					 source);
	g_object_unref (dir);
}

static void
rescan_music_dirs (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	GFile *root;

	g_object_get (source, "mount-root", &root, NULL);

	priv->scanned = 0;
	g_queue_push_tail (&priv->to_scan, root);

	find_music_dirs (source);
}

static void
import_complete_cb (RhythmDBImportJob *job, int total, RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	GSettings *settings;
	RBShell *shell;

	if (priv->ejecting) {
		rb_device_source_default_eject (RB_DEVICE_SOURCE (source));
	} else {
		g_object_get (source, "shell", &shell, NULL);
		rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (priv->import_errors), RB_DISPLAY_PAGE (source));
		g_object_unref (shell);

		g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADED, NULL);

		g_object_get (source, "encoding-settings", &settings, NULL);
		rb_transfer_target_transfer (RB_TRANSFER_TARGET (source), settings, NULL, FALSE);
		g_object_unref (settings);

		rb_media_player_source_purge_metadata_cache (RB_MEDIA_PLAYER_SOURCE (source));
	}

	g_clear_object (&priv->import_job);
}

static void
actually_load (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	RBTaskList *tasklist;
	RhythmDBEntryType *entry_type;
	RBShell *shell;
	GFile *root;
	char *name;
	char *label;

	if (priv->loaded) {
		rb_debug ("already loading");
		return;
	}
	priv->loaded = TRUE;
	rb_media_player_source_load (RB_MEDIA_PLAYER_SOURCE (source));

	/* identify storage containers and find music dirs within them */
	g_object_get (source, "mount-root", &root, "entry-type", &entry_type, NULL);

	priv->cancel = g_cancellable_new ();
	priv->import_job = rhythmdb_import_job_new (priv->db, entry_type, priv->ignore_type, priv->error_type);
	g_signal_connect_object (priv->import_job, "complete", G_CALLBACK (import_complete_cb), source, 0);

	priv->scanned = 0;
	g_queue_init (&priv->to_scan);
	g_queue_push_tail (&priv->to_scan, root);
	g_object_unref (entry_type);

	find_music_dirs (source);

	g_object_get (source, "name", &name, "shell", &shell, NULL);
	label = g_strdup_printf (_("Scanning %s"), name);
	g_object_set (priv->import_job, "task-label", label, NULL);

	g_object_get (shell, "task-list", &tasklist, NULL);
	rb_task_list_add_task (tasklist, RB_TASK_PROGRESS (priv->import_job));
	g_object_unref (tasklist);
	g_object_unref (shell);

	g_free (label);
	g_free (name);
}

static void
volume_mount_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
	RBAndroidSource *source = RB_ANDROID_SOURCE (user_data);
	GVolume *volume = G_VOLUME (source_object);
	GError *error = NULL;

	rb_debug ("volume mount finished");
	if (g_volume_mount_finish (volume, res, &error)) {
		actually_load (source);
	} else {
		rb_error_dialog (NULL, _("Error mounting Android device"), "%s", error->message);
		g_clear_error (&error);
	}
}

static gboolean
ensure_loaded (RBAndroidSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	RBSourceLoadStatus status;
	GMount *mount;

	if (priv->loaded) {
		g_object_get (source, "load-status", &status, NULL);
		return (status == RB_SOURCE_LOAD_STATUS_LOADED);
	}

	g_object_set (source, "load-status", RB_SOURCE_LOAD_STATUS_LOADING, NULL);

	mount = g_volume_get_mount (priv->volume);
	if (mount != NULL) {
		rb_debug ("volume is mounted");
		g_object_unref (mount);
		actually_load (source);
		return FALSE;
	}

	rb_debug ("mounting volume");
	g_volume_mount (priv->volume, G_MOUNT_MOUNT_NONE, NULL, NULL, volume_mount_cb, source);
	return FALSE;
}

static void
delete_data_destroy (gpointer data)
{
	g_list_free_full (data, (GDestroyNotify) rhythmdb_entry_unref);
}


static gboolean
can_delete_directory (RBAndroidSource *source, GFile *dir)
{
	GFile *root;
	char *path;
	int i;
	int c;

	g_object_get (source, "mount-root", &root, NULL);

	/*
	 * path here will be sdcard/Music/something for anything we want to delete
	 */
	path = g_file_get_relative_path (root, dir);
	c = 0;
	for (i = 0; path[i] != '\0'; i++) {
		if (path[i] == '/')
			c++;
	}

	g_free (path);
	g_object_unref (root);
	return (c > 1);
}

static void
delete_entries_task (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	RBAndroidSource *source = RB_ANDROID_SOURCE (source_object);
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	GList *l;

	for (l = task_data; l != NULL; l = l->next) {
		RhythmDBEntry *entry;
		const char *uri;
		GFile *file;
		GFile *dir;

		entry = l->data;
		uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		file = g_file_new_for_uri (uri);
		g_file_delete (file, NULL, NULL);

		/* now walk up the directory structure and delete empty dirs
		 * until we reach the root or one of the device's audio folders.
		 */
		dir = g_file_get_parent (file);
		while (can_delete_directory (source, dir)) {
			GFile *parent;

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

	g_task_return_boolean (task, TRUE);
	g_object_unref (task);
}

static void
impl_delete_entries (RBMediaPlayerSource *source, GList *entries, GAsyncReadyCallback callback, gpointer data)
{
	GTask *task;
	GList *task_entries;

	task = g_task_new (source, NULL, callback, data);
	task_entries = g_list_copy_deep (entries, (GCopyFunc) rhythmdb_entry_ref, NULL);
	g_task_set_task_data (task, task_entries, delete_data_destroy);
	g_task_run_in_thread (task, delete_entries_task);
}

static void
impl_show_properties (RBMediaPlayerSource *source, GtkWidget *info_box, GtkWidget *notebook)
{
	RhythmDBQueryModel *model;
	GtkBuilder *builder;
	GtkWidget *widget;
	GObject *plugin;
	char *text;

	g_object_get (source, "plugin", &plugin, NULL);
	builder = rb_builder_load_plugin_file (G_OBJECT (plugin), "android-info.ui", NULL);
	g_object_unref (plugin);

	/* 'basic' tab stuff */

	widget = GTK_WIDGET (gtk_builder_get_object (builder, "android-basic-info"));
	gtk_box_pack_start (GTK_BOX (info_box), widget, TRUE, TRUE, 0);

	g_object_get (source, "base-query-model", &model, NULL);
	widget = GTK_WIDGET (gtk_builder_get_object (builder, "num-tracks"));
	text = g_strdup_printf ("%d", gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL));
	gtk_label_set_text (GTK_LABEL (widget), text);
	g_free (text);
	g_object_unref (model);

	g_object_unref (builder);
}

static void
impl_get_entries (RBMediaPlayerSource *source,
		  const char *category,
		  GHashTable *map)
{
	RhythmDBQueryModel *model;
	GtkTreeIter iter;
	gboolean podcast;

	/* we don't have anything else to distinguish podcasts from regular
	 * tracks, so just use the genre.
	 */
	podcast = (g_str_equal (category, SYNC_CATEGORY_PODCAST));

	g_object_get (source, "base-query-model", &model, NULL);
	if (gtk_tree_model_get_iter_first (GTK_TREE_MODEL (model), &iter) == FALSE) {
		g_object_unref (model);
		return;
	}

	do {
		RhythmDBEntry *entry;
		const char *genre;
		entry = rhythmdb_query_model_iter_to_entry (model, &iter);
		genre = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE);
		if (g_str_equal (genre, "Podcast") == podcast) {
			_rb_media_player_source_add_to_map (map, entry);
		}
	} while (gtk_tree_model_iter_next (GTK_TREE_MODEL (model), &iter));

	g_object_unref (model);
}

static guint64
impl_get_capacity (RBMediaPlayerSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);
	return priv->storage_capacity;
}

static guint64
impl_get_free_space (RBMediaPlayerSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE(source);
	return priv->storage_free_space;
}

static gboolean
impl_can_paste (RBSource *source)
{
	return TRUE;
}

static RBTrackTransferBatch *
impl_paste (RBSource *source, GList *entries)
{
	gboolean defer;
	GSettings *settings;
	RBTrackTransferBatch *batch;

	defer = (ensure_loaded (RB_ANDROID_SOURCE (source)) == FALSE);
	g_object_get (source, "encoding-settings", &settings, NULL);
	batch = rb_transfer_target_transfer (RB_TRANSFER_TARGET (source), settings, entries, defer);
	g_object_unref (settings);
	return batch;
}

static gboolean
impl_can_delete (RBSource *source)
{
	return TRUE;
}

static void
impl_delete_selected (RBSource *source)
{
	RBEntryView *view;
	GList *sel;

	view = rb_source_get_entry_view (source);
	sel = rb_entry_view_get_selected_entries (view);

	impl_delete_entries (RB_MEDIA_PLAYER_SOURCE (source), sel, NULL, NULL);
	g_list_free_full (sel, (GDestroyNotify) rhythmdb_entry_unref);
}


static void
impl_eject (RBDeviceSource *source)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);

	if (priv->import_job != NULL) {
		rhythmdb_import_job_cancel (priv->import_job);
		priv->ejecting = TRUE;
	} else {
		rb_device_source_default_eject (source);
	}
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
	rb_sanitize_path_for_msdos_filesystem (s);
	res = g_uri_escape_string (s, G_URI_RESERVED_CHARS_ALLOWED_IN_PATH_ELEMENT, TRUE);
	g_free (s);
	return res;
}


static char *
build_device_uri (RBAndroidSource *source, RhythmDBEntry *entry, const char *media_type, const char *extension)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	const char *in_artist;
	char *artist, *album, *title;
	gulong track_number, disc_number;
	char *number;
	char *file = NULL;
	char *storage_uri;
	char *uri;
	char *ext;
	GFile *storage = NULL;

	if (extension != NULL) {
		ext = g_strconcat (".", extension, NULL);
	} else {
		ext = g_strdup ("");
	}

	in_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM_ARTIST);
	if (in_artist[0] == '\0') {
		in_artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	}
	artist = sanitize_path (in_artist);
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
		file = g_strdup_printf (G_DIR_SEPARATOR_S "%s%s", title, ext);
	}

	if (file == NULL) {
		track_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
		disc_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER);
		if (disc_number > 0)
			number = g_strdup_printf ("%.02u.%.02u", (guint)disc_number, (guint)track_number);
		else
			number = g_strdup_printf ("%.02u", (guint)track_number);

		/* artist/album/number - title */
		file = g_strdup_printf (G_DIR_SEPARATOR_S "%s" G_DIR_SEPARATOR_S "%s" G_DIR_SEPARATOR_S "%s%%20-%%20%s%s",
					artist, album, number, title, ext);
		g_free (number);
	}

	g_free (artist);
	g_free (album);
	g_free (title);
	g_free (ext);

	/* pick storage container to use somehow
	for (l = priv->storage; l != NULL; l = l->next) {
	}
	*/
	if (priv->storage)
		storage = priv->storage->data;

	if (storage == NULL) {
		rb_debug ("couldn't find a container to store anything in");
		g_free (file);
		return NULL;
	}

	storage_uri = g_file_get_uri (storage);
	uri = g_strconcat (storage_uri, file, NULL);
	g_free (file);
	g_free (storage_uri);

	return uri;
}

static void
impl_track_upload (RBTransferTarget *target,
		   RhythmDBEntry *entry,
		   const char *dest,
		   guint64 filesize,
		   const char *media_type,
		   GError **error)
{
	RBAndroidSource *source = RB_ANDROID_SOURCE (target);
	char *realdest;
	GFile *dfile, *sfile;

	realdest = build_device_uri (source, entry, media_type, rb_gst_media_type_to_extension (media_type));
	dfile = g_file_new_for_uri (realdest);
	sfile = g_file_new_for_uri (dest);

	rb_debug ("creating parent dirs for %s", realdest);
	if (rb_uri_create_parent_dirs (realdest, error) == FALSE) {
		g_file_delete (sfile, NULL, NULL);
		g_free (realdest);
		g_object_unref (dfile);
		g_object_unref (sfile);
		return;
	}

	rb_debug ("moving %s to %s", dest, realdest);
	if (g_file_move (sfile, dfile, G_FILE_COPY_OVERWRITE, NULL, NULL, NULL, error) == FALSE) {
		g_file_delete (sfile, NULL, NULL);
	}

	g_free (realdest);
	g_object_unref (dfile);
	g_object_unref (sfile);
}

static gboolean
impl_track_added (RBTransferTarget *target,
		  RhythmDBEntry *entry,
		  const char *dest,
		  guint64 dest_size,
		  const char *media_type)
{
	RBAndroidSource *source = RB_ANDROID_SOURCE (target);
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	RhythmDBEntryType *entry_type;
	RBShell *shell;
	RhythmDB *db;
	char *realdest;

	g_object_get (source, "shell", &shell, NULL);
	g_object_get (shell, "db", &db, NULL);

	g_object_get (source, "entry-type", &entry_type, NULL);

	realdest = build_device_uri (source, entry, media_type, rb_gst_media_type_to_extension (media_type));
	rhythmdb_add_uri_with_types (db,
				     realdest,
				     entry_type,
				     priv->ignore_type,
				     priv->error_type);
	g_free (realdest);

	update_free_space (source);

	g_object_unref (entry_type);
	g_object_unref (shell);
	g_object_unref (db);
	return FALSE;
}


static void
impl_selected (RBDisplayPage *page)
{
	ensure_loaded (RB_ANDROID_SOURCE (page));
}

static void
impl_delete_thyself (RBDisplayPage *page)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (page);

	if (priv->import_errors != NULL) {
		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (priv->import_errors));
		priv->import_errors = NULL;
	}

	RB_DISPLAY_PAGE_CLASS (rb_android_source_parent_class)->delete_thyself (page);
}

static void
impl_pack_content (RBBrowserSource *source, GtkWidget *content)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (source);
	gtk_grid_attach (GTK_GRID (priv->grid), content, 0, 1, 1, 1);
}

static void
rb_android_source_init (RBAndroidSource *source)
{

}

static void
impl_constructed (GObject *object)
{
	RBAndroidSource *source;
	RBAndroidSourcePrivate *priv;
	RhythmDBEntryType *entry_type;
	RBShell *shell;
	char **output_formats;

	source = RB_ANDROID_SOURCE (object);
	priv = GET_PRIVATE (source);
	priv->grid = gtk_grid_new ();

	RB_CHAIN_GOBJECT_METHOD (rb_android_source_parent_class, constructed, object);

	priv->info_bar = gtk_info_bar_new ();
	gtk_grid_attach (GTK_GRID (priv->grid), priv->info_bar, 0, 0, 1, 1);

	gtk_container_add (GTK_CONTAINER (source), priv->grid);
	gtk_widget_show_all (priv->grid);
	gtk_widget_hide (priv->info_bar);

	rb_device_source_set_display_details (RB_DEVICE_SOURCE (source));

	g_object_get (source,
		      "shell", &shell,
		      "entry-type", &entry_type,
		      NULL);

	g_object_get (shell, "db", &priv->db, NULL);

	priv->import_errors = rb_import_errors_source_new (shell,
							   priv->error_type,
							   entry_type,
							   priv->ignore_type);

	g_object_get (priv->device_info, "output-formats", &output_formats, NULL);
	if (output_formats != NULL) {
		GstEncodingTarget *target;
		int i;

		target = gst_encoding_target_new ("android-device", "device", "", NULL);
		for (i = 0; output_formats[i] != NULL; i++) {
			const char *media_type = rb_gst_mime_type_to_media_type (output_formats[i]);
			if (media_type != NULL) {
				GstEncodingProfile *profile;
				profile = rb_gst_get_encoding_profile (media_type);
				if (profile != NULL) {
					gst_encoding_target_add_profile (target, profile);
				}
			}
		}
		g_object_set (source, "encoding-target", target, NULL);
	}
	g_strfreev (output_formats);

	g_object_unref (shell);
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IGNORE_ENTRY_TYPE:
		priv->ignore_type = g_value_get_object (value);
		break;
	case PROP_ERROR_ENTRY_TYPE:
		priv->error_type = g_value_get_object (value);
		break;
	case PROP_DEVICE_INFO:
		priv->device_info = g_value_dup_object (value);
		break;
	case PROP_VOLUME:
		priv->volume = g_value_dup_object (value);
		break;
	case PROP_MOUNT_ROOT:
		priv->mount_root = g_value_dup_object (value);
		break;
	case PROP_GUDEV_DEVICE:
		priv->gudev_device = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (object);

	switch (prop_id) {
	case PROP_IGNORE_ENTRY_TYPE:
		g_value_set_object (value, priv->ignore_type);
		break;
	case PROP_ERROR_ENTRY_TYPE:
		g_value_set_object (value, priv->error_type);
		break;
	case PROP_DEVICE_INFO:
		g_value_set_object (value, priv->device_info);
		break;
	case PROP_VOLUME:
		g_value_set_object (value, priv->volume);
		break;
	case PROP_MOUNT_ROOT:
		g_value_set_object (value, priv->mount_root);
		break;
	case PROP_GUDEV_DEVICE:
		g_value_set_object (value, priv->gudev_device);
		break;
	case PROP_DEVICE_SERIAL:
		g_value_set_string (value, g_udev_device_get_property (priv->gudev_device, "ID_SERIAL"));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (object);

	if (priv->cancel != NULL) {
		g_cancellable_cancel (priv->cancel);
		g_clear_object (&priv->cancel);
	}

	if (priv->db != NULL) {
		if (priv->ignore_type != NULL) {
			rhythmdb_entry_delete_by_type (priv->db, priv->ignore_type);
			g_clear_object (&priv->ignore_type);
		}
		if (priv->error_type != NULL) {
			rhythmdb_entry_delete_by_type (priv->db, priv->error_type);
			g_clear_object (&priv->error_type);
		}

		g_clear_object (&priv->db);
	}

	if (priv->import_job != NULL) {
		rhythmdb_import_job_cancel (priv->import_job);
		g_clear_object (&priv->import_job);
	}
	if (priv->rescan_id != 0) {
		g_source_remove (priv->rescan_id);
	}

	g_clear_object (&priv->device_info);
	g_clear_object (&priv->volume);
	g_clear_object (&priv->mount_root);
	g_clear_object (&priv->gudev_device);

	G_OBJECT_CLASS (rb_android_source_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBAndroidSourcePrivate *priv = GET_PRIVATE (object);

	g_list_free_full (priv->storage, g_object_unref);

	G_OBJECT_CLASS (rb_android_source_parent_class)->finalize (object);
}


static void
rb_android_device_source_init (RBDeviceSourceInterface *interface)
{
	interface->eject = impl_eject;
}

static void
rb_android_transfer_target_init (RBTransferTargetInterface *interface)
{
	interface->track_upload = impl_track_upload;
	interface->track_added = impl_track_added;
}

static void
rb_android_source_class_init (RBAndroidSourceClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);
	RBSourceClass *source_class = RB_SOURCE_CLASS (klass);
	RBBrowserSourceClass *browser_class = RB_BROWSER_SOURCE_CLASS (klass);
	RBMediaPlayerSourceClass *mps_class = RB_MEDIA_PLAYER_SOURCE_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->constructed = impl_constructed;
	object_class->dispose = impl_dispose;
	object_class->finalize = impl_finalize;

	page_class->delete_thyself = impl_delete_thyself;
	page_class->selected = impl_selected;

	browser_class->pack_content = impl_pack_content;

	source_class->can_delete = impl_can_delete;
	source_class->delete_selected = impl_delete_selected;
	source_class->can_move_to_trash = (RBSourceFeatureFunc) rb_false_function;
	source_class->can_paste = impl_can_paste;
	source_class->paste = impl_paste;
	source_class->want_uri = rb_device_source_want_uri;
	source_class->uri_is_source = rb_device_source_uri_is_source;

	mps_class->get_entries = impl_get_entries;
	mps_class->get_capacity = impl_get_capacity;
	mps_class->get_free_space = impl_get_free_space;
	mps_class->delete_entries = impl_delete_entries;
	mps_class->show_properties = impl_show_properties;

	g_object_class_install_property (object_class,
					 PROP_ERROR_ENTRY_TYPE,
					 g_param_spec_object ("error-entry-type",
							      "Error entry type",
							      "Entry type to use for import error entries added by this source",
							      RHYTHMDB_TYPE_ENTRY_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_IGNORE_ENTRY_TYPE,
					 g_param_spec_object ("ignore-entry-type",
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
	g_object_class_install_property (object_class,
					 PROP_VOLUME,
					 g_param_spec_object ("volume",
							      "volume",
							      "GVolume object",
							      G_TYPE_VOLUME,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_MOUNT_ROOT,
					 g_param_spec_object ("mount-root",
							      "mount root",
							      "Mount root",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_GUDEV_DEVICE,
					 g_param_spec_object ("gudev-device",
							      "gudev-device",
							      "GUdev device object",
							      G_UDEV_TYPE_DEVICE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_override_property (object_class, PROP_DEVICE_SERIAL, "serial");

	g_type_class_add_private (klass, sizeof (RBAndroidSourcePrivate));
}

static void
rb_android_source_class_finalize (RBAndroidSourceClass *klass)
{
}

void
_rb_android_source_register_type (GTypeModule *module)
{
	rb_android_source_register_type (module);
}

