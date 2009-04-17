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

/**
 * rb_gst_process_embedded_image:
 * @taglist:	a #GstTagList containing an image
 * @tag:	the tag name
 *
 * Converts embedded image data extracted from a tag list into
 * a #GdkPixbuf.  The returned #GdkPixbuf is owned by the caller.
 *
 * Returns: a #GdkPixbuf, or NULL.
 */
GdkPixbuf *
rb_gst_process_embedded_image (const GstTagList *taglist, const char *tag)
{
	GstBuffer *buf;
	GdkPixbufLoader *loader;
	GdkPixbuf *pixbuf;
	GError *error = NULL;
	const GValue *val;

	val = gst_tag_list_get_value_index (taglist, tag, 0);
	if (val == NULL) {
		rb_debug ("no value for tag %s in the tag list" , tag);
		return NULL;
	}

	buf = gst_value_get_buffer (val);
	if (buf == NULL) {
		rb_debug ("apparently couldn't get image buffer");
		return NULL;
	}

	/* probably should check media type?  text/uri-list won't work too well in a pixbuf loader */

	loader = gdk_pixbuf_loader_new ();
	rb_debug ("sending %d bytes to pixbuf loader", buf->size);
	if (gdk_pixbuf_loader_write (loader, buf->data, buf->size, &error) == FALSE) {
		rb_debug ("pixbuf loader doesn't like the data: %s", error->message);
		g_error_free (error);
		g_object_unref (loader);
		return NULL;
	}

	pixbuf = gdk_pixbuf_loader_get_pixbuf (loader);
	if (pixbuf != NULL) {
		g_object_ref (pixbuf);
	}
	
	gdk_pixbuf_loader_close (loader, NULL);
	g_object_unref (loader);

	if (pixbuf == NULL) {
		rb_debug ("pixbuf loader didn't give us a pixbuf");
		return NULL;
	}

	rb_debug ("returning embedded image: %d x %d / %d",
		  gdk_pixbuf_get_width (pixbuf),
		  gdk_pixbuf_get_height (pixbuf),
		  gdk_pixbuf_get_bits_per_sample (pixbuf));
	return pixbuf;
}

/**
 * rb_gst_process_tag_string:
 * @taglist:	a #GstTagList containing a string tag
 * @tag:	tag name
 * @field:	returns the #RBMetaDataField corresponding to the tag
 * @value:	returns the tag value
 *
 * Processes a tag string, determining the metadata field identifier
 * corresponding to the tag name, and converting the tag data into the
 * appropriate value type.
 *
 * Return value: %TRUE if the tag was successfully converted.
 */
gboolean
rb_gst_process_tag_string (const GstTagList *taglist,
			   const char *tag,
			   RBMetaDataField *field,
			   GValue *value)
{
	const GValue *tagval;

	if (gst_tag_list_get_tag_size (taglist, tag) < 0) {
		rb_debug ("no values in taglist for tag %s", tag);
		return FALSE;
	}

	/* only handle a few fields here */
	if (!strcmp (tag, GST_TAG_TITLE))
		*field = RB_METADATA_FIELD_TITLE;
	else if (!strcmp (tag, GST_TAG_GENRE))
		*field = RB_METADATA_FIELD_GENRE;
	else if (!strcmp (tag, GST_TAG_COMMENT))
		*field = RB_METADATA_FIELD_COMMENT;
	else if (!strcmp (tag, GST_TAG_BITRATE))
		*field = RB_METADATA_FIELD_BITRATE;
#ifdef GST_TAG_MUSICBRAINZ_TRACKID
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID))
		*field = RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
#endif
	else {
		rb_debug ("tag %s doesn't correspond to a metadata field we're interested in", tag);
		return FALSE;
	}

	/* most of the fields we care about are strings */
	switch (*field) {
	case RB_METADATA_FIELD_BITRATE:
		g_value_init (value, G_TYPE_ULONG);
		break;

	case RB_METADATA_FIELD_TITLE:
	case RB_METADATA_FIELD_GENRE:
	case RB_METADATA_FIELD_COMMENT:
	case RB_METADATA_FIELD_MUSICBRAINZ_TRACKID:
	default:
		g_value_init (value, G_TYPE_STRING);
		break;
	}

	tagval = gst_tag_list_get_value_index (taglist, tag, 0);
	if (!g_value_transform (tagval, value)) {
		rb_debug ("Could not transform tag value type %s into %s",
			  g_type_name (G_VALUE_TYPE (tagval)),
			  g_type_name (G_VALUE_TYPE (value)));
		g_value_unset (value);
		return FALSE;
	}

	return TRUE;
}

