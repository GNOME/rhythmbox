/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

#include <glib/gi18n.h>

#include <gst/pbutils/install-plugins.h>

#include "rb-source.h"
#include "rb-track-transfer-batch.h"
#include "rb-track-transfer-queue.h"
#include "rb-encoder.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-gst-media-types.h"
#include "rb-task-progress.h"
#include "rb-file-helpers.h"

enum
{
	STARTED,
	COMPLETE,
	CANCELLED,
	GET_DEST_URI,
	OVERWRITE_PROMPT,
	TRACK_STARTED,
	TRACK_PROGRESS,
	TRACK_DONE,
	TRACK_PREPARE,
	TRACK_POSTPROCESS,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_ENCODING_TARGET,
	PROP_SETTINGS,
	PROP_QUEUE,
	PROP_SOURCE,
	PROP_DESTINATION,
	PROP_TOTAL_ENTRIES,
	PROP_DONE_ENTRIES,
	PROP_PROGRESS,
	PROP_ENTRY_LIST,
	PROP_TASK_LABEL,
	PROP_TASK_DETAIL,
	PROP_TASK_PROGRESS,
	PROP_TASK_OUTCOME,
	PROP_TASK_NOTIFY,
	PROP_TASK_CANCELLABLE
};

static void	rb_track_transfer_batch_class_init (RBTrackTransferBatchClass *klass);
static void	rb_track_transfer_batch_init (RBTrackTransferBatch *batch);
static void	rb_track_transfer_batch_task_progress_init (RBTaskProgressInterface *iface);

static gboolean start_next (RBTrackTransferBatch *batch);
static void start_encoding (RBTrackTransferBatch *batch, gboolean overwrite);
static void track_transfer_completed (RBTrackTransferBatch *batch,
				      const char *dest_uri,
				      guint64 dest_size,
				      const char *mediatype,
				      gboolean skipped,
				      GError *error);

static guint	signals[LAST_SIGNAL] = { 0 };

struct _RBTrackTransferBatchPrivate
{
	RBTrackTransferQueue *queue;

	GstEncodingTarget *target;
	GSettings *settings;
	GList *missing_plugin_profiles;

	RBSource *source;
	RBSource *destination;

	GList *entries;
	GList *done_entries;

	guint64 total_duration;
	guint64 total_size;
	double total_fraction;

	RhythmDBEntry *current;
	double current_entry_fraction;
	char *current_dest_uri;
	gboolean current_dest_uri_sanitized;
	double current_fraction;
	RBEncoder *current_encoder;
	GstEncodingProfile *current_profile;
	gboolean cancelled;

	char *task_label;
	gboolean task_notify;
};

G_DEFINE_TYPE_EXTENDED (RBTrackTransferBatch,
			rb_track_transfer_batch,
			G_TYPE_OBJECT,
			0,
			G_IMPLEMENT_INTERFACE (RB_TYPE_TASK_PROGRESS, rb_track_transfer_batch_task_progress_init));

/**
 * SECTION:rbtracktransferbatch
 * @short_description: batch track transfer job
 *
 * Manages the transfer of a set of tracks (using #RBEncoder), providing overall
 * status information and allowing the transfer to be cancelled as a single unit.
 */

/**
 * rb_track_transfer_batch_new:
 * @target: a #GstEncodingTarget describing allowable encodings (or NULL for defaults)
 * @source: the #RBSource from which the entries are to be transferred
 * @destination: the #RBSource to which the entries are to be transferred
 * @queue: the #RBTrackTransferQueue instance
 *
 * Creates a new transfer batch with the specified encoding target.  If no target
 * is specified, the default target will be used with the user's preferred
 * encoding type.
 *
 * One or more entries must be added to the batch (using #rb_track_transfer_batch_add)
 * before the batch can be started (#rb_track_transfer_manager_start_batch).
 *
 * Return value: new #RBTrackTransferBatch object
 */
RBTrackTransferBatch *
rb_track_transfer_batch_new (GstEncodingTarget *target,
			     GSettings *settings,
			     GObject *source,
			     GObject *destination,
			     GObject *queue)
{
	GObject *obj;

	obj = g_object_new (RB_TYPE_TRACK_TRANSFER_BATCH,
			    "encoding-target", target,
			    "settings", settings,
			    "source", source,
			    "destination", destination,
			    "queue", queue,
			    NULL);
	return RB_TRACK_TRANSFER_BATCH (obj);
}

/**
 * rb_track_transfer_batch_add:
 * @batch: a #RBTrackTransferBatch
 * @entry: the source #RhythmDBEntry to transfer
 *
 * Adds an entry to be transferred.
 */
void
rb_track_transfer_batch_add (RBTrackTransferBatch *batch, RhythmDBEntry *entry)
{
	batch->priv->entries = g_list_append (batch->priv->entries, rhythmdb_entry_ref (entry));
}

static gboolean
select_profile_for_entry (RBTrackTransferBatch *batch, RhythmDBEntry *entry, GstEncodingProfile **rprofile, gboolean allow_missing)
{
	const char *source_media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
	const GList *p;
	int best = 0;

	for (p = gst_encoding_target_get_profiles (batch->priv->target); p != NULL; p = p->next) {
		GstEncodingProfile *profile = GST_ENCODING_PROFILE (p->data);
		char *profile_media_type;
		const char *preferred_media_type;
		gboolean transcode_lossless;
		gboolean is_preferred;
		gboolean is_lossless;
		gboolean is_source;
		gboolean is_missing;
		int rank;

		profile_media_type = rb_gst_encoding_profile_get_media_type (profile);
		if (batch->priv->settings) {
			preferred_media_type = g_settings_get_string (batch->priv->settings, "media-type");
			if (rb_gst_media_type_is_lossless (preferred_media_type)) {
				transcode_lossless = FALSE;
			} else {
				transcode_lossless = g_settings_get_boolean (batch->priv->settings, "transcode-lossless");
			}

			is_preferred = (rb_gst_media_type_matches_profile (profile, preferred_media_type));
		} else {
			preferred_media_type = NULL;
			transcode_lossless = FALSE;
			is_preferred = FALSE;
		}

		is_missing = (g_list_find (batch->priv->missing_plugin_profiles, profile) != NULL);
		if (g_str_has_prefix (source_media_type, "audio/x-raw") == FALSE) {
			is_source = rb_gst_media_type_matches_profile (profile, source_media_type);
		} else {
			/* always transcode raw audio */
			is_source = FALSE;
		}

		if (profile_media_type != NULL) {
			is_lossless = (rb_gst_media_type_is_lossless (profile_media_type));
		} else {
			is_lossless = (rb_gst_media_type_is_lossless (source_media_type));
		}

		if (is_missing && allow_missing == FALSE && is_source == FALSE) {
			/* this only applies if transcoding would be required */
			rb_debug ("can't use encoding %s due to missing plugins", profile_media_type);
			rank = 0;
		} else if (transcode_lossless && is_lossless) {
			/* this overrides is_source so all lossless files get transcoded */
			rb_debug ("don't want lossless encoding %s", profile_media_type);
			rank = 0;
		} else if (is_source) {
			/* this overrides is_preferred so we don't transcode unneccessarily */
			rb_debug ("can use source encoding %s", profile_media_type);
			rank = 100;
			profile = NULL;
		} else if (is_preferred) {
			/* otherwise, always use the preferred encoding if available */
			rb_debug ("can use preferred encoding %s", profile_media_type);
			rank = 50;
		} else if (is_lossless == FALSE) {
			/* if we can't use the preferred encoding, we prefer lossy encodings over lossless, for space reasons */
			rb_debug ("can use lossy encoding %s", profile_media_type);
			rank = 25;
		} else {
			rb_debug ("can use lossless encoding %s", profile_media_type);
			rank = 10;
		}

		g_free (profile_media_type);
		if (rank > best) {
			*rprofile = profile;
			best = rank;
		}
	}

	return (best > 0);
}

/**
 * rb_track_transfer_batch_check_profiles:
 * @batch: a #RBTrackTransferBatch
 * @missing_plugin_profiles: (out) (element-type GstPbutils.EncodingProfile): holds a #GList of #GstEncodingProfiles on return
 * @error_count: holds the number of entries that cannot be transferred on return
 *
 * Checks that all entries in the batch can be transferred in a format
 * supported by the destination.  If no encoding profile is available for
 * some entries, but installing additional plugins could make a profile
 * available, a list of profiles that require additional plugins is returned.
 *
 * Return value: %TRUE if some entries can be transferred without additional plugins
 */
gboolean
rb_track_transfer_batch_check_profiles (RBTrackTransferBatch *batch, GList **missing_plugin_profiles, int *error_count)
{
	RBEncoder *encoder = rb_encoder_new ();
	gboolean ret = FALSE;
	const GList *l;

	rb_debug ("checking profiles");

	/* first, figure out which profiles that we care about would require additional plugins to use */
	g_list_free (batch->priv->missing_plugin_profiles);
	batch->priv->missing_plugin_profiles = NULL;

	for (l = gst_encoding_target_get_profiles (batch->priv->target); l != NULL; l = l->next) {
		GstEncodingProfile *profile = GST_ENCODING_PROFILE (l->data);
		char *profile_media_type;
		profile_media_type = rb_gst_encoding_profile_get_media_type (profile);
		if (profile_media_type != NULL &&
		    (rb_gst_media_type_is_lossless (profile_media_type) == FALSE) &&
		    rb_encoder_get_missing_plugins (encoder, profile, NULL, NULL)) {
			batch->priv->missing_plugin_profiles = g_list_append (batch->priv->missing_plugin_profiles, profile);
		}
		g_free (profile_media_type);
	}
	g_object_unref (encoder);

	rb_debug ("have %d profiles with missing plugins", g_list_length (batch->priv->missing_plugin_profiles));

	for (l = batch->priv->entries; l != NULL; l = l->next) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;
		GstEncodingProfile *profile;

		profile = NULL;
		if (select_profile_for_entry (batch, entry, &profile, FALSE) == TRUE) {
			if (profile != NULL) {
				rb_debug ("found profile %s for %s",
					  gst_encoding_profile_get_name (profile),
					  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			} else {
				rb_debug ("copying entry %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			}
			ret = TRUE;
			continue;
		}

		(*error_count)++;
		if (select_profile_for_entry (batch, entry, &profile, TRUE) == FALSE) {
			rb_debug ("unable to transfer %s (media type %s)",
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE));
		} else {
			rb_debug ("require additional plugins to transfer %s (media type %s)",
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION),
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE));
			if (*missing_plugin_profiles == NULL) {
				*missing_plugin_profiles = g_list_copy (batch->priv->missing_plugin_profiles);
			}
		}
	}
	return ret;
}

/**
 * rb_track_transfer_batch_cancel:
 * @batch: a #RBTrackTransferBatch
 *
 * Cancels the batch.
 */
void
rb_track_transfer_batch_cancel (RBTrackTransferBatch *batch)
{
	rb_track_transfer_queue_cancel_batch (batch->priv->queue, batch);
}

static void
task_progress_cancel (RBTaskProgress *progress)
{
	rb_track_transfer_batch_cancel (RB_TRACK_TRANSFER_BATCH (progress));
}

/**
 * _rb_track_transfer_batch_start:
 * @batch: a #RBTrackTransferBatch
 *
 * Starts the batch transfer.  Only to be called by the #RBTrackTransferQueue.
 */
void
_rb_track_transfer_batch_start (RBTrackTransferBatch *batch)
{
	gboolean total_duration_valid;
	gboolean total_size_valid;
	gboolean origin_valid;
	guint64 filesize;
	gulong duration;
	RBSource *origin = NULL;
	RBShell *shell;
	GList *l;

	g_object_get (batch->priv->queue, "shell", &shell, NULL);

	/* calculate total duration and file size and figure out the
	 * origin source if we weren't given one to start with.
	 */
	total_duration_valid = TRUE;
	total_size_valid = TRUE;
	origin_valid = TRUE;
	for (l = batch->priv->entries; l != NULL; l = l->next) {
		RhythmDBEntry *entry = (RhythmDBEntry *)l->data;

		filesize = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
		if (total_size_valid && filesize > 0) {
			batch->priv->total_size += filesize;
		} else {
			total_size_valid = FALSE;
			batch->priv->total_size = 0;
		}

		duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
		if (total_duration_valid && duration > 0) {
			batch->priv->total_duration += duration;
		} else {
			total_duration_valid = FALSE;
			batch->priv->total_duration = 0;
		}

		if (batch->priv->source == NULL) {
			RhythmDBEntryType *entry_type;
			RBSource *entry_origin;

			entry_type = rhythmdb_entry_get_entry_type (entry);
			entry_origin = rb_shell_get_source_by_entry_type (shell, entry_type);
			if (origin == NULL && origin_valid  == TRUE) {
				origin = entry_origin;
			} else if (origin != entry_origin) {
				origin = NULL;
				origin_valid = FALSE;
			}
		}
	}

	g_object_unref (shell);

	if (origin != NULL) {
		batch->priv->source = origin;
	}

	batch->priv->cancelled = FALSE;
	batch->priv->total_fraction = 0.0;

	g_signal_emit (batch, signals[STARTED], 0);
	g_object_notify (G_OBJECT (batch), "task-progress");
	g_object_notify (G_OBJECT (batch), "task-detail");

	start_next (batch);
}

/**
 * _rb_track_transfer_batch_cancel:
 * @batch: a #RBTrackTransferBatch
 *
 * Cancels a batch transfer.  Only to be called by the #RBTrackTransferQueue.
 */
void
_rb_track_transfer_batch_cancel (RBTrackTransferBatch *batch)
{
	batch->priv->cancelled = TRUE;
	rb_debug ("batch being cancelled");

	if (batch->priv->current_encoder != NULL) {
		rb_encoder_cancel (batch->priv->current_encoder);

		/* other things take care of cleaning up the encoder */
	}

	g_signal_emit (batch, signals[CANCELLED], 0);
	g_object_notify (G_OBJECT (batch), "task-outcome");

	/* anything else? */
}

/**
 * _rb_track_transfer_batch_continue:
 * @batch: a #RBTrackTransferBatch
 * @overwrite: if %TRUE, overwrite the current file, otherwise skip
 *
 * Continues a transfer that was suspended because the current
 * destination URI exists.  Only to be called by the #RBTrackTransferQueue.
 */
void
_rb_track_transfer_batch_continue (RBTrackTransferBatch *batch, gboolean overwrite)
{
	if (overwrite) {
		start_encoding (batch, TRUE);
	} else {
		track_transfer_completed (batch, NULL, 0, NULL, TRUE, NULL);
	}
}

static void
emit_progress (RBTrackTransferBatch *batch)
{
	int done;
	int total;
	double fraction;

	g_object_get (batch,
		      "total-entries", &total,
		      "done-entries", &done,
		      "progress", &fraction,
		      NULL);
	g_signal_emit (batch, signals[TRACK_PROGRESS], 0,
		       batch->priv->current,
		       batch->priv->current_dest_uri,
		       done,
		       total,
		       fraction);
	g_object_notify (G_OBJECT (batch), "task-progress");
}

static void
encoder_progress_cb (RBEncoder *encoder, double fraction, RBTrackTransferBatch *batch)
{
	batch->priv->current_fraction = fraction;
	emit_progress (batch);
}

static void
track_transfer_completed (RBTrackTransferBatch *batch,
			  const char *dest_uri,
			  guint64 dest_size,
			  const char *mediatype,
			  gboolean skipped,
			  GError *error)
{
	RhythmDBEntry *entry;

	entry = batch->priv->current;
	batch->priv->current = NULL;

	batch->priv->current_profile = NULL;

	/* update batch state to reflect that the track is done */
	batch->priv->total_fraction += batch->priv->current_entry_fraction;
	batch->priv->done_entries = g_list_append (batch->priv->done_entries, entry);

	if (batch->priv->cancelled == FALSE) {
		/* keep ourselves alive until the end of the function, since it's
		 * possible that a signal handler will cancel us.
		 */
		g_object_ref (batch);
		if (skipped == FALSE) {
			g_signal_emit (batch, signals[TRACK_DONE], 0,
				       entry,
				       dest_uri,
				       dest_size,
				       mediatype,
				       error);
		}

		start_next (batch);

		g_object_unref (batch);
	}
}


typedef struct {
	char *dest_uri;
	guint64 dest_size;
	char *mediatype;
} TransferPostprocessData;

static void
transfer_postprocess_data_destroy (gpointer data)
{
	TransferPostprocessData *td = data;
	g_free (td->dest_uri);
	g_free (td->mediatype);
	g_free (td);
}

static void
postprocess_transfer_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBTrackTransferBatch *batch;
	GError *error = NULL;
	TransferPostprocessData *td = g_task_get_task_data (G_TASK (result));

	batch = RB_TRACK_TRANSFER_BATCH (source_object);
	if (g_task_propagate_boolean (G_TASK (result), &error) == FALSE) {
		rb_debug ("postprocessing failed for transfer %s: %s", td->dest_uri, error->message);
		track_transfer_completed (batch, NULL, 0, NULL, FALSE, error);
		g_clear_error (&error);
	} else {
		rb_debug ("postprocessing done for %s", td->dest_uri);
		track_transfer_completed (batch, td->dest_uri, td->dest_size, td->mediatype, FALSE, NULL);
	}
}

static void
postprocess_transfer (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	RBTrackTransferBatch *batch;
	TransferPostprocessData *td = task_data;

	batch = RB_TRACK_TRANSFER_BATCH (source_object);
	g_signal_emit (batch, signals[TRACK_POSTPROCESS], 0, task, batch->priv->current, td->dest_uri, td->dest_size, td->mediatype);
	if (g_task_had_error (task) == FALSE)
		g_task_return_boolean (task, TRUE);
}

static void
encoder_completed_cb (RBEncoder *encoder,
		      const char *dest_uri,
		      guint64 dest_size,
		      const char *mediatype,
		      GError *error,
		      RBTrackTransferBatch *batch)
{
	g_object_unref (batch->priv->current_encoder);
	batch->priv->current_encoder = NULL;

	if (error == NULL) {
		rb_debug ("encoder finished (size %" G_GUINT64_FORMAT ")", dest_size);
	} else if (g_error_matches (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_DEST_EXISTS)) {
		rb_debug ("encoder stopped because destination %s already exists", dest_uri);
		g_signal_emit (batch, signals[OVERWRITE_PROMPT], 0, dest_uri);
		return;
	} else {
		rb_debug ("encoder finished (error: %s)", error->message);
	}

	if (g_signal_has_handler_pending (batch, signals[TRACK_POSTPROCESS], 0, TRUE)) {
		GTask *task;
		TransferPostprocessData *td;

		task = g_task_new (batch, NULL, postprocess_transfer_cb, NULL);
		td = g_new0 (TransferPostprocessData, 1);
		td->dest_uri = g_strdup (dest_uri);
		td->dest_size = dest_size;
		td->mediatype = g_strdup (mediatype);
		g_task_set_task_data (task, td, transfer_postprocess_data_destroy);

		rb_debug ("postprocessing for %s", dest_uri);
		g_task_run_in_thread (task, postprocess_transfer);
	} else {
		rb_debug ("no postprocessing for %s", dest_uri);
		track_transfer_completed (batch, dest_uri, dest_size, mediatype, FALSE, error);
	}
}

static char *
get_extension_from_location (RhythmDBEntry *entry)
{
	char *extension = NULL;
	const char *ext;
	GFile *f;
	char *b;

	f = g_file_new_for_uri (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
	b = g_file_get_basename (f);
	g_object_unref (f);

	ext = strrchr (b, '.');
	if (ext != NULL) {
		extension = g_strdup (ext+1);
	}
	g_free (b);

	return extension;
}

static void
start_encoding (RBTrackTransferBatch *batch, gboolean overwrite)
{
	if (batch->priv->current_encoder != NULL) {
		g_object_unref (batch->priv->current_encoder);
	}
	batch->priv->current_encoder = rb_encoder_new ();

	g_signal_connect_object (batch->priv->current_encoder, "progress",
				 G_CALLBACK (encoder_progress_cb),
				 batch, 0);
	g_signal_connect_object (batch->priv->current_encoder, "completed",
				 G_CALLBACK (encoder_completed_cb),
				 batch, 0);

	rb_encoder_encode (batch->priv->current_encoder,
			   batch->priv->current,
			   batch->priv->current_dest_uri,
			   overwrite,
			   batch->priv->current_profile);
}

static void
prepare_transfer_task (GTask *task, gpointer source_object, gpointer task_data, GCancellable *cancellable)
{
	RBTrackTransferBatch *batch;
	GError *error = NULL;

	batch = RB_TRACK_TRANSFER_BATCH (source_object);
	rb_debug ("creating parent dirs for %s", batch->priv->current_dest_uri);
	if (rb_uri_create_parent_dirs (batch->priv->current_dest_uri, &error) == FALSE) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_INVALID_FILENAME)) {
			char *dest;

			g_clear_error (&error);
			dest = rb_sanitize_uri_for_filesystem (batch->priv->current_dest_uri, "msdos");
			g_free (batch->priv->current_dest_uri);

			rb_debug ("retrying parent dir creation with sanitized uri: %s", dest);
			batch->priv->current_dest_uri = dest;

			rb_uri_create_parent_dirs (batch->priv->current_dest_uri, &error);
		}
	}

	if (error == NULL) {
		rb_debug ("preparing for %s", batch->priv->current_dest_uri);
		g_signal_emit (batch, signals[TRACK_PREPARE], 0, task, batch->priv->current, batch->priv->current_dest_uri);
	}

	if (error != NULL) {
		g_task_return_error (task, error);
	} else {
		g_task_return_boolean (task, TRUE);
	}
	g_object_unref (task);
}

static void
prepare_transfer_cb (GObject *source_object, GAsyncResult *result, gpointer data)
{
	RBTrackTransferBatch *batch;
	GError *error = NULL;

	batch = RB_TRACK_TRANSFER_BATCH (source_object);
	if (g_task_propagate_boolean (G_TASK (result), &error) == FALSE) {
		rb_debug ("failed to prepare transfer of %s: %s", batch->priv->current_dest_uri, error->message);
		track_transfer_completed (batch, NULL, 0, NULL, FALSE, error);
	} else {
		rb_debug ("successfully prepared to transfer %s", batch->priv->current_dest_uri);
		g_signal_emit (batch, signals[TRACK_STARTED], 0,
			       batch->priv->current,
			       batch->priv->current_dest_uri);
		start_encoding (batch, FALSE);
		g_object_notify (G_OBJECT (batch), "task-detail");
	}
}

static gboolean
start_next (RBTrackTransferBatch *batch)
{
	GstEncodingProfile *profile = NULL;

	if (batch->priv->cancelled == TRUE) {
		return FALSE;
	}

	rb_debug ("%d entries remain in the batch", g_list_length (batch->priv->entries));
	batch->priv->current_fraction = 0.0;

	while ((batch->priv->entries != NULL) && (batch->priv->cancelled == FALSE)) {
		RhythmDBEntry *entry;
		guint64 filesize;
		gulong duration;
		double fraction;
		GList *n;
		char *media_type;
		char *extension;

		n = batch->priv->entries;
		batch->priv->entries = g_list_remove_link (batch->priv->entries, n);
		entry = (RhythmDBEntry *)n->data;
		g_list_free_1 (n);

		rb_debug ("attempting to transfer %s", rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));

		/* calculate the fraction of the transfer that this entry represents */
		filesize = rhythmdb_entry_get_uint64 (entry, RHYTHMDB_PROP_FILE_SIZE);
		duration = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_DURATION);
		if (batch->priv->total_duration > 0) {
			g_assert (duration > 0);	/* otherwise total_duration would be 0 */
			fraction = ((double)duration) / (double) batch->priv->total_duration;
		} else if (batch->priv->total_size > 0) {
			g_assert (filesize > 0);	/* otherwise total_size would be 0 */
			fraction = ((double)filesize) / (double) batch->priv->total_size;
		} else {
			int count = g_list_length (batch->priv->entries) +
				    g_list_length (batch->priv->done_entries) + 1;
			fraction = 1.0 / ((double)count);
		}

		profile = NULL;
		if (select_profile_for_entry (batch, entry, &profile, FALSE) == FALSE) {
			rb_debug ("skipping entry %s, can't find an encoding profile",
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			rhythmdb_entry_unref (entry);
			batch->priv->total_fraction += fraction;
			continue;
		}

		if (profile != NULL) {
			media_type = rb_gst_encoding_profile_get_media_type (profile);
			extension = g_strdup (rb_gst_media_type_to_extension (media_type));

			rb_gst_encoding_profile_set_preset (profile, NULL);
			if (batch->priv->settings != NULL) {
				GVariant *preset_settings;
				char *active_preset;

				preset_settings = g_settings_get_value (batch->priv->settings,
									"media-type-presets");
				active_preset = NULL;
				g_variant_lookup (preset_settings, media_type, "s", &active_preset);

				rb_debug ("setting preset %s for media type %s",
					  active_preset, media_type);
				rb_gst_encoding_profile_set_preset (profile, active_preset);

				g_free (active_preset);
			}
		} else {
			media_type = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
			extension = g_strdup (rb_gst_media_type_to_extension (media_type));
			if (extension == NULL) {
				extension = get_extension_from_location (entry);
			}
		}

		g_free (batch->priv->current_dest_uri);
		batch->priv->current_dest_uri = NULL;
		batch->priv->current_dest_uri_sanitized = FALSE;
		g_signal_emit (batch, signals[GET_DEST_URI], 0,
			       entry,
			       media_type,
			       extension,
			       &batch->priv->current_dest_uri);
		g_free (media_type);
		g_free (extension);

		if (batch->priv->current_dest_uri == NULL) {
			rb_debug ("unable to build destination URI for %s, skipping",
				  rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION));
			rhythmdb_entry_unref (entry);
			batch->priv->total_fraction += fraction;
			continue;
		}

		batch->priv->current = entry;
		batch->priv->current_entry_fraction = fraction;
		batch->priv->current_profile = profile;
		break;
	}

	if (batch->priv->current != NULL) {
		GTask *task;

		task = g_task_new (batch, NULL, prepare_transfer_cb, NULL);
		g_task_run_in_thread (task, prepare_transfer_task);
	} else {
		g_signal_emit (batch, signals[COMPLETE], 0);
		g_object_notify (G_OBJECT (batch), "task-outcome");
		return FALSE;
	}

	return TRUE;
}



static void
rb_track_transfer_batch_init (RBTrackTransferBatch *batch)
{
	batch->priv = G_TYPE_INSTANCE_GET_PRIVATE (batch,
						   RB_TYPE_TRACK_TRANSFER_BATCH,
						   RBTrackTransferBatchPrivate);
}

static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RBTrackTransferBatch *batch = RB_TRACK_TRANSFER_BATCH (object);
	switch (prop_id) {
	case PROP_ENCODING_TARGET:
		batch->priv->target = GST_ENCODING_TARGET (g_value_dup_object (value));
		break;
	case PROP_SETTINGS:
		batch->priv->settings = g_value_dup_object (value);
		break;
	case PROP_QUEUE:
		batch->priv->queue = g_value_get_object (value);
		break;
	case PROP_SOURCE:
		batch->priv->source = g_value_dup_object (value);
		break;
	case PROP_DESTINATION:
		batch->priv->destination = g_value_dup_object (value);
		break;
	case PROP_TASK_LABEL:
		batch->priv->task_label = g_value_dup_string (value);
		break;
	case PROP_TASK_DETAIL:
		/* ignore */
		break;
	case PROP_TASK_PROGRESS:
		/* ignore */
		break;
	case PROP_TASK_OUTCOME:
		/* ignore */
		break;
	case PROP_TASK_NOTIFY:
		batch->priv->task_notify = g_value_get_boolean (value);
		break;
	case PROP_TASK_CANCELLABLE:
		/* ignore */
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_get_property (GObject *object,
		   guint prop_id,
		   GValue *value,
		   GParamSpec *pspec)
{
	RBTrackTransferBatch *batch = RB_TRACK_TRANSFER_BATCH (object);
	switch (prop_id) {
	case PROP_ENCODING_TARGET:
		g_value_set_object (value, batch->priv->target);
		break;
	case PROP_SETTINGS:
		g_value_set_object (value, batch->priv->settings);
		break;
	case PROP_QUEUE:
		g_value_set_object (value, batch->priv->queue);
		break;
	case PROP_SOURCE:
		g_value_set_object (value, batch->priv->source);
		break;
	case PROP_DESTINATION:
		g_value_set_object (value, batch->priv->destination);
		break;
	case PROP_TOTAL_ENTRIES:
		{
			int count;
			count = g_list_length (batch->priv->done_entries) +
				g_list_length (batch->priv->entries);
			if (batch->priv->current != NULL) {
				count++;
			}
			g_value_set_int (value, count);
		}
		break;
	case PROP_DONE_ENTRIES:
		g_value_set_int (value, g_list_length (batch->priv->done_entries));
		break;
	case PROP_TASK_PROGRESS:
	case PROP_PROGRESS:		/* needed? */
		{
			double p = batch->priv->total_fraction;
			if (batch->priv->current != NULL) {
				p += batch->priv->current_fraction * batch->priv->current_entry_fraction;
			}
			g_value_set_double (value, p);
		}
		break;
	case PROP_ENTRY_LIST:
		{
			GList *l;
			l = g_list_copy (batch->priv->entries);
			if (batch->priv->current != NULL) {
				l = g_list_append (l, batch->priv->current);
			}
			l = g_list_concat (l, g_list_copy (batch->priv->done_entries));
			g_list_foreach (l, (GFunc) rhythmdb_entry_ref, NULL);
			g_value_set_pointer (value, l);
		}
		break;
	case PROP_TASK_LABEL:
		g_value_set_string (value, batch->priv->task_label);
		break;
	case PROP_TASK_DETAIL:
		{
			int done;
			int total;

			done = g_list_length (batch->priv->done_entries);
			total = done + g_list_length (batch->priv->entries);
			if (batch->priv->current) {
				total++;
				done++;
			}
			g_value_take_string (value, g_strdup_printf (_("%d of %d"), done, total));
		}
		break;
	case PROP_TASK_OUTCOME:
		if (batch->priv->cancelled) {
			g_value_set_enum (value, RB_TASK_OUTCOME_CANCELLED);
		} else if ((batch->priv->entries == NULL) && (batch->priv->done_entries != NULL)) {
			g_value_set_enum (value, RB_TASK_OUTCOME_COMPLETE);
		} else {
			g_value_set_enum (value, RB_TASK_OUTCOME_NONE);
		}
		break;
	case PROP_TASK_NOTIFY:
		/* we might want to notify sometimes, but we never did before */
		g_value_set_boolean (value, FALSE);
		break;
	case PROP_TASK_CANCELLABLE:
		g_value_set_boolean (value, TRUE);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBTrackTransferBatch *batch = RB_TRACK_TRANSFER_BATCH (object);

	g_clear_object (&batch->priv->source);
	g_clear_object (&batch->priv->destination);
	g_clear_object (&batch->priv->settings);

	if (batch->priv->target != NULL) {
		gst_encoding_target_unref (batch->priv->target);
		batch->priv->target = NULL;
	}

	G_OBJECT_CLASS (rb_track_transfer_batch_parent_class)->dispose (object);
}

static void
impl_finalize (GObject *object)
{
	RBTrackTransferBatch *batch = RB_TRACK_TRANSFER_BATCH (object);

	rb_list_destroy_free (batch->priv->entries, (GDestroyNotify) rhythmdb_entry_unref);
	rb_list_destroy_free (batch->priv->done_entries, (GDestroyNotify) rhythmdb_entry_unref);
	if (batch->priv->current != NULL) {
		rhythmdb_entry_unref (batch->priv->current);
	}
	g_free (batch->priv->task_label);

	G_OBJECT_CLASS (rb_track_transfer_batch_parent_class)->finalize (object);
}

static void
rb_track_transfer_batch_task_progress_init (RBTaskProgressInterface *interface)
{
	interface->cancel = task_progress_cancel;
}

static void
rb_track_transfer_batch_class_init (RBTrackTransferBatchClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->finalize = impl_finalize;
	object_class->dispose = impl_dispose;

	/**
	 * RBTrackTransferBatch:encoding-target:
	 *
	 * A GstEncodingTarget describing allowable target formats.
	 * If NULL, the default set of profiles will be used.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENCODING_TARGET,
					 g_param_spec_object ("encoding-target",
							      "encoding target",
							      "GstEncodingTarget",
							      GST_TYPE_ENCODING_TARGET,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT));
	/**
	 * RBTrackTransferBatch:settings
	 *
	 * GSettings instance holding profile preferences
	 */
	g_object_class_install_property (object_class,
					 PROP_SETTINGS,
					 g_param_spec_object ("settings",
							      "profile settings",
							      "GSettings instance holding profile settings",
							      G_TYPE_SETTINGS,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrackTransferBatch:queue
	 *
	 * The #RBTrackTransferQueue instance
	 */
	g_object_class_install_property (object_class,
					 PROP_QUEUE,
					 g_param_spec_object ("queue",
							      "transfer queue",
							      "RBTrackTransferQueue instance",
							      RB_TYPE_TRACK_TRANSFER_QUEUE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrackTransferBatch:source:
	 *
	 * The RBSource from which the tracks are being transferred.
	 */
	g_object_class_install_property (object_class,
					 PROP_SOURCE,
					 g_param_spec_object ("source",
							      "source source",
							      "RBSource from which the tracks are being transferred",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrackTransferBatch:destination:
	 *
	 * The RBSource to which the tracks are being transferred.
	 */
	g_object_class_install_property (object_class,
					 PROP_DESTINATION,
					 g_param_spec_object ("destination",
							      "destination source",
							      "RBSource to which the tracks are being transferred",
							      RB_TYPE_SOURCE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	/**
	 * RBTrackTransferBatch:total-entries:
	 *
	 * Total number of entries in the transfer batch.
	 */
	g_object_class_install_property (object_class,
					 PROP_TOTAL_ENTRIES,
					 g_param_spec_int ("total-entries",
							   "total entries",
							   "Number of entries in the batch",
							   0, G_MAXINT, 0,
							   G_PARAM_READABLE));
	/**
	 * RBTrackTransferBatch:done-entries:
	 *
	 * Number of entries in the batch that have been transferred.
	 */
	g_object_class_install_property (object_class,
					 PROP_DONE_ENTRIES,
					 g_param_spec_int ("done-entries",
							   "done entries",
							   "Number of entries already transferred",
							   0, G_MAXINT, 0,
							   G_PARAM_READABLE));
	/**
	 * RBTrackTransferBatch:progress:
	 *
	 * Fraction of the transfer batch that has been processed.
	 */
	g_object_class_install_property (object_class,
					 PROP_PROGRESS,
					 g_param_spec_double ("progress",
							      "progress fraction",
							      "Fraction of the batch that has been transferred",
							      0.0, 1.0, 0.0,
							      G_PARAM_READABLE));

	/**
	 * RBTrackTransferBatch:entry-list:
	 *
	 * A list of all entries in the batch.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_LIST,
					 g_param_spec_pointer ("entry-list",
							       "entry list",
							       "list of all entries in the batch",
							       G_PARAM_READABLE));

	g_object_class_override_property (object_class, PROP_TASK_LABEL, "task-label");
	g_object_class_override_property (object_class, PROP_TASK_DETAIL, "task-detail");
	g_object_class_override_property (object_class, PROP_TASK_PROGRESS, "task-progress");
	g_object_class_override_property (object_class, PROP_TASK_OUTCOME, "task-outcome");
	g_object_class_override_property (object_class, PROP_TASK_NOTIFY, "task-notify");
	g_object_class_override_property (object_class, PROP_TASK_CANCELLABLE, "task-cancellable");

	/**
	 * RBTrackTransferBatch::started:
	 * @batch: the #RBTrackTransferBatch
	 *
	 * Emitted when the batch is started.  This will be after
	 * all previous batches have finished, which is not necessarily
	 * when #rb_track_transfer_manager_start_batch is called.
	 */
	signals [STARTED] =
		g_signal_new ("started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, started),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBTrackTransferBatch::complete:
	 * @batch: the #RBTrackTransferBatch
	 *
	 * Emitted when the batch is complete.  This will be immediately
	 * after the final entry transfer is complete.
	 */
	signals [COMPLETE] =
		g_signal_new ("complete",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, complete),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBTrackTransferBatch::cancelled:
	 * @batch: the #RBTrackTransferBatch
	 *
	 * Emitted when the batch is cancelled.
	 *
	 * hmm.  will 'complete' still be emitted in this case?
	 */
	signals [CANCELLED] =
		g_signal_new ("cancelled",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, cancelled),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      0);

	/**
	 * RBTrackTransferBatch::get-dest-uri:
	 * @batch: the #RBTrackTransferBatch
	 * @entry: the #RhythmDBEntry to be transferred
	 * @mediatype: the destination media type for the transfer
	 * @extension: usual extension for the destionation media type
	 *
	 * The batch emits this to allow the creator to provide a destination
	 * URI for an entry being transferred.  This is emitted after the
	 * output media type is decided, so the usual extension for the media
	 * type can be taken into consideration.
	 */
	signals [GET_DEST_URI] =
		g_signal_new ("get-dest-uri",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, get_dest_uri),
			      NULL, NULL,
			      NULL,
			      G_TYPE_STRING,
			      3, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_STRING);

	/**
	 * RBTrackTransferBatch::overwrite-prompt:
	 * @batch: the #RBTrackTransferBatch
	 * @uri: the destination URI that already exists
	 *
	 * Emitted when the destination URI for a transfer already exists.
	 * The handler must call _rb_track_transfer_batch_continue or
	 * _rb_track_transfer_batch_cancel when it has figured out what to
	 * do.
	 */
	signals [OVERWRITE_PROMPT] =
		g_signal_new ("overwrite-prompt",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, overwrite_prompt),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_STRING);

	/**
	 * RBTrackTransferBatch::track-started:
	 * @batch: the #RBTrackTransferBatch
	 * @entry: the #RhythmDBEntry being transferred
	 * @dest: the destination URI for the transfer
	 *
	 * Emitted when a new entry is about to be transferred.
	 * This will be emitted for each entry in the batch, unless
	 * the batch is cancelled.
	 */
	signals [TRACK_STARTED] =
		g_signal_new ("track-started",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, track_started),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      2, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING);

	/**
	 * RBTrackTransferBatch::track-progress:
	 * @batch: the #RBTrackTransferBatch
	 * @entry: the #RhythmDBEntry being transferred
	 * @dest: the destination URI for the transfer
	 * @done: some measure of how much of the transfer is done
	 * @total: the total amount of that same measure
	 * @fraction: the fraction of the transfer that is done
	 *
	 * Emitted regularly throughout the transfer to allow progress bars
	 * and other UI elements to be updated.
	 */
	signals [TRACK_PROGRESS] =
		g_signal_new ("track-progress",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, track_progress),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      5, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE);

	/**
	 * RBTrackTransferBatch::track-done:
	 * @batch: the #RBTrackTransferBatch
	 * @entry: the #RhythmDBEntry that was transferred
	 * @dest: the destination URI for the transfer
	 * @dest_size: size of the destination file
	 * @dest_mediatype: the media type of the destination file
	 * @error: any error that occurred during transfer
	 *
	 * Emitted when a track transfer is complete, whether because
	 * the track was fully transferred, because an error occurred,
	 * or because the batch was cancelled (maybe..).
	 */
	signals [TRACK_DONE] =
		g_signal_new ("track-done",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, track_done),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      5, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_POINTER);

	/**
	 * RBTrackTransferBatch::track-prepare:
	 * @batch: the #RBTrackTransferBatch
	 * @task: the current #GTask
	 * @entry: the #RhythmDBEntry being transferred
	 * @dest: the destination URI for the transfer
	 *
	 * Emitted when a track transfer is about to start, allowing signal handlers
	 * to perform any preparation required.  The signal is emitted on the task
	 * thread, so no UI interaction is possible.
	 *
	 * Use g_task_return_error() with the provided #GTask to report errors.
	 */
	signals [TRACK_PREPARE] =
		g_signal_new ("track-prepare",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, track_prepare),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      3, G_TYPE_TASK, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING);

	/**
	 * RBTrackTransferBatch::track-postprocess:
	 * @batch: the #RBTrackTransferBatch
	 * @task: the current #GTask
	 * @entry: the #RhythmDBEntry being transferred
	 * @dest: the destination URI for the transfer
	 * @dest_size: the size of the destination file
	 * @dest_mediatype: the media type of the destination file
	 *
	 * Emitted when a track transfer is finishing, allowing signal handlers
	 * to perform any post-processing required.  The signal is emitted on the
	 * task thread, so no UI interaction is possible
	 *
	 * Use g_task_return_error() with the provided #GTask to report errors.
	 */
	signals [TRACK_POSTPROCESS] =
		g_signal_new ("track-postprocess",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, track_postprocess),
			      NULL, NULL, NULL,
			      G_TYPE_NONE,
			      5, G_TYPE_TASK, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING);

	g_type_class_add_private (klass, sizeof (RBTrackTransferBatchPrivate));
}
