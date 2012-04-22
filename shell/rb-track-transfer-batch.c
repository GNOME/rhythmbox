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
#include "rb-marshal.h"
#include "rb-debug.h"
#include "rb-util.h"
#include "rb-gst-media-types.h"

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
	CONFIGURE_PROFILE,
	LAST_SIGNAL
};

enum
{
	PROP_0,
	PROP_ENCODING_TARGET,
	PROP_SOURCE,
	PROP_DESTINATION,
	PROP_TOTAL_ENTRIES,
	PROP_DONE_ENTRIES,
	PROP_PROGRESS,
	PROP_ENTRY_LIST
};

static void	rb_track_transfer_batch_class_init (RBTrackTransferBatchClass *klass);
static void	rb_track_transfer_batch_init (RBTrackTransferBatch *batch);

static gboolean start_next (RBTrackTransferBatch *batch);
static void start_encoding (RBTrackTransferBatch *batch, gboolean overwrite);
static void track_transfer_completed (RBTrackTransferBatch *batch,
				      guint64 dest_size,
				      const char *mediatype,
				      gboolean skipped,
				      GError *error);

static guint	signals[LAST_SIGNAL] = { 0 };

struct _RBTrackTransferBatchPrivate
{
	RBTrackTransferQueue *queue;

	GstEncodingTarget *target;
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
	double current_fraction;
	RBEncoder *current_encoder;
	GstEncodingProfile *current_profile;
	gboolean cancelled;
};

G_DEFINE_TYPE (RBTrackTransferBatch, rb_track_transfer_batch, G_TYPE_OBJECT)

/**
 * SECTION:rb-track-transfer-batch
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
			     GObject *source,
			     GObject *destination)
{
	GObject *obj;

	obj = g_object_new (RB_TYPE_TRACK_TRANSFER_BATCH,
			    "encoding-target", target,
			    "source", source,
			    "destination", destination,
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
	/* probably want a way to pass in some policy about lossless encoding
	 * here.  possibilities:
	 * - convert everything to lossy
	 * - if transcoding is required, use lossy
	 * - keep lossless encoded files lossless
	 * - if transcoding is required, use lossless
	 * - convert everything to lossless
	 *
	 * of course this only applies to targets that include lossless profiles..
	 */

	const char *media_type = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
	GstEncodingProfile *lossless = NULL;
	gboolean found_lossy = FALSE;
	const GList *p;

	for (p = gst_encoding_target_get_profiles (batch->priv->target); p != NULL; p = p->next) {
		GstEncodingProfile *profile = GST_ENCODING_PROFILE (p->data);
		char *profile_media_type;
		gboolean is_missing;
		gboolean skip;

		if (g_str_has_prefix (media_type, "audio/x-raw") == FALSE &&
		    rb_gst_media_type_matches_profile (profile, media_type)) {
			/* source file is already in a supported encoding, so just copy it */
			*rprofile = NULL;
			return TRUE;
		}

		skip = FALSE;
		is_missing = (g_list_find (batch->priv->missing_plugin_profiles, profile) != NULL);

		profile_media_type = rb_gst_encoding_profile_get_media_type (profile);
		if (profile_media_type == NULL) {
			if (g_str_has_prefix (media_type, "audio/x-raw")) {
				skip = TRUE;
			}
		} else if (rb_gst_media_type_is_lossless (profile_media_type)) {
			skip = TRUE;
			if (allow_missing == FALSE && is_missing) {
				/* ignore entirely */
			} else if (lossless == NULL) {
				/* remember the first lossless profile that works */
				lossless = profile;
			}
		} else {
			found_lossy = TRUE;
			if (allow_missing == FALSE && is_missing) {
				skip = TRUE;
			}
		}

		if (skip == FALSE && *rprofile == NULL) {
			*rprofile = profile;
		}
		g_free (profile_media_type);
	}

	/* if we only found a lossless encoding, use it */
	if (*rprofile == NULL && found_lossy == FALSE && lossless != NULL) {
		*rprofile = lossless;
	}

	return (*rprofile != NULL);
}

/**
 * rb_track_transfer_batch_check_profiles:
 * @batch: a #RBTrackTransferBatch
 * @missing_plugin_profiles: holds a #GList of #GstEncodingProfiles on return
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

/**
 * _rb_track_transfer_batch_start:
 * @batch: a #RBTrackTransferBatch
 * @queue: the #RBTrackTransferQueue
 *
 * Starts the batch transfer.  Only to be called by the #RBTrackTransferQueue.
 */
void
_rb_track_transfer_batch_start (RBTrackTransferBatch *batch, GObject *queue)
{
	gboolean total_duration_valid;
	gboolean total_size_valid;
	gboolean origin_valid;
	guint64 filesize;
	gulong duration;
	RBSource *origin = NULL;
	RBShell *shell;
	GList *l;

	g_object_get (queue, "shell", &shell, NULL);

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

	batch->priv->queue = RB_TRACK_TRANSFER_QUEUE (queue);
	batch->priv->cancelled = FALSE;
	batch->priv->total_fraction = 0.0;

	g_signal_emit (batch, signals[STARTED], 0);

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
		track_transfer_completed (batch, 0, NULL, TRUE, NULL);
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
}

static void
encoder_progress_cb (RBEncoder *encoder, double fraction, RBTrackTransferBatch *batch)
{
	batch->priv->current_fraction = fraction;
	emit_progress (batch);
}

static void
track_transfer_completed (RBTrackTransferBatch *batch,
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
				       batch->priv->current_dest_uri,
				       dest_size,
				       mediatype,
				       error);
		}

		start_next (batch);

		g_object_unref (batch);
	}
}

static void
encoder_completed_cb (RBEncoder *encoder,
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
		rb_debug ("encoder stopped because destination %s already exists",
			  batch->priv->current_dest_uri);
		g_signal_emit (batch, signals[OVERWRITE_PROMPT], 0, batch->priv->current_dest_uri);
		return;
	} else {
		rb_debug ("encoder finished (error: %s)", error->message);
	}

	track_transfer_completed (batch, dest_size, mediatype, FALSE, error);
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

static gboolean
start_next (RBTrackTransferBatch *batch)
{
	GstEncodingProfile *profile = NULL;

	if (batch->priv->cancelled == TRUE) {
		return FALSE;
	}

	if (batch->priv->entries == NULL) {
		/* guess we must be done.. */
		g_signal_emit (batch, signals[COMPLETE], 0);
		return FALSE;
	}

	batch->priv->current_fraction = 0.0;

	rb_debug ("%d entries remain in the batch", g_list_length (batch->priv->entries));

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
			g_signal_emit (batch, signals[CONFIGURE_PROFILE], 0, media_type, profile);
		} else {
			media_type = rhythmdb_entry_dup_string (entry, RHYTHMDB_PROP_MEDIA_TYPE);
			extension = g_strdup (rb_gst_media_type_to_extension (media_type));
			if (extension == NULL) {
				extension = get_extension_from_location (entry);
			}
		}

		g_free (batch->priv->current_dest_uri);
		batch->priv->current_dest_uri = NULL;
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
		g_signal_emit (batch, signals[TRACK_STARTED], 0,
			       batch->priv->current,
			       batch->priv->current_dest_uri);
		start_encoding (batch, FALSE);
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
		batch->priv->target = GST_ENCODING_TARGET (gst_value_dup_mini_object (value));
		break;
	case PROP_SOURCE:
		batch->priv->source = g_value_dup_object (value);
		break;
	case PROP_DESTINATION:
		batch->priv->destination = g_value_dup_object (value);
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
		gst_value_set_mini_object (value, GST_MINI_OBJECT (batch->priv->target));
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
	case PROP_PROGRESS:
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
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBTrackTransferBatch *batch = RB_TRACK_TRANSFER_BATCH (object);

	if (batch->priv->source != NULL) {
		g_object_unref (batch->priv->source);
		batch->priv->source = NULL;
	}

	if (batch->priv->destination != NULL) {
		g_object_unref (batch->priv->destination);
		batch->priv->destination = NULL;
	}

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

	G_OBJECT_CLASS (rb_track_transfer_batch_parent_class)->finalize (object);
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
	 * RBTrackTransferBatch:encoding-target
	 *
	 * A GstEncodingTarget describing allowable target formats.
	 * If NULL, the default set of profiles will be used.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENCODING_TARGET,
					 gst_param_spec_mini_object ("encoding-target",
								     "encoding target",
								     "GstEncodingTarget",
								     GST_TYPE_ENCODING_TARGET,
								     G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrackTransferBatch:source
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
	 * RBTrackTransferBatch:destination
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
	 * RBTrackTransferBatch:total-entries
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
	 * RBTrackTransferBatch:done-entries
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
	 * RBTrackTransferBatch:progress
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
	 * RBTrackTransferBatch:entry-list
	 *
	 * A list of all entries in the batch.
	 */
	g_object_class_install_property (object_class,
					 PROP_ENTRY_LIST,
					 g_param_spec_pointer ("entry-list",
							       "entry list",
							       "list of all entries in the batch",
							       G_PARAM_READABLE));

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
			      g_cclosure_marshal_VOID__VOID,
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
			      g_cclosure_marshal_VOID__VOID,
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
			      g_cclosure_marshal_VOID__VOID,
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
			      rb_marshal_STRING__BOXED_STRING_STRING,
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
			      g_cclosure_marshal_VOID__STRING,
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
			      rb_marshal_VOID__BOXED_STRING,
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
			      rb_marshal_VOID__BOXED_STRING_INT_INT_DOUBLE,
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
			      NULL, NULL,
			      rb_marshal_VOID__BOXED_STRING_UINT64_STRING_POINTER,
			      G_TYPE_NONE,
			      5, RHYTHMDB_TYPE_ENTRY, G_TYPE_STRING, G_TYPE_UINT64, G_TYPE_STRING, G_TYPE_POINTER);

	/**
	 * RBTrackTransferBatch::configure-profile:
	 * @batch: the #RBTrackTransferBatch
	 * @mediatype: the target media type
	 * @profile: the #GstEncodingProfile
	 *
	 * Emitted to allow configuration of encoding profile settings
	 * (mostly by setting presets on sub-profiles).
	 */
	signals [CONFIGURE_PROFILE] =
		g_signal_new ("configure-profile",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferBatchClass, configure_profile),
			      NULL, NULL,
			      rb_marshal_VOID__STRING_POINTER,
			      G_TYPE_NONE,
			      2, G_TYPE_STRING, GST_TYPE_ENCODING_PROFILE);

	g_type_class_add_private (klass, sizeof (RBTrackTransferBatchPrivate));
}

