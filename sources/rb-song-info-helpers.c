/*
 *  arch-tag: Implementation of various RBNode song information utility functions
 */

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <monkey-media.h>
#include <time.h>
#include <string.h>

#include "rb-song-info-helpers.h"
#include "rb-string-helpers.h"

void
rb_song_set_title (RBNode *node, 
                   MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	char *collated, *folded;
        gboolean success;

	success = monkey_media_stream_info_get_value (info,
                                                      MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE,
                                                      0,
                                                      &val);

        g_return_if_fail (success == TRUE);

	rb_node_set_property (node,
			      RB_NODE_PROP_NAME,
			      &val);

	folded = g_utf8_casefold (g_value_get_string (&val), -1);
	g_value_unset (&val);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);
	
	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, collated);
	g_free (collated);
	rb_node_set_property (node,
			      RB_NODE_PROP_NAME_SORT_KEY,
			      &val);
	g_value_unset (&val);
}

void
rb_song_set_duration (RBNode *node,
                      MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	GValue string_val = { 0, };
	long minutes = 0, seconds = 0;
	char *tmp;
        gboolean success;
	
	success = monkey_media_stream_info_get_value (info,
                                                      MONKEY_MEDIA_STREAM_INFO_FIELD_DURATION,
                                                      0,
                                                      &val);

        g_return_if_fail (success == TRUE);

	rb_node_set_property (node,
			      RB_NODE_PROP_DURATION,
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
			      RB_NODE_PROP_DURATION_STR,
			      &string_val);

	g_value_unset (&string_val);

	g_value_unset (&val);
}

void
rb_song_set_artist (RBNode *node,
                    MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	char *artist, *collated, *folded;
        GValue swapped_val = { 0, };
        GValue value = { 0, };
        char *key;
        gboolean success;

	success = monkey_media_stream_info_get_value (info,
                                                      MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST,
                                                      0,
                                                      &val);
        
        g_return_if_fail (success == TRUE);
	
	artist = rb_prefix_to_suffix (g_value_get_string (&val));

	if (artist == NULL)
		artist = g_strdup (g_value_get_string (&val));
	
        g_value_init (&swapped_val, G_TYPE_STRING);
        g_value_set_string (&swapped_val, artist);
        rb_node_set_property (node,
                              RB_NODE_PROP_NAME,
                              &swapped_val);
        g_value_unset (&swapped_val);

        folded = g_utf8_casefold (artist, -1);
        key = g_utf8_collate_key (folded, -1);
        g_free (folded);
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, key);
        g_free (key);

        rb_node_set_property (node,
                              RB_NODE_PROP_NAME_SORT_KEY,
                              &value);

        g_value_unset (&value);

	rb_node_set_property (node,
			      RB_NODE_PROP_ARTIST,
			      &val);
        rb_node_set_property (node,
			      RB_NODE_PROP_REAL_ARTIST,
			      &val);
		
	g_value_unset (&val);

	g_value_init (&val, G_TYPE_STRING);

	folded = g_utf8_casefold (artist, -1);
	g_free (artist);

	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);

	g_value_set_string (&val, collated);
	g_free (collated);

	rb_node_set_property (node,
			      RB_NODE_PROP_ARTIST_SORT_KEY,
			      &val);
	g_value_unset (&val);
}

void
rb_song_set_album (RBNode *node,
                   MonkeyMediaStreamInfo *info)
{
	GValue val = { 0, };
	char *collated, *folded;
        GValue value = { 0, };
        char *key;
        gboolean success;

	success = monkey_media_stream_info_get_value (info,
                                                      MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM,
                                                      0,
                                                      &val);

        g_return_if_fail (success == TRUE);
	
        rb_node_set_property (node,
                              RB_NODE_PROP_NAME,
                              &val);

        folded = g_utf8_casefold (g_value_get_string (&val), -1);
        key = g_utf8_collate_key (folded, -1);
        g_free (folded);
        g_value_init (&value, G_TYPE_STRING);
        g_value_set_string (&value, key);
        g_free (key);
        
        rb_node_set_property (node,
                              RB_NODE_PROP_NAME_SORT_KEY,
                              &value);

        g_value_unset (&value);
	
	rb_node_set_property (node,
			      RB_NODE_PROP_ALBUM,
			      &val);
	rb_node_set_property (node,
			      RB_NODE_PROP_REAL_ALBUM,
			      &val);
		
	folded = g_utf8_casefold (g_value_get_string (&val), -1);
	g_value_unset (&val);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);

	g_value_init (&val, G_TYPE_STRING);
	g_value_set_string (&val, collated);
	g_free (collated);
	rb_node_set_property (node,
			      RB_NODE_PROP_ALBUM_SORT_KEY,
			      &val);
	g_value_unset (&val);
}
