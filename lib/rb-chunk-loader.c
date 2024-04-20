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

#include <lib/rb-chunk-loader.h>
#include <lib/rb-debug.h>

/**
 * SECTION:rbchunkloader
 * @short_description: simple utility for asynchronously fetching data by URL in chunks
 *
 */


static void rb_chunk_loader_class_init (RBChunkLoaderClass *klass);
static void rb_chunk_loader_init (RBChunkLoader *loader);

struct _RBChunkLoaderPrivate
{
	char *uri;
	gssize chunk_size;
	guint8 *chunk;
	guint64 total;

	GError *error;
	GFile *file;
	GFileInputStream *stream;
	GCancellable *cancel;

	RBChunkLoaderCallback callback;
	gpointer callback_data;
	GDestroyNotify destroy_data;
};

G_DEFINE_TYPE (RBChunkLoader, rb_chunk_loader, G_TYPE_OBJECT);

static void
stream_close_cb (GObject *obj, GAsyncResult *res, gpointer data)
{
	GError *error = NULL;

	g_input_stream_close_finish (G_INPUT_STREAM (obj), res, &error);

	if (error != NULL) {
		rb_debug ("unable to close input stream: %s", error->message);
		g_clear_error (&error);
	}

	/* release reference taken before calling cleanup() */
	g_object_unref (obj);
}

static void
cleanup (RBChunkLoader *loader)
{
	g_input_stream_close_async (G_INPUT_STREAM (loader->priv->stream),
				    G_PRIORITY_DEFAULT,
				    loader->priv->cancel,
				    stream_close_cb,
				    loader);
}

static void
stream_read_async_cb (GObject *obj, GAsyncResult *res, gpointer data)
{
	RBChunkLoader *loader = RB_CHUNK_LOADER (data);
	gssize done;

	done = g_input_stream_read_finish (G_INPUT_STREAM (obj),
					   res,
					   &loader->priv->error);
	if (done == -1) {
		rb_debug ("error reading from stream: %s", loader->priv->error->message);
		g_object_ref (loader);
		loader->priv->callback (loader, NULL, 0, loader->priv->callback_data);
		cleanup (loader);
	} else if (done == 0) {
		rb_debug ("reached end of input stream");
		g_object_ref (loader);
		loader->priv->callback (loader, NULL, 0, loader->priv->callback_data);
		cleanup (loader);
	} else {
		GBytes *bytes;

		bytes = g_bytes_new_take (loader->priv->chunk, done);
		loader->priv->callback (loader, bytes, loader->priv->total, loader->priv->callback_data);
		g_bytes_unref (bytes);

		loader->priv->chunk = g_malloc0 (loader->priv->chunk_size+1);
		g_input_stream_read_async (G_INPUT_STREAM (loader->priv->stream),
					   loader->priv->chunk,
					   loader->priv->chunk_size,
					   G_PRIORITY_DEFAULT,
					   loader->priv->cancel,
					   stream_read_async_cb,
					   loader);
	}
}

static void
stream_info_async_cb (GObject *obj, GAsyncResult *res, gpointer data)
{
	RBChunkLoader *loader = RB_CHUNK_LOADER (data);
	GFileInfo *info;
	GError *error = NULL;

	info = g_file_input_stream_query_info_finish (G_FILE_INPUT_STREAM (obj), res, &error);
	if (info != NULL) {
		loader->priv->total = g_file_info_get_attribute_uint64 (info, G_FILE_ATTRIBUTE_STANDARD_SIZE);
	} else {
		loader->priv->total = 0;
		rb_debug ("couldn't get size of source file: %s", error->message);
		g_clear_error (&error);
	}

	g_input_stream_read_async (G_INPUT_STREAM (loader->priv->stream),
				   loader->priv->chunk,
				   loader->priv->chunk_size,
				   G_PRIORITY_DEFAULT,
				   loader->priv->cancel,
				   stream_read_async_cb,
				   loader);
}

static void
file_read_async_cb (GObject *obj, GAsyncResult *res, gpointer data)
{
	RBChunkLoader *loader = RB_CHUNK_LOADER (data);

	loader->priv->stream = g_file_read_finish (G_FILE (obj),
						   res,
						   &loader->priv->error);
	if (loader->priv->error != NULL) {
		loader->priv->callback (loader, NULL, 0, loader->priv->callback_data);
		return;
	}

	g_file_input_stream_query_info_async (loader->priv->stream,
					      G_FILE_ATTRIBUTE_STANDARD_SIZE,
					      G_PRIORITY_DEFAULT,
					      loader->priv->cancel,
					      stream_info_async_cb,
					      loader);


}

/**
 * rb_chunk_loader_start:
 * @loader: a #RBChunkLoader
 * @uri: the uri to load
 * @chunk_size: maximum chunk size
 *
 * Starts loading data from the specified URI, passing it in chunks
 * of at most @chunk_size to the callback.
 */
void
rb_chunk_loader_start (RBChunkLoader *loader, const char *uri, gssize chunk_size)
{
	g_assert (loader->priv->uri == NULL);
	g_assert (loader->priv->callback != NULL);

	loader->priv->uri = g_strdup (uri);
	loader->priv->chunk_size = chunk_size;
	loader->priv->chunk = g_malloc0 (chunk_size+1);

	loader->priv->cancel = g_cancellable_new ();

	loader->priv->file = g_file_new_for_commandline_arg (loader->priv->uri);
	g_file_read_async (loader->priv->file,
			   G_PRIORITY_DEFAULT,
			   loader->priv->cancel,
			   file_read_async_cb,
			   loader);
}

/**
 * rb_chunk_loader_cancel:
 * @loader: a #RBChunkLoader
 *
 * Cancels the loading operation, ensuring that the callback
 * will not be called again.
 */
void
rb_chunk_loader_cancel (RBChunkLoader *loader)
{
	g_cancellable_cancel (loader->priv->cancel);
}

/**
 * rb_chunk_loader_set_callback:
 * @loader: a #RBChunkLoader
 * @callback: the data/error callback
 * @user_data: data to pass to the callback
 * @destroy_data: function to call to destroy user_data
 *
 * Sets the loader data callback.  This will be called with each
 * chunk of data read, or with NULL to indicate the end of the file
 * or that an error has occurred.  To determine which of these is
 * the case, call @rb_chunk_loader_get_error.
 *
 * This must be called before @rb_chunk_loader_start.
 */
void
rb_chunk_loader_set_callback (RBChunkLoader *loader,
			      RBChunkLoaderCallback callback,
			      gpointer user_data,
			      GDestroyNotify destroy_data)
{
	g_assert (loader->priv->callback == NULL);
	g_assert (loader->priv->file == NULL);

	loader->priv->callback = callback;
	loader->priv->callback_data = user_data;
	loader->priv->destroy_data = destroy_data;
}

/**
 * rb_chunk_loader_get_error:
 * @loader: a #RBChunkLoader
 *
 * If an error has occurred that prevents the loader from providing
 * any further data, this function will return a #GError, otherwise
 * NULL.
 *
 * Return value: loader error or NULL
 */
GError *
rb_chunk_loader_get_error (RBChunkLoader *loader)
{
	if (loader->priv->error)
		return g_error_copy (loader->priv->error);
	return NULL;
}

/**
 * rb_chunk_loader_new:
 *
 * Creates and returns a new #RBChunkLoader instance.
 *
 * Return value: #RBChunkLoader instance
 */
RBChunkLoader *
rb_chunk_loader_new (void)
{
	return RB_CHUNK_LOADER (g_object_new (RB_TYPE_CHUNK_LOADER, NULL));
}

static void
impl_finalize (GObject *object)
{
	RBChunkLoader *loader = RB_CHUNK_LOADER (object);

	g_free (loader->priv->uri);
	g_free (loader->priv->chunk);
	g_clear_error (&loader->priv->error);

	if (loader->priv->cancel) {
		g_object_unref (loader->priv->cancel);
		loader->priv->cancel = NULL;
	}

	if (loader->priv->file) {
		g_object_unref (loader->priv->file);
		loader->priv->file = NULL;
	}

	if (loader->priv->stream) {
		g_object_unref (loader->priv->stream);
		loader->priv->stream = NULL;
	}

	if (loader->priv->destroy_data) {
		loader->priv->destroy_data (loader->priv->callback_data);
	}

	G_OBJECT_CLASS (rb_chunk_loader_parent_class)->finalize (object);
}

static void
rb_chunk_loader_init (RBChunkLoader *loader)
{
	loader->priv = G_TYPE_INSTANCE_GET_PRIVATE (loader, RB_TYPE_CHUNK_LOADER, RBChunkLoaderPrivate);
}

static void
rb_chunk_loader_class_init (RBChunkLoaderClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->finalize = impl_finalize;

	g_type_class_add_private (klass, sizeof (RBChunkLoaderPrivate));
}
