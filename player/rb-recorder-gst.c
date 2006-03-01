/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Implementation of GStreamer recorder backend
 *
 * Copyright (C) 2004-2005 William Jon McCann <mccann@jhu.edu>
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
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <glib/gi18n.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <nautilus-burn-recorder.h>
#include <gst/gst.h>
#ifdef HAVE_GSTREAMER_0_8
#include <gst/gconf/gconf.h>
#include <gst/play/play.h>
#endif

#include "rb-recorder.h"
#include "rb-recorder-marshal.h"

#include "rb-debug.h"

#ifdef HAVE_GSTREAMER_0_8
#define gst_caps_unref gst_caps_free
#endif
#ifndef HAVE_BURN_DRIVE_UNREF
#define nautilus_burn_drive_unref nautilus_burn_drive_free
#define nautilus_burn_drive_ref nautilus_burn_drive_copy
#endif


static void rb_recorder_class_init (RBRecorderClass *klass);
static void rb_recorder_init       (RBRecorder      *recorder);
static void rb_recorder_finalize   (GObject         *object);

struct _RBRecorderPrivate {
        char       *src_uri;
        char       *dest_file;
        char       *tmp_dir;
        
        GstElement *pipeline;
        GstElement *decoder;
        GstElement *src;
        GstElement *typefind;
        GstElement *audioconvert;
        GstElement *audioscale;
        GstElement *encoder;
        GstElement *sink;

#ifdef HAVE_GSTREAMER_0_8
        GstPad     *src_pad;
        GError     *error;

        guint       idle_id;
        guint       error_signal_id;
        guint       eos_signal_id;
#elif HAVE_GSTREAMER_0_10
	GstElement *capsfilter;
	gboolean    got_audio_pad;
#endif

        guint       tick_timeout_id;

        GTimer     *start_timer;
        guint64     start_pos;

        double      progress;
        GList      *tracks;

        NautilusBurnDrive    *drive;
        NautilusBurnRecorder *recorder;

        gboolean    playing;
};

typedef enum {
        EOS,
        ACTION_CHANGED,
        TRACK_PROGRESS_CHANGED,
        BURN_PROGRESS_CHANGED,
        INSERT_MEDIA_REQUEST,
        WARN_DATA_LOSS,
        ERROR,
        LAST_SIGNAL
} RBRecorderSignalType;

#ifdef HAVE_GSTREAMER_0_8
typedef struct {
        RBRecorder *object;
        GError     *error;
        GValue     *info;
} RBRecorderSignal;
#endif

static guint rb_recorder_signals [LAST_SIGNAL] = { 0 };

#define RB_RECORDER_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_RECORDER, RBRecorderPrivate))

GQuark
rb_recorder_error_quark (void)
{
        static GQuark quark = 0;
        if (!quark)
                quark = g_quark_from_static_string ("rb_recorder_error");

        return quark;
}

G_DEFINE_TYPE(RBRecorder, rb_recorder, G_TYPE_OBJECT)

static void
rb_recorder_class_init (RBRecorderClass *klass)
{
        GObjectClass *object_class;

        object_class = (GObjectClass *) klass;

        object_class->finalize = rb_recorder_finalize;

        rb_recorder_signals [EOS] =
                g_signal_new ("eos",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        rb_recorder_signals [TRACK_PROGRESS_CHANGED] =
                g_signal_new ("track-progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              rb_recorder_marshal_VOID__DOUBLE_LONG,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_DOUBLE,
                              G_TYPE_LONG);
        rb_recorder_signals [BURN_PROGRESS_CHANGED] =
                g_signal_new ("burn-progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              rb_recorder_marshal_VOID__DOUBLE_LONG,
                              G_TYPE_NONE,
                              2,
                              G_TYPE_DOUBLE,
                              G_TYPE_LONG);
        rb_recorder_signals [ACTION_CHANGED] =
                g_signal_new ("action-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        rb_recorder_signals [INSERT_MEDIA_REQUEST] =
                g_signal_new ("insert-media-request",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              rb_recorder_marshal_BOOLEAN__BOOLEAN_BOOLEAN_BOOLEAN,
                              G_TYPE_BOOLEAN,
                              3,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN,
                              G_TYPE_BOOLEAN);
	rb_recorder_signals [WARN_DATA_LOSS] =
		g_signal_new ("warn-data-loss",
			      G_TYPE_FROM_CLASS (klass),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      rb_recorder_marshal_INT__VOID,
			      G_TYPE_INT, 0);
        rb_recorder_signals [ERROR] =
                g_signal_new ("error",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);

        g_type_class_add_private (klass, sizeof (RBRecorderPrivate));
}

static NautilusBurnDrive *
rb_recorder_get_default_drive (void)
{
        NautilusBurnDrive *drive  = NULL;
        GList             *drives = NULL;

        drives = nautilus_burn_drive_get_list (TRUE, FALSE);

        if (drives)
                drive = nautilus_burn_drive_ref ((NautilusBurnDrive*) drives->data);

        g_list_foreach (drives, (GFunc)nautilus_burn_drive_unref, NULL);
        g_list_free (drives);

        return drive;
}

gboolean
rb_recorder_enabled (void)
{
        NautilusBurnDrive *drive;
        gboolean           enabled;

        drive = rb_recorder_get_default_drive ();

        enabled = (drive != NULL);

        if (drive) {
                nautilus_burn_drive_unref (drive);
        }

        return enabled;
}

static void
rb_recorder_init (RBRecorder *recorder)
{
        recorder->priv = RB_RECORDER_GET_PRIVATE (recorder);

        recorder->priv->tmp_dir = g_strdup (g_get_tmp_dir ());

        recorder->priv->drive = rb_recorder_get_default_drive ();
}

static void
rb_recorder_gst_free_pipeline (RBRecorder *recorder)
{
        rb_debug ("Freeing rb_recorder pipeline");

        if (recorder->priv->pipeline == NULL)
                return;

        if (recorder->priv->tick_timeout_id > 0) {
                g_source_remove (recorder->priv->tick_timeout_id);
                recorder->priv->tick_timeout_id = 0;
                if (recorder->priv->start_timer) {
                        g_timer_destroy (recorder->priv->start_timer);
                        recorder->priv->start_timer = NULL;
                }
        }

#ifdef HAVE_GSTREAMER_0_8
        if (recorder->priv->idle_id > 0) {
                g_source_remove (recorder->priv->idle_id);
                recorder->priv->idle_id = 0;
        }

        if (recorder->priv->error_signal_id > 0) {
                if (g_signal_handler_is_connected (G_OBJECT (recorder->priv->pipeline),
                                                   recorder->priv->error_signal_id))
                        g_signal_handler_disconnect (G_OBJECT (recorder->priv->pipeline),
                                                     recorder->priv->error_signal_id);
                recorder->priv->error_signal_id = 0;
        }

        if (recorder->priv->eos_signal_id > 0) {
                if (g_signal_handler_is_connected (G_OBJECT (recorder->priv->pipeline),
                                                   recorder->priv->eos_signal_id))
                        g_signal_handler_disconnect (G_OBJECT (recorder->priv->pipeline),
                                                     recorder->priv->eos_signal_id);
                recorder->priv->eos_signal_id = 0;
        }
#elif HAVE_GSTREAMER_0_10
	recorder->priv->got_audio_pad = FALSE;
#endif

        gst_element_set_state (recorder->priv->pipeline, GST_STATE_NULL);

        gst_object_unref (GST_OBJECT (recorder->priv->pipeline));
        recorder->priv->pipeline = NULL;
}

static gboolean
add_track (RBRecorder *recorder,
           const char *cdtext)
{
        NautilusBurnRecorderTrack *track;
        char                      *filename;

        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);

        filename = g_strdup (recorder->priv->dest_file);

        track = g_new0 (NautilusBurnRecorderTrack, 1);

        track->type = NAUTILUS_BURN_RECORDER_TRACK_TYPE_AUDIO;
        track->contents.audio.filename = filename;
        if (cdtext)
                track->contents.audio.cdtext = g_strdup (cdtext);

        recorder->priv->tracks = g_list_append (recorder->priv->tracks, track);

        return TRUE;
}

#ifdef HAVE_GSTREAMER_0_8
static void
eos_cb (GstElement *element,
        RBRecorder *recorder)
{
        rb_debug ("EOS");

        if (recorder->priv->pipeline)
                gst_element_set_state (recorder->priv->pipeline, GST_STATE_NULL);

        g_signal_emit (G_OBJECT (recorder), rb_recorder_signals [EOS], 0);
}

static gboolean
error_signal_idle (RBRecorderSignal *signal)
{
        g_signal_emit (G_OBJECT (signal->object),
                       rb_recorder_signals [ERROR],
                       0,
                       signal->error);

        /* close if not already closing */
        if (signal->object->priv->src_uri != NULL)
                rb_recorder_close (signal->object, NULL);

        g_object_unref (signal->object);
        g_error_free (signal->error);
        g_free (signal);

        return FALSE;
}

static void
rb_recorder_gst_signal_error (RBRecorder *recorder,
                              const char *msg)
{
        RBRecorderSignal *signal;

        signal = g_new0 (RBRecorderSignal, 1);
        signal->object = recorder;
        signal->error = g_error_new_literal (RB_RECORDER_ERROR,
                                             RB_RECORDER_ERROR_GENERAL,
                                             msg);
	g_object_ref (recorder);

        g_idle_add ((GSourceFunc)error_signal_idle, signal);
}

static void
error_cb (GstElement      *element,
          GstElement      *source,
          GError          *error,
          gchar           *debug,
          RBRecorder      *recorder)
{
        rb_debug ("Error");

        rb_recorder_gst_signal_error (recorder, error->message);
}
#elif HAVE_GSTREAMER_0_10
static void
rb_recorder_gst_signal_error (RBRecorder *recorder,
			      const char *msg)
{
	GError *error;

	g_object_ref (recorder);
	error = g_error_new_literal (RB_RECORDER_ERROR,
				     RB_RECORDER_ERROR_GENERAL,
				     msg);
	g_signal_emit (G_OBJECT (recorder),
		       rb_recorder_signals [ERROR],
		       0,
		       error);

	/* close if not already closing */
	if (recorder->priv->src_uri != NULL)
		rb_recorder_close (recorder, NULL);

	g_object_unref (recorder);
	g_error_free (error);
}

static gboolean
pipe_message (GstBus *bus, GstMessage *message, RBRecorder *recorder)
{
	switch (message->type) {
	case GST_MESSAGE_EOS: 
		rb_debug ("EOS");

		if (recorder->priv->pipeline)
			gst_element_set_state (recorder->priv->pipeline, GST_STATE_NULL);

		g_signal_emit (G_OBJECT (recorder), 
			       rb_recorder_signals [EOS], 0);
		break;
	case GST_MESSAGE_ERROR:
	{
		GError *error;
		gchar *debug;

		rb_debug ("Error");
		gst_message_parse_error (message, &error, &debug);
		if (error) {
			rb_recorder_gst_signal_error (recorder, error->message);
			g_error_free (error);
		} else {
			rb_recorder_gst_signal_error (recorder, NULL);
		}

		g_free (debug);
		break;
	}
	default:
		break;
	}

	return TRUE;
}
#endif
 
static void
rb_recorder_new_pad_cb (GstElement *decodebin,
			GstPad     *pad,
			gboolean    last,
			RBRecorder *recorder)
{
	GstCaps      *caps;
	GstStructure *str;
	GstPad       *audio_pad;

	audio_pad = gst_element_get_pad (recorder->priv->audioconvert, "sink");

	/* Only link once. */
	if (GST_PAD_IS_LINKED (audio_pad))
		return;

	/* Only link audio. */
	caps = gst_pad_get_caps (pad);
	str = gst_caps_get_structure (caps, 0);
	if (!g_strrstr (gst_structure_get_name (str), "audio")) {
		gst_caps_unref (caps);
		return;
	}

	gst_caps_unref (caps);
#ifdef HAVE_GSTREAMER_0_10
	if (gst_pad_link (pad, audio_pad))
		recorder->priv->got_audio_pad = TRUE;
#endif
}

static void
rb_recorder_construct (RBRecorder *recorder,
                       const char *uri,
                       GError    **error)
{
        char    *element_name = NULL;
        GstCaps *filtercaps;
#ifdef HAVE_GSTREAMER_0_10
	GstBus  *bus;
#endif

#define MAKE_ELEMENT_OR_LOSE(NAME, NICE) G_STMT_START {                 \
                element_name = #NAME ;                                  \
                rb_debug ("Constructing element \"" #NICE "\"");        \
                recorder->priv->NICE = gst_element_factory_make (#NAME, #NICE); \
                if (!recorder->priv->NICE)                              \
                        goto missing_element;                           \
        } G_STMT_END

        rb_recorder_gst_free_pipeline (recorder);

        /* The recording pipeline looks like:
         *  { src ! decodebin ! audioconvert ! audioscale
         *    ! audio/x-raw-int,rate=44100,width=16,depth=16 ! wavenc ! sink }
         */

        recorder->priv->pipeline = gst_pipeline_new ("pipeline");
        if (!recorder->priv->pipeline) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Failed to create pipeline"));
                rb_recorder_gst_free_pipeline (recorder);
                return;
        }

#ifdef HAVE_GSTREAMER_0_8
        recorder->priv->error_signal_id =
                g_signal_connect_object (G_OBJECT (recorder->priv->pipeline),
                                         "error",
                                         G_CALLBACK (error_cb),
                                         recorder, 0);
#elif HAVE_GSTREAMER_0_10
	bus = gst_element_get_bus (recorder->priv->pipeline);
	gst_bus_add_watch (bus, (GstBusFunc) pipe_message, recorder);
	gst_object_unref (bus);
#endif

        /* Construct elements */

        /* The source */

        MAKE_ELEMENT_OR_LOSE(gnomevfssrc, src);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->src);

#ifdef HAVE_GSTREAMER_0_8
        recorder->priv->src_pad = gst_element_get_pad (recorder->priv->src, "src");
	if (recorder->priv->src_pad == NULL) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_INTERNAL,
                             _("Could not access source pad"));
		return;
	}
#endif

        /* The queue */

        MAKE_ELEMENT_OR_LOSE(typefind, typefind);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->typefind);

        MAKE_ELEMENT_OR_LOSE(decodebin, decoder);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->decoder);
	
	g_signal_connect_object (G_OBJECT (recorder->priv->decoder), 
				 "new_decoded_pad", G_CALLBACK (rb_recorder_new_pad_cb), 
				 recorder, 0);

        MAKE_ELEMENT_OR_LOSE(audioconvert, audioconvert);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->audioconvert);

#ifdef HAVE_GSTREAMER_0_8
        MAKE_ELEMENT_OR_LOSE(audioscale, audioscale);
#elif HAVE_GSTREAMER_0_10
        MAKE_ELEMENT_OR_LOSE(audioresample, audioscale);
#endif
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->audioscale);

#ifdef HAVE_GSTREAMER_0_10
	MAKE_ELEMENT_OR_LOSE(capsfilter, capsfilter);
	gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->capsfilter);
#endif
	
        MAKE_ELEMENT_OR_LOSE(wavenc, encoder);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->encoder);

        /* Output sink */

        MAKE_ELEMENT_OR_LOSE(gnomevfssink, sink);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->sink);

        filtercaps = gst_caps_new_simple ("audio/x-raw-int",
                                          "channels", G_TYPE_INT, 2,
                                          "rate",     G_TYPE_INT, 44100,
                                          "width",    G_TYPE_INT, 16,
                                          "depth",    G_TYPE_INT, 16,
                                          NULL);
#ifdef HAVE_GSTREAMER_0_8
        gst_element_link_many (recorder->priv->src,
                               recorder->priv->typefind,
                               recorder->priv->decoder,
                               recorder->priv->audioconvert,
                               recorder->priv->audioscale,
                               NULL);

        gst_element_link_filtered (recorder->priv->audioscale,
                                   recorder->priv->encoder,
                                   filtercaps);
        gst_caps_free (filtercaps);

        gst_element_link (recorder->priv->encoder,
                          recorder->priv->sink);

        recorder->priv->eos_signal_id =
                g_signal_connect_object (G_OBJECT (recorder->priv->pipeline), "eos",
                                         G_CALLBACK (eos_cb), recorder, 0);
#elif HAVE_GSTREAMER_0_10
	g_object_set (recorder->priv->capsfilter, "filter-caps",  filtercaps, NULL);

	gst_element_link_many (recorder->priv->src,
			       recorder->priv->typefind,
			       recorder->priv->decoder,
			       NULL);

	gst_element_link_many (recorder->priv->audioconvert,
			       recorder->priv->audioscale,
			       recorder->priv->capsfilter,
			       recorder->priv->encoder,
			       recorder->priv->sink,
			       NULL);
#endif

        rb_debug ("Pipeline construction complete");
        return;

 missing_element:
        {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Failed to create %s element; check your installation"),
                             element_name);
                rb_recorder_gst_free_pipeline (recorder);
        }
}

static void
recorder_track_free (NautilusBurnRecorderTrack *track)
{
        if (track->contents.audio.filename) {
                char *lockfile = NULL;
                char *ext      = g_strrstr (track->contents.audio.filename, ".wav");
                if (ext)
                        lockfile = g_strndup (track->contents.audio.filename,
                                              ext - track->contents.audio.filename);

                if (g_file_test (track->contents.audio.filename, G_FILE_TEST_EXISTS)
                    && unlink (track->contents.audio.filename) != 0)
                        g_warning (_("Unable to unlink '%s'"), track->contents.audio.filename);
                if (lockfile) {
                        /* remove lockfile created by mkstemp */
                        if (unlink (lockfile) != 0)
                                g_warning (_("Unable to unlink '%s'"), lockfile);
                }
        }

        nautilus_burn_recorder_track_free (track);
}

static void
rb_recorder_finalize (GObject *object)
{
        RBRecorder *recorder = RB_RECORDER (object);

        rb_debug ("Finalize rb_recorder");

        rb_recorder_close (recorder, NULL);

        if (recorder->priv->recorder)
                nautilus_burn_recorder_cancel (recorder->priv->recorder, FALSE);

        g_list_foreach (recorder->priv->tracks,
                        (GFunc)recorder_track_free,
                        NULL);
        g_list_free (recorder->priv->tracks);

        G_OBJECT_CLASS (rb_recorder_parent_class)->finalize (object);
}

RBRecorder *
rb_recorder_new (GError **error)
{
        RBRecorder *recorder;

#ifdef HAVE_GSTREAMER_0_8
        GstElement *dummy;

        dummy = gst_element_factory_make ("fakesink", "fakesink");
        if (!dummy
            || !gst_scheduler_factory_make (NULL, GST_ELEMENT (dummy))) {
                g_set_error (error, RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Couldn't initialize scheduler.  Did you run gst-register?"));
                return NULL;
        }
#endif

        recorder = g_object_new (RB_TYPE_RECORDER, NULL, NULL);

        return recorder;
}

static gboolean
tick_timeout_cb (RBRecorder *recorder)
{
        gint64 position, total;
        double fraction;
        double rate;
        double elapsed;
        double secs;
        GstFormat format = GST_FORMAT_BYTES;
#ifdef HAVE_GSTREAMER_0_10
	GstState  state;
#endif

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (recorder->priv != NULL, FALSE);
        g_return_val_if_fail (recorder->priv->pipeline != NULL, FALSE);

#ifdef HAVE_GSTREAMER_0_8
        if (gst_element_get_state (recorder->priv->pipeline) != GST_STATE_PLAYING) {
#elif HAVE_GSTREAMER_0_10
	if (!gst_element_get_state (recorder->priv->pipeline, &state,  NULL, 3 * GST_SECOND)) {
		g_warning (_("Could not retrieve state from processing pipeline"));
		return TRUE;
	}
	if (state != GST_STATE_PLAYING) {		
#endif
                recorder->priv->tick_timeout_id = 0;
                if (recorder->priv->start_timer) {
                        g_timer_destroy (recorder->priv->start_timer);
                        recorder->priv->start_timer = NULL;
                }
                return FALSE;
        }

#ifdef  HAVE_GSTREAMER_0_8
        if (!gst_pad_query (recorder->priv->src_pad, GST_QUERY_POSITION, &format, &position)) {
                g_warning (_("Could not get current track position"));
                return TRUE;
        }

        if (!gst_pad_query (recorder->priv->src_pad, GST_QUERY_TOTAL, &format, &total)) {
#elif HAVE_GSTREAMER_0_10
		if (!gst_element_query_position (recorder->priv->src, &format, &position) || !gst_element_query_duration (recorder->priv->src, &format, &total)) {
#endif
                g_warning (_("Could not get current track position"));
                return TRUE;
        }

        if (! recorder->priv->start_timer) {
                recorder->priv->start_timer = g_timer_new ();
                recorder->priv->start_pos = position;
        }

        fraction = (float)position / (float)total;

        elapsed = g_timer_elapsed (recorder->priv->start_timer, NULL);

        rate = (double)(position - recorder->priv->start_pos) / elapsed;

        if (rate >= 1)
                secs = ceil ((total - position) / rate);
        else
                secs = -1;

        if (fraction != recorder->priv->progress) {
                recorder->priv->progress = fraction;
                g_signal_emit (G_OBJECT (recorder),
                               rb_recorder_signals [TRACK_PROGRESS_CHANGED],
                               0,
                               fraction, (long)secs);
        }

#ifdef HAVE_GSTREAMER_0_8
        /* Extra kick in the pants to keep things moving on a busy system */
        gst_bin_iterate (GST_BIN (recorder->priv->pipeline));
#endif

        return TRUE;
}

static gboolean
rb_recorder_sync_pipeline (RBRecorder *recorder,
                           GError    **error)
{
        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (recorder->priv != NULL, FALSE);
        g_return_val_if_fail (recorder->priv->pipeline != NULL, FALSE);
        
        rb_debug ("Syncing pipeline");
        if (recorder->priv->playing) {
                rb_debug ("Playing pipeline");
#ifdef HAVE_GSTREAMER_0_8
                if (gst_element_set_state (recorder->priv->pipeline, GST_STATE_PLAYING) == GST_STATE_FAILURE) {
#elif HAVE_GSTREAMER_0_10
                if (gst_element_set_state (recorder->priv->pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
#endif
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("Could not start pipeline playing"));
                        return FALSE;
                }
#ifdef HAVE_GSTREAMER_0_8
                recorder->priv->idle_id = g_idle_add ((GSourceFunc)gst_bin_iterate,
                                                      GST_BIN (recorder->priv->pipeline));
                recorder->priv->tick_timeout_id = g_timeout_add (200,
                                                                 (GSourceFunc)tick_timeout_cb,
                                                                 recorder);
#elif HAVE_GSTREAMER_0_10
		recorder->priv->tick_timeout_id = 
			g_timeout_add (200, (GSourceFunc)tick_timeout_cb, recorder);
#endif
        } else {
                rb_debug ("Pausing pipeline");
#ifdef HAVE_GSTREAMER_0_8
                if (gst_element_set_state (recorder->priv->pipeline, GST_STATE_PAUSED) == GST_STATE_FAILURE) {
#elif HAVE_GSTREAMER_0_10
                if (gst_element_set_state (recorder->priv->pipeline, GST_STATE_PAUSED) == GST_STATE_CHANGE_FAILURE) {
#endif
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("Could not pause playback"));
                        return FALSE;
                }
#ifdef HAVE_GSTREAMER_0_8
                if (recorder->priv->idle_id > 0) {
                        g_source_remove (recorder->priv->idle_id);
                        recorder->priv->idle_id = 0;
                }
#endif
                if (recorder->priv->tick_timeout_id > 0) {
                        g_source_remove (recorder->priv->tick_timeout_id);
                        recorder->priv->tick_timeout_id = 0;
                        if (recorder->priv->start_timer) {
                                g_timer_destroy (recorder->priv->start_timer);
                                recorder->priv->start_timer = NULL;
                        }
                }
        }
        return TRUE;
}

void
rb_recorder_close (RBRecorder *recorder,
                   GError    **error)
{
        g_return_if_fail (recorder != NULL);
        g_return_if_fail (RB_IS_RECORDER (recorder));

        rb_debug ("Closing rb_recorder");

#ifdef HAVE_GSTREAMER_0_10
	recorder->priv->got_audio_pad = FALSE;
#endif
        recorder->priv->playing = FALSE;
        g_free (recorder->priv->src_uri);
        recorder->priv->src_uri = NULL;

        g_free (recorder->priv->dest_file);
        recorder->priv->dest_file = NULL;

        if (recorder->priv->pipeline == NULL)
                return;

        rb_recorder_gst_free_pipeline (recorder);
}

void
rb_recorder_set_tmp_dir (RBRecorder   *recorder,
                         const char   *path,
                         GError      **error)
{
        g_return_if_fail (recorder != NULL);
        g_return_if_fail (RB_IS_RECORDER (recorder));
        g_return_if_fail (path != NULL);

        /* Check to make sure it exists and has enough space */

        g_free (recorder->priv->tmp_dir);
        recorder->priv->tmp_dir = g_strdup (path);
}

static char *
get_dest_from_uri (const char *tmp_dir,
                   const char *src_uri)
{
        char *lock_filename;
        char *filename;
        int   fd;

        lock_filename = g_build_filename (tmp_dir, "rb-burn-tmp.XXXXXX", NULL);
        fd = g_mkstemp (lock_filename);
        close (fd);

        /* keep empty file around until finalize
           it will serve as a lock file to protect our new filename */

        filename = g_strdup_printf ("%s.wav", lock_filename);
        g_free (lock_filename);

        return filename;
}

void
rb_recorder_open (RBRecorder   *recorder,
                  const char   *src_uri,
                  const char   *cdtext,
                  GError      **error)
{
        char    *dest_file;
        gboolean audiocd_mode = src_uri && g_str_has_prefix (src_uri, "audiocd://");
        
        g_return_if_fail (recorder != NULL);
        g_return_if_fail (RB_IS_RECORDER (recorder));
        g_return_if_fail (recorder->priv != NULL);
        g_return_if_fail (audiocd_mode != TRUE);

        rb_recorder_close (recorder, NULL);

        if (src_uri == NULL) {
                recorder->priv->playing = FALSE;
                return;
        }

        rb_recorder_construct (recorder, src_uri, error);
        if (error && *error)
                return;
                
#ifdef HAVE_GSTREAMER_0_8
        if (recorder->priv->idle_id > 0) {
                g_source_remove (recorder->priv->idle_id);
                recorder->priv->idle_id = 0;
        }
#elif HAVE_GSTREAMER_0_10
	recorder->priv->got_audio_pad = FALSE;
#endif

        g_object_set (G_OBJECT (recorder->priv->src), "iradio-mode", FALSE, NULL);
        gst_element_set_state (recorder->priv->src, GST_STATE_NULL);
        g_object_set (G_OBJECT (recorder->priv->src), "location", src_uri, NULL);

        g_free (recorder->priv->src_uri);
        recorder->priv->src_uri = g_strdup (src_uri);

        dest_file = get_dest_from_uri (recorder->priv->tmp_dir, src_uri);
        gst_element_set_state (recorder->priv->sink, GST_STATE_NULL);
        g_object_set (G_OBJECT (recorder->priv->sink), "location", dest_file, NULL);
        
        g_free (recorder->priv->dest_file);
        recorder->priv->dest_file = g_strdup (dest_file);
        g_free (dest_file);

        recorder->priv->playing = FALSE;

        add_track (recorder, cdtext);

        if (!rb_recorder_sync_pipeline (recorder, error)) {
                rb_recorder_close (recorder, NULL);
        }
}

void
rb_recorder_write (RBRecorder *recorder,
                   GError    **error)
{
        g_return_if_fail (recorder != NULL);
        g_return_if_fail (RB_IS_RECORDER (recorder));
        g_return_if_fail (recorder->priv != NULL);

        g_return_if_fail (recorder->priv->src_uri != NULL);

        recorder->priv->playing = TRUE;

        g_return_if_fail (recorder->priv->pipeline != NULL);

        g_signal_emit (recorder,
                       rb_recorder_signals [ACTION_CHANGED],
                       0,
                       RB_RECORDER_ACTION_FILE_CONVERTING);

        rb_recorder_sync_pipeline (recorder, error);
}

void
rb_recorder_pause (RBRecorder *recorder,
                   GError    **error)
{
        g_return_if_fail (recorder != NULL);
        g_return_if_fail (RB_IS_RECORDER (recorder));
        g_return_if_fail (recorder->priv != NULL);

        if (!recorder->priv->playing)
                return;

        recorder->priv->playing = FALSE;

        g_return_if_fail (recorder->priv->pipeline != NULL);

        rb_recorder_sync_pipeline (recorder, NULL);
}

char *
rb_recorder_get_device (RBRecorder  *recorder,
                        GError     **error)
{
        NautilusBurnDrive *drive;

        g_return_val_if_fail (recorder != NULL, NULL);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), NULL);

        if (error)
                *error = NULL;

        drive = recorder->priv->drive;

        if (! drive) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Cannot find drive"));
                return NULL;
        }

        return g_strdup (drive->device);
}

gboolean
rb_recorder_set_device (RBRecorder  *recorder,
                        const char  *device,
                        GError     **error)
{
        GList *drives;
        GList *tmp;

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (device != NULL, FALSE);

        if (error)
                *error = NULL;

        if (recorder->priv->drive) {
                nautilus_burn_drive_unref (recorder->priv->drive);
                recorder->priv->drive = NULL;
        }

        drives = nautilus_burn_drive_get_list (TRUE, FALSE);

        for (tmp = drives; tmp != NULL; tmp = tmp->next) {
                NautilusBurnDrive *drive = (NautilusBurnDrive *) tmp->data;

                if (strcmp (drive->device, device) == 0) {
                        recorder->priv->drive = drive;
                        break;
                }

                nautilus_burn_drive_unref (drive);
        }
        g_list_free (drives);

        if (! recorder->priv->drive) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Cannot find drive %s"),
                             device);
                return FALSE;
        }

        if (! (recorder->priv->drive->type & NAUTILUS_BURN_DRIVE_TYPE_CD_RECORDER)) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Drive %s is not a recorder"),
                             device);
                return FALSE;
        }

        return TRUE;
}

static void
rb_recorder_action_changed_cb (NautilusBurnRecorder        *cdrecorder,
                               NautilusBurnRecorderActions  cd_action,
                               NautilusBurnRecorderMedia    cd_media,
                               gpointer                     data)
{
        RBRecorder      *recorder = (RBRecorder*) data;
        RBRecorderAction action;

        switch (cd_action) {
        case NAUTILUS_BURN_RECORDER_ACTION_PREPARING_WRITE:
                action = RB_RECORDER_ACTION_DISC_PREPARING_WRITE;
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_WRITING:
                action = RB_RECORDER_ACTION_DISC_WRITING;
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_FIXATING:
                action = RB_RECORDER_ACTION_DISC_FIXATING;
                break;
        case NAUTILUS_BURN_RECORDER_ACTION_BLANKING:
                action = RB_RECORDER_ACTION_DISC_BLANKING;
                break;
        default:
                action = RB_RECORDER_ACTION_UNKNOWN;
        }

        g_signal_emit (recorder,
                       rb_recorder_signals [ACTION_CHANGED],
                       0,
                       action);
}

#ifdef NAUTILUS_BURN_DRIVE_SIZE_TO_TIME
/* If nautilus-cd-burner >= 2.12 */
static void
rb_recorder_burn_progress_cb (NautilusBurnRecorder *cdrecorder,
                              gdouble               fraction,
                              long                  secs,
                              gpointer              data)
{
        RBRecorder *recorder = (RBRecorder*) data;

        g_signal_emit (recorder,
                       rb_recorder_signals [BURN_PROGRESS_CHANGED],
                       0,
                       fraction,
                       secs);
}
#else
/* If nautilus-cd-burner == 2.10 */
static void
rb_recorder_burn_progress_cb (NautilusBurnRecorder *cdrecorder,
                              gdouble               fraction,
                              gpointer              data)
{
        RBRecorder *recorder = (RBRecorder*) data;
        long        secs     = -1;

        g_signal_emit (recorder,
                       rb_recorder_signals [BURN_PROGRESS_CHANGED],
                       0,
                       fraction,
                       secs);
}
#endif

static gboolean
rb_recorder_insert_cd_request_cb (NautilusBurnRecorder *cdrecorder,
                                  gboolean              is_reload,
                                  gboolean              can_rewrite,
                                  gboolean              busy_cd,
                                  gpointer              data)
{
        RBRecorder *recorder = (RBRecorder*) data;
        gboolean    res = FALSE;

        g_signal_emit (recorder,
                       rb_recorder_signals [INSERT_MEDIA_REQUEST],
                       0,
                       is_reload,
                       can_rewrite,
                       busy_cd,
                       &res);

        return res;
}

static int
rb_recorder_warn_data_loss_cb (NautilusBurnRecorder *cdrecorder,
                               RBRecorder           *recorder)
{
        int res = 0;
        int ret = 0;
        int retry_val;
        int cancel_val;
        int erase_val;

        g_signal_emit (G_OBJECT (recorder),
                       rb_recorder_signals [WARN_DATA_LOSS],
                       0, &res);

#ifdef NAUTILUS_BURN_DRIVE_SIZE_TO_TIME
        retry_val  = NAUTILUS_BURN_RECORDER_RESPONSE_RETRY;
        cancel_val = NAUTILUS_BURN_RECORDER_RESPONSE_CANCEL;
        erase_val  = NAUTILUS_BURN_RECORDER_RESPONSE_ERASE;
#else
        retry_val  = TRUE;
        cancel_val = TRUE;
        erase_val  = FALSE;
#endif

        switch (res) {
        case RB_RECORDER_RESPONSE_RETRY:
                ret = retry_val;
                break;
        case RB_RECORDER_RESPONSE_ERASE:
                ret = erase_val;
                break;
        case RB_RECORDER_RESPONSE_CANCEL:
        default:
                ret = cancel_val;
                break;
        }

        return ret;
}

char *
rb_recorder_get_default_device (void)
{
        NautilusBurnDrive *drive;
        char              *device = NULL;

        drive = rb_recorder_get_default_drive ();

        if (drive) {
                device = g_strdup (drive->device);
                nautilus_burn_drive_unref (drive);
        }

        return device;
}

gint64
rb_recorder_get_media_length (RBRecorder *recorder,
                              GError    **error)
{
        gint64                size;
        gint64                secs;

        g_return_val_if_fail (recorder != NULL, -1);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), -1);
        g_return_val_if_fail (recorder->priv != NULL, -1);

        if (! recorder->priv->drive) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_INTERNAL,
                             _("No writable drives found."));

                return -1;
        }

        size = nautilus_burn_drive_get_media_size (recorder->priv->drive);
        if (size > 0) {
#ifdef NAUTILUS_BURN_DRIVE_SIZE_TO_TIME
                secs = NAUTILUS_BURN_DRIVE_SIZE_TO_TIME (size);
#else
                secs = SIZE_TO_TIME (size);
#endif
        } else {
                secs = size;
        }

        return secs;
}

/* Copyright (C) Bastien Nocera */
/* From xine-lib, whoop */
typedef struct __attribute__((__packed__)) {
	gint16   wFormatTag;
	gint16   nChannels;
	gint32   nSamplesPerSec;
	gint32   nAvgBytesPerSec;
	gint16   nBlockAlign;
	gint16   wBitsPerSample;
	gint16   cbSize;
} waveformat;
#ifndef ATTRIBUTE_PACKED
#pragma pack()
#endif

#define ACB_ERROR_OPEN				-1
#define ACB_ERROR_NOT_WAVE_TOO_SMALL		-2
#define ACB_ERROR_NOT_WAVE_FILE			-3
#define ACB_ERROR_NOT_WAVE_FORMAT		-4

/* Copyright (C) Bastien Nocera */
/* Data from
 * http://www.onicos.com/staff/iz/formats/wav.html
 */
static gint64
acb_wave_time (const char *filename)
{
#define WAV_SIGNATURE_SIZE 16
#define LPCM_BITRATE (16 * 44100 * 2)
        char        buffer [WAV_SIGNATURE_SIZE];
        int         fd, len;
        waveformat *wav;

        fd = open (filename, 0);
        if (fd < 0)
                return ACB_ERROR_OPEN;

        if (read (fd, buffer, WAV_SIGNATURE_SIZE) != WAV_SIGNATURE_SIZE)
                return ACB_ERROR_NOT_WAVE_TOO_SMALL;

        if ((buffer [0] != 'R') ||
            (buffer [1] != 'I') ||
            (buffer [2] != 'F') ||
            (buffer [3] != 'F') ||
            (buffer [8] != 'W') ||
            (buffer [9] != 'A') ||
            (buffer [10] != 'V') ||
            (buffer [11] != 'E') ||
            (buffer [12] != 'f') ||
            (buffer [13] != 'm') ||
            (buffer [14] != 't') ||
            (buffer [15] != ' '))
                return ACB_ERROR_NOT_WAVE_FORMAT;

        if (read (fd, &len, sizeof(len)) != sizeof (len)) {
                close (fd);
                return ACB_ERROR_NOT_WAVE_TOO_SMALL;
        }

        if (GINT_FROM_LE (len) != 16) {
                close (fd);
                g_print ("file len not defined\n");
                return ACB_ERROR_NOT_WAVE_FORMAT;
        }

        wav = g_malloc (len);
        if (read (fd, wav, len) != len) {
                g_free (wav);
                close (fd);
                return ACB_ERROR_NOT_WAVE_FILE;
        }

        close (fd);

        if (wav->nChannels != 2
            || wav->nSamplesPerSec != 44100
            || wav->wBitsPerSample != 16) {
                g_free (wav);
                return ACB_ERROR_NOT_WAVE_FORMAT;
        }

        g_free (wav);

        {
                struct stat buf;

                if (stat (filename, &buf) != 0)
                        return ACB_ERROR_OPEN;

                return buf.st_size * 8 / LPCM_BITRATE;
        }
}

static gint64
get_tracks_length (RBRecorder *recorder,
                   GError    **error)
{
        GList *l;
        gint64 total = 0;

        if (!recorder->priv->tracks)
                return -1;

        for (l = recorder->priv->tracks; l; l = l->next) {
                NautilusBurnRecorderTrack *track = l->data;
                gint64                     length;

                length = acb_wave_time (track->contents.audio.filename);
                if (length < 0) {
                        g_warning (_("Could not get track time for file: %s"),
                                   track->contents.audio.filename);
                        return -1;
                }
                total += length;
        }

        return total;
}

int
rb_recorder_burn_cancel (RBRecorder *recorder)
{

        g_return_val_if_fail (recorder != NULL, RB_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), RB_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (recorder->priv != NULL, RB_RECORDER_RESULT_ERROR);

        g_return_val_if_fail (recorder->priv->recorder != NULL, RB_RECORDER_RESULT_ERROR);

        nautilus_burn_recorder_cancel (recorder->priv->recorder, FALSE);

        return RB_RECORDER_RESULT_FINISHED;
}

int
rb_recorder_burn (RBRecorder *recorder,
                  int         speed,
                  GError    **error)
{
        NautilusBurnRecorder          *cdrecorder;
        NautilusBurnRecorderWriteFlags flags;
        GError                        *local_error = NULL;
        int                            res;
        gboolean                       result;
        gint64                         tracks_length;

        g_return_val_if_fail (recorder != NULL, RB_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), RB_RECORDER_RESULT_ERROR);
        g_return_val_if_fail (recorder->priv != NULL, RB_RECORDER_RESULT_ERROR);

        g_return_val_if_fail (recorder->priv->recorder == NULL, RB_RECORDER_RESULT_ERROR);

        if (!recorder->priv->tracks)
                return RB_RECORDER_RESULT_ERROR;

        if (! recorder->priv->drive) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_INTERNAL,
                             _("No writable drives found."));
                return RB_RECORDER_RESULT_ERROR;
        }

        tracks_length = get_tracks_length (recorder, error);
        if (tracks_length <= 0) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_INTERNAL,
                             _("Could not determine audio track durations."));
                return RB_RECORDER_RESULT_ERROR;
        }

        cdrecorder = nautilus_burn_recorder_new ();
        recorder->priv->recorder = cdrecorder;

        g_signal_connect_object (G_OBJECT (cdrecorder), "progress-changed",
                                 G_CALLBACK (rb_recorder_burn_progress_cb), recorder, 0);
        g_signal_connect_object (G_OBJECT (cdrecorder), "action-changed",
                                 G_CALLBACK (rb_recorder_action_changed_cb), recorder, 0);
        g_signal_connect_object (G_OBJECT (cdrecorder), "insert-media-request",
                                 G_CALLBACK (rb_recorder_insert_cd_request_cb), recorder, 0);
        g_signal_connect_object (G_OBJECT (cdrecorder), "warn-data-loss",
                                 G_CALLBACK (rb_recorder_warn_data_loss_cb), recorder, 0);

        flags = 0;
        if (FALSE)
                flags |= NAUTILUS_BURN_RECORDER_WRITE_DUMMY_WRITE;
        if (TRUE)
                flags |= NAUTILUS_BURN_RECORDER_WRITE_DEBUG;
        if (TRUE)
                flags |= NAUTILUS_BURN_RECORDER_WRITE_DISC_AT_ONCE;

#ifdef NAUTILUS_BURN_DRIVE_SIZE_TO_TIME
        /* If nautilus-cd-burner >= 2.12 */
        res = nautilus_burn_recorder_write_tracks (cdrecorder,
                                                   recorder->priv->drive,
                                                   recorder->priv->tracks,
                                                   speed,
                                                   flags,
                                                   &local_error);
#else
        /* If nautilus-cd-burner == 2.10 */
        res = nautilus_burn_recorder_write_tracks (cdrecorder,
                                                   recorder->priv->drive,
                                                   recorder->priv->tracks,
                                                   speed,
                                                   flags);
#endif

        if (res == NAUTILUS_BURN_RECORDER_RESULT_FINISHED) {
                result = RB_RECORDER_RESULT_FINISHED;
        } else if (res == NAUTILUS_BURN_RECORDER_RESULT_ERROR) {
                char *msg;


                if (local_error) {
                        msg = g_strdup_printf (_("There was an error writing to the CD:\n%s"),
                                               local_error->message);
                        g_error_free (local_error);
                } else {
                        msg = g_strdup (_("There was an error writing to the CD"));
                }

                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             msg);
                g_free (msg);
                result = RB_RECORDER_RESULT_ERROR;
        } else {
                /* cancelled */
                result = RB_RECORDER_RESULT_CANCEL;
        }
        
        g_object_unref (cdrecorder);
        recorder->priv->recorder = NULL;

        return result;
}
