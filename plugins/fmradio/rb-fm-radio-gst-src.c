/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2007  James Henstridge <james@jamesh.id.au>
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

#include "rb-debug.h"

#include <gst/gst.h>

#define RB_TYPE_FM_RADIO_SRC (rb_fm_radio_src_get_type())
#define RB_FM_RADIO_SRC(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj),RB_TYPE_FM_RADIO_SRC,RBFMRadioSrc))
#define RB_FM_RADIO_SRC_CLASS(klass) (G_TYPE_CHECK_CLASS_CAST((klass),RB_TYPE_FM_RADIO_SRC,RBFMRadioSrcClass))
#define RB_IS_LASTFM_SRC(obj) (G_TYPE_CHECK_INSTANCE_TYPE((obj),RB_TYPE_FM_RADIO_SRC))
#define RB_IS_LASTFM_SRC_CLASS(obj) (G_TYPE_CHECK_CLASS_TYPE((klass),RB_TYPE_FM_RADIO_SRC))

typedef struct _RBFMRadioSrc RBFMRadioSrc;
typedef struct _RBFMRadioSrcClass RBFMRadioSrcClass;

struct _RBFMRadioSrc
{
	GstBin parent;

	GstElement *audiotestsrc;
	GstPad *ghostpad;
};

struct _RBFMRadioSrcClass
{
	GstBinClass parent_class;
};

static GstStaticPadTemplate srctemplate = GST_STATIC_PAD_TEMPLATE ("src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS_ANY);

GType rb_fm_radio_src_get_type (void);
static void rb_fm_radio_src_uri_handler_init (gpointer g_iface,
					      gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (RBFMRadioSrc,
			 rb_fm_radio_src,
			 GST_TYPE_BIN,
			 G_IMPLEMENT_INTERFACE (GST_TYPE_URI_HANDLER,
						rb_fm_radio_src_uri_handler_init));

static void
rb_fm_radio_src_init (RBFMRadioSrc *src)
{
	GstPad *pad;

	rb_debug ("creating rb silence src element");

	src->audiotestsrc = gst_element_factory_make ("audiotestsrc", NULL);
	gst_bin_add (GST_BIN (src), src->audiotestsrc);
	gst_object_ref (src->audiotestsrc);

	/* set the audiotestsrc to generate silence (wave type 4) */
	g_object_set (src->audiotestsrc,
		      "wave", 4,
		      NULL);

	pad = gst_element_get_static_pad (src->audiotestsrc, "src");
	src->ghostpad = gst_ghost_pad_new ("src", pad);
	gst_element_add_pad (GST_ELEMENT (src), src->ghostpad);
	gst_object_ref (src->ghostpad);
	gst_object_unref (pad);
	
}

static void
rb_fm_radio_src_finalize (GObject *object)
{
	RBFMRadioSrc *src = RB_FM_RADIO_SRC (object);

	if (src->ghostpad)
		gst_object_unref (src->ghostpad);
	if (src->audiotestsrc)
		gst_object_unref (src->audiotestsrc);

	G_OBJECT_CLASS (rb_fm_radio_src_parent_class)->finalize (object);
}

static void
rb_fm_radio_src_class_init (RBFMRadioSrcClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;

	object_class = G_OBJECT_CLASS (klass);
	object_class->finalize = rb_fm_radio_src_finalize;

	element_class = GST_ELEMENT_CLASS (klass);
	gst_element_class_add_pad_template (element_class,
		gst_static_pad_template_get (&srctemplate));
	gst_element_class_set_details_simple (element_class,
					      "RB Silence Source",
					      "Source/File",
					      "Outputs buffers of silence",
					      "James Henstridge <james@jamesh.id.au>");
}


/* URI handler interface */

static guint
rb_fm_radio_src_uri_get_type (GType type)
{
	return GST_URI_SRC;
}

static const gchar *const *
rb_fm_radio_src_uri_get_protocols (GType type)
{
	static const gchar *protocols[] = {"xrbsilence", NULL};
	return protocols;
}

static gchar *
rb_fm_radio_src_uri_get_uri (GstURIHandler *handler)
{
	return g_strdup ("xrbsilence:///");
}

static gboolean
rb_fm_radio_src_uri_set_uri (GstURIHandler *handler, const gchar *uri, GError **error)
{
	if (g_str_has_prefix (uri, "xrbsilence://") == FALSE)
		return FALSE;

	return TRUE;
}

static void
rb_fm_radio_src_uri_handler_init (gpointer g_iface, gpointer iface_data)
{
	GstURIHandlerInterface *iface = (GstURIHandlerInterface *) g_iface;

	iface->get_type = rb_fm_radio_src_uri_get_type;
	iface->get_protocols = rb_fm_radio_src_uri_get_protocols;
	iface->get_uri = rb_fm_radio_src_uri_get_uri;
	iface->set_uri = rb_fm_radio_src_uri_set_uri;
}

static gboolean
plugin_init (GstPlugin *plugin)
{
	gboolean ret = gst_element_register (plugin, "rbsilencesrc", GST_RANK_PRIMARY, RB_TYPE_FM_RADIO_SRC);
	return ret;
}

GST_PLUGIN_DEFINE (GST_VERSION_MAJOR,
		   GST_VERSION_MINOR,
		   rbsilencesrc,
		   "element to output silence",
		   plugin_init,
		   VERSION,
		   "GPL",
		   PACKAGE,
		   "");
