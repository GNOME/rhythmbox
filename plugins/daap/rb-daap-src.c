/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Implementatin of DAAP (iTunes Music Sharing) GStreamer source
 *
 *  Copyright (C) 2005 Charles Schmidt <cschmidt2@emich.edu>
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

#include <gst/gst.h>
#include <string.h>

#include "rb-daap-plugin.h"
#include "rb-daap-src.h"

#define RB_TYPE_DAAP_SRC (rb_daap_src_get_type())
#define RB_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_DAAP_SRC,RBDAAPSrc))
#define RB_DAAP_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_DAAP_SRC,RBDAAPSrcClass))
#define RB_IS_DAAP_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_DAAP_SRC))
#define RB_IS_DAAP_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_DAAP_SRC))

typedef struct _RBDAAPSrc RBDAAPSrc;
typedef struct _RBDAAPSrcClass RBDAAPSrcClass;

struct _RBDAAPSrc
{
	GstBin parent;

	/* uri */
	gchar *daap_uri;

	GstElement *souphttpsrc;
	GstPad *ghostpad;
};

struct _RBDAAPSrcClass
{
	GstBinClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

GST_DEBUG_CATEGORY_STATIC (rb_daap_src_debug);
#define GST_CAT_DEFAULT rb_daap_src_debug

static RBDaapPlugin *daap_plugin = NULL;

static void rb_daap_src_uri_handler_init (gpointer g_iface, gpointer iface_data);

#define RB_DAAP_SRC_CATEGORY_INIT GST_DEBUG_CATEGORY_INIT (rb_daap_src_debug, \
				 "daapsrc", GST_DEBUG_FG_WHITE, \
				 "Rhythmbox built in DAAP source element");

G_DEFINE_TYPE_WITH_CODE (RBDAAPSrc, rb_daap_src, GST_TYPE_BIN,
	G_IMPLEMENT_INTERFACE(GST_TYPE_URI_HANDLER, rb_daap_src_uri_handler_init);
	RB_DAAP_SRC_CATEGORY_INIT);

static void rb_daap_src_dispose (GObject *object);
static void rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec);
static void rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec);

static gboolean plugin_init (GstPlugin *plugin);

static GstStateChangeReturn rb_daap_src_change_state (GstElement *element, GstStateChange transition);

void
rb_daap_src_set_plugin (GObject *plugin)
{
	g_assert (RB_IS_DAAP_PLUGIN (plugin));
	daap_plugin = RB_DAAP_PLUGIN (plugin);
}

enum
{
	PROP_0,
	PROP_LOCATION,
};

static void
rb_daap_src_class_init (RBDAAPSrcClass *klass)
{
	GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
	GstElementClass *element_class = GST_ELEMENT_CLASS (klass);

	gobject_class->dispose = rb_daap_src_dispose;
	gobject_class->set_property = rb_daap_src_set_property;
	gobject_class->get_property = rb_daap_src_get_property;

	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_metadata (element_class, "RBDAAP Source",
		"Source/File",
		"Read a DAAP (music share) file",
		"Charles Schmidt <cschmidt2@emich.edu");

	element_class->change_state = rb_daap_src_change_state;

	g_object_class_install_property (gobject_class, PROP_LOCATION,
			g_param_spec_string ("location",
					     "file location",
					     "location of the file to read",
					     NULL,
					     G_PARAM_READWRITE));
}

static void
rb_daap_src_init (RBDAAPSrc *src)
{
	GstPad *pad;

	/* create actual source */
	src->souphttpsrc = gst_element_factory_make ("souphttpsrc", NULL);
	if (src->souphttpsrc == NULL) {
		g_warning ("couldn't create souphttpsrc element");
		return;
	}

	gst_bin_add (GST_BIN (src), src->souphttpsrc);
	gst_object_ref (src->souphttpsrc);

	/* create ghost pad */
	pad = gst_element_get_static_pad (src->souphttpsrc, "src");
	src->ghostpad = gst_ghost_pad_new ("src", pad);
	gst_element_add_pad (GST_ELEMENT (src), src->ghostpad);
	gst_object_ref (src->ghostpad);
	gst_object_unref (pad);

	src->daap_uri = NULL;
}

static void
rb_daap_src_dispose (GObject *object)
{
	RBDAAPSrc *src;
	src = RB_DAAP_SRC (object);

	if (src->ghostpad) {
		gst_object_unref (src->ghostpad);
		src->ghostpad = NULL;
	}

	if (src->souphttpsrc) {
		gst_object_unref (src->souphttpsrc);
		src->souphttpsrc = NULL;
	}

	g_free (src->daap_uri);
	src->daap_uri = NULL;

	G_OBJECT_CLASS (rb_daap_src_parent_class)->dispose (object);
}

static void
rb_daap_src_set_property (GObject *object,
			  guint prop_id,
			  const GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
			/* XXX check stuff */
			if (src->daap_uri) {
				g_free (src->daap_uri);
				src->daap_uri = NULL;
			}
			src->daap_uri = g_strdup (g_value_get_string (value));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_src_get_property (GObject *object,
		          guint prop_id,
			  GValue *value,
			  GParamSpec *pspec)
{
	RBDAAPSrc *src = RB_DAAP_SRC (object);

	switch (prop_id) {
		case PROP_LOCATION:
			g_value_set_string (value, src->daap_uri);
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
			break;
	}
}

static void
rb_daap_src_set_header (const char *name, const char *value, gpointer headers)
{
	gst_structure_set (headers, name, G_TYPE_STRING, value, NULL);
}

static GstStructure *
rb_daap_src_soup_message_headers_to_gst_structure (SoupMessageHeaders *headers)
{
	GstStructure *gst_headers = gst_structure_new ("extra-headers", NULL, NULL);

	if (gst_headers == NULL)
		return gst_headers;

	soup_message_headers_foreach (headers,
				      rb_daap_src_set_header,
				      gst_headers);

	return gst_headers;
}

GstStateChangeReturn
rb_daap_src_change_state (GstElement *element, GstStateChange transition)
{
	RBDAAPSrc *src = RB_DAAP_SRC (element);

	switch (transition) {
		case GST_STATE_CHANGE_NULL_TO_READY:
		{
			const char *http = "http";
			char *httpuri;
			SoupMessageHeaders *headers;
			GstStructure *gst_headers;
			RBDAAPSource *source;

			/* Retrieve extra headers for the HTTP connection. */
			source = rb_daap_plugin_find_source_for_uri (daap_plugin, src->daap_uri);
			if (source == NULL) {
				g_warning ("Unable to lookup source for URI: %s", src->daap_uri);
				return GST_STATE_CHANGE_FAILURE;
			}

			/* The following can fail if the source is no longer connected */
			headers = rb_daap_source_get_headers (source, src->daap_uri);
			if (headers == NULL) {
				return GST_STATE_CHANGE_FAILURE;
			}

			gst_headers = rb_daap_src_soup_message_headers_to_gst_structure
					(headers);
			if (gst_headers == NULL) {
				return GST_STATE_CHANGE_FAILURE;
			}
			soup_message_headers_unref (headers);

			g_object_set (src->souphttpsrc, "extra-headers", gst_headers, NULL);
			gst_structure_free (gst_headers);

			/* Set daap://... URI as http:// on souphttpsrc to ready connection. */
			httpuri = g_strdup (src->daap_uri);
			memcpy (httpuri, http, 4);

			g_object_set (src->souphttpsrc, "location", httpuri, NULL);
			g_free (httpuri);
			break;
		}

		case GST_STATE_CHANGE_READY_TO_PAUSED:
			break;

		case GST_STATE_CHANGE_PAUSED_TO_PLAYING:
			break;

		default:
			break;
	}

	return GST_ELEMENT_CLASS (rb_daap_src_parent_class)->change_state (element, transition);
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbdaapsrc", GST_RANK_PRIMARY, RB_TYPE_DAAP_SRC);
	return ret;
}


gboolean
rb_register_gst_plugin (void)
{
	gboolean ret = gst_plugin_register_static (GST_VERSION_MAJOR,
				    GST_VERSION_MINOR,
				    "rbdaap",
				    "element to access DAAP music share files",
				    plugin_init,
				    VERSION,
				    "GPL",
				    "",
				    PACKAGE,
				    "");
	return ret;
}

/*** GSTURIHANDLER INTERFACE *************************************************/

static GstURIType
rb_daap_src_uri_get_type (GType type)
{
	return GST_URI_SRC;
}

static const gchar * const *
rb_daap_src_uri_get_protocols (GType type)
{
	static const gchar * const protocols[] = {"daap", NULL};

	return protocols;
}

static gchar *
rb_daap_src_uri_get_uri (GstURIHandler *handler)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	return g_strdup (src->daap_uri);
}

static gboolean
rb_daap_src_uri_set_uri (GstURIHandler *handler,
			 const gchar *uri,
			 GError **error)
{
	RBDAAPSrc *src = RB_DAAP_SRC (handler);

	if (GST_STATE (src) == GST_STATE_PLAYING || GST_STATE (src) == GST_STATE_PAUSED) {
		return FALSE;
	}

	g_object_set (G_OBJECT (src), "location", uri, NULL);

	return TRUE;
}

static void
rb_daap_src_uri_handler_init (gpointer g_iface,
			      gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_daap_src_uri_get_type;
	iface->get_protocols = rb_daap_src_uri_get_protocols;
	iface->get_uri = rb_daap_src_uri_get_uri;
	iface->set_uri = rb_daap_src_uri_set_uri;
}
