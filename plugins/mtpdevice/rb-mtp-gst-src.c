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
#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdlib.h>

#include <glib/gi18n.h>
#include <libmtp.h>
#include <gst/gst.h>
#include <gst/base/gstbasesrc.h>

#include "rb-mtp-thread.h"
#include "rb-debug.h"
#include "rb-file-helpers.h"

#define RB_TYPE_MTP_SRC (rb_mtp_src_get_type())
#define RB_MTP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_MTP_SRC,RBMTPSrc))
#define RB_MTP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_MTP_SRC,RBMTPSrcClass))
#define RB_IS_MTP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_MTP_SRC))
#define RB_IS_MTP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_MTP_SRC))

typedef struct _RBMTPSrc RBMTPSrc;
typedef struct _RBMTPSrcClass RBMTPSrcClass;

struct _RBMTPSrc
{
	GstBaseSrc parent;

	RBMtpThread *device_thread;

	char *track_uri;
	uint32_t track_id;
	char *tempfile;
	int fd;
	guint64 read_position;

	GError *download_error;
	GMutex download_mutex;
	GCond download_cond;
	gboolean download_done;
};

struct _RBMTPSrcClass
{
	GstBaseSrcClass parent_class;
};

enum
{
	PROP_0,
	PROP_URI,
	PROP_DEVICE_THREAD
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

GType rb_mtp_src_get_type (void);
static void uri_handler_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (RBMTPSrc,
			 rb_mtp_src,
			 GST_TYPE_BASE_SRC,
			 G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
						uri_handler_init));

static void
rb_mtp_src_init (RBMTPSrc *src)
{
}

static gboolean
rb_mtp_src_set_uri (RBMTPSrc *src, const char *uri)
{
	const char *trackid;

	rb_debug ("stream uri: %s", uri);

	src->track_uri = g_strdup (uri);

	/* extract the track ID */
	if (g_str_has_prefix (uri, "xrbmtp://") == FALSE) {
		rb_debug ("unexpected uri scheme");
		return FALSE;
	}
	trackid = uri + strlen ("xrbmtp://");
	src->track_id = strtoul (trackid, NULL, 0);

	/* delete any existing file */
	if (src->tempfile != NULL) {
		rb_debug ("deleting tempfile %s", src->tempfile);
		remove (src->tempfile);
		g_free (src->tempfile);
		src->tempfile = NULL;
	}

	return TRUE;
}

static GstFlowReturn
rb_mtp_src_fill (GstBaseSrc *basesrc, guint64 offset, guint length, GstBuffer *buf)
{
	RBMTPSrc *src = RB_MTP_SRC (basesrc);
	GstMapInfo mapinfo;
	int ret;

	/* seek if required */
	if (offset != src->read_position) {
		off_t res;
		res = lseek (src->fd, offset, SEEK_SET);
		if (res < 0 || res != offset) {
			GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
			return GST_FLOW_ERROR;
		}

		src->read_position = offset;
	}

	if (length > 0) {
		gst_buffer_map (buf, &mapinfo, GST_MAP_WRITE);
		ret = read (src->fd, mapinfo.data, length);
		if (ret < length) {
			GST_ELEMENT_ERROR (src, RESOURCE, READ, (NULL), GST_ERROR_SYSTEM);
			gst_buffer_unmap (buf, &mapinfo);
			gst_buffer_unref (buf);
			return GST_FLOW_ERROR;
		}

		gst_buffer_unmap (buf, &mapinfo);
		gst_buffer_resize (buf, 0, length);
		GST_BUFFER_OFFSET (buf) = offset;
		GST_BUFFER_OFFSET_END (buf) = offset + length;

		src->read_position += length;
	}

	return GST_FLOW_OK;
}

static gboolean
rb_mtp_src_is_seekable (GstBaseSrc *basesrc)
{
	return TRUE;
}

static gboolean
rb_mtp_src_get_size (GstBaseSrc *basesrc, guint64 *size)
{
	RBMTPSrc *src = RB_MTP_SRC (basesrc);
	struct stat stat_results;

	if (fstat (src->fd, &stat_results) < 0) {
		return FALSE;
	}

	*size = stat_results.st_size;
	return TRUE;
}

static void
download_cb (LIBMTP_track_t *track, const char *filename, GError *error, RBMTPSrc *src)
{
	rb_debug ("mtp download callback for %s: %s", filename, error ? error->message : "OK");
	g_mutex_lock (&src->download_mutex);

	if (filename == NULL) {
		src->download_error = g_error_copy (error);
	} else {
		src->tempfile = g_strdup (filename);
	}
	src->download_done = TRUE;

	g_cond_signal (&src->download_cond);
	g_mutex_unlock (&src->download_mutex);
}

static gboolean
rb_mtp_src_start (GstBaseSrc *basesrc)
{
	RBMTPSrc *src = RB_MTP_SRC (basesrc);

	if (src->device_thread == NULL) {
		rb_debug ("no thread yet");
		return FALSE;
	}

	/* download the file, if we haven't already */
	if (src->tempfile == NULL) {
		g_mutex_lock (&src->download_mutex);
		src->download_done = FALSE;
		rb_mtp_thread_download_track (src->device_thread,
					      src->track_id,
					      "",
					      (RBMtpDownloadCallback)download_cb,
					      g_object_ref (src),
					      g_object_unref);

		while (src->download_done == FALSE) {
			g_cond_wait (&src->download_cond, &src->download_mutex);
		}
		g_mutex_unlock (&src->download_mutex);
		rb_debug ("download finished");

		if (src->download_error) {
			int code;
			switch (src->download_error->code) {
			case RB_MTP_THREAD_ERROR_NO_SPACE:
				code = GST_RESOURCE_ERROR_NO_SPACE_LEFT;
				break;

			case RB_MTP_THREAD_ERROR_TEMPFILE:
				code = GST_RESOURCE_ERROR_OPEN_WRITE;
				break;

			default:
			case RB_MTP_THREAD_ERROR_GET_TRACK:
				code = GST_RESOURCE_ERROR_READ;
				break;

			}

			GST_WARNING_OBJECT (src, "error: %s", src->download_error->message);
			gst_element_message_full (GST_ELEMENT (src),
						  GST_MESSAGE_ERROR,
						  GST_RESOURCE_ERROR, code,
						  src->download_error->message, NULL,
						  __FILE__, GST_FUNCTION, __LINE__);
			return FALSE;
		}
	}

	/* open file - maybe do this in create after waiting for it to finish downloading */
	src->fd = open (src->tempfile, O_RDONLY, 0);
	if (src->fd < 0) {
		switch (errno) {
		case ENOENT:
			GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, (NULL),
					   ("Could not find temporary file"));
			break;
		default:
			GST_ELEMENT_ERROR (src, RESOURCE, OPEN_READ, (NULL),
					   ("Could not open temporary file for reading"));
		}
		return FALSE;
	}

	src->read_position = 0;

	return TRUE;
}

static gboolean
rb_mtp_src_stop (GstBaseSrc *basesrc)
{
	RBMTPSrc *src = RB_MTP_SRC (basesrc);
	close (src->fd);
	src->fd = 0;
	src->read_position = 0;
	return TRUE;
}

static void
rb_mtp_src_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBMTPSrc *src = RB_MTP_SRC (object);

	switch (prop_id) {
	case PROP_URI:
		rb_mtp_src_set_uri (src, g_value_get_string (value));
		break;
	case PROP_DEVICE_THREAD:
		src->device_thread = g_value_dup_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_src_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBMTPSrc *src = RB_MTP_SRC (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_string (value, src->track_uri);
		break;
	case PROP_DEVICE_THREAD:
		g_value_set_object (value, src->device_thread);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_mtp_src_dispose (GObject *object)
{
	RBMTPSrc *src;
	src = RB_MTP_SRC (object);

	if (src->device_thread) {
		g_object_unref (src->device_thread);
		src->device_thread = NULL;
	}

	G_OBJECT_CLASS (rb_mtp_src_parent_class)->dispose (object);
}

static void
rb_mtp_src_finalize (GObject *object)
{
	RBMTPSrc *src;
	src = RB_MTP_SRC (object);

	if (src->download_error) {
		g_error_free (src->download_error);
	}

	if (src->tempfile != NULL) {
		rb_debug ("deleting tempfile %s", src->tempfile);
		remove (src->tempfile);
		g_free (src->tempfile);
		src->tempfile = NULL;
	}

	G_OBJECT_CLASS (rb_mtp_src_parent_class)->finalize (object);
}

static void
rb_mtp_src_class_init (RBMTPSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstBaseSrcClass *basesrc_class;
	GstElementClass *element_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->dispose = rb_mtp_src_dispose;
	gobject_class->finalize = rb_mtp_src_finalize;
	gobject_class->set_property = rb_mtp_src_set_property;
	gobject_class->get_property = rb_mtp_src_get_property;

	basesrc_class = GST_BASE_SRC_CLASS (klass);
	basesrc_class->start = GST_DEBUG_FUNCPTR (rb_mtp_src_start);
	basesrc_class->stop = GST_DEBUG_FUNCPTR (rb_mtp_src_stop);
	basesrc_class->is_seekable = GST_DEBUG_FUNCPTR (rb_mtp_src_is_seekable);
	basesrc_class->get_size = GST_DEBUG_FUNCPTR (rb_mtp_src_get_size);
	basesrc_class->fill = GST_DEBUG_FUNCPTR (rb_mtp_src_fill);

	g_object_class_install_property (gobject_class,
					 PROP_URI,
					 g_param_spec_string ("uri",
							      "uri",
							      "MTP track uri",
							      NULL,
							      G_PARAM_READWRITE));
	g_object_class_install_property (gobject_class,
					 PROP_DEVICE_THREAD,
					 g_param_spec_object ("device-thread",
							      "device-thread",
							      "device handling thread",
							      G_TYPE_OBJECT,
							      G_PARAM_READWRITE));

	element_class = GST_ELEMENT_CLASS (klass);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details_simple (element_class,
					      "RB MTP Source",
					      "Source/File",
					      "Downloads and plays files from MTP devices",
					      "Jonathan Matthew <jonathan@d14n.org>");
}


/* URI handler interface */

static guint
rb_mtp_src_uri_get_type (GType type)
{
	return GST_URI_SRC;
}

static const gchar *const *
rb_mtp_src_uri_get_protocols (GType type)
{
	static const gchar *protocols[] = {"xrbmtp", NULL};
	return protocols;
}

static gchar *
rb_mtp_src_uri_get_uri (GstURIHandler *handler)
{
	RBMTPSrc *src = RB_MTP_SRC (handler);
	return g_strdup (src->track_uri);
}

static gboolean
rb_mtp_src_uri_set_uri (GstURIHandler *handler, const gchar *uri, GError **error)
{
	RBMTPSrc *src = RB_MTP_SRC (handler);

	if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
		return FALSE;
	}

	if (g_str_has_prefix (uri, "xrbmtp://") == FALSE) {
		return FALSE;
	}

	return rb_mtp_src_set_uri (src, uri);
}

static void
uri_handler_init (gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_mtp_src_uri_get_type;
	iface->get_protocols = rb_mtp_src_uri_get_protocols;
	iface->get_uri = rb_mtp_src_uri_get_uri;
	iface->set_uri = rb_mtp_src_uri_set_uri;
}
