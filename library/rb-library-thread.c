/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *  
 *  $Id$
 */

#include <monkey-media-stream-info.h>
#include <libgnomevfs/gnome-vfs-uri.h>
#include <libgnomevfs/gnome-vfs-directory.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <glib.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>

#include "rb-library.h"
#include "rb-library-private.h"

static void set_node_to_fileinfo (RBNode *song, MonkeyMediaStreamInfo *info);
static void update_song (Library *l, RBNode *song);

/**
 * get_mtime: return mtime for @node
 */
static time_t
get_mtime (RBNode *node)
{
	GnomeVFSFileInfo *info = gnome_vfs_file_info_new ();
	time_t ret;
	gnome_vfs_get_file_info (rb_node_get_string_property (node, "uri"),
				 info, GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	ret = info->mtime;
	gnome_vfs_file_info_unref (info);
	return ret;
}

/**
 * Main loop for the updating thread
 */
gpointer
library_thread_main (gpointer data)
{
	Library *l = LIBRARY (data);
	GList *node;

	while (TRUE)
	{
		//printf ("Thread: waiting on lock run_thread\n");
		g_mutex_lock (l->priv->mutex->run_thread);
		g_mutex_unlock (l->priv->mutex->run_thread);
		//printf ("Thread: through lock run_thread\n");

		//printf ("Thread: waiting on lock songs_to_update\n");
		g_mutex_lock (l->priv->mutex->songs_to_update); 
		//printf ("Thread: through lock songs_to_update\n");
		if (l->priv->songs_to_update == NULL) {
			//printf ("Thread: no items, locking run_thread\n");
			g_mutex_lock (l->priv->mutex->run_thread);
			g_mutex_unlock (l->priv->mutex->songs_to_update);
			continue;
		}
		//printf ("Thread: examining node, queue length is %d\n", g_list_length (l->priv->songs_to_update));
		node = l->priv->songs_to_update;
		l->priv->songs_to_update = g_list_remove_link (l->priv->songs_to_update, l->priv->songs_to_update);
		g_mutex_unlock (l->priv->mutex->songs_to_update);
		update_song (l, node->data);
		//printf ("Thread: Done updating song\n");
		g_list_free (node);
	}
}

static void
update_song (Library *l, RBNode *song)
{
	RBNode *album = NULL, *artist = NULL;
	MonkeyMediaStreamInfo *info;
	int id;
	const char *uri;
	GValue val = {0, };

	uri = rb_node_get_string_property (song, SONG_PROPERTY_URI);

	g_assert (uri != NULL);

	/* get fileinfo */
	info = monkey_media_stream_info_new (uri, NULL);

	if (info == NULL)
	{
		/* invalid mimetype, do not add */
		return;
	}

	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    &val);
	artist = library_private_add_artist_if_needed (l, g_value_get_string (&val));
	g_value_unset (&val);
		
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    &val);
	album = library_private_add_album_if_needed (l, g_value_get_string (&val), artist);
	g_value_unset (&val);

	id = library_private_build_id (l);
	rb_node_set_int_property (song, "id", id);
	
	library_private_remove_song (l, song);
	set_node_to_fileinfo (song, info);
	library_private_add_song (l, song, artist, album);
	
	g_object_unref (G_OBJECT (info));

	library_private_append_node_signal (l, song, NODE_CREATED);
}

/**
 * set_node_to_fileinfo: set @song to @info
 */
static void
set_node_to_fileinfo (RBNode *song, MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	const char *tmp;

	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
		rb_node_set_string_property (song, "name", g_strdup (tmp));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DATE,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
		rb_node_set_string_property (song, "date", g_strdup (tmp));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
		rb_node_set_string_property (song, "genre", g_strdup (tmp));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_COMMENT,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
		rb_node_set_string_property (song, "comment", g_strdup (tmp));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_CODEC_INFO,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
		rb_node_set_string_property (song, "codecinfo", g_strdup (tmp));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					    &val);
	tmp = g_value_get_string (&val);
	if (tmp != NULL)
	{
		int track_int;
		char *pretty_track = NULL;
		char **parts;
		char *part1;
		int i;
		
		if (strstr (tmp, "/") != NULL)
			part1 = g_strndup (tmp, strlen (tmp) - strlen (strstr (tmp, "/")));
		else
			part1 = g_strdup (tmp);
		track_int = part1 != NULL ? atoi (part1) : 0;
		g_free (part1);

		parts = g_strsplit (tmp, "/", 0);

		for (i = 0; parts != NULL && parts[i] != NULL && i < 2; i++)
		{
			if (pretty_track == NULL)
				pretty_track = g_strdup_printf ("%.2d", atoi (parts[i]));
			else
				pretty_track = g_strdup_printf (_("%s of %.2d"), pretty_track,
								  atoi (parts[i]));
		}

		g_strfreev (parts);
		
		rb_node_set_string_property (song, "tracknum", pretty_track);
	}
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_AUDIO_BIT_RATE,
					    &val);
	rb_node_set_int_property (song, "bitrate", g_value_get_int (&val));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
					    &val);
	rb_node_set_int_property (song, "filesize", g_value_get_long (&val));
	g_value_unset (&val);
	monkey_media_stream_info_get_value (info,
					    MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    &val);
	rb_node_set_int_property (song, "duration", g_value_get_long (&val));
	g_value_unset (&val);
	rb_node_set_int_property (song, "mtime", get_mtime (song));
}
