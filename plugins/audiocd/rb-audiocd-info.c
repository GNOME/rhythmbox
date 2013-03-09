/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 * Copyright (C) 2012 Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 * 
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include "config.h"

#include <stdio.h>
#include <glib/gi18n.h>
#include <gst/gst.h>
#include <gst/audio/gstaudiocdsrc.h>
#include <gst/tag/tag.h>
#include <gio/gio.h>

#include "rb-audiocd-info.h"

static gboolean
read_gst_disc_info (RBAudioCDInfo *info, GError **error)
{
	GstElement *source;
	GstElement *sink;
	GstElement *pipeline;
	GstBus *bus;
	gboolean done;
	GstToc *toc = NULL;

	source = gst_element_make_from_uri (GST_URI_SRC, "cdda://", NULL, NULL);
	if (source == NULL) {
		/* if cdparanoiasrc wasn't in base and installed by default
		 * everywhere, plugin install might be worth trying here.
		 */
		g_set_error_literal (error,
				     GST_CORE_ERROR,
				     GST_CORE_ERROR_MISSING_PLUGIN,
				     _("Could not find a GStreamer CD source plugin"));
		return FALSE;
	}

	g_object_set (source, "device", info->device, NULL);
	pipeline = gst_pipeline_new (NULL);
	sink = gst_element_factory_make ("fakesink", NULL);
	gst_bin_add_many (GST_BIN (pipeline), source, sink, NULL);
	gst_element_link (source, sink);

	if (g_object_class_find_property (G_OBJECT_GET_CLASS (source), "paranoia-mode"))
		g_object_set (source, "paranoia-mode", 0, NULL);

	gst_element_set_state (pipeline, GST_STATE_PAUSED);

	bus = gst_pipeline_get_bus (GST_PIPELINE (pipeline));
	done = FALSE;
	while (done == FALSE) {
		GstMessage *msg;
		GstTagList *tags;

		msg = gst_bus_timed_pop (bus, 3 * GST_SECOND);
		if (msg == NULL)
			break;

		switch (GST_MESSAGE_TYPE (msg)) {
		case GST_MESSAGE_TAG:
			gst_message_parse_tag (msg, &tags);

			gst_tag_list_get_string (tags,
						 GST_TAG_CDDA_MUSICBRAINZ_DISCID,
						 &info->musicbrainz_disc_id);
			gst_tag_list_get_string (tags,
						 GST_TAG_CDDA_MUSICBRAINZ_DISCID_FULL,
						 &info->musicbrainz_full_disc_id);

			gst_tag_list_free (tags);
			break;

		case GST_MESSAGE_STATE_CHANGED:
			if (GST_MESSAGE_SRC (msg) == GST_OBJECT (pipeline)) {
				GstState oldstate;
				GstState newstate;
				GstState pending;

				gst_message_parse_state_changed (msg, &oldstate, &newstate, &pending);
				if (newstate == GST_STATE_PAUSED && pending == GST_STATE_VOID_PENDING)
					done = TRUE;
			}
			break;
		case GST_MESSAGE_ERROR:
			gst_message_parse_error (msg, error, NULL);
			done = TRUE;
			break;
		case GST_MESSAGE_TOC:
			gst_message_parse_toc (msg, &toc, NULL);
			break;
		default:
			break;
		}

		gst_message_unref (msg);
	}

	if (toc != NULL) {
		gint i;
		GList *tmp, *entries = gst_toc_get_entries (toc);

		info->num_tracks = g_list_length (entries);
		info->tracks = g_new0 (RBAudioCDTrack, info->num_tracks);
		for (i = 0, tmp = entries; tmp; tmp = tmp->next, i++) {
			RBAudioCDTrack *track = &info->tracks[i];
			GstTocEntry *entry = (GstTocEntry*) tmp->data;
			guint64 duration = 0;
			gint64 start, stop;

			/* FIXME : GstToc can't give us this kind of info */
			track->is_audio = TRUE;
			/* FIXME : Might not be 100% the same thing as the entry order */
			track->track_num = i + 1;

			if (gst_toc_entry_get_start_stop_times (entry, &start, &stop))
				duration = stop - start;
			track->duration = duration / GST_MSECOND;
		}
	}

	gst_element_set_state (pipeline, GST_STATE_NULL);
	gst_object_unref (bus);
	gst_object_unref (pipeline);
	return (*error == NULL);
}

static void
read_gvfs_disc_info (RBAudioCDInfo *info)
{
	GFile *cdda;
	GFileInfo *fileinfo;
	GFileEnumerator *tracks;
	const char *attr;
	char *uri;
	char *dev;

	dev = g_path_get_basename (info->device);
	uri = g_strdup_printf ("cdda://%s", dev);
	g_free (dev);

	cdda = g_file_new_for_uri (uri);
	g_free (uri);

	fileinfo = g_file_query_info (cdda, "xattr::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (fileinfo == NULL) {
		g_object_unref (cdda);
		return;
	}

	attr = g_file_info_get_attribute_string (fileinfo, "xattr::org.gnome.audio.title");
	if (attr != NULL) {
		info->album = g_strdup (attr);
	}
	attr = g_file_info_get_attribute_string (fileinfo, "xattr::org.gnome.audio.artist");
	if (attr != NULL) {
		info->album_artist = g_strdup (attr);
	}
	attr = g_file_info_get_attribute_string (fileinfo, "xattr::org.gnome.audio.genre");
	if (attr != NULL) {
		info->genre = g_strdup (attr);
	}

	tracks = g_file_enumerate_children (cdda, G_FILE_ATTRIBUTE_STANDARD_NAME ",xattr::*", G_FILE_QUERY_INFO_NONE, NULL, NULL);
	if (tracks != NULL) {
		for (fileinfo = g_file_enumerator_next_file (tracks, NULL, NULL);
		     fileinfo != NULL;
		     fileinfo = g_file_enumerator_next_file (tracks, NULL, NULL)) {
			const char *name;
			const char *attr;
			int track_num;

			name = g_file_info_get_name (fileinfo);
			if (name == NULL || sscanf (name, "Track %d.wav", &track_num) != 1) {
				continue;
			}

			if (track_num < 1 || track_num > info->num_tracks) {
				continue;
			}
			GST_ERROR ("track_num:%d info->tracks[track_num-1].track_num:%d",
				   track_num,
				   info->tracks[track_num-1].track_num);
			g_assert (track_num == info->tracks[track_num-1].track_num);

			attr = g_file_info_get_attribute_string (fileinfo, "xattr::org.gnome.audio.title");
			if (attr != NULL) {
				info->tracks[track_num - 1].title = g_strdup (attr);
			}
			attr = g_file_info_get_attribute_string (fileinfo, "xattr::org.gnome.audio.artist");
			if (attr != NULL) {
				info->tracks[track_num - 1].artist = g_strdup (attr);
			}
		}
	}
	g_object_unref (tracks);

	g_object_unref (cdda);
}

static void
audiocd_info_thread (GSimpleAsyncResult *result, GObject *object, GCancellable *cancellable)
{
	RBAudioCDInfo *info;
	GError *error = NULL;

	info = g_simple_async_result_get_op_res_gpointer (result);

	if (read_gst_disc_info (info, &error)) {
		read_gvfs_disc_info (info);
	} else {
		rb_audiocd_info_free (info);
		g_simple_async_result_set_op_res_gpointer (result, NULL, NULL);
		g_simple_async_result_take_error (result, error);
	}
}

void
rb_audiocd_info_free (RBAudioCDInfo *info)
{
	int i;

	g_free (info->device);
	g_free (info->musicbrainz_disc_id);
	g_free (info->musicbrainz_full_disc_id);
	g_free (info->album);
	g_free (info->genre);
	g_free (info->album_artist);

	for (i = 0; i < info->num_tracks; i++) {
		g_free (info->tracks[i].artist);
		g_free (info->tracks[i].title);
	}
	g_free (info->tracks);
	g_free (info);
}

void
rb_audiocd_info_get (const char *device,
		     GCancellable *cancellable,
		     GAsyncReadyCallback callback,
		     gpointer user_data)
{
	GSimpleAsyncResult *result;
	RBAudioCDInfo *info;

	result = g_simple_async_result_new (NULL, callback, user_data, rb_audiocd_info_get);
	g_simple_async_result_set_check_cancellable (result, cancellable);

	info = g_new0 (RBAudioCDInfo, 1);
	info->device = g_strdup (device);
	g_simple_async_result_set_op_res_gpointer (result, info, NULL);

	g_simple_async_result_run_in_thread (result, audiocd_info_thread, G_PRIORITY_DEFAULT, cancellable);
}

RBAudioCDInfo *
rb_audiocd_info_finish (GAsyncResult *result,
			GError **error)
{
	g_return_val_if_fail (g_simple_async_result_is_valid (result, NULL, rb_audiocd_info_get),
			      NULL);
	if (g_simple_async_result_propagate_error (G_SIMPLE_ASYNC_RESULT (result), error))
		return NULL;

	return g_simple_async_result_get_op_res_gpointer (G_SIMPLE_ASYNC_RESULT (result));
}
