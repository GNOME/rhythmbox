/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*- */
/*
 *  Copyright (C) 2010 Jonathan Matthew  <jonathan@d14n.org>
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

#include "rb-track-transfer-queue.h"
#include "rb-encoder.h"
#include "rb-library-source.h"
#include "rb-debug.h"
#include "rb-dialog.h"
#include "rb-alert-dialog.h"
#include "rb-gst-media-types.h"
#include "rb-missing-plugins.h"

#include <glib/gi18n.h>

#include <gst/gst.h>
#include <gst/pbutils/install-plugins.h>

enum
{
	PROP_0,
	PROP_SHELL,
	PROP_BATCH
};

enum
{
	TRANSFER_PROGRESS,
	MISSING_PLUGINS,
	LAST_SIGNAL
};

static void rb_track_transfer_queue_class_init	(RBTrackTransferQueueClass *klass);
static void rb_track_transfer_queue_init	(RBTrackTransferQueue *queue);

static void start_next_batch (RBTrackTransferQueue *queue);

static guint signals[LAST_SIGNAL] = { 0 };

struct _RBTrackTransferQueuePrivate
{
	RBShell *shell;

	GQueue *batch_queue;
	enum {
		OVERWRITE_PROMPT,
		OVERWRITE_ALL,
		OVERWRITE_NONE
	} overwrite_decision;
	RBTrackTransferBatch *current;
	time_t current_start_time;
};

G_DEFINE_TYPE (RBTrackTransferQueue, rb_track_transfer_queue, G_TYPE_OBJECT)

/**
 * SECTION:rbtracktransferqueue
 * @short_description: track transfer queue and surrounding junk
 *
 */

/**
 * rb_track_transfer_queue_new:
 * @shell: the #RBShell
 *
 * Creates the #RBTrackTransferQueue instance
 *
 * Return value: the #RBTrackTransferQueue
 */
RBTrackTransferQueue *
rb_track_transfer_queue_new (RBShell *shell)
{
	return g_object_new (RB_TYPE_TRACK_TRANSFER_QUEUE, "shell", shell, NULL);
}

static void
overwrite_response_cb (GtkDialog *dialog, int response, RBTrackTransferQueue *queue)
{
	gtk_widget_destroy (GTK_WIDGET (dialog));

	switch (response) {
	case GTK_RESPONSE_YES:
		rb_debug ("replacing existing file");
		_rb_track_transfer_batch_continue (queue->priv->current, TRUE);
		break;

	case GTK_RESPONSE_NO:
		rb_debug ("skipping existing file");
		_rb_track_transfer_batch_continue (queue->priv->current, FALSE);
		break;

	case GTK_RESPONSE_REJECT:
		rb_debug ("skipping all existing files");
		queue->priv->overwrite_decision = OVERWRITE_NONE;
		_rb_track_transfer_batch_continue (queue->priv->current, FALSE);
		break;

	case GTK_RESPONSE_ACCEPT:
		rb_debug ("replacing all existing files");
		queue->priv->overwrite_decision = OVERWRITE_ALL;
		_rb_track_transfer_batch_continue (queue->priv->current, TRUE);
		break;

	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:		/* not sure what the user really wants here */
		rb_debug ("cancelling batch");
		rb_track_transfer_queue_cancel_batch (queue, queue->priv->current);
		break;

	default:
		g_assert_not_reached ();
		break;
	}
}

static void
overwrite_prompt (RBTrackTransferBatch *batch, const char *uri, RBTrackTransferQueue *queue)
{
	switch (queue->priv->overwrite_decision) {
	case OVERWRITE_PROMPT:
	{
		GtkWindow *window;
		GtkWidget *dialog;
		GFile *file;
		GFileInfo *info;
		char *text;
		char *free_name;
		const char *display_name;

		free_name = NULL;
		display_name = NULL;
		file = g_file_new_for_uri (uri);
		info = g_file_query_info (file,
					  G_FILE_ATTRIBUTE_STANDARD_DISPLAY_NAME,
					  G_FILE_QUERY_INFO_NONE,
					  NULL,
					  NULL);
		if (info != NULL) {
			display_name = g_file_info_get_display_name (info);
		}

		if (display_name == NULL) {
			free_name = g_file_get_uri (file);
			display_name = free_name;
		}

		g_object_get (queue->priv->shell, "window", &window, NULL);
		text = g_strdup_printf (_("The file \"%s\" already exists. Do you want to replace it?"),
					display_name);
		dialog = rb_alert_dialog_new (window,
					      0,
					      GTK_MESSAGE_WARNING,
					      GTK_BUTTONS_NONE,
					      text,
					      NULL);
		g_object_unref (window);
		g_free (text);

		rb_alert_dialog_set_details_label (RB_ALERT_DIALOG (dialog), NULL);
		gtk_dialog_add_buttons (GTK_DIALOG (dialog),
					_("_Cancel"), GTK_RESPONSE_CANCEL,
					_("_Skip"), GTK_RESPONSE_NO,
					_("_Replace"), GTK_RESPONSE_YES,
					_("S_kip All"), GTK_RESPONSE_REJECT,
					_("Replace _All"), GTK_RESPONSE_ACCEPT,
					NULL);

		g_signal_connect (dialog, "response", G_CALLBACK (overwrite_response_cb), queue);
		gtk_widget_show (GTK_WIDGET (dialog));
		g_free (free_name);
		if (info != NULL) {
			g_object_unref (info);
		}
		g_object_unref (file);
		break;
	}

	case OVERWRITE_ALL:
		rb_debug ("already decided to replace all existing files");
		_rb_track_transfer_batch_continue (batch, TRUE);
		break;

	case OVERWRITE_NONE:
		rb_debug ("already decided to skip all existing files");
		_rb_track_transfer_batch_continue (batch, FALSE);
		break;

	default:
		g_assert_not_reached ();
	}
}

static void
batch_complete (RBTrackTransferBatch *batch, RBTrackTransferQueue *queue)
{
	if (batch != queue->priv->current) {
		rb_debug ("what?");
		return;
	}

	/* batch itself will ensure we get a progress signal showing the
	 * whole batch complete, so we don't need one here.
	 */

	queue->priv->current = NULL;
	g_object_unref (batch);

	start_next_batch (queue);
}

static int
estimate_time_left (RBTrackTransferQueue *queue, double progress)
{
	time_t now;
	time_t elapsed;
	double total_time;

	time (&now);
	elapsed = now - queue->priv->current_start_time;
	total_time = ((double)elapsed) / progress;
	return ((time_t) total_time) - elapsed;
}

static void
batch_progress (RBTrackTransferBatch *batch,
		RhythmDBEntry *entry,
		const char *dest,
		int done,
		int total,
		double fraction,
		RBTrackTransferQueue *queue)
{
	g_signal_emit (queue, signals[TRANSFER_PROGRESS], 0, done, total, fraction, estimate_time_left (queue, fraction));
}

static void
actually_start_batch (RBTrackTransferQueue *queue)
{
	g_signal_connect_object (queue->priv->current,
				 "overwrite-prompt",
				 G_CALLBACK (overwrite_prompt),
				 queue, 0);
	g_signal_connect_object (queue->priv->current,
				 "complete",
				 G_CALLBACK (batch_complete),
				 queue, 0);
	g_signal_connect_object (queue->priv->current,
				 "track-progress",
				 G_CALLBACK (batch_progress),
				 queue, 0);
	_rb_track_transfer_batch_start (queue->priv->current);
}

static GPtrArray *
get_missing_plugin_strings (GList *profiles, gboolean get_descriptions)
{
	RBEncoder *encoder;
	GPtrArray *strings;
	GList *l;

	encoder = rb_encoder_new ();
	strings = g_ptr_array_new_with_free_func (g_free);
	for (l = profiles; l != NULL; l = l->next) {
		GstEncodingProfile *profile = GST_ENCODING_PROFILE (l->data);
		char **details, **descriptions;
		char **d;
		int i;

		rb_encoder_get_missing_plugins (encoder, profile, &details, &descriptions);
		d = get_descriptions ? descriptions : details;
		for (i = 0; d[i] != NULL; i++) {
			g_ptr_array_add (strings, g_strdup (d[i]));
		}
		g_strfreev (details);
		g_strfreev (descriptions);
	}
	g_ptr_array_add (strings, NULL);
	g_object_unref (encoder);

	return strings;
}

static void
missing_plugins_retry_cb (gpointer inst, gboolean retry, RBTrackTransferQueue *queue)
{
	rb_debug ("plugin install finished (retry %d), checking media types again", retry);
	g_queue_push_head (queue->priv->batch_queue, queue->priv->current);
	queue->priv->current = NULL;
	start_next_batch (queue);
}

static void
missing_encoder_response_cb (GtkDialog *dialog, gint response, RBTrackTransferQueue *queue)
{
	GClosure *retry;
	GstEncodingTarget *target;
	GPtrArray *details;
	GList *profiles;
	const GList *l;
	RBEncoder *encoder;

	switch (response) {
	case GTK_RESPONSE_YES:
		/* 'continue' -> start the batch */
		rb_debug ("starting batch regardless of missing plugins");
		actually_start_batch (queue);
		break;

	case GTK_RESPONSE_CANCEL:
	case GTK_RESPONSE_DELETE_EVENT:
		/* 'cancel' -> cancel the batch and start the next one */
		rb_debug ("cancelling batch");
		_rb_track_transfer_batch_cancel (queue->priv->current);
		g_object_unref (queue->priv->current);
		queue->priv->current = NULL;

		start_next_batch (queue);
		break;

	case GTK_RESPONSE_ACCEPT:
		/* 'install plugins' -> try to install encoder/muxer */

		/* get profiles that need plugins installed */
		profiles = NULL;
		encoder = rb_encoder_new ();
		g_object_get (queue->priv->current, "encoding-target", &target, NULL);
		for (l = gst_encoding_target_get_profiles (target); l != NULL; l = l->next) {
			GstEncodingProfile *profile = GST_ENCODING_PROFILE (l->data);
			char *profile_media_type;
			profile_media_type = rb_gst_encoding_profile_get_media_type (profile);
			if (profile_media_type != NULL &&
			    (rb_gst_media_type_is_lossless (profile_media_type) == FALSE) &&
			    rb_encoder_get_missing_plugins (encoder, profile, NULL, NULL)) {
				profiles = g_list_append (profiles, profile);
			}
			g_free (profile_media_type);
		}
		g_object_unref (encoder);
		g_object_unref (target);

		if (profiles == NULL) {
			rb_debug ("apparently we don't need any plugins any more");
			actually_start_batch (queue);
			break;
		}

		rb_debug ("attempting plugin installation");
		details = get_missing_plugin_strings (profiles, FALSE);
		retry = g_cclosure_new ((GCallback) missing_plugins_retry_cb,
					g_object_ref (queue),
					(GClosureNotify) g_object_unref);
		g_closure_set_marshal (retry, g_cclosure_marshal_VOID__BOOLEAN);
		if (rb_missing_plugins_install ((const char **)details->pdata, FALSE, retry)) {
			rb_debug ("attempting to install missing plugins for transcoding");
		} else {
			rb_debug ("proceeding without the missing plugins for transcoding");
			actually_start_batch (queue);
		}

		g_closure_sink (retry);
		g_ptr_array_free (details, TRUE);
		g_list_free (profiles);
		break;

	default:
		g_assert_not_reached ();
	}

	gtk_widget_destroy (GTK_WIDGET (dialog));
}

static void
start_next_batch (RBTrackTransferQueue *queue)
{
	int count;
	int total;
	gboolean can_continue;
	GtkWidget *dialog;
	GtkWindow *window;
	GList *profiles = NULL;
	char *message;

	if (queue->priv->current != NULL) {
		return;
	}

	queue->priv->current = RB_TRACK_TRANSFER_BATCH (g_queue_pop_head (queue->priv->batch_queue));
	g_object_notify (G_OBJECT (queue), "batch");

	if (queue->priv->current == NULL) {
		/* indicate to anyone watching that we're not doing anything */
		g_signal_emit (queue, signals[TRANSFER_PROGRESS], 0, 0, 0, 0.0, 0);
		return;
	}

	queue->priv->overwrite_decision = OVERWRITE_PROMPT;
	g_object_get (queue->priv->current, "total-entries", &total, NULL);

	count = 0;
	can_continue = rb_track_transfer_batch_check_profiles (queue->priv->current,
							       &profiles,
							       &count);

	if (can_continue && count == 0 && profiles == NULL) {
		/* no problems, go ahead */
		actually_start_batch (queue);
		return;
	}

	if (profiles == NULL) {
		const char *str;
		str = ngettext ("%d file cannot be transferred as it must be converted into "
				"a format supported by the target device but no suitable "
				"encoding profiles are available",
				"%d files cannot be transferred as they must be converted into "
				"a format supported by the target device but no suitable "
				"encoding profiles are available",
				count);
		message = g_strdup_printf (str, count);
	} else {
		GPtrArray *descriptions;
		GstEncodingTarget *target;
		char *plugins;
		gboolean is_library;

		descriptions = get_missing_plugin_strings (profiles, TRUE);
		plugins = g_strjoinv ("\n", (char **)descriptions->pdata);

		/* this is a tiny bit hackish */
		g_object_get (queue->priv->current, "encoding-target", &target, NULL);
		is_library = (g_strcmp0 (gst_encoding_target_get_name (target), "rhythmbox-library") == 0);
		gst_encoding_target_unref (target);

		if (is_library) {
			/* XXX should provide the option of picking a different format? */
			message = g_strdup_printf (_("Additional software is required to encode media "
						     "in your preferred format:\n%s"), plugins);
		} else {
			const char *str;
			str = ngettext ("Additional software is required to convert %d file "
					"into a format supported by the target device:\n%s",
					"Additional software is required to convert %d files "
					"into a format supported by the target device:\n%s",
					count);
			message = g_strdup_printf (str, count, plugins);
		}

		g_free (plugins);
		g_ptr_array_free (descriptions, TRUE);
	}

	g_object_get (queue->priv->shell, "window", &window, NULL);
	dialog = rb_alert_dialog_new (window,
				      0,
				      GTK_MESSAGE_ERROR,
				      GTK_BUTTONS_NONE,
				      _("Unable to transfer tracks"),
				      message);
	g_object_unref (window);
	g_free (message);

	gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Cancel the transfer"), GTK_RESPONSE_CANCEL);
	if (can_continue) {
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Skip these files"), GTK_RESPONSE_YES);
	}
	if (profiles != NULL && gst_install_plugins_supported ()) {
		gtk_dialog_add_button (GTK_DIALOG (dialog), _("_Install"), GTK_RESPONSE_ACCEPT);
	}

	rb_alert_dialog_set_details_label (RB_ALERT_DIALOG (dialog), NULL);
	g_signal_connect_object (dialog, "response", G_CALLBACK (missing_encoder_response_cb), queue, 0);
	gtk_widget_show (dialog);

	if (profiles != NULL) {
		g_list_free (profiles);
	}
}

/**
 * rb_track_transfer_queue_start_batch:
 * @queue: the #RBTrackTransferQueue
 * @batch: the #RBTrackTransferBatch to add to the queue
 *
 * Adds a new transfer batch to the transfer queue; if the queue is currently
 * empty, the transfer will start immediately, but not before the call returns.
 */
void
rb_track_transfer_queue_start_batch (RBTrackTransferQueue *queue,
				     RBTrackTransferBatch *batch)
{
	g_queue_push_tail (queue->priv->batch_queue, g_object_ref (batch));
	start_next_batch (queue);
}

/**
 * rb_track_transfer_queue_cancel_batch:
 * @queue: the #RBTrackTransferQueue
 * @batch: the #RBTrackTransferBatch to cancel, or NULL for the current batch
 *
 * Removes a transfer batch from the queue.  If an entry from the
 * batch is currently being transferred, the transfer will be
 * aborted.
 */
void
rb_track_transfer_queue_cancel_batch (RBTrackTransferQueue *queue,
				      RBTrackTransferBatch *batch)
{
	gboolean found = FALSE;
	if (batch == NULL || batch == queue->priv->current) {
		batch = queue->priv->current;
		queue->priv->current = NULL;
		found = TRUE;
	} else {
		if (g_queue_find (queue->priv->batch_queue, batch)) {
			g_queue_remove (queue->priv->batch_queue, batch);
			found = TRUE;
		}
	}

	if (found) {
		_rb_track_transfer_batch_cancel (batch);
		g_object_unref (batch);

		start_next_batch (queue);
	}
}


struct FindBatchData
{
	GList *results;
	RBSource *source;
};

static void
find_batches (RBTrackTransferBatch *batch, struct FindBatchData *data)
{
	RBSource *src = NULL;
	RBSource *dest = NULL;

	g_object_get (batch, "source", &src, "destination", &dest, NULL);
	if (src == data->source || dest == data->source) {
		data->results = g_list_prepend (data->results, batch);
	}
	g_object_unref (src);
	g_object_unref (dest);
}

/**
 * rb_track_transfer_queue_find_batch_by_source:
 * @queue: the #RBTrackTransferQueue
 * @source: the #RBSource to search for
 *
 * Finds all transfer batches where @source is the source or destination.
 * This should be used to wait for transfers to finish (or cancel them) before
 * ejecting a device.  The transfer batches are returned in the order they're
 * found in the queue, so waiting for the @RBTrackTransferBatch::complete signal
 * on the last one is sufficient to wait for them all to finish.
 *
 * Return value: (element-type RBTrackTransferBatch) (transfer container): #GList of #RBTrackTransferBatch objects, not referenced
 */
GList *
rb_track_transfer_queue_find_batch_by_source (RBTrackTransferQueue *queue, RBSource *source)
{
	struct FindBatchData data;
	data.results = NULL;
	data.source = source;

	/* check the current batch */
	if (queue->priv->current != NULL) {
		find_batches (queue->priv->current, &data);
	}

	g_queue_foreach (queue->priv->batch_queue, (GFunc) find_batches, &data);
	return data.results;
}

/**
 * rb_track_transfer_queue_cancel_for_source:
 * @queue: the #RBTrackTransferQueue
 * @source: the #RBSource to cancel transfers to/from
 *
 * Cancels all transfers to or from a specified source.
 */
void
rb_track_transfer_queue_cancel_for_source (RBTrackTransferQueue *queue, RBSource *source)
{
	GList *batches;
	GList *l;

	batches = rb_track_transfer_queue_find_batch_by_source (queue, source);
	for (l = batches; l != NULL; l = l->next) {
		rb_track_transfer_queue_cancel_batch (queue, l->data);
	}
	g_list_free (batches);
}

static void
rb_track_transfer_queue_init (RBTrackTransferQueue *queue)
{
	queue->priv = G_TYPE_INSTANCE_GET_PRIVATE (queue,
						   RB_TYPE_TRACK_TRANSFER_QUEUE,
						   RBTrackTransferQueuePrivate);

	queue->priv->batch_queue = g_queue_new ();
}


static void
impl_set_property (GObject *object,
		   guint prop_id,
		   const GValue *value,
		   GParamSpec *pspec)
{
	RBTrackTransferQueue *queue = RB_TRACK_TRANSFER_QUEUE (object);

	switch (prop_id) {
	case PROP_SHELL:
		queue->priv->shell = g_value_get_object (value);
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
	RBTrackTransferQueue *queue = RB_TRACK_TRANSFER_QUEUE (object);

	switch (prop_id) {
	case PROP_SHELL:
		g_value_set_object (value, queue->priv->shell);
		break;
	case PROP_BATCH:
		g_value_set_object (value, queue->priv->current);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBTrackTransferQueue *queue = RB_TRACK_TRANSFER_QUEUE (object);

	if (queue->priv->current != NULL) {
		_rb_track_transfer_batch_cancel (queue->priv->current);
		g_object_unref (queue->priv->current);
		queue->priv->current = NULL;
	}

	if (queue->priv->batch_queue != NULL) {
		g_queue_foreach (queue->priv->batch_queue, (GFunc) _rb_track_transfer_batch_cancel, NULL);
		g_queue_foreach (queue->priv->batch_queue, (GFunc) g_object_unref, NULL);
		g_queue_free (queue->priv->batch_queue);
	}

	if (queue->priv->shell != NULL) {
		/* we don't own a reference on the shell. */
		queue->priv->shell = NULL;
	}

	G_OBJECT_CLASS (rb_track_transfer_queue_parent_class)->dispose (object);
}

static void
rb_track_transfer_queue_class_init (RBTrackTransferQueueClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->set_property = impl_set_property;
	object_class->get_property = impl_get_property;
	object_class->dispose = impl_dispose;

	/**
	 * RBTrackTransferQueue:shell:
	 *
	 * The #RBShell
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "shell",
							      "the RBShell",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrackTransferQueue:batch:
	 *
	 * The current #RBTrackTransferBatch being processed
	 */
	g_object_class_install_property (object_class,
					 PROP_BATCH,
					 g_param_spec_object ("batch",
							      "batch",
							      "current RBTrackTransferBatch",
							      RB_TYPE_TRACK_TRANSFER_BATCH,
							      G_PARAM_READABLE));
	/**
	 * RBTrackTransferQueue::transfer-progress:
	 * @queue: the #RBTrackTransferQueue
	 * @done: the number of entries transferred
	 * @total: the total number of entries in the batch
	 * @fraction: the fraction of the batch that has been transferred
	 * @time_left: the estimated remaining time (in seconds)
	 *
	 * Emitted regularly to convey progress information.  At the end of any given
	 * transfer batch, there will be one signal emission with @done == @total and
	 * @fraction == 1.0.
	 */
	signals[TRANSFER_PROGRESS] =
		g_signal_new ("transfer-progress",
			      RB_TYPE_TRACK_TRANSFER_QUEUE,
			      G_SIGNAL_RUN_LAST,
			      G_STRUCT_OFFSET (RBTrackTransferQueueClass, transfer_progress),
			      NULL, NULL,
			      NULL,
			      G_TYPE_NONE,
			      4, G_TYPE_INT, G_TYPE_INT, G_TYPE_DOUBLE, G_TYPE_INT);
	/**
	 * RBTrackTransferQueue::missing-plugins:
	 * @queue: the #RBTrackTransferQueue
	 * @details: the list of plugin detail strings describing the missing plugins
	 * @descriptions: the list of descriptions for the missing plugins
	 * @closure: a #GClosure to be called when the plugin installation is complete
	 *
	 * Emitted to request installation of one or more encoder plugins for a
	 * destination media format.  When the closure included in the signal args
	 * is called, the transfer batch will be started.
	 */
	signals[MISSING_PLUGINS] =
		g_signal_new ("missing-plugins",
			      G_OBJECT_CLASS_TYPE (object_class),
			      G_SIGNAL_RUN_LAST,
			      0,
			      NULL, NULL,
			      NULL,
			      G_TYPE_BOOLEAN,
			      3,
			      G_TYPE_STRV, G_TYPE_STRV, G_TYPE_CLOSURE);

	g_type_class_add_private (klass, sizeof (RBTrackTransferQueuePrivate));
}
