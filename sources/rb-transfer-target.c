/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2011  Jonathan Matthew  <jonathan@d14n.org>
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include <stdlib.h>
#include <string.h>

#include <gst/gst.h>
#include <gst/pbutils/encoding-target.h>
#include <glib/gi18n.h>

#include <sources/rb-transfer-target.h>
#include <shell/rb-track-transfer-queue.h>
#include <backends/rb-encoder.h>
#include <lib/rb-debug.h>
#include <lib/rb-file-helpers.h>
#include <widgets/rb-dialog.h>
#include <shell/rb-task-list.h>

/* arbitrary length limit for file extensions */
#define EXTENSION_LENGTH_LIMIT	8

G_DEFINE_INTERFACE (RBTransferTarget, rb_transfer_target, 0)

/**
 * SECTION:rbtransfertarget
 * @short_description: interface for sources that can receive track transfers
 * @include: rb-transfer-target.h
 *
 * Sources that can accept track transfers should implement this interface
 * and call the associated functions to perform transfers.  The source
 * needs to be able to construct target URIs for transfers, and can optionally
 * perform its own processing after transfers have finished.  The source
 * must also provide a #GstEncodingTarget that describes the formats it
 * accepts.
 */


/**
 * rb_transfer_target_build_dest_uri:
 * @target: an #RBTransferTarget
 * @entry: a #RhythmDBEntry being transferred
 * @media_type: destination media type
 * @extension: extension associated with destination media type
 *
 * Constructs a URI to use as the destination for a transfer or transcoding
 * operation.  The URI may be on the device itself, if the device is mounted
 * into the normal filesystem or through gvfs, or it may be a temporary
 * location used to store the file before uploading it to the device.
 *
 * The destination URI should conform to the device's normal URI format,
 * and should use the provided extension instead of the extension from
 * the source entry.
 *
 * Return value: constructed URI
 */
char *
rb_transfer_target_build_dest_uri (RBTransferTarget *target,
				   RhythmDBEntry *entry,
				   const char *media_type,
				   const char *extension)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	char *uri;

	/* if we require a separate upload step, always transfer to a temporary file */
	if (iface->track_upload)
		return g_strdup (RB_ENCODER_DEST_TEMPFILE);

	uri = iface->build_dest_uri (target, entry, media_type, extension);
	if (uri != NULL) {
		rb_debug ("built dest uri for media type '%s', extension '%s': %s",
			  media_type, extension, uri);
	} else {
		rb_debug ("couldn't build dest uri for media type %s, extension %s",
			  media_type, extension);
	}

	return uri;
}

/**
 * rb_transfer_target_track_prepare:
 * @target: an #RBTransferTarget
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @error: returns error information
 *
 * Performs any preparation necessary before starting the transfer.
 * This is called on a task thread, so no UI interaction is possible.
 *
 */
void
rb_transfer_target_track_prepare (RBTransferTarget *target,
				  RhythmDBEntry *entry,
				  const char *uri,
				  GError **error)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	if (iface->track_prepare)
		iface->track_prepare (target, entry, uri, error);
}

/**
 * rb_transfer_target_track_upload:
 * @target: an #RBTransferTarget
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @dest_size: the size of the destination file
 * @media_type: the media type of the destination file
 * @error: returns error information
 *
 * This is called after a transfer to a temporary file has finished,
 * allowing the transfer target to upload the file to a device or a
 * remote service.
 */
void
rb_transfer_target_track_upload (RBTransferTarget *target,
				 RhythmDBEntry *entry,
				 const char *uri,
				 guint64 dest_size,
				 const char *media_type,
				 GError **error)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	g_assert (iface->track_upload != NULL);
	iface->track_upload (target, entry, uri, dest_size, media_type, error);
}

/**
 * rb_transfer_target_track_added:
 * @target: an #RBTransferTarget
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @filesize: size of the destination file
 * @media_type: media type of the destination file
 *
 * This is called when a transfer to the target has completed.
 * If the source's @track_added method returns %TRUE, the destination
 * URI will be added to the database using the entry type for the device.
 *
 * If the target uses a temporary area as the destination for transfers,
 * it can instead upload the destination file to the device and create an
 * entry for it, then return %FALSE.
 */
void
rb_transfer_target_track_added (RBTransferTarget *target,
				RhythmDBEntry *entry,
				const char *uri,
				guint64 filesize,
				const char *media_type)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	gboolean add_to_db = TRUE;

	if (iface->track_added)
		add_to_db = iface->track_added (target, entry, uri, filesize, media_type);

	if (add_to_db) {
		RhythmDBEntryType *entry_type;
		RhythmDB *db;
		RBShell *shell;

		g_object_get (target, "shell", &shell, NULL);
		g_object_get (shell, "db", &db, NULL);
		g_object_unref (shell);

		g_object_get (target, "entry-type", &entry_type, NULL);
		rhythmdb_add_uri_with_types (db, uri, entry_type, NULL, NULL);
		g_object_unref (entry_type);

		g_object_unref (db);
	}
}

/**
 * rb_transfer_target_track_add_error:
 * @target: an #RBTransferTarget
 * @entry: the source #RhythmDBEntry for the transfer
 * @uri: the destination URI
 * @error: the transfer error information
 *
 * This is called when a transfer fails.  If the source's
 * impl_track_add_error implementation returns %TRUE, an error dialog
 * will be displayed to the user containing the error message, unless
 * the error indicates that the destination file already exists.
 */
void
rb_transfer_target_track_add_error (RBTransferTarget *target,
				    RhythmDBEntry *entry,
				    const char *uri,
				    GError *error)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	gboolean show_dialog = TRUE;

	/* hrm, want the subclass to decide whether to display the error and
	 * whether to cancel the batch (may have some device-specific errors?)
	 *
	 * for now we'll just cancel on the most common things..
	 */
	if (iface->track_add_error)
		show_dialog = iface->track_add_error (target, entry, uri, error);

	if (show_dialog) {
		if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_EXISTS)) {
			rb_debug ("not displaying 'file exists' error for %s", uri);
		} else {
			rb_error_dialog (NULL, _("Error transferring track"), "%s", error->message);
		}
	}
}

/**
 * rb_transfer_target_get_format_descriptions:
 * @target: an #RBTransferTarget
 *
 * Returns a #GList of allocated media format descriptions for
 * the formats supported by the target.  The list and the strings
 * it holds must be freed by the caller.
 *
 * Return value: (element-type utf8) (transfer full): list of descriptions.
 */
GList *
rb_transfer_target_get_format_descriptions (RBTransferTarget *target)
{
	GstEncodingTarget *enctarget;
	const GList *l;
	GList *desc = NULL;
	g_object_get (target, "encoding-target", &enctarget, NULL);
	if (enctarget != NULL) {
		for (l = gst_encoding_target_get_profiles (enctarget); l != NULL; l = l->next) {
			GstEncodingProfile *profile = l->data;
			desc = g_list_append (desc, g_strdup (gst_encoding_profile_get_description (profile)));
		}
		gst_encoding_target_unref (enctarget);
	}
	return desc;
}

/**
 * rb_transfer_target_should_transfer:
 * @target: an #RBTransferTarget
 * @entry: a #RhythmDBEntry to consider transferring
 *
 * Checks whether @entry should be transferred to the target.
 * The target can check whether a matching entry already exists on the device,
 * for instance.  @rb_transfer_target_check_duplicate may form part of
 * an implementation.  If this method returns %FALSE, the entry
 * will be skipped.
 *
 * Return value: %TRUE if the entry should be transferred to the target
 */
gboolean
rb_transfer_target_should_transfer (RBTransferTarget *target, RhythmDBEntry *entry)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);

	return iface->should_transfer (target, entry);
}

/**
 * rb_transfer_target_check_category:
 * @target: an #RBTransferTarget
 * @entry: a #RhythmDBEntry to check
 *
 * This checks that the entry type of @entry is in a suitable
 * category for transfer.  This can be used to implement
 * @should_transfer.
 *
 * Return value: %TRUE if the entry is in a suitable category
 */
gboolean
rb_transfer_target_check_category (RBTransferTarget *target, RhythmDBEntry *entry)
{
	RhythmDBEntryCategory cat;
	RhythmDBEntryType *entry_type;

	entry_type = rhythmdb_entry_get_entry_type (entry);
	g_object_get (entry_type, "category", &cat, NULL);
	return (cat == RHYTHMDB_ENTRY_NORMAL);
}

/**
 * rb_transfer_target_check_duplicate:
 * @target: an #RBTransferTarget
 * @entry: a #RhythmDBEntry to check
 *
 * This checks for an existing entry in the target that matches
 * the title, album, artist, and track number of the entry being
 * considered.  This can be used to implement @should_transfer.
 *
 * Return value: %TRUE if the entry already exists on the target.
 */
gboolean
rb_transfer_target_check_duplicate (RBTransferTarget *target, RhythmDBEntry *entry)
{
	RhythmDBEntryType *entry_type;
	RhythmDB *db;
	RBShell *shell;
	const char *title;
	const char *album;
	const char *artist;
	gulong track_number;
	GtkTreeModel *query_model;
	GtkTreeIter iter;
	gboolean is_dup;

	g_object_get (target, "shell", &shell, "entry-type", &entry_type, NULL);
	g_object_get (shell, "db", &db, NULL);
	g_object_unref (shell);

	query_model = GTK_TREE_MODEL (rhythmdb_query_model_new_empty (db));
	title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
	artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
	track_number = rhythmdb_entry_get_ulong (entry, RHYTHMDB_PROP_TRACK_NUMBER);
	rhythmdb_do_full_query (db, RHYTHMDB_QUERY_RESULTS (query_model),
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TYPE, entry_type,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ARTIST, artist,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_ALBUM, album,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TITLE, title,
				RHYTHMDB_QUERY_PROP_EQUALS,
				RHYTHMDB_PROP_TRACK_NUMBER, track_number,
				RHYTHMDB_QUERY_END);

	is_dup = gtk_tree_model_get_iter_first (GTK_TREE_MODEL (query_model), &iter);
	g_object_unref (entry_type);
	g_object_unref (query_model);
	g_object_unref (db);
	if (is_dup) {
		rb_debug ("not transferring %lu - %s - %s - %s as already present",
			  track_number, title, album, artist);
	}
	return is_dup;
}

static gboolean
default_should_transfer (RBTransferTarget *target, RhythmDBEntry *entry)
{
	if (rb_transfer_target_check_category (target, entry) == FALSE)
		return FALSE;

	return (rb_transfer_target_check_duplicate (target, entry) == FALSE);
}

static char *
get_dest_uri_cb (RBTrackTransferBatch *batch,
		 RhythmDBEntry *entry,
		 const char *mediatype,
		 const char *extension,
		 RBTransferTarget *target)
{
	char *free_ext = NULL;
	char *uri;

	/* make sure the extension isn't ludicrously long */
	if (extension == NULL) {
		extension = "";
	} else if (strlen (extension) > EXTENSION_LENGTH_LIMIT) {
		free_ext = g_strdup (extension);
		free_ext[EXTENSION_LENGTH_LIMIT] = '\0';
		extension = free_ext;
	}
	uri = rb_transfer_target_build_dest_uri (target, entry, mediatype, extension);
	g_free (free_ext);
	return uri;
}

static void
track_prepare_cb (RBTrackTransferBatch *batch,
		  GTask *task,
		  RhythmDBEntry *entry,
		  const char *uri,
		  RBTransferTarget *target)
{
	GError *error = NULL;
	rb_transfer_target_track_prepare (target, entry, uri, &error);
	if (error != NULL)
		g_task_return_error (task, error);
}

static void
track_postprocess_cb (RBTrackTransferBatch *batch,
		      GTask *task,
		      RhythmDBEntry *entry,
		      const char *uri,
		      guint64 dest_size,
		      const char *media_type,
		      RBTransferTarget *target)
{
	GError *error = NULL;

	rb_transfer_target_track_upload (target, entry, uri, dest_size, media_type, &error);
	if (error != NULL)
		g_task_return_error (task, error);
}

static void
track_done_cb (RBTrackTransferBatch *batch,
	       RhythmDBEntry *entry,
	       const char *dest,
	       guint64 dest_size,
	       const char *dest_mediatype,
	       GError *error,
	       RBTransferTarget *target)
{
	if (error != NULL) {
		if (g_error_matches (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_OUT_OF_SPACE) ||
		    g_error_matches (error, RB_ENCODER_ERROR, RB_ENCODER_ERROR_DEST_READ_ONLY)) {
			rb_debug ("fatal transfer error: %s", error->message);
			rb_track_transfer_batch_cancel (batch);
		}
		rb_transfer_target_track_add_error (target, entry, dest, error);
	} else {
		rb_transfer_target_track_added (target, entry, dest, dest_size, dest_mediatype);
	}
}

/**
 * rb_transfer_target_transfer:
 * @target: an #RBTransferTarget
 * @settings: #GSettings instance holding encoding settings
 * @entries: (element-type RB.RhythmDBEntry): a #GList of entries to transfer
 * @defer: if %TRUE, don't start the transfer until
 *
 * Starts tranferring @entries to the target.  This returns the
 * #RBTrackTransferBatch that it starts, so the caller can track
 * the progress of the transfer, or NULL if the target doesn't
 * want any of the entries.
 *
 * Return value: (transfer full): an #RBTrackTransferBatch, or NULL
 */
RBTrackTransferBatch *
rb_transfer_target_transfer (RBTransferTarget *target, GSettings *settings, GList *entries, gboolean defer)
{
	RBTransferTargetInterface *iface = RB_TRANSFER_TARGET_GET_IFACE (target);
	RBTrackTransferQueue *xferq;
	RBTaskList *tasklist;
	RBShell *shell;
	GList *l;
	RhythmDBEntryType *our_entry_type;
	RBTrackTransferBatch *batch;
	gboolean start_batch = FALSE;

	g_object_get (target,
		      "shell", &shell,
		      "entry-type", &our_entry_type,
		      NULL);
	g_object_get (shell,
		      "track-transfer-queue", &xferq,
		      "task-list", &tasklist,
		      NULL);
	g_object_unref (shell);

	batch = g_object_steal_data (G_OBJECT (target), "transfer-target-batch");

	if (batch == NULL) {
		batch = rb_track_transfer_batch_new (NULL, settings, NULL, G_OBJECT (target), G_OBJECT (xferq));

		g_signal_connect_object (batch, "get-dest-uri", G_CALLBACK (get_dest_uri_cb), target, 0);
		g_signal_connect_object (batch, "track-prepare", G_CALLBACK (track_prepare_cb), target, 0);
		if (iface->track_upload)
			g_signal_connect_object (batch, "track-postprocess", G_CALLBACK (track_postprocess_cb), target, 0);
		g_signal_connect_object (batch, "track-done", G_CALLBACK (track_done_cb), target, 0);
	} else {
		start_batch = TRUE;
	}

	for (l = entries; l != NULL; l = l->next) {
		RhythmDBEntry *entry;
		RhythmDBEntryType *entry_type;
		const char *location;

		entry = (RhythmDBEntry *)l->data;
		location = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_LOCATION);
		entry_type = rhythmdb_entry_get_entry_type (entry);

		if (entry_type != our_entry_type) {
			if (rb_transfer_target_should_transfer (target, entry)) {
				rb_debug ("pasting entry %s", location);
				rb_track_transfer_batch_add (batch, entry);
				start_batch = TRUE;
			} else {
				rb_debug ("target doesn't want entry %s", location);
			}
		} else {
			rb_debug ("can't copy entry %s from the target to itself", location);
		}
	}
	g_object_unref (our_entry_type);

	if (start_batch) {
		if (defer) {
			g_object_set_data_full (G_OBJECT (target), "transfer-target-batch", g_object_ref (batch), g_object_unref);
		} else {
			GstEncodingTarget *encoding_target;
			char *name;
			char *label;

			g_object_get (target, "encoding-target", &encoding_target, NULL);
			g_object_set (batch, "encoding-target", encoding_target, NULL);
			gst_encoding_target_unref (encoding_target);

			g_object_get (target, "name", &name, NULL);
			label = g_strdup_printf (_("Transferring tracks to %s"), name);
			g_object_set (batch, "task-label", label, NULL);
			g_free (name);
			g_free (label);
			
			rb_task_list_add_task (tasklist, RB_TASK_PROGRESS (batch));

			rb_track_transfer_queue_start_batch (xferq, batch);
		}
	} else {
		g_object_unref (batch);
		batch = NULL;
	}
	g_object_unref (xferq);
	g_object_unref (tasklist);
	return batch;
}


static void
rb_transfer_target_default_init (RBTransferTargetInterface *interface)
{
	interface->should_transfer = default_should_transfer;

	g_object_interface_install_property (interface,
					     g_param_spec_object ("encoding-target",
								  "encoding target",
								  "GstEncodingTarget",
								  GST_TYPE_ENCODING_TARGET,
								  G_PARAM_READWRITE));
}
