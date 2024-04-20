/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2012  Jonathan Matthew  <jonathan@d14n.org>
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

#include "config.h"

#include <lib/rb-async-copy.h>
#include <lib/rb-debug.h>

/**
 * SECTION:rbasynccopy
 * @short_description: performs asynchronous file copies (like g_file_copy_async)
 *
 */


static void rb_async_copy_class_init (RBAsyncCopyClass *klass);
static void rb_async_copy_init (RBAsyncCopy *copy);

struct _RBAsyncCopyPrivate
{
	GError *error;
	GCancellable *cancel;

	GFile *src;
	GFile *dest;

	RBAsyncCopyCallback callback;
	gpointer callback_data;
	GDestroyNotify destroy_data;

	RBAsyncCopyProgressCallback progress;
	gpointer progress_data;
	GDestroyNotify destroy_progress_data;
};

G_DEFINE_TYPE (RBAsyncCopy, rb_async_copy, G_TYPE_OBJECT);

static void
progress_cb (goffset current_num_bytes, goffset total_bytes, gpointer data)
{
	RBAsyncCopy *copy = RB_ASYNC_COPY (data);

	if (copy->priv->progress)
		copy->priv->progress (copy, current_num_bytes, total_bytes, copy->priv->progress_data);
}

static void
copy_cb (GObject *src, GAsyncResult *res, gpointer data)
{
	RBAsyncCopy *copy = RB_ASYNC_COPY (data);
	gboolean result;

	result = g_file_copy_finish (G_FILE (src), res, &copy->priv->error);

	rb_debug ("copy finished: %s", (result == FALSE) ? copy->priv->error->message : "ok");
	copy->priv->callback (copy, result, copy->priv->callback_data);
}

/**
 * rb_async_copy_start:
 * @copy: a #RBAsyncCopy
 * @src: source URI
 * @dest: destination URI
 * @callback: completion callback
 * @user_data: data for completion callback
 * @destroy_data: destroy function for user_data
 *
 * Starts copying @src to @dest, calling @callback on completion or error.
 */
void
rb_async_copy_start (RBAsyncCopy *copy,
		     const char *src,
		     const char *dest,
		     RBAsyncCopyCallback callback,
		     gpointer user_data,
		     GDestroyNotify destroy_data)
{
	g_assert (copy->priv->src == NULL);

	copy->priv->cancel = g_cancellable_new ();

	copy->priv->callback = callback;
	copy->priv->callback_data = user_data;
	copy->priv->destroy_data = destroy_data;

	copy->priv->src = g_file_new_for_commandline_arg (src);
	copy->priv->dest = g_file_new_for_commandline_arg (dest);

	g_file_copy_async (copy->priv->src,
			   copy->priv->dest,
			   G_FILE_COPY_OVERWRITE,
			   G_PRIORITY_DEFAULT,
			   copy->priv->cancel,
			   progress_cb,
			   copy,
			   copy_cb,
			   copy);
}

/**
 * rb_async_copy_cancel:
 * @copy: a #RBAsyncCopy
 *
 * Cancels the loading operation, ensuring that the callback
 * will not be called again.
 */
void
rb_async_copy_cancel (RBAsyncCopy *copy)
{
	g_cancellable_cancel (copy->priv->cancel);
}

/**
 * rb_async_copy_set_callback:
 * @copy: a #RBAsyncCopy
 * @callback: the progress callback
 * @user_data: data to pass to the callback
 * @destroy_data: function to call to destroy user_data
 *
 * Sets the progress callback for the copy.  The callback will
 * be called periodically while the copy is proceeding.
 */
void
rb_async_copy_set_progress (RBAsyncCopy *copy,
			    RBAsyncCopyProgressCallback callback,
			    gpointer user_data,
			    GDestroyNotify destroy_data)
{
	g_assert (copy->priv->progress == NULL);
	g_assert (copy->priv->src == NULL);

	copy->priv->progress = callback;
	copy->priv->progress_data = user_data;
	copy->priv->destroy_progress_data = destroy_data;
}

/**
 * rb_async_copy_get_error:
 * @copy: a #RBAsyncCopy
 *
 * If an error has occurred that prevents the copy from proceeding,
 * this function will return a #GError, otherwise NULL.
 *
 * Return value: copy error or NULL
 */
GError *
rb_async_copy_get_error (RBAsyncCopy *copy)
{
	if (copy->priv->error)
		return g_error_copy (copy->priv->error);
	return NULL;
}

/**
 * rb_async_copy_new:
 *
 * Creates and returns a new #RBAsyncCopy instance.
 *
 * Return value: #RBAsyncCopy instance
 */
RBAsyncCopy *
rb_async_copy_new (void)
{
	return RB_ASYNC_COPY (g_object_new (RB_TYPE_ASYNC_COPY, NULL));
}

static void
impl_finalize (GObject *object)
{
	RBAsyncCopy *copy = RB_ASYNC_COPY (object);

	g_clear_error (&copy->priv->error);

	if (copy->priv->cancel) {
		g_object_unref (copy->priv->cancel);
		copy->priv->cancel = NULL;
	}

	if (copy->priv->src) {
		g_object_unref (copy->priv->src);
		copy->priv->src = NULL;
	}
	if (copy->priv->dest) {
		g_object_unref (copy->priv->dest);
		copy->priv->dest = NULL;
	}

	if (copy->priv->destroy_data) {
		copy->priv->destroy_data (copy->priv->callback_data);
	}
	if (copy->priv->destroy_progress_data) {
		copy->priv->destroy_progress_data (copy->priv->progress_data);
	}

	G_OBJECT_CLASS (rb_async_copy_parent_class)->finalize (object);
}

static void
rb_async_copy_init (RBAsyncCopy *copy)
{
	copy->priv = G_TYPE_INSTANCE_GET_PRIVATE (copy, RB_TYPE_ASYNC_COPY, RBAsyncCopyPrivate);
}

static void
rb_async_copy_class_init (RBAsyncCopyClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;

	g_type_class_add_private (klass, sizeof (RBAsyncCopyPrivate));
}
