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
#include "rb-string-helpers.h"

static gboolean
is_different (RBNode *node, const char *property, GValue *value)
{
	GValue val = { 0, };
	gboolean equal;

	rb_node_get_property (node, property, &val);

	equal = (strcmp (g_value_get_string (&val), g_value_get_string (value)) == 0);

	g_value_unset (&val);

	return !equal;
}

static void
set_value (RBNode *node, const char *property,
	   MonkeyMediaStreamInfo *info,
	   MonkeyMediaStreamInfoField field)
{
	GValue val = { 0, };

	monkey_media_stream_info_get_value (info,
					    field,
					    0,
					    &val);

	rb_node_set_property (node,
			      property,
			      &val);

	g_value_unset (&val);
}

static void
set_mtime (RBNode *node, const char *location)
{
	GnomeVFSFileInfo *info;
	GValue val = { 0, };
	
	info = gnome_vfs_file_info_new ();
	
	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	
	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, info->mtime);
	
	rb_node_set_property (node,
			      RB_SONG_PROP_MTIME,
			      &val);
	
	g_value_unset (&val);

	gnome_vfs_file_info_unref (info);
}

static void
set_duration (RBNode *node,
	      MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	GValue string_val = { 0, };
	long minutes = 0, seconds = 0;
	char *tmp;
	
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
					    0,
				            &val);
	rb_node_set_property (node,
			      RB_SONG_PROP_REAL_DURATION,
			      &val);
	
	g_value_init (&string_val, G_TYPE_STRING);

	if (g_value_get_long (&val) > 0)
	{
		minutes = g_value_get_long (&val) / 60;
		seconds = g_value_get_long (&val) % 60;
	}
	
	tmp = g_strdup_printf ("%ld:%02ld", minutes, seconds);
	g_value_set_string (&string_val, tmp);
	g_free (tmp);
	
	rb_node_set_property (node,
			      RB_SONG_PROP_DURATION,
			      &string_val);

	g_value_unset (&string_val);
	
	g_value_unset (&val);
}

static void
set_track_number (RBNode *node,
		  MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	int cur, max;
	char *tmp;
	
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					    0,
				            &val);
	cur = g_value_get_int (&val);
	
	rb_node_set_property (node,
			      RB_SONG_PROP_REAL_TRACK_NUMBER,
			      &val);
	g_value_unset (&val);
	
	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,
					    0,
				            &val);
	max = g_value_get_int (&val);
	g_value_unset (&val);
	
	if (cur >= 0 && max == -1)
		tmp = g_strdup_printf ("%.2d", cur);
	else if (cur >= 0 && max >= 0)
		tmp = g_strdup_printf (_("%.2d of %.2d"), cur, max);
	else
		tmp = g_strdup ("");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, tmp);

	rb_node_set_property (node,
			      RB_SONG_PROP_TRACK_NUMBER,
			      &val);

	g_free (tmp);

	g_value_unset (&val);
}

static gboolean
set_genre (RBNode *node,
	   MonkeyMediaStreamInfo *info,
	   RBLibrary *library,
	   gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *genre;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_GENRE,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_SONG_PROP_GENRE, &val) == TRUE)
	{
		g_value_unset (&val);

		return TRUE;
	}
	
	genre = rb_node_get_genre_by_name (g_value_get_string (&val));
	
	if (genre == NULL)
	{
		genre = rb_node_new (RB_NODE_TYPE_GENRE);

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME,
				      &val);

		rb_node_add_child (genre, rb_library_get_all_albums (library));
		rb_node_add_child (rb_library_get_all_genres (library), genre);
	}
	
	if (check_reparent == FALSE)
		rb_node_ref (genre);

	rb_node_set_property (node,
			      RB_SONG_PROP_GENRE,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property (node,
			      RB_SONG_PROP_REAL_GENRE,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static gboolean
set_artist (RBNode *node,
	    MonkeyMediaStreamInfo *info,
	    RBLibrary *library,
	    gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *artist;
	char *swapped;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_SONG_PROP_ARTIST, &val) == TRUE)
	{
		g_value_unset (&val);

		return TRUE;
	}
	
	swapped = rb_prefix_to_suffix (g_value_get_string (&val));
	if (swapped == NULL)
		swapped = g_strdup (g_value_get_string (&val));
	
	artist = rb_node_get_artist_by_name (swapped);
	
	if (artist == NULL)
	{
		GValue swapped_val = { 0, };
		
		artist = rb_node_new (RB_NODE_TYPE_ARTIST);

		g_value_init (&swapped_val, G_TYPE_STRING);
		g_value_set_string (&swapped_val, swapped);
		rb_node_set_property (artist,
				      RB_NODE_PROP_NAME,
				      &swapped_val);
		g_value_unset (&swapped_val);

		rb_node_add_child (artist, rb_library_get_all_songs (library));
		rb_node_add_child (rb_library_get_all_artists (library), artist);
	}

	if (check_reparent == FALSE)
	{
		rb_node_add_child (rb_node_song_get_genre (node), artist);

		rb_node_ref (artist);
	}

	rb_node_set_property (node,
			      RB_SONG_PROP_ARTIST,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, artist);
	rb_node_set_property (node,
			      RB_SONG_PROP_REAL_ARTIST,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static gboolean
set_album (RBNode *node,
	   MonkeyMediaStreamInfo *info,
	   RBLibrary *library,
	   gboolean check_reparent)
{
	GValue val = { 0, };
	RBNode *album;

	monkey_media_stream_info_get_value (info,
				            MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
					    0,
				            &val);

	if (check_reparent == TRUE &&
	    is_different (node, RB_SONG_PROP_ALBUM, &val) == TRUE)
	{
		g_value_unset (&val);

		return TRUE;
	}
	
	album = rb_node_get_album_by_name (g_value_get_string (&val));
	
	if (album == NULL)
	{
		album = rb_node_new (RB_NODE_TYPE_ALBUM);

		rb_node_set_property (album,
				      RB_NODE_PROP_NAME,
				      &val);

		rb_node_add_child (rb_library_get_all_albums (library), album);
	}

	if (check_reparent == FALSE)
	{
		rb_node_add_child (rb_node_song_get_artist (node), album);
	
		rb_node_ref (album);
	}

	rb_node_set_property (node,
			      RB_SONG_PROP_ALBUM,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, album);
	rb_node_set_property (node,
			      RB_SONG_PROP_REAL_ALBUM,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static void
rb_node_song_destroyed_cb (RBNode *node,
			   gpointer unused)
{
	rb_node_unref (rb_node_song_get_album (node));
	rb_node_unref (rb_node_song_get_artist (node));
	rb_node_unref (rb_node_song_get_genre (node));
}

static void
rb_node_song_sync (RBNode *node,
		   RBLibrary *library,
		   gboolean check_reparent)
{
	MonkeyMediaStreamInfo *info;
	GValue value = { 0, };
	char *location;

	rb_node_get_property (node,
			      RB_SONG_PROP_LOCATION,
			      &value);
	location = g_strdup (g_value_get_string (&value));
	g_value_unset (&value);
	
	info = monkey_media_stream_info_new (location, NULL);
	if (info == NULL)
	{
		g_free (location);
		g_object_unref (G_OBJECT (node));
		return;
	}

	/* track number */
	set_track_number (node, info);

	/* duration */
	set_duration (node, info);

	/* filesize */
	set_value (node, RB_SONG_PROP_FILE_SIZE,
		   info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE);
	
	/* title */
	set_value (node, RB_NODE_PROP_NAME,
		   info, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE);

	/* mtime */
	set_mtime (node, location);

	/* genre, artist & album */
	if (set_genre (node, info, library, check_reparent) == TRUE ||
	    set_artist (node, info, library, check_reparent) == TRUE ||
	    set_album (node, info, library, check_reparent) == TRUE)
	{
		/* reparent */
		rb_node_unref (node);

		rb_library_add_uri (library, location);
	}

	if (check_reparent == FALSE)
	{
		rb_node_add_child (rb_node_song_get_album (node), node);
		rb_node_add_child (rb_library_get_all_songs (library), node);

		g_signal_connect (G_OBJECT (node),
				  "destroyed",
				  G_CALLBACK (rb_node_song_destroyed_cb),
				  NULL);
	}

	g_free (location);

	g_object_unref (G_OBJECT (info));
}

void
rb_node_song_init (RBNode *node,
		   const char *uri,
		   RBLibrary *library)
{
	GValue value = { 0, };

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, uri);
	rb_node_set_property (node,
			      RB_SONG_PROP_LOCATION,
			      &value);
	g_value_unset (&value);

	rb_node_song_sync (node, library, FALSE);
}

void
rb_node_song_update_if_changed (RBNode *node,
			        RBLibrary *library)
{
	GnomeVFSFileInfo *info;
	GValue value = { 0, };

	info = gnome_vfs_file_info_new ();
	
	rb_node_get_property (node,
			      RB_SONG_PROP_LOCATION,
			      &value);
	gnome_vfs_get_file_info (g_value_get_string (&value), info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	g_value_unset (&value);

	rb_node_get_property (node,
			      RB_SONG_PROP_MTIME,
			      &value);

	if (info->mtime != g_value_get_long (&value))
		rb_node_song_sync (node, library, TRUE);
	
	gnome_vfs_file_info_unref (info);
	g_value_unset (&value);
}

RBNode *
rb_node_song_get_genre (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      RB_SONG_PROP_REAL_GENRE,
			      &value);
	ret = g_value_get_pointer (&value);
	g_value_unset (&value);

	return ret;
}

RBNode *
rb_node_song_get_artist (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      RB_SONG_PROP_REAL_ARTIST,
			      &value);
	ret = g_value_get_pointer (&value);
	g_value_unset (&value);

	return ret;
}

RBNode *
rb_node_song_get_album (RBNode *node)
{
	GValue value = { 0, };
	RBNode *ret;
	
	rb_node_get_property (node,
			      RB_SONG_PROP_REAL_ALBUM,
			      &value);
	ret = g_value_get_pointer (&value);
	g_value_unset (&value);

	return ret;
}

gboolean
rb_node_song_has_genre (RBNode *node,
			RBNode *genre)
{
	if (rb_node_get_node_type (genre) == RB_NODE_TYPE_ALL_ARTISTS)
		return TRUE;

	return (rb_node_song_get_genre (node) == genre);
}

gboolean
rb_node_song_has_artist (RBNode *node,
			 RBNode *artist)
{
	if (rb_node_get_node_type (artist) == RB_NODE_TYPE_ALL_ALBUMS)
		return TRUE;

	return (rb_node_song_get_artist (node) == artist);
}

gboolean
rb_node_song_has_album (RBNode *node,
			RBNode *album)
{
	if (rb_node_get_node_type (album) == RB_NODE_TYPE_ALL_SONGS)
		return TRUE;

	return (rb_node_song_get_album (node) == album);
}

void
rb_node_song_restore (RBNode *node)
{
	g_object_set_data (G_OBJECT (node), "no_virgin", GINT_TO_POINTER (TRUE));

	g_signal_connect (G_OBJECT (node),
			  "destroyed",
			  G_CALLBACK (rb_node_song_destroyed_cb),
			  NULL);

	rb_node_ref (rb_node_song_get_genre (node));
	rb_node_ref (rb_node_song_get_artist (node));
	rb_node_ref (rb_node_song_get_album (node));
}
