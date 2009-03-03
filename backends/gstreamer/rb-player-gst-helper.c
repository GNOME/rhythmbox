/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2009 Bastien Nocera <hadess@hadess.net>
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

#include <gst/gst.h>

#include <rb-player-gst-helper.h>
#include <rb-debug.h>

GstElement *
rb_player_gst_try_audio_sink (const char *plugin_name, const char *name)
{
	GstElement *audio_sink;

	audio_sink = gst_element_factory_make (plugin_name, name);
	if (audio_sink == NULL)
		return NULL;

	/* Assume the fakesink will work */
	if (g_str_equal (plugin_name, "fakesink")) {
		g_object_set (audio_sink, "sync", TRUE, NULL);
		return audio_sink;
	}

	if (audio_sink) {
		GstStateChangeReturn ret;
		GstBus *bus;

		/* use the 'music and movies' profile for gconfaudiosink */
		if (strcmp (plugin_name, "gconfaudiosink") == 0 &&
		    g_object_class_find_property (G_OBJECT_GET_CLASS (audio_sink), "profile")) {
			rb_debug ("setting profile property on gconfaudiosink");
			g_object_set (audio_sink, "profile", 1, NULL);
		}

		/* need to set bus explicitly as it's not in a bin yet and
		 * we need one to catch error messages */
		bus = gst_bus_new ();
		gst_element_set_bus (audio_sink, bus);

		/* state change NULL => READY should always be synchronous */
		ret = gst_element_set_state (audio_sink, GST_STATE_READY);
		gst_element_set_bus (audio_sink, NULL);

		if (ret == GST_STATE_CHANGE_FAILURE) {
			/* doesn't work, drop this audio sink */
			rb_debug ("audio sink %s failed to change to READY state", plugin_name);
			gst_element_set_state (audio_sink, GST_STATE_NULL);
			gst_object_unref (audio_sink);
			audio_sink = NULL;
		} else {
			rb_debug ("audio sink %s changed to READY state successfully", plugin_name);
		}
		gst_object_unref (bus);
	}

	return audio_sink;
}

static gint
find_property_element (GstElement *element, const char *property)
{
	gint res = 1;
	char *name = gst_element_get_name (element);

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (element), property) != NULL) {
		rb_debug ("found property \"%s\" on element %s", property, name);
		return 0;
	} else {
		rb_debug ("didn't find property \"%s\" on element %s", property, name);
		g_object_unref (element);
	}
		
	g_free (name);
	return res;
}

GstElement *
rb_player_gst_find_element_with_property (GstElement *element, const char *property)
{
	GstIterator *iter;
	GstElement *result;

	if (GST_IS_BIN (element) == FALSE) {
		if (g_object_class_find_property (G_OBJECT_GET_CLASS (element),
						  property) != NULL) {
			return g_object_ref (element);
		}
		return NULL;
	}

	rb_debug ("iterating bin looking for property %s", property);
	iter = gst_bin_iterate_recurse (GST_BIN (element));
	result = gst_iterator_find_custom (iter,
					   (GCompareFunc) find_property_element,
					   (gpointer) property);
	gst_iterator_free (iter);
	return result;
}
