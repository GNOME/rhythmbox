/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2009  Jonathan Matthew  <jonathan@d14n.org>
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
#include <libmtp.h>
#include <gst/gst.h>

#include "rb-mtp-thread.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

#define RB_TYPE_MTP_SINK (rb_mtp_sink_get_type())
#define RB_MTP_SINK(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_MTP_SINK,RBMTPSink))
#define RB_MTP_SINK_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_MTP_SINK,RBMTPSinkClass))
#define RB_IS_MTP_SINK(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_MTP_SINK))
#define RB_IS_MTP_SINK_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_MTP_SINK))

typedef struct _RBMTPSink RBMTPSink;
typedef struct _RBMTPSinkClass RBMTPSinkClass;

struct _RBMTPSink
{
	GstBin parent;

	RBMtpThread *device_thread;

	LIBMTP_track_t *track;
	char **folder_path;
	char *tempfile;

	GstElement *fdsink;
	GstPad *ghostpad;

	GError *upload_error;
	GMutex *upload_mutex;
	GCond *upload_cond;
	gboolean got_folder;
	gboolean upload_done;
};

struct _RBMTPSinkClass
{
	GstBinClass parent_class;
};

enum
{
	PROP_0,
	PROP_URI,
	PROP_MTP_TRACK,
	PROP_FOLDER_PATH,
	PROP_DEVICE_THREAD
};

static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

static GstElementDetails rb_mtp_sink_details =
GST_ELEMENT_DETAILS ("RB MTP Sink",
	"Sink/File",
	"Uploads tracks to MTP devices",
	"Jonathan Matthew <jonathan@d14n.org>");

GType rb_mtp_sink_get_type (void);
static void rb_mtp_sink_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType mtp_sink_type)
{
	static const GInterfaceInfo urihandler_info = {
		rb_mtp_sink_uri_handler_init,
		NULL,
		NULL
	};

	g_type_add_interface_static (mtp_sink_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

GST_BOILERPLATE_FULL (RBMTPSink, rb_mtp_sink, GstBin, GST_TYPE_BIN, _do_init);

static void
rb_mtp_sink_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&sinktemplate));
	gst_element_class_set_details (element_class, &rb_mtp_sink_details);
}

static void
rb_mtp_sink_init (RBMTPSink *sink, RBMTPSinkClass *klass)
{
	GstPad *pad;

	sink->upload_mutex = g_mutex_new ();
	sink->upload_cond = g_cond_new ();

	/* create actual sink */
	sink->fdsink = gst_element_factory_make ("fdsink", NULL);
	if (sink->fdsink == NULL) {
		g_warning ("couldn't create fdsink element");
		return;
	}

	gst_bin_add (GST_BIN (sink), sink->fdsink);
	gst_object_ref (sink->fdsink);

	/* create ghost pad */
	pad = gst_element_get_pad (sink->fdsink, "sink");
	sink->ghostpad = gst_ghost_pad_new ("sink", pad);
	gst_element_add_pad (GST_ELEMENT (sink), sink->ghostpad);
	gst_object_ref (sink->ghostpad);
	gst_object_unref (pad);

}

static GstStateChangeReturn
rb_mtp_sink_open_tempfile (RBMTPSink *sink)
{
	int fd;
	GError *tmperror = NULL;

	fd = g_file_open_tmp ("rb-mtp-temp-XXXXXX", &sink->tempfile, &tmperror);
	if (fd == -1) {
		GST_ELEMENT_ERROR (sink, RESOURCE, OPEN_WRITE, (_("Unable to open temporary file: %s"), tmperror->message), NULL);
		return GST_STATE_CHANGE_FAILURE;
	}
	rb_debug ("opened temporary file %s", sink->tempfile);

	g_object_set (sink->fdsink, "fd", fd, "sync", FALSE, NULL);
	return GST_STATE_CHANGE_SUCCESS;
}

static GstStateChangeReturn
rb_mtp_sink_close_tempfile (RBMTPSink *sink)
{
	if (sink->tempfile != NULL) {
		rb_debug ("deleting tempfile %s", sink->tempfile);
		remove (sink->tempfile);
		g_free (sink->tempfile);
		sink->tempfile = NULL;
	}

	return GST_STATE_CHANGE_SUCCESS;
}

static void
folder_callback (uint32_t folder_id, RBMTPSink *sink)
{
	g_mutex_lock (sink->upload_mutex);
	if (folder_id == 0) {
		rb_debug ("mtp folder create failed");
	} else {
		rb_debug ("mtp folder for upload: %u", folder_id);
		sink->track->parent_id = folder_id;
	}

	sink->got_folder = TRUE;

	g_cond_signal (sink->upload_cond);
	g_mutex_unlock (sink->upload_mutex);
}

static void
upload_callback (LIBMTP_track_t *track, GError *error, RBMTPSink *sink)
{
	rb_debug ("mtp upload callback for %s: item ID %d", track->filename, track->item_id);
	g_mutex_lock (sink->upload_mutex);

	if (error != NULL) {
		sink->upload_error = g_error_copy (error);
	}
	sink->upload_done = TRUE;

	g_cond_signal (sink->upload_cond);
	g_mutex_unlock (sink->upload_mutex);
}

static void
rb_mtp_sink_handle_message (GstBin *bin, GstMessage *message)
{
	/* when we get an EOS message from the fdsink, close the fd and upload the
	 * file to the device.
	 */
	if (GST_MESSAGE_TYPE (message) == GST_MESSAGE_EOS) {
		int fd;
		struct stat stat_buf;

		RBMTPSink *sink = RB_MTP_SINK (bin);

		/* fill in the file size and close the fd */
		g_object_get (sink->fdsink, "fd", &fd, NULL);
		fstat (fd, &stat_buf);
		sink->track->filesize = stat_buf.st_size;
		close (fd);

		rb_debug ("handling EOS from fdsink; file size is %" G_GUINT64_FORMAT, sink->track->filesize);

		/* we can just block waiting for mtp thread operations to finish here
		 * as we're on a streaming thread.
		 */
		g_mutex_lock (sink->upload_mutex);

		if (sink->folder_path != NULL) {
			/* find or create the target folder.
			 * if this fails, we just upload to the default music folder
			 * rather than giving up entirely.
			 */
			sink->got_folder = FALSE;
			rb_mtp_thread_create_folder (sink->device_thread,
						     (const char **)sink->folder_path,
						     (RBMtpCreateFolderCallback) folder_callback,
						     g_object_ref (sink),
						     g_object_unref);
			while (sink->got_folder == FALSE) {
				g_cond_wait (sink->upload_cond, sink->upload_mutex);
			}
		}

		/* and upload the file */
		sink->upload_done = FALSE;
		rb_mtp_thread_upload_track (sink->device_thread,
					    sink->track,
					    sink->tempfile,
					    (RBMtpUploadCallback) upload_callback,
					    g_object_ref (sink),
					    g_object_unref);

		while (sink->upload_done == FALSE) {
			g_cond_wait (sink->upload_cond, sink->upload_mutex);
		}
		g_mutex_unlock (sink->upload_mutex);

		/* post error message if the upload failed - this should get there before
		 * this EOS message does, so it should work OK.
		 */
		if (sink->upload_error != NULL) {
			int code;

			switch (sink->upload_error->code) {
			case RB_MTP_THREAD_ERROR_NO_SPACE:
				code = GST_RESOURCE_ERROR_NO_SPACE_LEFT;
				break;

			default:
			case RB_MTP_THREAD_ERROR_SEND_TRACK:
				code = GST_RESOURCE_ERROR_WRITE;
				break;
			}

			GST_WARNING_OBJECT (sink, "error: %s", sink->upload_error->message);
			gst_element_message_full (GST_ELEMENT (sink),
						  GST_MESSAGE_ERROR,
						  GST_RESOURCE_ERROR, code,
						  g_strdup (sink->upload_error->message), NULL,
						  __FILE__, GST_FUNCTION, __LINE__);
		}
	}

	GST_BIN_CLASS (parent_class)->handle_message (bin, message);
}

static GstStateChangeReturn
rb_mtp_sink_change_state (GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	RBMTPSink *sink = RB_MTP_SINK (element);

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
			ret = rb_mtp_sink_open_tempfile (sink);
			if (ret != GST_STATE_CHANGE_SUCCESS)
				return ret;
			break;

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			break;

		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			break;

		default:
			break;
	}

	ret = GST_ELEMENT_CLASS (parent_class)->change_state (element, transition);

	switch (transition) {
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			break;
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			break;
		case GST_STATE_CHANGE_READY_TO_NULL:
			ret = rb_mtp_sink_close_tempfile (sink);
			break;
		default:
			break;
	}

	return ret;
}

static void
rb_mtp_sink_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBMTPSink *sink = RB_MTP_SINK (object);
	char **path;

	switch (prop_id) {
	case PROP_MTP_TRACK:
		sink->track = g_value_get_pointer (value);
		break;
	case PROP_FOLDER_PATH:
		path = g_value_get_pointer (value);
		sink->folder_path = g_strdupv (path);
		break;
	case PROP_DEVICE_THREAD:
		sink->device_thread = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_sink_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBMTPSink *sink = RB_MTP_SINK (object);

	switch (prop_id) {
	case PROP_MTP_TRACK:
		g_value_set_pointer (value, sink->track);
		break;
	case PROP_FOLDER_PATH:
		g_value_set_pointer (value, sink->folder_path);
		break;
	case PROP_DEVICE_THREAD:
		g_value_set_object (value, sink->device_thread);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_sink_dispose (GObject *object)
{
	RBMTPSink *sink;
	sink = RB_MTP_SINK (object);

	if (sink->ghostpad) {
		gst_object_unref (sink->ghostpad);
		sink->ghostpad = NULL;
	}

	if (sink->fdsink) {
		gst_object_unref (sink->fdsink);
		sink->fdsink = NULL;
	}

	if (sink->device_thread) {
		g_object_unref (sink->device_thread);
		sink->device_thread = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rb_mtp_sink_finalize (GObject *object)
{
	RBMTPSink *sink;
	sink = RB_MTP_SINK (object);

	g_mutex_free (sink->upload_mutex);
	g_cond_free (sink->upload_cond);

	if (sink->upload_error) {
		g_error_free (sink->upload_error);
	}

	if (sink->folder_path) {
		g_strfreev (sink->folder_path);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_mtp_sink_class_init (RBMTPSinkClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;
	GstBinClass *bin_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->dispose = rb_mtp_sink_dispose;
	gobject_class->finalize = rb_mtp_sink_finalize;
	gobject_class->set_property = rb_mtp_sink_set_property;
	gobject_class->get_property = rb_mtp_sink_get_property;

	element_class = GST_ELEMENT_CLASS (klass);
	element_class->change_state = rb_mtp_sink_change_state;
	
	bin_class = GST_BIN_CLASS (klass);
	bin_class->handle_message = rb_mtp_sink_handle_message;

	g_object_class_install_property (gobject_class,
					 PROP_MTP_TRACK,
					 g_param_spec_pointer ("mtp-track",
						 	       "libmtp track",
							       "libmtp track",
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_FOLDER_PATH,
					 g_param_spec_pointer ("folder-path",
							       "folder path",
							       "upload folder path",
							       G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_DEVICE_THREAD,
					 g_param_spec_object ("device-thread",
							      "device-thread",
							      "device handling thread",
							      RB_TYPE_MTP_THREAD,
							      G_PARAM_READWRITE));
}


/* URI handler interface */

static guint
rb_mtp_sink_uri_get_type (void)
{
	return GST_URI_SINK;
}

static gchar **
rb_mtp_sink_uri_get_protocols (void)
{
	static gchar *protocols[] = {"xrbmtp", NULL};
	return protocols;
}

static const gchar *
rb_mtp_sink_uri_get_uri (GstURIHandler *handler)
{
	/* more or less */
	return "xrbmtp://";
}

static gboolean
rb_mtp_sink_uri_set_uri (GstURIHandler *handler, const gchar *uri)
{
	RBMTPSink *sink = RB_MTP_SINK (handler);

	if (GST_STATE (sink) == GST_STATE_PLAYING || GST_STATE (sink) == GST_STATE_PAUSED) {
		return FALSE;
	}

	if (g_str_has_prefix (uri, "xrbmtp://") == FALSE) {
		return FALSE;
	}

	/* URI doesn't actually contain any information, it all comes from the track */

	return TRUE;
}

static void
rb_mtp_sink_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_mtp_sink_uri_get_type;
	iface->get_protocols = rb_mtp_sink_uri_get_protocols;
	iface->get_uri = rb_mtp_sink_uri_get_uri;
	iface->set_uri = rb_mtp_sink_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbmtpsink", GST_RANK_PRIMARY, RB_TYPE_MTP_SINK);
	return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
			  GST_VERSION_MINOR,
			  "rbmtpsink",
			  "element to upload files to MTP devices",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");
