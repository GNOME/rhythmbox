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
#include <gst/tag/tag.h>

#include <rb-player-gst-helper.h>
#include <rb-player-gst-filter.h>
#include <rb-player-gst-tee.h>
#include <rb-debug.h>

/* data structure for pipeline block-add/remove-unblock operations */
typedef struct {
	GObject *player;
	GstElement *element;
	GstElement *fixture;
} RBGstPipelineOp;

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
	else if (!strcmp (tag, GST_TAG_MUSICBRAINZ_TRACKID))
		*field = RB_METADATA_FIELD_MUSICBRAINZ_TRACKID;
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

/**
 * rb_gst_error_get_error_code:
 * @error: error received from GStreamer
 *
 * Maps a GStreamer error to an #RBPlayerError error code.
 *
 * Return value: the #RBPlayerError value to use
 */
int
rb_gst_error_get_error_code (const GError *error)
{
	if (error->domain == GST_RESOURCE_ERROR &&
	    (error->code == GST_RESOURCE_ERROR_NOT_FOUND ||
	     error->code == GST_RESOURCE_ERROR_OPEN_READ ||
	     error->code == GST_RESOURCE_ERROR_READ)) {
		return RB_PLAYER_ERROR_NOT_FOUND;
	} else if ((error->domain == GST_CORE_ERROR)
		|| (error->domain == GST_LIBRARY_ERROR)
		|| (error->domain == GST_RESOURCE_ERROR && error->code == GST_RESOURCE_ERROR_BUSY)) {
		return RB_PLAYER_ERROR_NO_AUDIO;
	} else {
		return RB_PLAYER_ERROR_GENERAL;
	}
}

/* pipeline block-add/remove-unblock operations */

static RBGstPipelineOp *
new_pipeline_op (GObject *player, GstElement *fixture, GstElement *element)
{
	RBGstPipelineOp *op;
	op = g_new0 (RBGstPipelineOp, 1);
	op->player = g_object_ref (player);
	op->fixture = gst_object_ref (fixture);
	op->element = gst_object_ref (element);
	return op;
}

static void
free_pipeline_op (RBGstPipelineOp *op)
{
	g_object_unref (op->player);
	gst_object_unref (op->element);
	gst_object_unref (op->fixture);
	g_free (op);
}

static void
pipeline_op_done (GstPad *pad, gboolean blocked, GstPad *new_pad)
{
	GstEvent *segment;
	if (new_pad == NULL)
		return;

	/* send a very unimaginative new segment through the new pad */
	segment = gst_event_new_new_segment (TRUE,
					     1.0,
					     GST_FORMAT_DEFAULT,
					     0,
					     GST_CLOCK_TIME_NONE,
					     0);
	gst_pad_send_event (new_pad, segment);
	gst_object_unref (new_pad);
}

static gboolean
pipeline_op (GObject *player,
	     GstElement *fixture,
	     GstElement *element,
	     gboolean use_pad_block,
	     GstPadBlockCallback callback)
{
	RBGstPipelineOp *op;
	GstPad *fixture_pad;
	GstPad *block_pad;

	op = new_pipeline_op (player, fixture, element);

	/* seems like we should be able to just block the src pad connected
	 * to the fixture's sink pad..
	 */
	fixture_pad = gst_element_get_static_pad (fixture, "sink");
	block_pad = gst_pad_get_peer (fixture_pad);
	gst_object_unref (fixture_pad);

	if (use_pad_block) {
		char *whatpad;
		whatpad = gst_object_get_path_string (GST_OBJECT (block_pad));
		rb_debug ("blocking pad %s to perform an operation", whatpad);
		g_free (whatpad);

		gst_pad_set_blocked_async (block_pad,
					   TRUE,
					   callback,
					   op);
	} else {
		rb_debug ("not using pad blocking, calling op directly");
		(*callback) (block_pad, FALSE, op);
	}

	gst_object_unref (block_pad);
	return TRUE;
}

/* RBPlayerGstFilter implementation */

/**
 * rb_gst_create_filter_bin:
 *
 * Creates an initial bin to use for dynamically plugging filter elements into the
 * pipeline.
 *
 * Return value: filter bin
 */
GstElement *
rb_gst_create_filter_bin ()
{
	GstElement *bin;
	GstElement *audioconvert;
	GstElement *identity;
	GstPad *pad;

	bin = gst_bin_new ("filterbin");

	audioconvert = gst_element_factory_make ("audioconvert", "filteraudioconvert");
	identity = gst_element_factory_make ("identity", "filteridentity");
	gst_bin_add_many (GST_BIN (bin), audioconvert, identity, NULL);
	gst_element_link (audioconvert, identity);

	pad = gst_element_get_static_pad (audioconvert, "sink");
	gst_element_add_pad (bin, gst_ghost_pad_new ("sink", pad));
	gst_object_unref (pad);

	pad = gst_element_get_static_pad (identity, "src");
	gst_element_add_pad (bin, gst_ghost_pad_new ("src", pad));
	gst_object_unref (pad);

	return bin;
}

static void
really_add_filter (GstPad *pad,
		   gboolean blocked,
		   RBGstPipelineOp *op)
{
	GstPad *binsinkpad;
	GstPad *binsrcpad;
	GstPad *realpad;
	GstPad *prevpad;
	GstElement *bin;
	GstElement *identity;
	GstElement *audioconvert;
	GstElement *audioconvert2;
	GstPadLinkReturn link;

	rb_debug ("adding filter %p", op->element);

	/*
	 * it kind of looks like we need audioconvert elements on either side of each filter
	 * to prevent caps renegotiation from causing 'internal data flow error' errors.
	 * this probably means we'd be doing a few unnecessary conversions when there are
	 * multiple filters in the pipeline, but at least it works.
	 */

	/* create containing bin */
	bin = gst_bin_new (NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);
	audioconvert2 = gst_element_factory_make ("audioconvert", NULL);
	gst_bin_add_many (GST_BIN (bin), audioconvert, op->element, audioconvert2, NULL);
	gst_element_link_many (audioconvert, op->element, audioconvert2, NULL);

	/* create ghost pads */
	realpad = gst_element_get_static_pad (audioconvert, "sink");
	binsinkpad = gst_ghost_pad_new ("sink", realpad);
	gst_element_add_pad (bin, binsinkpad);
	gst_object_unref (realpad);

	realpad = gst_element_get_static_pad (audioconvert2, "src");
	binsrcpad = gst_ghost_pad_new ("src", realpad);
	gst_element_add_pad (bin, binsrcpad);
	gst_object_unref (realpad);

	/* chuck it into the filter bin */
	gst_bin_add (GST_BIN (op->fixture), bin);
	identity = gst_bin_get_by_name (GST_BIN (op->fixture), "filteridentity");
	realpad = gst_element_get_static_pad (identity, "sink");
	prevpad = gst_pad_get_peer (realpad);
	gst_object_unref (identity);

	gst_pad_unlink (prevpad, realpad);

	link = gst_pad_link (prevpad, binsinkpad);
	gst_object_unref (prevpad);
	if (link != GST_PAD_LINK_OK) {
		g_warning ("couldn't link new filter into pipeline (sink): %d", link);
		/* make some attempt at cleaning up; probably won't work though */
		gst_pad_link (prevpad, realpad);
		gst_object_unref (realpad);
		gst_bin_remove (GST_BIN (op->fixture), bin);
		gst_object_unref (bin);

		free_pipeline_op (op);
		return;
	}

	link = gst_pad_link (binsrcpad, realpad);
	gst_object_unref (realpad);
	if (link != GST_PAD_LINK_OK) {
		g_warning ("couldn't link new filter into pipeline (src): %d", link);
		/* doubt we can do anything helpful here.. */
	}

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after adding filter");
		gst_element_set_state (bin, GST_STATE_PLAYING);
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	} else {
		gst_element_set_state (bin, GST_STATE_PAUSED);
	}

	_rb_player_gst_filter_emit_filter_inserted (RB_PLAYER_GST_FILTER (op->player), op->element);

	free_pipeline_op (op);
}

static void
really_remove_filter (GstPad *pad,
		      gboolean blocked,
		      RBGstPipelineOp *op)
{
	GstPad *mypad;
	GstPad *prevpad, *nextpad;
	GstElement *bin;

	/* get the containing bin and remove it */
	bin = GST_ELEMENT (gst_element_get_parent (op->element));
	if (bin == NULL) {
		return;
	}

	rb_debug ("removing filter %p", op->element);
	_rb_player_gst_filter_emit_filter_pre_remove (RB_PLAYER_GST_FILTER (op->player), op->element);

	/* probably check return? */
	gst_element_set_state (bin, GST_STATE_NULL);

	/* unlink our sink */
	mypad = gst_element_get_static_pad (bin, "sink");
	prevpad = gst_pad_get_peer (mypad);
	gst_pad_unlink (prevpad, mypad);
	gst_object_unref (mypad);

	/* unlink our src */
	mypad = gst_element_get_static_pad (bin, "src");
	nextpad = gst_pad_get_peer (mypad);
	gst_pad_unlink (mypad, nextpad);
	gst_object_unref (mypad);

	/* link previous and next pads */
	gst_pad_link (prevpad, nextpad);

	gst_object_unref (prevpad);
	gst_object_unref (nextpad);

	gst_bin_remove (GST_BIN (op->fixture), bin);
	gst_object_unref (bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after removing filter");
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	}

	free_pipeline_op (op);
}

/**
 * rb_gst_add_filter:
 * @player: player object (must implement @RBPlayerGstFilter interface)
 * @filterbin: the filter bin
 * @element: the filter to add
 * @use_pad_block: if %TRUE, block the src pad connected to the filter bin
 *
 * Inserts a filter into the filter bin, using pad blocking (if requested) to
 * avoid breaking the data flow.  Pad blocking should be used when the pipeline
 * is in PLAYING state, or when in PAUSED state where a streaming thread will
 * be holding the stream lock for the filter bin.
 */
gboolean
rb_gst_add_filter (RBPlayer *player, GstElement *filterbin, GstElement *element, gboolean use_pad_block)
{
	return pipeline_op (G_OBJECT (player),
			    filterbin,
			    element,
			    use_pad_block,
			    (GstPadBlockCallback) really_add_filter);
}

/**
 * rb_gst_remove_filter:
 * @player: player object (must implement @RBPlayerGstFilter interface)
 * @filterbin: the filter bin
 * @element: the filter to remove
 * @use_pad_block: if %TRUE, block the src pad connected to the filter bin
 *
 * Removes a filter from the filter bin, using pad blocking (if requested) to
 * avoid breaking the data flow.  Pad blocking should be used when the pipeline
 * is in PLAYING state, or when in PAUSED state where a streaming thread will
 * be holding the stream lock for the filter bin.
 */
gboolean
rb_gst_remove_filter (RBPlayer *player, GstElement *filterbin, GstElement *element, gboolean use_pad_block)
{
	return pipeline_op (G_OBJECT (player),
			    filterbin,
			    element,
			    use_pad_block,
			    (GstPadBlockCallback) really_remove_filter);
}

/* RBPlayerGstTee implementation */

static void
really_add_tee (GstPad *pad, gboolean blocked, RBGstPipelineOp *op)
{
	GstElement *queue;
	GstElement *audioconvert;
	GstElement *bin;
	GstElement *parent_bin;
	GstPad *sinkpad;
	GstPad *ghostpad;

	rb_debug ("really adding tee %p", op->element);

	/* set up containing bin */
	bin = gst_bin_new (NULL);
	queue = gst_element_factory_make ("queue", NULL);
	audioconvert = gst_element_factory_make ("audioconvert", NULL);

	/* The bin contains elements that change state asynchronously
	 * and not as part of a state change in the entire pipeline.
	 */
	g_object_set (bin, "async-handling", TRUE, NULL);

	g_object_set (queue, "max-size-buffers", 3, NULL);

	gst_bin_add_many (GST_BIN (bin), queue, audioconvert, op->element, NULL);
	gst_element_link_many (queue, audioconvert, op->element, NULL);

	/* add ghost pad */
	sinkpad = gst_element_get_static_pad (queue, "sink");
	ghostpad = gst_ghost_pad_new ("sink", sinkpad);
	gst_element_add_pad (bin, ghostpad);
	gst_object_unref (sinkpad);

	/* add it into the pipeline */
	parent_bin = GST_ELEMENT_PARENT (op->fixture);
	gst_bin_add (GST_BIN (parent_bin), bin);
	gst_element_link (op->fixture, bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after adding tee");

		gst_element_set_state (parent_bin, GST_STATE_PLAYING);
		gst_object_ref (ghostpad);
		gst_pad_set_blocked_async (pad,
					   FALSE,
					   (GstPadBlockCallback)pipeline_op_done,
					   ghostpad);
	} else {
		gst_element_set_state (bin, GST_STATE_PAUSED);
		gst_object_ref (ghostpad);
		pipeline_op_done (NULL, FALSE, ghostpad);
	}

	_rb_player_gst_tee_emit_tee_inserted (RB_PLAYER_GST_TEE (op->player), op->element);

	free_pipeline_op (op);
}

static void
really_remove_tee (GstPad *pad, gboolean blocked, RBGstPipelineOp *op)
{
	GstElement *bin;
	GstElement *parent;

	rb_debug ("really removing tee %p", op->element);

	_rb_player_gst_tee_emit_tee_pre_remove (RB_PLAYER_GST_TEE (op->player), op->element);

	/* find bin, remove everything */
	bin = GST_ELEMENT_PARENT (op->element);
	g_object_ref (bin);

	parent = GST_ELEMENT_PARENT (bin);
	gst_bin_remove (GST_BIN (parent), bin);

	gst_element_set_state (bin, GST_STATE_NULL);
	gst_bin_remove (GST_BIN (bin), op->element);
	g_object_unref (bin);

	/* if we're supposed to be playing, unblock the sink */
	if (blocked) {
		rb_debug ("unblocking pad after removing tee");
		gst_pad_set_blocked_async (pad, FALSE, (GstPadBlockCallback)pipeline_op_done, NULL);
	}

	free_pipeline_op (op);
}

/**
 * rb_gst_add_tee:
 * @player: player object (must implement @RBPlayerGstTee interface)
 * @tee: a tee element
 * @element: the tee branch to add
 * @use_pad_block: if %TRUE, block the src pad connected to the filter bin
 *
 * Attaches a branch to the tee, using pad blocking (if requested) to
 * avoid breaking the data flow.  Pad blocking should be used when the pipeline
 * is in PLAYING state, or when in PAUSED state where a streaming thread will
 * be holding the stream lock for the filter bin.
 */
gboolean
rb_gst_add_tee (RBPlayer *player, GstElement *tee, GstElement *element, gboolean use_pad_block)
{
	return pipeline_op (G_OBJECT (player),
			    tee,
			    element,
			    use_pad_block,
			    (GstPadBlockCallback) really_add_tee);
}

/**
 * rb_gst_remove_tee:
 * @player: player object (must implement @RBPlayerGstTee interface)
 * @tee: a tee element
 * @element: the tee branch to remove
 * @use_pad_block: if %TRUE, block the src pad connected to the filter bin
 *
 * Removes a branch from the tee, using pad blocking (if requested) to
 * avoid breaking the data flow.  Pad blocking should be used when the pipeline
 * is in PLAYING state, or when in PAUSED state where a streaming thread will
 * be holding the stream lock for the filter bin.
 */
gboolean
rb_gst_remove_tee (RBPlayer *player, GstElement *tee, GstElement *element, gboolean use_pad_block)
{
	return pipeline_op (G_OBJECT (player),
			    tee,
			    element,
			    use_pad_block,
			    (GstPadBlockCallback) really_remove_tee);
}

