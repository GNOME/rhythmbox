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
#include <libgnomevfs/gnome-vfs-file-info.h>
#include <libgnomevfs/gnome-vfs-ops.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>
#include <string.h>

#include "rb-node-song.h"
#include "rb-string-helpers.h"

static void rb_node_song_class_init (RBNodeSongClass *klass);
static void rb_node_song_init (RBNodeSong *node);
static void rb_node_song_finalize (GObject *object);
static void rb_node_song_restored (RBNode *node);
static void rb_node_song_sync (RBNodeSong *node,
		               RBLibrary *library,
		               gboolean check_reparent);

static GObjectClass *parent_class = NULL;

GType
rb_node_song_get_type (void)
{
	static GType rb_node_song_type = 0;

	if (rb_node_song_type == 0) {
		static const GTypeInfo our_info = {
			sizeof (RBNodeSongClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_node_song_class_init,
			NULL,
			NULL,
			sizeof (RBNodeSong),
			0,
			(GInstanceInitFunc) rb_node_song_init
		};

		rb_node_song_type = g_type_register_static (RB_TYPE_NODE,
							    "RBNodeSong",
							    &our_info, 0);
	}

	return rb_node_song_type;
}

static void
rb_node_song_class_init (RBNodeSongClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBNodeClass *node_class = RB_NODE_CLASS (klass);

	node_class->restored = rb_node_song_restored;

	parent_class = g_type_class_peek_parent (klass);
	
	object_class->finalize = rb_node_song_finalize;
}

static void
rb_node_song_init (RBNodeSong *node)
{
}

static void
rb_node_song_finalize (GObject *object)
{
	RBNodeSong *node;
	RBNode *parent;

	node = RB_NODE_SONG (object);
	
	parent = rb_node_song_get_album (node);
	if (parent != NULL)
		rb_node_unref (parent);
	parent = rb_node_song_get_artist (node);
	if (parent != NULL)
		rb_node_unref (parent);
	parent = rb_node_song_get_genre (node);
	if (parent != NULL)
		rb_node_unref (parent);
	
	G_OBJECT_CLASS (parent_class)->finalize (object);
}

RBNodeSong *
rb_node_song_new (const char *location,
		  RBLibrary *library)
{
	RBNodeSong *node;
	GValue value = { 0, };
	
	g_return_val_if_fail (location != NULL, NULL);
	g_return_val_if_fail (RB_IS_LIBRARY (library), NULL);

	node = RB_NODE_SONG (g_object_new (RB_TYPE_NODE_SONG,
					   "id", rb_node_new_id (),
					   NULL));

	g_return_val_if_fail (RB_NODE (node)->priv != NULL, NULL);

	g_value_init (&value, G_TYPE_STRING);
	g_value_set_string (&value, location);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_LOCATION,
			      &value);
	g_value_unset (&value);

	rb_node_song_sync (node, library, FALSE);

	return node;
}

static gboolean
is_different (RBNodeSong *node, int property, GValue *value)
{
	gboolean equal;
	const char *string;

	string = rb_node_get_property_string (RB_NODE (node), property);

	equal = (strcmp (string, g_value_get_string (value)) == 0);

	return !equal;
}

static void
set_value (RBNodeSong *node, int property,
	   MonkeyMediaStreamInfo *info,
	   MonkeyMediaStreamInfoField field)
{
	GValue val = { 0, };

	monkey_media_stream_info_get_value (info,
					    field,
					    0,
					    &val);

	rb_node_set_property (RB_NODE (node),
			      property,
			      &val);

	g_value_unset (&val);
}

static void
set_mtime (RBNodeSong *node, const char *location)
{
	GnomeVFSFileInfo *info;
	GValue val = { 0, };
	
	info = gnome_vfs_file_info_new ();
	
	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);
	
	g_value_init (&val, G_TYPE_LONG);
	g_value_set_long (&val, info->mtime);
	
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_MTIME,
			      &val);
	
	g_value_unset (&val);

	gnome_vfs_file_info_unref (info);
}

static void
set_duration (RBNodeSong *node,
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
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_REAL_DURATION,
			      &val);
	
	g_value_init (&string_val, G_TYPE_STRING);

	if (g_value_get_long (&val) > 0) {
		minutes = g_value_get_long (&val) / 60;
		seconds = g_value_get_long (&val) % 60;
	}
	
	tmp = g_strdup_printf ("%ld:%02ld", minutes, seconds);
	g_value_set_string (&string_val, tmp);
	g_free (tmp);
	
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_DURATION,
			      &string_val);

	g_value_unset (&string_val);
	
	g_value_unset (&val);
}

static void
set_track_number (RBNodeSong *node,
		  MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	int cur, max;
	char *tmp;
	
	if (monkey_media_stream_info_get_value (info,
				                MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER,
					        0,
				                &val) == FALSE)
	{
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, -1);
	}
	
	cur = g_value_get_int (&val);
	
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_REAL_TRACK_NUMBER,
			      &val);
	g_value_unset (&val);
	
	if (monkey_media_stream_info_get_value (info,
				                MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER,
					        0,
				                &val) == FALSE)
	{
		g_value_init (&val, G_TYPE_INT);
		g_value_set_int (&val, -1);
	}
	
	max = g_value_get_int (&val);
	g_value_unset (&val);
	
	if (cur > 0 && max == -1)
		tmp = g_strdup_printf ("%.2d", cur);
	else if (cur > 0 && max > 0)
		tmp = g_strdup_printf (_("%.2d of %.2d"), cur, max);
	else
		tmp = g_strdup ("");

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, tmp);

	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_TRACK_NUMBER,
			      &val);

	g_free (tmp);

	g_value_unset (&val);
}

static gboolean
set_genre (RBNodeSong *node,
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
	    is_different (node, RB_NODE_SONG_PROP_GENRE, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	genre = rb_library_get_genre_by_name (library,
					      g_value_get_string (&val));
	
	if (genre == NULL) {
		genre = rb_node_new ();

		rb_node_set_property (genre,
				      RB_NODE_PROP_NAME,
				      &val);

		rb_node_add_child (genre, rb_library_get_all_albums (library));
		rb_node_add_child (rb_library_get_all_genres (library), genre);
	}
	
	if (check_reparent == FALSE)
		rb_node_ref (genre);

	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_GENRE,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, genre);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_REAL_GENRE,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static gboolean
set_artist (RBNodeSong *node,
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
	    is_different (node, RB_NODE_SONG_PROP_ARTIST, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	swapped = rb_prefix_to_suffix (g_value_get_string (&val));
	if (swapped == NULL)
		swapped = g_strdup (g_value_get_string (&val));
	
	artist = rb_library_get_artist_by_name (library,
						swapped);
	
	if (artist == NULL) {
		GValue swapped_val = { 0, };
		
		artist = rb_node_new ();

		g_value_init (&swapped_val, G_TYPE_STRING);
		g_value_set_string (&swapped_val, swapped);
		rb_node_set_property (artist,
				      RB_NODE_PROP_NAME,
				      &swapped_val);
		g_value_unset (&swapped_val);

		rb_node_add_child (artist, rb_library_get_all_songs (library));
		rb_node_add_child (rb_library_get_all_artists (library), artist);
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_song_get_genre (node), artist);

		rb_node_ref (artist);
	}

	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_ARTIST,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, artist);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_REAL_ARTIST,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static gboolean
set_album (RBNodeSong *node,
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
	    is_different (node, RB_NODE_SONG_PROP_ALBUM, &val) == TRUE) {
		g_value_unset (&val);

		return TRUE;
	}
	
	album = rb_library_get_album_by_name (library,
					      g_value_get_string (&val));
	
	if (album == NULL) {
		album = rb_node_new ();

		rb_node_set_property (album,
				      RB_NODE_PROP_NAME,
				      &val);

		rb_node_add_child (rb_library_get_all_albums (library), album);
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_song_get_artist (node), album);
	
		rb_node_ref (album);
	}

	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_ALBUM,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_POINTER);
	g_value_set_pointer (&val, album);
	rb_node_set_property (RB_NODE (node),
			      RB_NODE_SONG_PROP_REAL_ALBUM,
			      &val);
	g_value_unset (&val);

	return FALSE;
}

static void
rb_node_song_sync (RBNodeSong *node,
		   RBLibrary *library,
		   gboolean check_reparent)
{
	MonkeyMediaStreamInfo *info;
	const char *location;

	location = rb_node_get_property_string (RB_NODE (node),
				                RB_NODE_SONG_PROP_LOCATION);
	
	info = monkey_media_stream_info_new (location, NULL);
	if (info == NULL) {
		rb_node_unref (RB_NODE (node));
		return;
	}

	/* track number */
	set_track_number (node, info);

	/* duration */
	set_duration (node, info);

	/* filesize */
	set_value (node, RB_NODE_SONG_PROP_FILE_SIZE,
		   info, MONKEY_MEDIA_STREAM_INFO_FIELD_FILE_SIZE);
	
	/* title */
	set_value (node, RB_NODE_PROP_NAME,
		   info, MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE);

	/* mtime */
	set_mtime (node, location);

	/* genre, artist & album */
	if (set_genre (node, info, library, check_reparent) == TRUE ||
	    set_artist (node, info, library, check_reparent) == TRUE ||
	    set_album (node, info, library, check_reparent) == TRUE) {
		/* reparent */
		rb_node_unref (RB_NODE (node));

		rb_library_add_uri (library, location);
	}

	if (check_reparent == FALSE) {
		rb_node_add_child (rb_node_song_get_album (node), RB_NODE (node));
		rb_node_add_child (rb_library_get_all_songs (library), RB_NODE (node));
	}

	g_object_unref (G_OBJECT (info));
}

void
rb_node_song_update_if_changed (RBNodeSong *node,
			        RBLibrary *library)
{
	GnomeVFSFileInfo *info;
	const char *location;
	long mtime;

	g_return_if_fail (RB_IS_NODE_SONG (node));
	g_return_if_fail (RB_IS_LIBRARY (library));

	info = gnome_vfs_file_info_new ();
	
	location = rb_node_get_property_string (RB_NODE (node),
						RB_NODE_SONG_PROP_LOCATION);
	gnome_vfs_get_file_info (location, info,
				 GNOME_VFS_FILE_INFO_FOLLOW_LINKS);

	mtime = rb_node_get_property_long (RB_NODE (node),
				           RB_NODE_SONG_PROP_MTIME);

	if (info->mtime != mtime)
		rb_node_song_sync (node, library, TRUE);
	
	gnome_vfs_file_info_unref (info);
}

RBNode *
rb_node_song_get_genre (RBNodeSong *node)
{
	g_return_val_if_fail (RB_IS_NODE_SONG (node), NULL);
	
	return rb_node_get_property_node (RB_NODE (node),
			                  RB_NODE_SONG_PROP_REAL_GENRE);
}

RBNode *
rb_node_song_get_artist (RBNodeSong *node)
{
	g_return_val_if_fail (RB_IS_NODE_SONG (node), NULL);
	
	return rb_node_get_property_node (RB_NODE (node),
					  RB_NODE_SONG_PROP_REAL_ARTIST);
}

RBNode *
rb_node_song_get_album (RBNodeSong *node)
{
	g_return_val_if_fail (RB_IS_NODE_SONG (node), NULL);
	
	return rb_node_get_property_node (RB_NODE (node),
					  RB_NODE_SONG_PROP_REAL_ALBUM);
}

gboolean
rb_node_song_has_genre (RBNodeSong *node,
			RBNode *genre,
			RBLibrary *library)
{
	if (rb_library_get_all_artists (library) == genre)
		return TRUE;

	return (rb_node_song_get_genre (node) == genre);
}

gboolean
rb_node_song_has_artist (RBNodeSong *node,
			 RBNode *artist,
			 RBLibrary *library)
{
	if (rb_library_get_all_albums (library) == artist)
		return TRUE;

	return (rb_node_song_get_artist (node) == artist);
}

gboolean
rb_node_song_has_album (RBNodeSong *node,
			RBNode *album,
			RBLibrary *library)
{
	if (rb_library_get_all_songs (library) == album)
		return TRUE;

	return (rb_node_song_get_album (node) == album);
}

static void
rb_node_song_restored (RBNode *node)
{
	rb_node_ref (rb_node_song_get_genre (RB_NODE_SONG (node)));
	rb_node_ref (rb_node_song_get_artist (RB_NODE_SONG (node)));
	rb_node_ref (rb_node_song_get_album (RB_NODE_SONG (node)));
}
