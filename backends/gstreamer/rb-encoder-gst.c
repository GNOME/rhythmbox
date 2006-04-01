/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Implementation of GStreamer encoding backend
 * 
 * Based on Sound-Juicer's ripping code
 *
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
 * Copyright (C) 2006 James Livingston <jrl@ids.org.au>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#include <config.h>

#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <gst/gst.h>
#include <string.h>
#include <profiles/gnome-media-profiles.h>
#include <gtk/gtk.h>

#include "rhythmdb.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"
#include "rb-encoder.h"
#include "rb-encoder-gst.h"
#include "rb-debug.h"


static void rb_encoder_gst_class_init (RBEncoderGstClass *klass);
static void rb_encoder_gst_init       (RBEncoderGst *encoder);
static void rb_encoder_gst_finalize   (GObject *object);
static void rb_encoder_init (RBEncoderIface *iface);


struct _RBEncoderGstPrivate {
	GstElement *pipeline;
	
	gboolean completion_emitted;

	gulong total_length;
	guint progress_id;
};

G_DEFINE_TYPE_WITH_CODE(RBEncoderGst, rb_encoder_gst, G_TYPE_OBJECT,
			G_IMPLEMENT_INTERFACE(RB_TYPE_ENCODER,
					      rb_encoder_init))
#define RB_ENCODER_GST_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_ENCODER_GST, RBEncoderGstPrivate))


static gboolean rb_encoder_gst_encode (RBEncoder *encoder,
				       RhythmDBEntry *entry,
				       const char *dest,
				       const char *mime_type);
static void rb_encoder_gst_cancel (RBEncoder *encoder);
static void rb_encoder_gst_emit_completed (RBEncoderGst *encoder);
	
static void
rb_encoder_gst_class_init (RBEncoderGstClass *klass)
{
        GObjectClass *object_class = (GObjectClass *) klass;

        object_class->finalize = rb_encoder_gst_finalize;

        g_type_class_add_private (klass, sizeof (RBEncoderGstPrivate));
}

static void
rb_encoder_gst_init (RBEncoderGst *encoder)
{
        encoder->priv = RB_ENCODER_GST_GET_PRIVATE (encoder);
}

static void
rb_encoder_init (RBEncoderIface *iface)
{
	iface->encode = rb_encoder_gst_encode;
	iface->cancel = rb_encoder_gst_cancel;
}

static void
rb_encoder_gst_finalize (GObject *object)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (object);

	if (encoder->priv->completion_emitted == FALSE) {
		g_warning ("completion not emitted for encoder");
		rb_encoder_gst_emit_completed (encoder);
	}
	
	if (encoder->priv->progress_id != 0)
		g_source_remove (encoder->priv->progress_id);	
	
	if (encoder->priv->pipeline) {
		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);
		g_object_unref (encoder->priv->pipeline);
		encoder->priv->pipeline = NULL;
	}	

        G_OBJECT_CLASS (rb_encoder_gst_parent_class)->finalize (object);
}

RBEncoder*
rb_encoder_gst_new (void)
{
	return RB_ENCODER (g_object_new (RB_TYPE_ENCODER_GST, NULL));
}

static void
rb_encoder_gst_emit_completed (RBEncoderGst *encoder)
{
	g_return_if_fail (encoder->priv->completion_emitted == FALSE);

	if (encoder->priv->progress_id != 0)
		g_source_remove (encoder->priv->progress_id);	

	encoder->priv->completion_emitted = TRUE;
	_rb_encoder_emit_completed (RB_ENCODER (encoder));
}


#ifdef HAVE_GSTREAMER_0_10
static gboolean
bus_watch_cb (GstBus *bus, GstMessage *message, gpointer data)
{
	RBEncoderGst *encoder = RB_ENCODER_GST (data);
	char *string;
	GError *error;

	/* ref ourselves, in case one of the signal handler unrefs us */
	g_object_ref (G_OBJECT (encoder));

	switch (GST_MESSAGE_TYPE (message)) {
	case GST_MESSAGE_ERROR:
		gst_message_parse_error (message, &error, &string);
		_rb_encoder_emit_error (RB_ENCODER (encoder), error);
		rb_debug ("received error %s", string);
		g_error_free (error);
		g_free (string);
		
		rb_encoder_cancel (RB_ENCODER (encoder));
		break;

	case GST_MESSAGE_WARNING:
		gst_message_parse_warning (message, &error, &string);
		rb_debug ("received warning %s", string);
		g_error_free (error);
		g_free (string);
		break;
	
	case GST_MESSAGE_EOS:
		rb_debug ("received EOS");

		gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);

		rb_encoder_gst_emit_completed (encoder);

		g_object_unref (encoder->priv->pipeline);
		encoder->priv->pipeline = NULL;
		break;
	
	default:
		rb_debug ("message of type %s", gst_message_type_get_name (GST_MESSAGE_TYPE (message)));
		break;
	}

	g_object_unref (G_OBJECT (encoder));
	return TRUE;
}
#endif

#ifdef HAVE_GSTREAMER_0_8
static void
gst_eos_cb (GstElement *element, RBEncoderGst *encoder)
{
	rb_debug ("received EOS");

	gst_element_set_state (encoder->priv->pipeline, GST_STATE_NULL);

	rb_encoder_gst_emit_completed (encoder);

	g_object_unref (encoder->priv->pipeline);
	encoder->priv->pipeline = NULL;
}

static void
gst_error_cb (GstElement *element,
			  GstElement *source,
			  GError *error,
			  gchar *debug,
			  RBEncoderGst *encoder)
{
	_rb_encoder_emit_error (RB_ENCODER (encoder), error);
	rb_debug ("received error %s", debug);
		
	rb_encoder_cancel (RB_ENCODER (encoder));
}

#endif

static gboolean
progress_timeout_cb (RBEncoderGst *encoder)
{
	gint64 nanos;
	gint secs;
	GstState state;
	static GstFormat format = GST_FORMAT_TIME;

	if (encoder->priv->pipeline == NULL)
		return FALSE;

	gst_element_get_state (encoder->priv->pipeline, &state, NULL, GST_CLOCK_TIME_NONE);
	if (state != GST_STATE_PLAYING)
		return FALSE;

	if (!gst_element_query_position (encoder->priv->pipeline, &format, &nanos)) {
		g_warning ("Could not get current track position");
		return TRUE;
	}

	secs = nanos / GST_SECOND;
	rb_debug ("encoding progress at %d out of %d", secs, encoder->priv->total_length);
	_rb_encoder_emit_progress (RB_ENCODER (encoder), ((double)secs) / encoder->priv->total_length);

	return TRUE;
}

static gboolean
start_pipeline (RBEncoderGst *encoder, GError **error)
{
	GstStateChangeReturn result;
	
#ifdef HAVE_GSTREAMER_0_10
	GstBus *bus;

	g_assert (encoder->priv->pipeline);
	
	bus = gst_pipeline_get_bus (GST_PIPELINE (encoder->priv->pipeline));
	gst_bus_add_watch (bus, bus_watch_cb, encoder);
#elif HAVE_GSTREAMER_0_8
	g_signal_connect_object (G_OBJECT (encoder->priv->pipeline),
				 "error", G_CALLBACK (gst_error_cb),
				 encoder, 0);
	g_signal_connect_object (G_OBJECT (encoder->priv->pipeline),
				 "eos", G_CALLBACK (gst_eos_cb),
				 encoder, 0);
#endif

	result = gst_element_set_state (encoder->priv->pipeline, GST_STATE_PLAYING);
	
	if (result != GST_STATE_CHANGE_FAILURE) {
		/* start reporting progress */
		if (encoder->priv->total_length > 0) {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), 0.0);
			encoder->priv->progress_id = g_timeout_add (250, (GSourceFunc)progress_timeout_cb, encoder);
		} else {
			_rb_encoder_emit_progress (RB_ENCODER (encoder), -1);
		}
	}
	
	return (result != GST_STATE_CHANGE_FAILURE);
}

#ifdef HAVE_GSTREAMER_0_8
/* this is basically what the function in 0.10 does */
static GstPad*
rb_gst_bin_find_unconnected_pad (GstBin *bin, GstPadDirection dir)
{
	const GList *elements, *el;

	elements = gst_bin_get_list (GST_BIN (bin));
	for (el = elements; el != NULL; el = el->next) {
		GstElement *element = GST_ELEMENT (el->data);
		const GList *pads, *pl;

		pads = gst_element_get_pad_list (element);

		for (pl = pads; pl != NULL; pl = pl->next) {
			GstPad *pad = GST_PAD (pl->data);

			if (!GST_PAD_IS_LINKED (pad) && GST_PAD_DIRECTION (pad) == dir)
				return pad;
		}
	}

	return NULL;
}

#define gst_bin_find_unconnected_pad rb_gst_bin_find_unconnected_pad

const char *GST_ENCODING_PROFILE = "audioscale ! audioconvert ! %s";
#elif HAVE_GSTREAMER_0_10
const char *GST_ENCODING_PROFILE = "audioresample ! audioconvert ! %s";
#endif

static GstElement*
add_encoding_pipeline (RBEncoderGst *encoder,
		       GstElement *start,
		       GMAudioProfile *profile,
		       GError **error)
{
	GstElement *queue, *encoding_bin, *queue2;
	GstPad *pad;
	char *tmp;

	queue = gst_element_factory_make ("queue", NULL);
	if (queue == NULL)
		return NULL;
	queue2 = gst_element_factory_make ("queue", NULL);
	if (queue2 == NULL)
		return NULL;

	/* Nice big buffers... */
	g_object_set (queue, "max-size-time", 120 * GST_SECOND, NULL);

	tmp = g_strdup_printf (GST_ENCODING_PROFILE, gm_audio_profile_get_pipeline (profile));
	encoding_bin = GST_ELEMENT (gst_parse_launch (tmp, error));
	g_free (tmp);

	if (encoding_bin == NULL)
		return NULL;


	/* find pads and ghost them if necessary */
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SRC)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("src", pad));
	if ((pad = gst_bin_find_unconnected_pad (GST_BIN (encoding_bin), GST_PAD_SINK)))
		gst_element_add_pad (encoding_bin, gst_ghost_pad_new ("sink", pad));

	gst_bin_add_many (GST_BIN (encoder->priv->pipeline), queue, encoding_bin, queue2, NULL);
	gst_element_link_many (start, queue, encoding_bin, queue2, NULL);

	return queue2;
}

static gboolean
add_tags_from_entry (RBEncoderGst *encoder,
		     RhythmDBEntry *entry,
		     GError **error)
{
	GstTagList *tags;
	gboolean result = TRUE;
	gulong day = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DATE);
	GDate *date = g_date_new_julian (day);

	tags = gst_tag_list_new ();


	
	gst_tag_list_add (tags, GST_TAG_MERGE_REPLACE_ALL,
			  /* TODO: compute replay-gain */
			  GST_TAG_TITLE, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE),
			  GST_TAG_ARTIST, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST),
			  GST_TAG_TRACK_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
			  GST_TAG_ALBUM_VOLUME_NUMBER, rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DISC_NUMBER),
			  GST_TAG_ALBUM, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM),
			  GST_TAG_GENRE, rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_GENRE),
                          GST_TAG_DATE, date,
			  GST_TAG_ENCODER, "Rhythmbox",
			  GST_TAG_ENCODER_VERSION, VERSION,
			  NULL);
	g_date_free (date);

#ifdef HAVE_GSTREAMER_0_10
	GstIterator *iter;
	gboolean done;

	iter = gst_bin_iterate_all_by_interface (GST_BIN (encoder->priv->pipeline), GST_TYPE_TAG_SETTER);
	done = FALSE;
	while (!done) {
		GstTagSetter *tagger = NULL;
		GstTagSetter **tagger_ptr = &tagger;

		switch (gst_iterator_next (iter, (gpointer*)tagger_ptr)) {
		case GST_ITERATOR_OK:
			gst_tag_setter_merge_tags (tagger, tags, GST_TAG_MERGE_REPLACE_ALL);
			break;
		case GST_ITERATOR_RESYNC:
			gst_iterator_resync (iter);
			break;
		case GST_ITERATOR_ERROR:
			g_set_error (error,
				     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
				     "Could not add tags to tag-setter");
			result = FALSE;
			done = TRUE;
			break;
		case GST_ITERATOR_DONE:
			done = TRUE;
			break;
		}
		
		if (tagger)
			gst_object_unref (tagger);
	}
	gst_iterator_free (iter);
#elif HAVE_GSTREAMER_0_8
	GstElement *tagger;

        tagger = gst_bin_get_by_interface (GST_BIN (encoder->priv->pipeline), GST_TYPE_TAG_SETTER);
	if (tagger)
		gst_tag_setter_merge (GST_TAG_SETTER (tagger), tags, GST_TAG_MERGE_REPLACE_ALL);
#endif

	gst_tag_list_free (tags);
	return result;
}

static gboolean
gnomevfs_allow_overwrite_cb (GstElement *element, GnomeVFSURI *uri, RBEncoderGst *encoder)
{
	GtkWidget *dialog;
	gint response;
	char *name;
	char *display_name;

	name = gnome_vfs_uri_to_string (uri, GNOME_VFS_URI_HIDE_USER_NAME | GNOME_VFS_URI_HIDE_PASSWORD);
	display_name = gnome_vfs_format_uri_for_display (name);

	dialog = gtk_message_dialog_new (NULL, 0,
					 GTK_MESSAGE_QUESTION, GTK_BUTTONS_YES_NO,
					 _("Do you want to overwrite the file \"%s\"?"),
					 display_name);
	response = gtk_dialog_run (GTK_DIALOG (dialog));
	gtk_widget_destroy (dialog);
	
	g_free (display_name);
	g_free (name);
	
	return (response == GTK_RESPONSE_YES);				 
}

static gboolean
attach_output_pipeline (RBEncoderGst *encoder,
			GstElement *end,
			const char *dest,
			GError **error)
{
	GstElement *sink;

	sink = gst_element_make_from_uri (GST_URI_SINK, dest, "sink");

	/* handle overwriting if we are using gnomevfssink
	 * it would be nice if GST had an interface for sinks with thi, but it doesn't
	 */
	if (g_type_is_a (G_OBJECT_TYPE (sink), g_type_from_name ("GstGnomeVFSSink"))) {
		g_signal_connect_object (G_OBJECT (sink),
					 "allow-overwrite", G_CALLBACK (gnomevfs_allow_overwrite_cb),
					 encoder, 0);
	}

	gst_bin_add (GST_BIN (encoder->priv->pipeline), sink);
	gst_element_link (end, sink);

	return TRUE;
}

static GMAudioProfile*
get_profile_from_mime_type (const char *mime_type)
{
	/* TODO */
	return NULL;
}

static GstElement *
create_pipeline_and_source (RBEncoderGst *encoder,
			    RhythmDBEntry *entry,
			    GError **error)
{
	const char *uri;
	GstElement *src;

	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
	src = gst_element_make_from_uri (GST_URI_SRC, uri, "source");
	if (src == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "could not create source element for '%s'", uri);
		return NULL;
	}

	encoder->priv->pipeline = gst_pipeline_new ("pipeline");
	gst_bin_add (GST_BIN (encoder->priv->pipeline), src);

	/* TODO: add progress reporting */

	return src;
}

static gboolean
copy_track (RBEncoderGst *encoder,
	    RhythmDBEntry *entry,
	    const char *dest,
	    GError **error)
{
	/* source ! sink */
	GstElement *src;

	g_assert (encoder->priv->pipeline == NULL);

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		return FALSE;

	if (!attach_output_pipeline (encoder, src, dest, error))
		return FALSE;

	if (!start_pipeline (encoder, error))
		return FALSE;

	return TRUE;
}

static gboolean
extract_track (RBEncoderGst *encoder,
	       RhythmDBEntry *entry,
	       const char *dest,
	       GError **error)
{
	/* cdsrc ! encoder ! sink */
	const char *uri;
	const char *device;
	const char *profile_name;
	GMAudioProfile *profile;
	GstElement *src, *end;

	g_assert (encoder->priv->pipeline == NULL);

	profile_name = eel_gconf_get_string (CONF_LIBRARY_PREFERRED_FORMAT);
	profile = gm_audio_profile_lookup (profile_name);
	if (profile == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
			     "Could not find encoding profile '%s'", profile_name);
		return FALSE;
	}

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		return FALSE;

	g_assert (encoder->priv->pipeline != NULL);

	/* setup cd extraction properties */
	uri = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);

	device = g_utf8_strrchr (uri, -1, '#');
	g_object_set (G_OBJECT (src),
		      "device", device + 1, /* skip the '#' */
		      "track", rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER),
		      NULL);
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (src), "paranoia-mode")) {
		int paranoia_mode;

		paranoia_mode = 255; /* TODO: make configurable */
		g_object_set (G_OBJECT (src), "paranoia-mode", paranoia_mode, NULL);
	}

	end = add_encoding_pipeline (encoder, src, profile, error);
	if (end == NULL)
		return FALSE;

	if (!attach_output_pipeline (encoder, end, dest, error))
		return FALSE;
	if (!add_tags_from_entry (encoder, entry, error))
		return FALSE;
	if (!start_pipeline (encoder, error))
		return FALSE;

	return TRUE;
}

static gboolean
transcode_track (RBEncoderGst *encoder,
	 	 RhythmDBEntry *entry,
		 const char *dest,
		 const char *mime_type,
		 GError **error)
{
	/* cdsrc ! encoder ! sink */
	GMAudioProfile *profile;
	GstElement *src, *spider, *end;

	g_assert (encoder->priv->pipeline == NULL);

	profile = get_profile_from_mime_type (mime_type);
	if (profile == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_FORMAT_UNSUPPORTED,
			     "Unable to locate encoding profile for mime-type '%s'", mime_type);
		return FALSE;
	}

	src = create_pipeline_and_source (encoder, entry, error);
	if (src == NULL)
		return FALSE;

	spider = gst_element_factory_make ("spider", "spider");
	if (spider == NULL) {
		g_set_error (error,
			     RB_ENCODER_ERROR, RB_ENCODER_ERROR_INTERNAL,
			     "Could not create encoding profile '%s'", gm_audio_profile_get_id (profile));
		return FALSE;
	}
	gst_bin_add (GST_BIN (encoder->priv->pipeline), spider);
	gst_element_link (src, spider);

	end = add_encoding_pipeline (encoder, spider, profile, error);
	if (end == NULL)
		return FALSE;

	if (!attach_output_pipeline (encoder, end, dest, error))
		return FALSE;
	if (!add_tags_from_entry (encoder, entry, error))
		return FALSE;
	if (!start_pipeline (encoder, error))
		return FALSE;

	return TRUE;
}

static void
create_parent_dirs_uri (GnomeVFSURI *uri)
{
	GnomeVFSURI *parent_uri;

	if (gnome_vfs_uri_exists (uri))
		return;

	parent_uri = gnome_vfs_uri_get_parent (uri);
	create_parent_dirs_uri (parent_uri);
	gnome_vfs_uri_unref (parent_uri);

	gnome_vfs_make_directory_for_uri (uri, 0750);
}

static void
create_parent_dirs (const char *uri)
{
	GnomeVFSURI *vfs_uri;
	GnomeVFSURI *parent_uri;

	vfs_uri = gnome_vfs_uri_new (uri);
	parent_uri = gnome_vfs_uri_get_parent (vfs_uri);

	create_parent_dirs_uri (parent_uri);

	gnome_vfs_uri_unref (parent_uri);
	gnome_vfs_uri_unref (vfs_uri);
}

static void
rb_encoder_gst_cancel (RBEncoder *encoder)
{
	RBEncoderGstPrivate *priv = RB_ENCODER_GST (encoder)->priv;

	if (priv->pipeline == NULL)
		return;

	gst_element_set_state (priv->pipeline, GST_STATE_NULL);
	g_object_unref (priv->pipeline);
	priv->pipeline = NULL;
}

static gboolean
rb_encoder_gst_encode (RBEncoder *encoder,
		       RhythmDBEntry *entry,
		       const char *dest,
		       const char *mime_type)
{
	RBEncoderGstPrivate *priv = RB_ENCODER_GST (encoder)->priv;
	const char *entry_mime_type;
	gboolean was_raw;
	gboolean result;
	GError *error = NULL;

	g_return_val_if_fail (priv->pipeline == NULL, FALSE);
	
	entry_mime_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MIMETYPE);
	was_raw = g_str_has_prefix (entry_mime_type, "audio/x-raw");

	priv->total_length = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);

	create_parent_dirs (dest);

	if ((mime_type == NULL && !was_raw) || (mime_type && (strcmp (mime_type, entry_mime_type) == 0))) {
		result = copy_track (RB_ENCODER_GST (encoder), entry, dest, &error);
	} else {
		if (mime_type == NULL) {
			result = extract_track (RB_ENCODER_GST (encoder), entry, dest, &error);
		} else {
			result = transcode_track (RB_ENCODER_GST (encoder), entry, dest, mime_type, &error);
		}
	}

	if (error) {
		rb_encoder_cancel (encoder);
		_rb_encoder_emit_error (encoder, error);
		g_error_free (error);
		rb_encoder_gst_emit_completed (RB_ENCODER_GST (encoder));
	}

	return result;
}
