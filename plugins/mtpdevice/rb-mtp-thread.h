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

#include <libmtp.h>

#include <gio/gio.h>
#include <glib.h>
#include <glib-object.h>
#include <gdk-pixbuf/gdk-pixbuf.h>

/*
 * Worker thread for dealing with MTP devices.
 * libmtp isn't thread-aware, and some operations are pretty slow
 * (getting track listings, sending and retrieving tracks), so
 * we do everything except the initial setup in a dedicated thread.
 */

#ifndef __RB_MTP_THREAD_H
#define __RB_MTP_THREAD_H

typedef enum
{
	RB_MTP_THREAD_ERROR_NO_SPACE,
	RB_MTP_THREAD_ERROR_TEMPFILE,
	RB_MTP_THREAD_ERROR_GET_TRACK,
	RB_MTP_THREAD_ERROR_SEND_TRACK
} RBMtpThreadError;

GType rb_mtp_thread_error_get_type (void);
#define RB_TYPE_MTP_THREAD_ERROR (rb_mtp_thread_error_get_type ())
GQuark rb_mtp_thread_error_quark (void);
#define RB_MTP_THREAD_ERROR rb_mtp_thread_error_quark ()

#define RB_TYPE_MTP_THREAD         (rb_mtp_thread_get_type ())
#define RB_MTP_THREAD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_MTP_THREAD, RBMtpThread))
#define RB_MTP_THREAD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_MTP_THREAD, RBMtpThreadClass))
#define RB_IS_MTP_THREAD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_MTP_THREAD))
#define RB_IS_MTP_THREAD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_MTP_THREAD))
#define RB_MTP_THREAD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_MTP_THREAD, RBMtpThreadClass))

typedef struct
{
	GObject parent;
	LIBMTP_mtpdevice_t *device;
	GHashTable *albums;

	GThread *thread;
	GAsyncQueue *queue;
} RBMtpThread;

typedef struct
{
	GObjectClass parent;
} RBMtpThreadClass;

/* callback types */
typedef void (*RBMtpOpenCallback) (LIBMTP_mtpdevice_t *device, gpointer user_data);
typedef void (*RBMtpTrackListCallback) (LIBMTP_track_t *tracklist, gpointer user_data);
typedef void (*RBMtpUploadCallback) (LIBMTP_track_t *track, GError *error, gpointer user_data);
typedef void (*RBMtpDownloadCallback) (uint32_t track_id, const char *filename, GError *error, gpointer user_data);
typedef void (*RBMtpThreadCallback) (LIBMTP_mtpdevice_t *device, gpointer user_data);
typedef void (*RBMtpCreateFolderCallback) (uint32_t folder_id, gpointer user_data);

GType		rb_mtp_thread_get_type (void);
RBMtpThread *	rb_mtp_thread_new (void);
void            _rb_mtp_thread_register_type (GTypeModule *module);

void		rb_mtp_thread_report_errors (RBMtpThread *thread);

void		rb_mtp_thread_open_device (RBMtpThread *thread,
					   LIBMTP_raw_device_t *raw_device,
					   RBMtpOpenCallback func,
					   gpointer data,
					   GDestroyNotify destroy_data);
void		rb_mtp_thread_get_track_list (RBMtpThread *thread,
					      RBMtpTrackListCallback func,
					      gpointer data,
					      GDestroyNotify destroy_data);

void		rb_mtp_thread_set_device_name (RBMtpThread *thread, const char *name);

void		rb_mtp_thread_queue_callback (RBMtpThread *thread,
					      RBMtpThreadCallback func,
					      gpointer data,
					      GDestroyNotify destroy_data);

/* folders */
void		rb_mtp_thread_create_folder (RBMtpThread *thread,
					     const char **path,
					     RBMtpCreateFolderCallback func,
					     gpointer data,
					     GDestroyNotify destroy_data);

/* albums */
void		rb_mtp_thread_add_to_album (RBMtpThread *thread, LIBMTP_track_t *track, const char *album);
void		rb_mtp_thread_remove_from_album (RBMtpThread *thread, LIBMTP_track_t *track, const char *album);
void		rb_mtp_thread_set_album_image (RBMtpThread *thread, const char *album, GdkPixbuf *image);

/* tracks */
void		rb_mtp_thread_delete_track (RBMtpThread *thread, LIBMTP_track_t *track);
void		rb_mtp_thread_upload_track (RBMtpThread *thread,
					    LIBMTP_track_t *track,
					    const char *filename,
					    RBMtpUploadCallback func,
					    gpointer data,
					    GDestroyNotify destroy_data);
void		rb_mtp_thread_download_track (RBMtpThread *thread,
					      uint32_t track_id,
					      const char *filename,
					      RBMtpDownloadCallback func,
					      gpointer data,
					      GDestroyNotify destroy_data);

#endif

