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

#define RB_TYPE_MTP_SRC (rb_mtp_src_get_type())
#define RB_MTP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_MTP_SRC,RBMTPSrc))
#define RB_MTP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_MTP_SRC,RBMTPSrcClass))
#define RB_IS_MTP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_MTP_SRC))
#define RB_IS_MTP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_MTP_SRC))

typedef struct _RBMTPSrc RBMTPSrc;
typedef struct _RBMTPSrcClass RBMTPSrcClass;

struct _RBMTPSrc
{
	GstBin parent;

	RBMtpThread *device_thread;

	char *track_uri;
	uint32_t track_id;
	char *tempfile;

	GstElement *filesrc;
	GstPad *ghostpad;

	GError *download_error;
	GMutex *download_mutex;
	GCond *download_cond;
	GstStateChangeReturn download_result;
};

struct _RBMTPSrcClass
{
	GstBinClass parent_class;
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

static GstElementDetails rb_mtp_src_details =
GST_ELEMENT_DETAILS ("RB MTP Source",
	"Source/File",
	"Downloads and plays files from MTP devices",
	"Jonathan Matthew <jonathan@d14n.org>");


GType rb_mtp_src_get_type (void);
static void rb_mtp_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType mtp_src_type)
{
	static const GInterfaceInfo urihandler_info = {
		rb_mtp_src_uri_handler_init,
		NULL,
		NULL
	};

	g_type_add_interface_static (mtp_src_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

GST_BOILERPLATE_FULL (RBMTPSrc, rb_mtp_src, GstBin, GST_TYPE_BIN, _do_init);

static void
rb_mtp_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details (element_class, &rb_mtp_src_details);
}

static void
rb_mtp_src_init (RBMTPSrc *src, RBMTPSrcClass *klass)
{
	GstPad *pad;

	src->download_mutex = g_mutex_new ();
	src->download_cond = g_cond_new ();

	/* create actual source */
	src->filesrc = gst_element_factory_make ("filesrc", NULL);
	if (src->filesrc == NULL) {
		g_warning ("couldn't create filesrc element");
		return;
	}

	gst_bin_add (GST_BIN (src), src->filesrc);
	gst_object_ref (src->filesrc);

	/* create ghost pad */
	pad = gst_element_get_pad (src->filesrc, "src");
	src->ghostpad = gst_ghost_pad_new ("src", pad);
	gst_element_add_pad (GST_ELEMENT (src), src->ghostpad);
	gst_object_ref (src->ghostpad);
	gst_object_unref (pad);
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
	return TRUE;
}

static void
download_cb (LIBMTP_track_t *track, const char *filename, GError *error, RBMTPSrc *src)
{
	rb_debug ("mtp download callback for %s: %s", filename, error ? error->message : "OK");
	g_mutex_lock (src->download_mutex);

	if (filename == NULL) {
		src->download_error = g_error_copy (error);
		src->download_result = GST_STATE_CHANGE_FAILURE;
	} else {
		src->download_result = GST_STATE_CHANGE_SUCCESS;
		src->tempfile = g_strdup (filename);
	}

	g_cond_signal (src->download_cond);
	g_mutex_unlock (src->download_mutex);
}

static GstStateChangeReturn
rb_mtp_src_get_file (RBMTPSrc *src)
{
	g_mutex_lock (src->download_mutex);
	src->download_result = GST_STATE_CHANGE_ASYNC;
	rb_mtp_thread_download_track (src->device_thread, src->track_id, "", (RBMtpDownloadCallback)download_cb, g_object_ref (src), g_object_unref);

	while (src->download_result == GST_STATE_CHANGE_ASYNC) {
		g_cond_wait (src->download_cond, src->download_mutex);
	}
	g_mutex_unlock (src->download_mutex);
	rb_debug ("download completed, state change return %s", gst_element_state_change_return_get_name (src->download_result));

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
	} else if (src->download_result == GST_STATE_CHANGE_SUCCESS) {
		g_object_set (src->filesrc, "location", src->tempfile, NULL);
	}
	return src->download_result;
}

static GstStateChangeReturn
rb_mtp_src_close_tempfile (RBMTPSrc *src)
{
	if (src->tempfile != NULL) {
		rb_debug ("deleting tempfile %s", src->tempfile);
		remove (src->tempfile);
		g_free (src->tempfile);
		src->tempfile = NULL;
	}

	return GST_STATE_CHANGE_SUCCESS;
}

static GstStateChangeReturn
rb_mtp_src_change_state (GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret = GST_STATE_CHANGE_SUCCESS;
	RBMTPSrc *src = RB_MTP_SRC (element);

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
			ret = rb_mtp_src_get_file (src);
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
			ret = rb_mtp_src_close_tempfile (src);
			break;
		default:
			break;
	}

	return ret;
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

	if (src->ghostpad) {
		gst_object_unref (src->ghostpad);
		src->ghostpad = NULL;
	}

	if (src->filesrc) {
		gst_object_unref (src->filesrc);
		src->filesrc = NULL;
	}

	if (src->device_thread) {
		g_object_unref (src->device_thread);
		src->device_thread = NULL;
	}

	G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
rb_mtp_src_finalize (GObject *object)
{
	RBMTPSrc *src;
	src = RB_MTP_SRC (object);

	g_mutex_free (src->download_mutex);
	g_cond_free (src->download_cond);

	if (src->download_error) {
		g_error_free (src->download_error);
	}

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_mtp_src_class_init (RBMTPSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->dispose = rb_mtp_src_dispose;
	gobject_class->finalize = rb_mtp_src_finalize;
	gobject_class->set_property = rb_mtp_src_set_property;
	gobject_class->get_property = rb_mtp_src_get_property;

	element_class = GST_ELEMENT_CLASS (klass);
	element_class->change_state = rb_mtp_src_change_state;

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
							      RB_TYPE_MTP_THREAD,
							      G_PARAM_READWRITE));
}


/* URI handler interface */

static guint
rb_mtp_src_uri_get_type (void)
{
	return GST_URI_SRC;
}

static gchar **
rb_mtp_src_uri_get_protocols (void)
{
	static gchar *protocols[] = {"xrbmtp", NULL};
	return protocols;
}

static const gchar *
rb_mtp_src_uri_get_uri (GstURIHandler *handler)
{
	RBMTPSrc *src = RB_MTP_SRC (handler);

	return src->track_uri;
}

static gboolean
rb_mtp_src_uri_set_uri (GstURIHandler *handler, const gchar *uri)
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
rb_mtp_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_mtp_src_uri_get_type;
	iface->get_protocols = rb_mtp_src_uri_get_protocols;
	iface->get_uri = rb_mtp_src_uri_get_uri;
	iface->set_uri = rb_mtp_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbmtpsrc", GST_RANK_PRIMARY, RB_TYPE_MTP_SRC);
	return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
			  GST_VERSION_MINOR,
			  "rbmtpsrc",
			  "element to download and play files from MTP devices",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");
