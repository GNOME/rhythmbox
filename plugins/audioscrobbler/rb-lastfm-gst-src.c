/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  Jonathan Matthew  <jonathan@kaolin.wh9.net>
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

#include "config.h"

#define _GNU_SOURCE
#include <string.h>

#include "rb-debug.h"

#include <libsoup/soup.h>
#include <gst/gst.h>

#define RB_TYPE_LASTFM_SRC (rb_lastfm_src_get_type())
#define RB_LASTFM_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_LASTFM_SRC,RBLastFMSrc))
#define RB_LASTFM_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_LASTFM_SRC,RBLastFMSrcClass))
#define RB_IS_LASTFM_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_LASTFM_SRC))
#define RB_IS_LASTFM_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_LASTFM_SRC))

typedef struct _RBLastFMSrc RBLastFMSrc;
typedef struct _RBLastFMSrcClass RBLastFMSrcClass;

struct _RBLastFMSrc
{
	GstBin parent;

	char *lastfm_uri;
	GstElement *http_src;
	GstPad *ghostpad;
};

struct _RBLastFMSrcClass
{
	GstBinClass parent_class;
};

enum
{
	PROP_0,
	PROP_URI,
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

static GstElementDetails rb_lastfm_src_details =
GST_ELEMENT_DETAILS ("RB Last.FM Source",
	"Source/File",
	"Reads a last.fm radio stream",
	"Jonathan Matthew <jonathan@kaolin.wh9.net>");

static void rb_lastfm_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

static void
_do_init (GType lastfm_src_type)
{
	static const GInterfaceInfo urihandler_info = {
		rb_lastfm_src_uri_handler_init,
		NULL,
		NULL
	};

	g_type_add_interface_static (lastfm_src_type, GST_TYPE_URI_HANDLER,
			&urihandler_info);
}

GST_BOILERPLATE_FULL (RBLastFMSrc, rb_lastfm_src, GstBin, GST_TYPE_BIN, _do_init);

static void
rb_lastfm_src_base_init (gpointer g_class)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS (g_class);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details (element_class, &rb_lastfm_src_details);
}

static void
rb_lastfm_src_init (RBLastFMSrc *src, RBLastFMSrcClass *klass)
{
	rb_debug ("creating rb last.fm src element");
}

static gboolean
rb_lastfm_src_pad_probe_cb (GstPad *pad, GstBuffer *buffer, RBLastFMSrc *src)
{
	guint8 *data = GST_BUFFER_DATA (buffer);
	char *s;
	static char sync[5] = "SYNC";

	s = memmem (data, GST_BUFFER_SIZE (buffer), sync, 4);
	if (s != NULL) {
		GstMessage *msg;
		GstStructure *s;

		rb_debug ("got song change tag");
		s = gst_structure_new ("rb-lastfm-new-song", NULL);
		msg = gst_message_new_application (GST_OBJECT (src), s);
		gst_element_post_message (GST_ELEMENT (src), msg);
	}

	return TRUE;
}

static void
rb_lastfm_src_set_uri (RBLastFMSrc *src, const char *uri)
{
	GstPad *pad;

	rb_debug ("stream uri: %s", uri);
	g_free (src->lastfm_uri);
	src->lastfm_uri = g_strdup (uri);

	if (src->http_src) {
		gst_element_remove_pad (GST_ELEMENT (src), src->ghostpad);
		gst_object_unref (src->ghostpad);
		src->ghostpad = NULL;

		gst_bin_remove (GST_BIN (src), src->http_src);
		gst_object_unref (src->http_src);
		src->http_src = NULL;
	}

	/* create actual HTTP source */
	src->http_src = gst_element_make_from_uri (GST_URI_SRC, src->lastfm_uri, NULL);
	gst_bin_add (GST_BIN (src), src->http_src);
	gst_object_ref (src->http_src);

	/* create ghost pad */
	pad = gst_element_get_pad (src->http_src, "src");
	src->ghostpad = gst_ghost_pad_new ("src", pad);
	gst_element_add_pad (GST_ELEMENT (src), src->ghostpad);
	gst_object_ref (src->ghostpad);
	gst_object_unref (pad);

	/* attach probe */
	gst_pad_add_buffer_probe (pad, G_CALLBACK (rb_lastfm_src_pad_probe_cb), src);
}

static void
rb_lastfm_src_set_property (GObject *object,
			    guint prop_id,
			    const GValue *value,
			    GParamSpec *pspec)
{
	RBLastFMSrc *src = RB_LASTFM_SRC (object);

	switch (prop_id) {
	case PROP_URI:
		rb_lastfm_src_set_uri (src, g_value_get_string (value));
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_lastfm_src_get_property (GObject *object,
			    guint prop_id,
			    GValue *value,
			    GParamSpec *pspec)
{
	RBLastFMSrc *src = RB_LASTFM_SRC (object);

	switch (prop_id) {
	case PROP_URI:
		g_value_set_string (value, src->lastfm_uri);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
rb_lastfm_src_finalize (GObject *object)
{
	RBLastFMSrc *src;
	src = RB_LASTFM_SRC (object);

	g_free (src->lastfm_uri);

	if (src->ghostpad)
		gst_object_unref (src->ghostpad);

	if (src->http_src)
		gst_object_unref (src->http_src);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_lastfm_src_class_init (RBLastFMSrcClass *klass)
{
	GObjectClass *gobject_class;
	GstElementClass *element_class;

	gobject_class = G_OBJECT_CLASS (klass);
	gobject_class->finalize = rb_lastfm_src_finalize;
	gobject_class->set_property = rb_lastfm_src_set_property;
	gobject_class->get_property = rb_lastfm_src_get_property;

	element_class = GST_ELEMENT_CLASS (klass);

	g_object_class_install_property (gobject_class,
					 PROP_URI,
					 g_param_spec_string ("uri",
						 	      "uri",
							      "last.fm stream uri",
							      NULL,
							      G_PARAM_READWRITE));
}


/* URI handler interface */

static guint
rb_lastfm_src_uri_get_type (void)
{
	return GST_URI_SRC;
}

static gchar **
rb_lastfm_src_uri_get_protocols (void)
{
	static gchar *protocols[] = {"lastfm", NULL};
	return protocols;
}

static const gchar *
rb_lastfm_src_uri_get_uri (GstURIHandler *handler)
{
	RBLastFMSrc *src = RB_LASTFM_SRC (handler);

	return src->lastfm_uri;
}

static gboolean
rb_lastfm_src_uri_set_uri (GstURIHandler *handler,
			   const gchar *uri)
{
	RBLastFMSrc *src = RB_LASTFM_SRC (handler);
	char *http_uri;

	if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
		return FALSE;
	}

	if (g_str_has_prefix (uri, "x-rb-lastfm://") == FALSE) {
		return FALSE;
	}

	http_uri = g_strdup_printf ("http://%s", uri + strlen ("x-rb-lastfm://"));
	g_object_set (src, "uri", http_uri, NULL);
	g_free (http_uri);

	return TRUE;
}

static void
rb_lastfm_src_uri_handler_init (gpointer g_iface,
				gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_lastfm_src_uri_get_type;
	iface->get_protocols = rb_lastfm_src_uri_get_protocols;
	iface->get_uri = rb_lastfm_src_uri_get_uri;
	iface->set_uri = rb_lastfm_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rblastfmsrc", GST_RANK_PRIMARY, RB_TYPE_LASTFM_SRC);
	return ret;
}

GST_PLUGIN_DEFINE_STATIC (GST_VERSION_MAJOR,
			  GST_VERSION_MINOR,
			  "rblastfm",
			  "element to access last.fm streams",
			  plugin_init,
			  VERSION,
			  "GPL",
			  PACKAGE,
			  "");
