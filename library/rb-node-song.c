/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <config.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>
#include <string.h>

#include "rb-node-song.h"

static void
rb_node_song_destroyed_cb (RBNode *node,
			   gpointer unused)
{
	rb_node_unref (rb_node_song_get_album_raw (node));
	rb_node_unref (rb_node_song_get_artist_raw (node));
	rb_node_unref (rb_node_song_get_genre_raw (node));
}

static void
rb_node_song_sync (RBNode *node,
		   RBLibrary *library)
{
	MonkeyMediaStreamInfo *info;
	GValue value = { 0, }, newvalue = { 0, };
	GnomeVFSFileInfo *vfsinfo;
	char *tmp, *location;
	long minutes = 0, seconds = 0;
	gboolean virgin;

	location = rb_node_song_get_location (node);
	info = monkey_media_stream_info_new (location, NULL);
	if (info == NULL)
	{
		g_free (location);
		g_object_unref (G_OBJECT (node));
		return;
	}

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
				            &value);
	rb_node_set_property (node,
			      "track_number",
			      &value);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
				            &value);
	rb_node_set_property (node,
			      "duration_raw",
			      &value);
	g_value_init (&newvalue, G_TYPE_STRING);
	if (g_value_get_long (&value) > 0)
	{
		minutes = g_value_get_long (&value) / 60;
		seconds = g_value_get_long (&value) % 60;
	}
	tmp = g_strdup_printf ("%ld:%02ld", minutes, seconds);
	g_value_set_string (&newvalue, tmp);
	g_free (tmp);
	rb_node_set_property (node,
			      "duration",
			      &newvalue);
	g_value_unset (&newvalue);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE,
				            &value);
	rb_node_set_property (node,
			      "file_size_raw",
			      &value);
	g_value_init (&newvalue, G_TYPE_STRING);
	tmp = gnome_vfs_format_file_size_for_display (g_value_get_long (&value));
	g_value_set_string (&newvalue, tmp);
	g_free (tmp);
	rb_node_set_property (node,
			      "file_size",
			      &newvalue);
	g_value_unset (&newvalue);
	g_value_unset (&value);

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
				            &value);
	rb_node_set_property (node,
			      "name",
			      &value);
	g_value_unset (&value);

	vfsinfo = gnome_vfs_file_info_new ();
	gnome_vfs_get_file_info (location, vfsinfo,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	g_value_init (&newvalue, G_TYPE_LONG);
	g_value_set_long (&newvalue, vfsinfo->mtime);
	rb_node_set_property (node,
			      "mtime",
			      &newvalue);
	g_value_unset (&newvalue);
	gnome_vfs_file_info_unref (vfsinfo);

	virgin = !GPOINTER_TO_INT (g_object_get_data (G_OBJECT (node), "no_virgin"));

	if (virgin == TRUE)
	{
		g_signal_connect (G_OBJECT (node),
				  "destroyed",
				  G_CALLBACK (rb_node_song_destroyed_cb),
				  NULL);
	}

	/* need to check whether we need to reparent */
	{
		RBNode *genre_node, *artist_node, *album_node;

		monkey_media_stream_info_get_value (info,
					            MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					            &value);
		if (virgin == FALSE &&
		    strcmp (rb_node_song_get_genre (node), g_value_get_string (&value)))
		{
			g_value_unset (&value);
			rb_node_unref (node);
			rb_library_add_uri (library, location);
			g_free (location);
			g_object_unref (G_OBJECT (info));
			return;
		}
		genre_node = rb_node_get_genre_by_name (g_value_get_string (&value));
		if (genre_node == NULL)
		{
			genre_node = rb_node_new (RB_NODE_TYPE_GENRE);

			rb_node_set_property (genre_node, "name", &value);
		}
		g_value_unset (&value);

		monkey_media_stream_info_get_value (info,
					            MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					            &value);
		if (virgin == FALSE &&
		    strcmp (rb_node_song_get_artist (node), g_value_get_string (&value)))
		{
			g_value_unset (&value);
			rb_node_unref (node);
			rb_library_add_uri (library, location);
			g_free (location);
			g_object_unref (G_OBJECT (info));
			return;
		}
		artist_node = rb_node_get_artist_by_name (g_value_get_string (&value));
		if (artist_node == NULL)
		{
			artist_node = rb_node_new (RB_NODE_TYPE_ARTIST);

			rb_node_set_property (artist_node, "name", &value);
		}
		g_value_unset (&value);

		monkey_media_stream_info_get_value (info,
					            MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					            &value);
		if (virgin == FALSE &&
		    strcmp (rb_node_song_get_album (node), g_value_get_string (&value)))
		{
			g_value_unset (&value);
			rb_node_unref (node);
			rb_library_add_uri (library, location);
			g_free (location);
			g_object_unref (G_OBJECT (info));
			return;
		}
		album_node = rb_node_get_album_by_name (g_value_get_string (&value));
		if (album_node == NULL)
		{
			album_node = rb_node_new (RB_NODE_TYPE_ALBUM);

			rb_node_set_property (album_node, "name", &value);
		}
		g_value_unset (&value);

		if (virgin == TRUE)
		{
			g_value_init (&newvalue, G_TYPE_LONG);
			g_value_set_long (&newvalue, rb_node_get_id (genre_node));
			rb_node_set_property (node, "genre", &newvalue);
			g_value_unset (&newvalue);

			rb_node_ref (genre_node);

			g_value_init (&newvalue, G_TYPE_LONG);
			g_value_set_long (&newvalue, rb_node_get_id (artist_node));
			rb_node_set_property (node, "artist", &newvalue);
			g_value_unset (&newvalue);

			rb_node_ref (artist_node);

			g_value_init (&newvalue, G_TYPE_LONG);
			g_value_set_long (&newvalue, rb_node_get_id (album_node));
			rb_node_set_property (node, "album", &newvalue);
			g_value_unset (&newvalue);

			rb_node_ref (album_node);

			rb_node_add_child (genre_node, rb_library_get_all_albums (library));
			rb_node_add_child (rb_library_get_all_genres (library), genre_node);
			rb_node_add_child (genre_node, artist_node);
			rb_node_add_child (artist_node, rb_library_get_all_songs (library));
			rb_node_add_child (rb_library_get_all_artists (library), artist_node);
			rb_node_add_child (artist_node, album_node);
			rb_node_add_child (rb_library_get_all_albums (library), album_node);
			rb_node_add_child (album_node, node);
			rb_node_add_child (rb_library_get_all_songs (library), node);
		}
	}

	g_object_set_data (G_OBJECT (node), "no_virgin", GINT_TO_POINTER (TRUE));

	g_free (location);
	g_object_unref (G_OBJECT (info));
}

void
rb_node_song_set_location (RBNode *node,
			   const char *uri,
			   RBLibrary *library)
{
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, uri);
	rb_node_set_property (node,
			      "location",
			      &value);
	g_value_unset (&value);

	rb_node_song_sync (node, library);
}

char *
rb_node_song_get_location (RBNode *node)
{
	GValue value = { 0, };
	char *ret;

	rb_node_get_property (node,
			      "location",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

void
rb_node_song_update_if_newer (RBNode *node,
			      RBLibrary *library)
{
	GnomeVFSFileInfo *info;
	GValue value = { 0, };
	char *uri;

	info = gnome_vfs_file_info_new ();
	
	uri = rb_node_song_get_location (node);
	gnome_vfs_get_file_info (uri, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	g_free (uri);

	rb_node_get_property (node,
			      "mtime",
			      &value);

	if (info->mtime != g_value_get_long (&value))
		rb_node_song_sync (node, library);
	
	gnome_vfs_file_info_unref (info);
	g_value_unset (&value);
}

char *
rb_node_song_get_title (RBNode *node)
{
	GValue value = { 0, };
	char *ret;

	rb_node_get_property (node,
			      "name",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_track_number (RBNode *node)
{
	GValue value = { 0, };
	char *ret;

	rb_node_get_property (node,
			      "track_number",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_duration (RBNode *node)
{
	GValue value = { 0, };
	char *ret;

	rb_node_get_property (node,
			      "duration",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

long
rb_node_song_get_duration_raw (RBNode *node)
{
	GValue value = { 0, };
	long ret;

	rb_node_get_property (node,
			      "duration_raw",
			      &value);
	ret = g_value_get_long (&value);
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_file_size (RBNode *node)
{
	GValue value = { 0, };
	char *ret;

	rb_node_get_property (node,
			      "file_size",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

GnomeVFSFileSize
rb_node_song_get_file_size_raw (RBNode *node)
{
	GValue value = { 0, };
	long ret;

	rb_node_get_property (node,
			      "file_size_raw",
			      &value);
	ret = g_value_get_long (&value);
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_genre (RBNode *node)
{
	GValue value = { 0, };
	RBNode *raw;
	char *ret;

	raw = rb_node_song_get_genre_raw (node);
	rb_node_get_property (raw,
			      "name",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

RBNode *
rb_node_song_get_genre_raw (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      "genre",
			      &value);
	ret = rb_node_from_id (g_value_get_long (&value));
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_artist (RBNode *node)
{
	GValue value = { 0, };
	RBNode *raw;
	char *ret;

	raw = rb_node_song_get_artist_raw (node);
	rb_node_get_property (raw,
			      "name",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

RBNode *
rb_node_song_get_artist_raw (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      "artist",
			      &value);
	ret = rb_node_from_id (g_value_get_long (&value));
	g_value_unset (&value);

	return ret;
}

char *
rb_node_song_get_album (RBNode *node)
{
	GValue value = { 0, };
	RBNode *raw;
	char *ret;

	raw = rb_node_song_get_album_raw (node);
	rb_node_get_property (raw,
			      "name",
			      &value);
	ret = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);

	return ret;
}

RBNode *
rb_node_song_get_album_raw (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      "album",
			      &value);
	ret = rb_node_from_id (g_value_get_long (&value));
	g_value_unset (&value);

	return ret;
}

gboolean
rb_node_song_has_genre (RBNode *node,
			RBNode *genre)
{
	if (rb_node_get_node_type (genre) == RB_NODE_TYPE_ALL_ARTISTS)
		return TRUE;

	return (rb_node_song_get_genre_raw (node) == genre);
}

gboolean
rb_node_song_has_artist (RBNode *node,
			 RBNode *artist)
{
	if (rb_node_get_node_type (artist) == RB_NODE_TYPE_ALL_ALBUMS)
		return TRUE;

	return (rb_node_song_get_artist_raw (node) == artist);
}

gboolean
rb_node_song_has_album (RBNode *node,
			RBNode *album)
{
	if (rb_node_get_node_type (album) == RB_NODE_TYPE_ALL_SONGS)
		return TRUE;

	return (rb_node_song_get_album_raw (node) == album);
}

void
rb_node_song_init (RBNode *node)
{
	g_object_set_data (G_OBJECT (node), "no_virgin", GINT_TO_POINTER (TRUE));

	g_signal_connect (G_OBJECT (node),
			  "destroyed",
			  G_CALLBACK (rb_node_song_destroyed_cb),
			  NULL);

	rb_node_ref (rb_node_song_get_genre_raw (node));
	rb_node_ref (rb_node_song_get_artist_raw (node));
	rb_node_ref (rb_node_song_get_album_raw (node));
}
