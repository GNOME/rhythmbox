/* -*- Mode: C; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 8 -*- */
/*
 * arch-tag: Implementation of GStreamer recorder backend
 *
 * Copyright (C) 2004 William Jon McCann <mccann@jhu.edu>
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
#include <gst/gst.h>
#include <gst/gconf/gconf.h>
#include <gst/play/play.h>
#include <libgnome/gnome-i18n.h>
#include <cd-recorder.h>
#include <sys/vfs.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <libgnomevfs/gnome-vfs-utils.h>

#include "rb-recorder.h"
#include "rb-recorder-marshal.h"

#include "rb-debug.h"

static void rb_recorder_class_init (RBRecorderClass *klass);
static void rb_recorder_init       (RBRecorder      *recorder);
static void rb_recorder_finalize   (GObject         *object);

struct _RBRecorderPrivate {
        char       *src_uri;
        char       *dest_file;
        char       *tmp_dir;
        
        GstElement *pipeline;
        
        GstElement *src;
        GstPad     *src_pad;
        GstElement *decoder;
        GstElement *typefind;
        GstElement *audioconvert;
        GstElement *audioscale;
        GstElement *encoder;
        GstElement *sink;

        guint       idle_id;
        guint       error_signal_id;
        guint       eos_signal_id;
        guint       tick_timeout_id;

        double      progress;

        GList      *tracks;
        CDDrive    *drive;

        gboolean    playing;

        GError     *error;
};

typedef enum {
        EOS,
        ACTION_CHANGED,
        TRACK_PROGRESS_CHANGED,
        BURN_PROGRESS_CHANGED,
        INSERT_MEDIA_REQUEST,
        ERROR,
        LAST_SIGNAL
} RBRecorderSignalType;

typedef struct {
        RBRecorder *object;
        GError     *error;
        GValue     *info;
} RBRecorderSignal;

static guint rb_recorder_signals[LAST_SIGNAL] = { 0 };

static GObjectClass *parent_class = NULL;

gboolean
rb_recorder_enabled (void)
{
        char    *device  = rb_recorder_get_default_device ();
        gboolean enabled = (device != NULL);

        g_free (device);

        return enabled;
}

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

        parent_class = g_type_class_peek_parent (klass);
        object_class = (GObjectClass *) klass;

        object_class->finalize = rb_recorder_finalize;

        rb_recorder_signals[EOS] =
                g_signal_new ("eos",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__VOID,
                              G_TYPE_NONE,
                              0);
        rb_recorder_signals[TRACK_PROGRESS_CHANGED] =
                g_signal_new ("track-progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__DOUBLE,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_DOUBLE);
        rb_recorder_signals[BURN_PROGRESS_CHANGED] =
                g_signal_new ("burn-progress-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__DOUBLE,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_DOUBLE);
        rb_recorder_signals[ACTION_CHANGED] =
                g_signal_new ("action-changed",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__INT,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_INT);
        rb_recorder_signals[INSERT_MEDIA_REQUEST] =
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
        rb_recorder_signals[ERROR] =
                g_signal_new ("error",
                              G_TYPE_FROM_CLASS (klass),
                              G_SIGNAL_RUN_LAST,
                              0,
                              NULL, NULL,
                              g_cclosure_marshal_VOID__POINTER,
                              G_TYPE_NONE,
                              1,
                              G_TYPE_POINTER);
}

static void
rb_recorder_init (RBRecorder *recorder)
{
        recorder->priv = g_new0 (RBRecorderPrivate, 1);

        recorder->priv->tmp_dir = g_strdup (g_get_tmp_dir ());
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
        }

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

        gst_object_unref (GST_OBJECT (recorder->priv->pipeline));
        recorder->priv->pipeline = NULL;
}

static gboolean
add_track (RBRecorder *recorder,
           const char *cdtext)
{
        Track *track;
        char  *filename;

        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);

        filename = g_strdup (recorder->priv->dest_file);

        track = g_new0 (Track, 1);

        track->type = TRACK_TYPE_AUDIO;
        track->contents.audio.filename = filename;
        if (cdtext)
                track->contents.audio.cdtext = g_strdup (cdtext);

        recorder->priv->tracks = g_list_append (recorder->priv->tracks, track);

        return TRUE;
}

static void
eos_cb (GstElement *element,
        RBRecorder *recorder)
{
        rb_debug ("EOS");

        if (recorder->priv->pipeline)
                gst_element_set_state (recorder->priv->pipeline, GST_STATE_NULL);

        g_signal_emit (G_OBJECT (recorder), rb_recorder_signals[EOS], 0);
}

static gboolean
error_signal_idle (RBRecorderSignal *signal)
{
        g_signal_emit (G_OBJECT (signal->object),
                       rb_recorder_signals[ERROR],
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

static void
rb_recorder_construct (RBRecorder *recorder,
                       const char *uri,
                       GError    **error)
{
        char    *element_name = NULL;
        GstCaps *filtercaps;

#define MAKE_ELEMENT_OR_LOSE(NAME, NICE) G_STMT_START {                 \
                element_name = #NAME ;                                  \
                rb_debug ("Constructing element \"" #NICE "\"");        \
                recorder->priv->NICE = gst_element_factory_make (#NAME, #NICE); \
                if (!recorder->priv->NICE)                              \
                        goto missing_element;                           \
        } G_STMT_END

        rb_recorder_gst_free_pipeline (recorder);

        /* The recording pipeline looks like:
         *  { src ! spider ! audioconvert ! audioscale
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

        recorder->priv->error_signal_id =
                g_signal_connect_object (G_OBJECT (recorder->priv->pipeline),
                                         "error",
                                         G_CALLBACK (error_cb),
                                         recorder, 0);

        /* Construct elements */

        /* The source */

        MAKE_ELEMENT_OR_LOSE(gnomevfssrc, src);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->src);

        recorder->priv->src_pad = gst_element_get_pad (recorder->priv->src, "src");
        g_assert (recorder->priv->src_pad); /* TODO: GError */

        /* The queue */

        MAKE_ELEMENT_OR_LOSE(typefind, typefind);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->typefind);

        MAKE_ELEMENT_OR_LOSE(spider, decoder);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->decoder);

        MAKE_ELEMENT_OR_LOSE(audioconvert, audioconvert);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->audioconvert);

        MAKE_ELEMENT_OR_LOSE(audioscale, audioscale);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->audioscale);

        MAKE_ELEMENT_OR_LOSE(wavenc, encoder);
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->encoder);

        /* Output sink */

        MAKE_ELEMENT_OR_LOSE(gnomevfssink, sink);
        if (recorder->priv->sink == NULL) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_NO_AUDIO,
                             _("Could not create audio output element; check your settings"));
                gst_object_unref (GST_OBJECT (recorder->priv->pipeline));
                recorder->priv->pipeline = NULL;
                return;
        }
        gst_bin_add (GST_BIN (recorder->priv->pipeline), recorder->priv->sink);

        gst_element_link_many (recorder->priv->src,
                               recorder->priv->typefind,
                               recorder->priv->decoder,
                               recorder->priv->audioconvert,
                               recorder->priv->audioscale,
                               NULL);

        filtercaps = gst_caps_new_simple ("audio/x-raw-int",
                                          "channels", G_TYPE_INT, 2,
                                          "rate",     G_TYPE_INT, 44100,
                                          "width",    G_TYPE_INT, 16,
                                          "depth",    G_TYPE_INT, 16,
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
recorder_track_free (Track *track)
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

        cd_recorder_track_free (track);
}

static void
rb_recorder_finalize (GObject *object)
{
        RBRecorder *recorder = RB_RECORDER (object);

        rb_debug ("Finalize rb_recorder");

        rb_recorder_close (recorder, NULL);

        g_list_foreach (recorder->priv->tracks,
                        (GFunc)recorder_track_free,
                        NULL);
        g_list_free (recorder->priv->tracks);

        g_free (recorder->priv);

        G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBRecorder *
rb_recorder_new (GError **error)
{
        RBRecorder *recorder;
        GstElement *dummy;

        rb_debug ("New rb_recorder");

        dummy = gst_element_factory_make ("fakesink", "fakesink");
        if (!dummy
            || !gst_scheduler_factory_make (NULL, GST_ELEMENT (dummy))) {
                g_set_error (error, RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Couldn't initialize scheduler.  Did you run gst-register?"));
                return NULL;
        }

        recorder = g_object_new (RB_TYPE_RECORDER, NULL);

        return recorder;
}

static gboolean
tick_timeout_cb (RBRecorder *recorder)
{
        guint64 position, total;
        double fraction;
        static GstFormat format = GST_FORMAT_BYTES;

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (recorder->priv != NULL, FALSE);
        g_return_val_if_fail (recorder->priv->pipeline != NULL, FALSE);

        if (gst_element_get_state (recorder->priv->pipeline) != GST_STATE_PLAYING) {
                recorder->priv->tick_timeout_id = 0;
                return FALSE;
        }

        if (!gst_pad_query (recorder->priv->src_pad, GST_QUERY_POSITION, &format, &position)) {
                g_warning (_("Could not get current track position"));
                recorder->priv->tick_timeout_id = 0;
                return TRUE;
        }

        if (!gst_pad_query (recorder->priv->src_pad, GST_QUERY_TOTAL, &format, &total)) {
                g_warning (_("Could not get current track position"));
                return TRUE;
        }

        fraction = (float)position / (float)total;
        if (fraction != recorder->priv->progress) {
                recorder->priv->progress = fraction;
                g_signal_emit (G_OBJECT (recorder),
                               rb_recorder_signals[TRACK_PROGRESS_CHANGED],
                               0,
                               fraction);
        }

        /* Extra kick in the pants to keep things moving on a busy system */
        gst_bin_iterate (GST_BIN (recorder->priv->pipeline));

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
                if (gst_element_set_state (recorder->priv->pipeline,
                                           GST_STATE_PLAYING) == GST_STATE_FAILURE) {
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("Could not start pipeline playing"));
                        return FALSE;
                }                               
                recorder->priv->idle_id = g_idle_add ((GSourceFunc)gst_bin_iterate,
                                                      GST_BIN (recorder->priv->pipeline));
                recorder->priv->tick_timeout_id = g_timeout_add (200,
                                                                 (GSourceFunc)tick_timeout_cb,
                                                                 recorder);
        } else {
                rb_debug ("Pausing pipeline");
                if (gst_element_set_state (recorder->priv->pipeline,
                                           GST_STATE_PAUSED) == GST_STATE_FAILURE) {
                        g_set_error (error,
                                     RB_RECORDER_ERROR,
                                     RB_RECORDER_ERROR_GENERAL,
                                     _("Could not pause playback"));
                        return FALSE;
                }
                if (recorder->priv->idle_id > 0) {
                        g_source_remove (recorder->priv->idle_id);
                        recorder->priv->idle_id = 0;
                }
                if (recorder->priv->tick_timeout_id > 0) {
                        g_source_remove (recorder->priv->tick_timeout_id);
                        recorder->priv->tick_timeout_id = 0;
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
                
        if (recorder->priv->idle_id > 0) {
                g_source_remove (recorder->priv->idle_id);
                recorder->priv->idle_id = 0;
        }

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
                       rb_recorder_signals[ACTION_CHANGED],
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

gboolean
rb_recorder_set_device  (RBRecorder  *recorder,
                         const char  *device,
                         GError     **error)
{
        GList *drives;
        GList *tmp;

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (device != NULL, FALSE);

        *error = NULL;

        drives = scan_for_cdroms (TRUE, FALSE);
        
        for (tmp = drives; tmp != NULL; tmp = tmp->next) {
                CDDrive *drive = (CDDrive*) tmp->data;
                if (strcmp (drive->device, device) == 0) {
                        recorder->priv->drive = drive;
                        break;
                }
                cd_drive_free (drive);
        }
        g_list_free (drives);

        if (recorder->priv->drive == NULL) {
                g_set_error (error, RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Cannot find drive %s"),
                             device);
                return FALSE;
        }

        if (!(recorder->priv->drive->type
              & (CDDRIVE_TYPE_CD_RECORDER
                 | CDDRIVE_TYPE_CDRW_RECORDER
                 | CDDRIVE_TYPE_DVD_RAM_RECORDER
                 | CDDRIVE_TYPE_DVD_RW_RECORDER
                 | CDDRIVE_TYPE_DVD_PLUS_R_RECORDER
                 | CDDRIVE_TYPE_DVD_PLUS_RW_RECORDER))) {
                g_set_error (error, RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("Drive %s is not a recorder"),
                             device);
                return FALSE;
        }

        return TRUE;
}

static void
rb_recorder_action_changed_cb (CDRecorder        *cdrecorder,
                               CDRecorderActions  cd_action,
                               CDRecorderMedia    cd_media,
                               gpointer           data)
{
        RBRecorder      *recorder = (RBRecorder*) data;
        RBRecorderAction action;

        switch (cd_action) {
        case PREPARING_WRITE:
                action = RB_RECORDER_ACTION_DISC_PREPARING_WRITE;
                break;
        case WRITING:
                action = RB_RECORDER_ACTION_DISC_WRITING;
                break;
        case FIXATING:
                action = RB_RECORDER_ACTION_DISC_FIXATING;
                break;
        case BLANKING:
                action = RB_RECORDER_ACTION_DISC_BLANKING;
                break;
        default:
                action = RB_RECORDER_ACTION_UNKNOWN;
        }

        g_signal_emit (recorder,
                       rb_recorder_signals[ACTION_CHANGED],
                       0,
                       action);
}

static void
rb_recorder_burn_progress_cb (CDRecorder *cdrecorder,
                              gdouble     fraction,
                              gpointer    data)
{
        RBRecorder *recorder = (RBRecorder*) data;

        g_signal_emit (recorder,
                       rb_recorder_signals[BURN_PROGRESS_CHANGED],
                       0,
                       fraction);
}

static gboolean
rb_recorder_insert_cd_request_cb (CDRecorder *cdrecorder,
                                  gboolean    is_reload,
                                  gboolean    can_rewrite,
                                  gboolean    busy_cd,
                                  gpointer    data)
{

        RBRecorder *recorder = (RBRecorder*) data;
        gboolean    res = FALSE;

        g_signal_emit (recorder,
                       rb_recorder_signals[INSERT_MEDIA_REQUEST],
                       0,
                       is_reload,
                       can_rewrite,
                       busy_cd,
                       &res);

        return res;
}

static CDDrive *
rb_recorder_get_default_drive (void)
{
        CDDrive *drive  = NULL;
        GList   *drives = NULL;

        drives = scan_for_cdroms (TRUE, FALSE);

        if (drives) {
                /* FIXME: really need someone to write a cd_drive_copy */
                drive = (CDDrive*) drives->data;
                drives = drives->next;
        }

        g_list_foreach (drives, (GFunc)cd_drive_free, NULL);
        g_list_free (drives);

        return drive;
}

char *
rb_recorder_get_default_device (void)
{
        CDDrive *drive;
        char    *device = NULL;

        drive = rb_recorder_get_default_drive ();

        if (drive) {
                device = g_strdup (drive->device);
                cd_drive_free (drive);
        }

        return device;
}

gint64
rb_recorder_get_media_length (RBRecorder *recorder,
                              GError    **error)
{
        char  *device;
        gint64 size;
        gint64 secs;

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (recorder->priv != NULL, FALSE);

        if (recorder->priv->drive)
                device = g_strdup (recorder->priv->drive->device);
        else
                device = rb_recorder_get_default_device ();
                
        size = cd_drive_get_media_size_from_path (device);
        if (size > 0)
                secs = SIZE_TO_TIME(size);
        else
                secs = size;

        g_free (device);

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
        char        buffer[WAV_SIGNATURE_SIZE];
        int         fd, len;
        waveformat *wav;

        fd = open (filename, 0);
        if (fd < 0)
                return ACB_ERROR_OPEN;

        if (read (fd, buffer, WAV_SIGNATURE_SIZE) != WAV_SIGNATURE_SIZE)
                return ACB_ERROR_NOT_WAVE_TOO_SMALL;

        if ((buffer[0] != 'R') ||
            (buffer[1] != 'I') ||
            (buffer[2] != 'F') ||
            (buffer[3] != 'F') ||
            (buffer[8] != 'W') ||
            (buffer[9] != 'A') ||
            (buffer[10] != 'V') ||
            (buffer[11] != 'E') ||
            (buffer[12] != 'f') ||
            (buffer[13] != 'm') ||
            (buffer[14] != 't') ||
            (buffer[15] != ' '))
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
                Track *track = l->data;
                gint64 length;

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

gboolean
rb_recorder_burn (RBRecorder *recorder,
                  GError    **error)
{
        CDRecorder          *cdrecorder;
        CDRecorderWriteFlags flags;
        int                  res;
        gboolean             result;
        gint64               tracks_length;
        gint64               media_length;

        g_return_val_if_fail (recorder != NULL, FALSE);
        g_return_val_if_fail (RB_IS_RECORDER (recorder), FALSE);
        g_return_val_if_fail (recorder->priv != NULL, FALSE);
                
        if (!recorder->priv->tracks)
                return FALSE;

        if (!recorder->priv->drive) {
                char *default_device = rb_recorder_get_default_device ();

                if (!default_device) {
                        g_warning (_("Could not determine default writer device"));
                        return FALSE;
                }

                rb_recorder_set_device  (recorder, default_device, error);
                g_free (default_device);
                if (error && *error)
                        return FALSE;
        }

        tracks_length = get_tracks_length (recorder, error);
        if (tracks_length <= 0) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_INTERNAL,
                             _("Could not determine audio track durations."));
                return FALSE;
        }

        media_length = rb_recorder_get_media_length (recorder, error);

        /* don't fail here if media length cannot be determined
         * cd_recorder_write_tracks will fail and issue a signal */
        if ((media_length > 0) && (tracks_length > media_length)) {
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             _("This playlist is %"
                               G_GINT64_FORMAT
                               " minutes long.  "
                               "This exceeds the %"
                               G_GINT64_FORMAT
                               " minute length of the media in the drive."),
                             tracks_length / 60,
                             media_length / 60);
                return FALSE;
        }
        
        cdrecorder = cd_recorder_new ();

        g_signal_connect_object (G_OBJECT (cdrecorder), "progress-changed",
                                 G_CALLBACK (rb_recorder_burn_progress_cb), recorder, 0);
        g_signal_connect_object (G_OBJECT (cdrecorder), "action-changed",
                                 G_CALLBACK (rb_recorder_action_changed_cb), recorder, 0);
        g_signal_connect_object (G_OBJECT (cdrecorder), "insert-cd-request",
                                 G_CALLBACK (rb_recorder_insert_cd_request_cb), recorder, 0);

        flags = 0;
        if (FALSE)
                flags |= CDRECORDER_DUMMY_WRITE;
        if (TRUE)
                flags |= CDRECORDER_DEBUG;
        if (TRUE)
                flags |= CDRECORDER_DISC_AT_ONCE;

        res = cd_recorder_write_tracks (cdrecorder,
                                        recorder->priv->drive,
                                        recorder->priv->tracks,
                                        0, 
                                        flags);

        if (res == RESULT_FINISHED) {
                result = TRUE;
        } else if (res == RESULT_ERROR) {
                char *msg;

                msg = cd_recorder_get_error_message (cdrecorder) ?
                        g_strdup_printf (_("There was an error writing to the CD:\n%s"),
                                         cd_recorder_get_error_message (cdrecorder))
                        : g_strdup (_("There was an error writing to the CD"));
                g_set_error (error,
                             RB_RECORDER_ERROR,
                             RB_RECORDER_ERROR_GENERAL,
                             msg);
                g_free (msg);
                result = FALSE;
        } else {
                /* cancelled */
                result = FALSE;
        }
        
        g_object_unref (cdrecorder);

        return result;
}
