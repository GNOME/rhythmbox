/*
 *  Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
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

#include <string.h>
#include <stdlib.h>

#include <glib.h>
#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-mtp-thread.h"
#include "rb-file-helpers.h"
#include "rb-dialog.h"
#include "rb-debug.h"

G_DEFINE_DYNAMIC_TYPE(RBMtpThread, rb_mtp_thread, G_TYPE_OBJECT)


typedef struct {
	enum {
		OPEN_DEVICE = 1,
		CLOSE_DEVICE,
		SET_DEVICE_NAME,
		THREAD_CALLBACK,

		CREATE_FOLDER,

		ADD_TO_ALBUM,
		REMOVE_FROM_ALBUM,
		SET_ALBUM_IMAGE,

		GET_TRACK_LIST,
		DELETE_TRACK,
		UPLOAD_TRACK,
		DOWNLOAD_TRACK
	} task;

	LIBMTP_raw_device_t *raw_device;
	LIBMTP_track_t *track;
	uint32_t track_id;
	uint32_t folder_id;
	uint32_t storage_id;
	char *album;
	char *filename;
	GdkPixbuf *image;
	char *name;
	char **path;

	gpointer callback;
	gpointer user_data;
	GDestroyNotify destroy_data;
} RBMtpThreadTask;

static char *
task_name (RBMtpThreadTask *task)
{
	switch (task->task) {
	case OPEN_DEVICE:	return g_strdup ("open device");
	case CLOSE_DEVICE:	return g_strdup ("close device");
	case SET_DEVICE_NAME:	return g_strdup_printf ("set device name to %s", task->name);
	case THREAD_CALLBACK:	return g_strdup ("thread callback");

	case CREATE_FOLDER:	return g_strdup_printf ("create folder %s", task->path[g_strv_length (task->path)-1]);

	case ADD_TO_ALBUM:	return g_strdup_printf ("add track %u to album %s", task->track_id, task->album);
	case REMOVE_FROM_ALBUM:	return g_strdup_printf ("remove track %u from album %s", task->track_id, task->album);
	case SET_ALBUM_IMAGE:	return g_strdup_printf ("set image for album %s", task->album);

	case GET_TRACK_LIST:	return g_strdup ("get track list");
	case DELETE_TRACK:	return g_strdup_printf ("delete track %u", task->track_id);
	case UPLOAD_TRACK:	return g_strdup_printf ("upload track from %s", task->filename);
	case DOWNLOAD_TRACK:	return g_strdup_printf ("download track %u to %s",
							task->track_id,
							task->filename[0] ? task->filename : "<temporary>");
	default:		return g_strdup_printf ("unknown task type %d", task->task);
	}
}

static RBMtpThreadTask *
create_task (int tasktype)
{
	RBMtpThreadTask *task = g_slice_new0 (RBMtpThreadTask);
	task->task = tasktype;
	return task;
}

static void
destroy_task (RBMtpThreadTask *task)
{
	/* don't think we ever own the track structure here;
	 * we only have it for uploads, and then we pass it back
	 * to the callback.
	 */

	g_free (task->album);
	g_free (task->filename);
	g_free (task->name);
	g_strfreev (task->path);

	if (task->image) {
		g_object_unref (task->image);
	}

	if (task->destroy_data) {
		task->destroy_data (task->user_data);
	}

	g_slice_free (RBMtpThreadTask, task);
}


static void
queue_task (RBMtpThread *thread, RBMtpThreadTask *task)
{
	char *name = task_name (task);
	rb_debug ("queueing task: %s", name);
	g_free (name);

	g_async_queue_push (thread->queue, task);
}

static void
open_device (RBMtpThread *thread, RBMtpThreadTask *task)
{
	RBMtpOpenCallback cb = task->callback;
	int retry;

	/* open the device */
	rb_debug ("attempting to open device");
	for (retry = 0; retry < 5; retry++) {
		if (retry > 0) {
			/* sleep a while before trying again */
			g_usleep (G_USEC_PER_SEC);
		}

		thread->device = LIBMTP_Open_Raw_Device (task->raw_device);
		if (thread->device != NULL) {
			break;
		}

		rb_debug ("attempt %d failed..", retry+1);
	}

	cb (thread->device, task->user_data);
}

static void
create_folder (RBMtpThread *thread, RBMtpThreadTask *task)
{
	RBMtpCreateFolderCallback cb = task->callback;
	LIBMTP_folder_t *folders;
	LIBMTP_folder_t *f;
	LIBMTP_folder_t *target = NULL;
	uint32_t folder_id;
	uint32_t storage_id;
	int i;

	folders = LIBMTP_Get_Folder_List (thread->device);
	if (folders == NULL) {
		rb_debug ("unable to get folder list");
		rb_mtp_thread_report_errors (thread);
		cb (0, task->user_data);
		return;
	}

	/* first find the default music folder */
	f = LIBMTP_Find_Folder (folders, thread->device->default_music_folder);
	if (f == NULL) {
		rb_debug ("unable to find default music folder");
		cb (0, task->user_data);
		LIBMTP_destroy_folder_t (folders);
		return;
	}
	storage_id = f->storage_id;
	folder_id = f->folder_id;

	/* descend through the folder tree, following the path */
	i = 0;
	while (task->path[i] != NULL) {

		/* look for a folder at this level with the same name as the
		 * next path component
		 */
		target = f->child;
		while (target != NULL) {
			if (g_strcmp0 (target->name, task->path[i]) == 0) {
				rb_debug ("found path element %d: %s", i, target->name);
				break;
			}
			target = target->sibling;
		}

		if (target == NULL) {
			rb_debug ("path element %d (%s) not found", i, task->path[i]);
			break;
		}
		f = target;
		folder_id = f->folder_id;
		i++;
	}

	/* now create any path elements that don't already exist */
	while (task->path[i] != NULL) {
		folder_id = LIBMTP_Create_Folder (thread->device, task->path[i], folder_id, storage_id);
		if (folder_id == 0) {
			rb_debug ("couldn't create path element %d: %s", i, task->path[i]);
			rb_mtp_thread_report_errors (thread);
			break;
		}
		rb_debug ("created path element %d: %s with folder ID %u", i, task->path[i], folder_id);
		i++;
	}

	cb (folder_id, task->user_data);
	LIBMTP_destroy_folder_t (folders);
}

static LIBMTP_album_t *
add_track_to_album (RBMtpThread *thread, const char *album_name, uint32_t track_id, uint32_t folder_id, uint32_t storage_id, gboolean *new_album)
{
	LIBMTP_album_t *album;

	album = g_hash_table_lookup (thread->albums, album_name);
	if (album != NULL) {
		/* add track to album */
		album->tracks = realloc (album->tracks, sizeof(uint32_t) * (album->no_tracks+1));
		album->tracks[album->no_tracks] = track_id;
		album->no_tracks++;
		rb_debug ("adding track ID %d to album ID %d; now has %d tracks",
			  track_id,
			  album->album_id,
			  album->no_tracks);

		if (new_album != NULL) {
			*new_album = FALSE;
		}
	} else {
		/* add new album */
		album = LIBMTP_new_album_t ();
		album->name = strdup (album_name);
		album->no_tracks = 1;
		album->tracks = malloc (sizeof(uint32_t));
		album->tracks[0] = track_id;
		album->parent_id = folder_id;
		album->storage_id = storage_id;

		rb_debug ("creating new album (%s) for track ID %d", album->name, track_id);

		g_hash_table_insert (thread->albums, album->name, album);
		if (new_album != NULL) {
			*new_album = TRUE;
		}
	}

	return album;
}

static void
write_album_to_device (RBMtpThread *thread, LIBMTP_album_t *album, gboolean new_album)
{
	if (new_album) {
		if (LIBMTP_Create_New_Album (thread->device, album) != 0) {
			rb_debug ("LIBMTP_Create_New_Album failed..");
			rb_mtp_thread_report_errors (thread);
		}
	} else {
		if (LIBMTP_Update_Album (thread->device, album) != 0) {
			rb_debug ("LIBMTP_Update_Album failed..");
			rb_mtp_thread_report_errors (thread);
		}
	}
}

static void
add_track_to_album_and_update (RBMtpThread *thread, RBMtpThreadTask *task)
{
	LIBMTP_album_t *album;
	gboolean new_album = FALSE;

	album = add_track_to_album (thread, task->album, task->track_id, task->folder_id, task->storage_id, &new_album);
	write_album_to_device (thread, album, new_album);
}

static void
remove_track_from_album (RBMtpThread *thread, RBMtpThreadTask *task)
{
	LIBMTP_album_t *album;
	int i;

	album = g_hash_table_lookup (thread->albums, task->album);
	if (album == NULL) {
		rb_debug ("Couldn't find an album for %s", task->album);
		return;
	}

	for (i = 0; i < album->no_tracks; i++) {
		if (album->tracks[i] == task->track_id) {
			break;
		}
	}

	if (i == album->no_tracks) {
		rb_debug ("Couldn't find track %d in album %d", task->track_id, album->album_id);
		return;
	}

	memmove (album->tracks + i, album->tracks + i + 1, album->no_tracks - (i+1));
	album->no_tracks--;

	if (album->no_tracks == 0) {
		rb_debug ("deleting empty album %d", album->album_id);
		if (LIBMTP_Delete_Object (thread->device, album->album_id) != 0) {
			rb_mtp_thread_report_errors (thread);
		}
		g_hash_table_remove (thread->albums, task->album);
	} else {
		rb_debug ("updating album %d: %d tracks remaining", album->album_id, album->no_tracks);
		if (LIBMTP_Update_Album (thread->device, album) != 0) {
			rb_mtp_thread_report_errors (thread);
		}
	}
}

static void
set_album_image (RBMtpThread *thread, RBMtpThreadTask *task)
{
	LIBMTP_filesampledata_t *albumart;
	LIBMTP_album_t *album;
	GError *error = NULL;
	char *image_data;
	gsize image_size;
	int ret;
	
	album = g_hash_table_lookup (thread->albums, task->album);
	if (album == NULL) {
		rb_debug ("Couldn't find an album for %s", task->album);
		return;
	}
	
	/* probably should scale the image down, since some devices have a size limit and they all have
	 * tiny displays anyway.
	 */

	if (gdk_pixbuf_save_to_buffer (task->image, &image_data, &image_size, "jpeg", &error, NULL) == FALSE) {
		rb_debug ("unable to convert album art image to a JPEG buffer: %s", error->message);
		g_error_free (error);
		return;
	}

	albumart = LIBMTP_new_filesampledata_t ();
	albumart->filetype = LIBMTP_FILETYPE_JPEG;
	albumart->data = image_data;
	albumart->size = image_size;

	ret = LIBMTP_Send_Representative_Sample (thread->device, album->album_id, albumart);
	if (ret != 0) {
		rb_mtp_thread_report_errors (thread);
	} else {
		rb_debug ("successfully set album art for %s (%" G_GSIZE_FORMAT " bytes)", task->album, image_size);
	}

	/* libmtp will try to free this if we don't clear the pointer */
	albumart->data = NULL;
	LIBMTP_destroy_filesampledata_t (albumart);
}

static void
get_track_list (RBMtpThread *thread, RBMtpThreadTask *task)
{
	RBMtpTrackListCallback cb = task->callback;
	LIBMTP_track_t *tracks = NULL;
	LIBMTP_album_t *albums;

	/* get all the albums */
	albums = LIBMTP_Get_Album_List (thread->device);
	rb_mtp_thread_report_errors (thread);
	if (albums != NULL) {
		LIBMTP_album_t *album;

		for (album = albums; album != NULL; album = album->next) {
			if (album->name == NULL)
				continue;

			rb_debug ("album: %s, %d tracks", album->name, album->no_tracks);
			g_hash_table_insert (thread->albums, album->name, album);
		}
	} else {
		rb_debug ("No albums");
	}

	tracks = LIBMTP_Get_Tracklisting_With_Callback (thread->device, NULL, NULL);
	rb_mtp_thread_report_errors (thread);
	if (tracks == NULL) {
		rb_debug ("no tracks on the device");
	}

	cb (tracks, task->user_data);
	/* the callback owns the tracklist */
}

static void
download_track (RBMtpThread *thread, RBMtpThreadTask *task)
{
	LIBMTP_file_t *fileinfo;
	LIBMTP_error_t *stack;
	GError *error = NULL;
	GFile *dir;
	RBMtpDownloadCallback cb = (RBMtpDownloadCallback) task->callback;

	/* first, check there's enough space to copy it */
	fileinfo = LIBMTP_Get_Filemetadata (thread->device, task->track_id);
	if (fileinfo == NULL) {
		stack = LIBMTP_Get_Errorstack (thread->device);
		rb_debug ("unable to get track metadata for %u: %s", task->track_id, stack->error_text);
		error = g_error_new (RB_MTP_THREAD_ERROR,
				     RB_MTP_THREAD_ERROR_GET_TRACK,
				     _("Unable to copy file from MTP device: %s"),
				     stack->error_text);
		LIBMTP_Clear_Errorstack (thread->device);

		cb (task->track_id, NULL, error, task->user_data);
		g_error_free (error);
		return;
	}

	if (task->filename[0] == '\0') {
		dir = g_file_new_for_path (g_get_tmp_dir ());
	} else {
		GFile *file = g_file_new_for_path (task->filename);
		dir = g_file_get_parent (file);
		g_object_unref (file);
	}
	rb_debug ("checking for %" G_GINT64_FORMAT " bytes available", fileinfo->filesize);
	if (rb_check_dir_has_space (dir, fileinfo->filesize) == FALSE) {
		char *dpath = g_file_get_path (dir);
		rb_debug ("not enough space in %s", dpath);
		error = g_error_new (RB_MTP_THREAD_ERROR, RB_MTP_THREAD_ERROR_NO_SPACE,
				     _("Not enough space in %s"), dpath);
		g_free (dpath);
	}
	LIBMTP_destroy_file_t (fileinfo);
	g_object_unref (dir);

	if (error != NULL) {
		rb_debug ("bailing out due to error: %s", error->message);
		cb (task->track_id, NULL, error, task->user_data);
		g_error_free (error);
		return;
	}

	if (task->filename[0] == '\0') {
		/* download to a temporary file */
		int fd;
		GError *tmperror = NULL;

		g_free (task->filename);
		fd = g_file_open_tmp ("rb-mtp-temp-XXXXXX", &task->filename, &tmperror);
		if (fd == -1) {
			rb_debug ("unable to open temporary file: %s", tmperror->message);
			error = g_error_new (RB_MTP_THREAD_ERROR,
					     RB_MTP_THREAD_ERROR_TEMPFILE,
					     _("Unable to open temporary file: %s"),
					     tmperror->message);
			g_error_free (tmperror);

			cb (task->track_id, NULL, error, task->user_data);
			g_error_free (error);
			return;
		} else {
			rb_debug ("downloading track %u to file descriptor %d", task->track_id, fd);
			if (LIBMTP_Get_Track_To_File_Descriptor (thread->device, task->track_id, fd, NULL, NULL)) {
				stack = LIBMTP_Get_Errorstack (thread->device);
				rb_debug ("unable to retrieve track %u: %s", task->track_id, stack->error_text);
				error = g_error_new (RB_MTP_THREAD_ERROR, RB_MTP_THREAD_ERROR_GET_TRACK,
						     _("Unable to copy file from MTP device: %s"),
						     stack->error_text);
				LIBMTP_Clear_Errorstack (thread->device);

				cb (task->track_id, NULL, error, task->user_data);
				g_error_free (error);
				close (fd);
				remove (task->filename);
				return;
			}
			rb_debug ("done downloading track");

			close (fd);
		}
	} else {
		if (LIBMTP_Get_Track_To_File (thread->device, task->track_id, task->filename, NULL, NULL)) {
			stack = LIBMTP_Get_Errorstack (thread->device);
			error = g_error_new (RB_MTP_THREAD_ERROR, RB_MTP_THREAD_ERROR_GET_TRACK,
					     _("Unable to copy file from MTP device: %s"),
					     stack->error_text);
			LIBMTP_Clear_Errorstack (thread->device);

			cb (task->track_id, NULL, error, task->user_data);
			g_error_free (error);
			return;
		}
	}

	cb (task->track_id, task->filename, NULL, task->user_data);
}

static int
upload_progress (const uint64_t sent, const uint64_t total, const void * const data)
{
	rb_debug ("upload: %" G_GUINT64_FORMAT " of %" G_GUINT64_FORMAT, sent, total);
	return 0;
}

static void
upload_track (RBMtpThread *thread, RBMtpThreadTask *task)
{
	RBMtpUploadCallback cb = (RBMtpUploadCallback) task->callback;
	LIBMTP_error_t *stack;
	GError *error = NULL;

	if (LIBMTP_Send_Track_From_File (thread->device, task->filename, task->track, upload_progress, NULL)) {
		stack = LIBMTP_Get_Errorstack (thread->device);
		rb_debug ("unable to send track: %s", stack->error_text);

		if (stack->errornumber == LIBMTP_ERROR_STORAGE_FULL) {
			error = g_error_new (RB_MTP_THREAD_ERROR, RB_MTP_THREAD_ERROR_NO_SPACE,
					     _("No space left on MTP device"));
		} else {
			error = g_error_new (RB_MTP_THREAD_ERROR, RB_MTP_THREAD_ERROR_SEND_TRACK,
					     _("Unable to send file to MTP device: %s"),
					     stack->error_text);
		}
		LIBMTP_Clear_Errorstack (thread->device);
		task->track->item_id = 0;		/* is this actually an invalid item ID? */
	}
	cb (task->track, error, task->user_data);
	g_clear_error (&error);
}

static gboolean 
run_task (RBMtpThread *thread, RBMtpThreadTask *task)
{
	char *name = task_name (task);
	rb_debug ("running task: %s", name);
	g_free (name);

	switch (task->task) {
	case OPEN_DEVICE:
		open_device (thread, task);
		break;

	case CLOSE_DEVICE:
		return TRUE;

	case SET_DEVICE_NAME:
		if (LIBMTP_Set_Friendlyname (thread->device, task->name)) {
			rb_mtp_thread_report_errors (thread);
		}
		break;

	case THREAD_CALLBACK:
		{
			RBMtpThreadCallback cb = (RBMtpThreadCallback)task->callback;
			cb (thread->device, task->user_data);
		}
		break;

	case CREATE_FOLDER:
		create_folder (thread, task);
		break;

	case ADD_TO_ALBUM:
		add_track_to_album_and_update (thread, task);
		break;

	case REMOVE_FROM_ALBUM:
		remove_track_from_album (thread, task);
		break;

	case SET_ALBUM_IMAGE:
		set_album_image (thread, task);
		break;

	case GET_TRACK_LIST:
		get_track_list (thread, task);
		break;

	case DELETE_TRACK:
		if (LIBMTP_Delete_Object (thread->device, task->track_id)) {
			rb_mtp_thread_report_errors (thread);
		}
		break;

	case UPLOAD_TRACK:
		upload_track (thread, task);
		break;

	case DOWNLOAD_TRACK:
		download_track (thread, task);
		break;

	default:
		g_assert_not_reached ();
	}

	return FALSE;
}

static gpointer
task_thread (RBMtpThread *thread)
{
	RBMtpThreadTask *task;
	gboolean quit = FALSE;
	GAsyncQueue *queue = g_async_queue_ref (thread->queue);

	rb_debug ("MTP device worker thread starting");
	while (quit == FALSE) {

		task = g_async_queue_pop (queue);
		quit = run_task (thread, task);
		destroy_task (task);
	}

	rb_debug ("MTP device worker thread exiting");
	
	/* clean up any queued tasks */
	while ((task = g_async_queue_try_pop (queue)) != NULL)
		destroy_task (task);

	g_async_queue_unref (queue);
	return NULL;
}

/* callable interface */

void
rb_mtp_thread_open_device (RBMtpThread *thread,
			   LIBMTP_raw_device_t *raw_device,
			   RBMtpOpenCallback callback,
			   gpointer data,
			   GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (OPEN_DEVICE);
	task->raw_device = raw_device;
	task->callback = callback;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_set_device_name (RBMtpThread *thread, const char *name)
{
	RBMtpThreadTask *task = create_task (SET_DEVICE_NAME);
	task->name = g_strdup (name);
	queue_task (thread, task);
}

void
rb_mtp_thread_create_folder (RBMtpThread *thread,
			     const char **path,
			     RBMtpCreateFolderCallback func,
			     gpointer data,
			     GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (CREATE_FOLDER);
	task->path = g_strdupv ((char **)path);
	task->callback = func;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_add_to_album (RBMtpThread *thread, LIBMTP_track_t *track, const char *album)
{
	RBMtpThreadTask *task = create_task (ADD_TO_ALBUM);
	task->track_id = track->item_id;
	task->folder_id = track->parent_id;
	task->storage_id = track->storage_id;
	task->album = g_strdup (album);
	queue_task (thread, task);
}

void
rb_mtp_thread_remove_from_album (RBMtpThread *thread, LIBMTP_track_t *track, const char *album)
{
	RBMtpThreadTask *task = create_task (REMOVE_FROM_ALBUM);
	task->track_id = track->item_id;
	task->storage_id = track->storage_id;
	task->album = g_strdup (album);
	queue_task (thread, task);
}

void
rb_mtp_thread_set_album_image (RBMtpThread *thread, const char *album, GdkPixbuf *image)
{
	RBMtpThreadTask *task = create_task (SET_ALBUM_IMAGE);
	task->album = g_strdup (album);
	task->image = g_object_ref (image);
	queue_task (thread, task);
}

void
rb_mtp_thread_get_track_list (RBMtpThread *thread,
			      RBMtpTrackListCallback callback,
			      gpointer data,
			      GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (GET_TRACK_LIST);
	task->callback = callback;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_delete_track (RBMtpThread *thread, LIBMTP_track_t *track)
{
	RBMtpThreadTask *task = create_task (DELETE_TRACK);
	task->track_id = track->item_id;
	task->storage_id = track->storage_id;
	queue_task (thread, task);
}

void
rb_mtp_thread_upload_track (RBMtpThread *thread,
			    LIBMTP_track_t *track,
			    const char *filename,
			    RBMtpUploadCallback func,
			    gpointer data,
			    GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (UPLOAD_TRACK);
	task->track = track;
	task->filename = g_strdup (filename);
	task->callback = func;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_download_track (RBMtpThread *thread,
			      uint32_t track_id,
			      const char *filename,
			      RBMtpDownloadCallback func,
			      gpointer data,
			      GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (DOWNLOAD_TRACK);
	task->track_id = track_id;
	task->filename = g_strdup (filename);
	task->callback = func;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_queue_callback (RBMtpThread *thread,
			      RBMtpThreadCallback func,
			      gpointer data,
			      GDestroyNotify destroy_data)
{
	RBMtpThreadTask *task = create_task (THREAD_CALLBACK);
	task->callback = func;
	task->user_data = data;
	task->destroy_data = destroy_data;
	queue_task (thread, task);
}

void
rb_mtp_thread_report_errors (RBMtpThread *thread)
{
	LIBMTP_error_t *stack;

	for (stack = LIBMTP_Get_Errorstack (thread->device); stack != NULL; stack = stack->next) {
		g_warning ("libmtp error: %s", stack->error_text);
	}

	LIBMTP_Clear_Errorstack (thread->device);
}

/* GObject things */

static void
impl_finalize (GObject *object)
{
	RBMtpThread *thread = RB_MTP_THREAD (object);
	RBMtpThreadTask *task;

	rb_debug ("killing MTP worker thread");
	task = create_task (CLOSE_DEVICE);
	queue_task (thread, task);
	if (thread->thread != g_thread_self ()) {
		g_thread_join (thread->thread);
		rb_debug ("MTP worker thread exited");
	} else {
		rb_debug ("we're on the MTP worker thread..");
	}

	g_async_queue_unref (thread->queue);

	g_hash_table_destroy (thread->albums);

	if (thread->device != NULL) {
		LIBMTP_Release_Device (thread->device);
	}

	G_OBJECT_CLASS (rb_mtp_thread_parent_class)->finalize (object);
}

static void
rb_mtp_thread_init (RBMtpThread *thread)
{
	thread->queue = g_async_queue_new ();
	
	thread->albums = g_hash_table_new_full (g_str_hash, g_str_equal, NULL, (GDestroyNotify) LIBMTP_destroy_album_t);

	thread->thread = g_thread_new ("mtp", (GThreadFunc) task_thread, thread);
}

static void
rb_mtp_thread_class_init (RBMtpThreadClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;
}

static void
rb_mtp_thread_class_finalize (RBMtpThreadClass *klass)
{
}

RBMtpThread *
rb_mtp_thread_new (void)
{
	return RB_MTP_THREAD (g_object_new (RB_TYPE_MTP_THREAD, NULL));
}

GQuark
rb_mtp_thread_error_quark (void)
{
	static GQuark quark = 0;
	if (!quark)
		quark = g_quark_from_static_string ("rb_mtp_thread_error");

	return quark;
}

#define ENUM_ENTRY(NAME, DESC) { NAME, "" #NAME "", DESC }

GType
rb_mtp_thread_error_get_type (void)
{
	static GType etype = 0;

	if (etype == 0)	{
		static const GEnumValue values[] = {
			ENUM_ENTRY (RB_MTP_THREAD_ERROR_NO_SPACE, "no-space"),
			ENUM_ENTRY (RB_MTP_THREAD_ERROR_TEMPFILE, "tempfile-failed"),
			ENUM_ENTRY (RB_MTP_THREAD_ERROR_GET_TRACK, "track-get-failed"),
			{ 0, 0, 0 }
		};

		etype = g_enum_register_static ("RBMTPThreadError", values);
	}

	return etype;
}

void
_rb_mtp_thread_register_type (GTypeModule *module)
{
	rb_mtp_thread_register_type (module);
}
