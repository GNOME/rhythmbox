/*  monkey-media
 *
 *  arch-tag: Implementation of MusicBrainz metadata loading object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 */

#include <config.h>
#include <unistd.h>
#include <stdio.h>
#include <gconf/gconf.h>
#include <gconf/gconf-client.h>
#include <musicbrainz/mb_c.h>
#include <libgnome/gnome-init.h>
#include <libxml/tree.h>
#include <string.h>

#include "monkey-media.h"
#include "monkey-media-private.h"
#include "monkey-media-musicbrainz.h"

static void monkey_media_musicbrainz_class_init (MonkeyMediaMusicbrainzClass *klass);
static void monkey_media_musicbrainz_init (MonkeyMediaMusicbrainz *mb);
static void monkey_media_musicbrainz_finalize (GObject *object);
static void sync_proxy_settings (MonkeyMediaMusicbrainz *mb);
static void sync_server_settings (MonkeyMediaMusicbrainz *mb);
static void proxy_settings_changed (GConfClient *client, guint cnxn_id,
			            GConfEntry *entry, MonkeyMediaMusicbrainz *mb);
static void server_settings_changed (GConfClient *client, guint cnxn_id,
			             GConfEntry *entry, MonkeyMediaMusicbrainz *mb);
static void free_info (MonkeyMediaMusicbrainz *mb);

typedef struct
{
	char *title;
	char *title_id;
	
	char *artist;
	char *artist_id;
	
	char *album;
	char *album_id;

	int track_number;
	int max_track_number;
} TrackInfo;
      
struct MonkeyMediaMusicbrainzPrivate 
{
	GMutex *lock;

	musicbrainz_t mb;

	gboolean use_proxy;
	gboolean use_auth;

	GConfClient *client;

	char *album_dir;
	char *mb_dir;

	char *loaded_id;

	MonkeyMediaMusicbrainzQueryType query_type;
	union
	{
		GPtrArray *tracks;
		TrackInfo *track;
	} track_info;
};

#define CONF_MONKEYMEDIA_DIR             "/system/monkey_media"
#define CONF_KEY_MUSICBRAINZ_SERVER      "/system/monkey_media/musicbrainz_server"
#define CONF_KEY_MUSICBRAINZ_SERVER_PORT "/system/monkey_media/musicbrainz_server_port"
#define CONF_PROXY_DIR                   "/system/http_proxy"
#define CONF_KEY_PROXY_USE_AUTH          "/system/http_proxy/use_authentication"
#define CONF_KEY_PROXY_USE               "/system/http_proxy/use_http_proxy"
#define CONF_KEY_PROXY_HOST              "/system/http_proxy/host"
#define CONF_KEY_PROXY_PORT              "/system/http_proxy/port"

#define MM_CD_METADATA_XML_VERSION "1.1"

static GObjectClass *parent_class = NULL;

static MonkeyMediaMusicbrainz *global_mb = NULL;
static GMutex *global_mb_lock = NULL;

GType
monkey_media_musicbrainz_get_type (void)
{
	static GType type = 0;

	if (type == 0) 
	{
		static const GTypeInfo our_info =
		{
			sizeof (MonkeyMediaMusicbrainzClass),
			NULL,
			NULL,
			(GClassInitFunc) monkey_media_musicbrainz_class_init,
			NULL,
			NULL,
			sizeof (MonkeyMediaMusicbrainz),
			0,
			(GInstanceInitFunc) monkey_media_musicbrainz_init,
		};

		type = g_type_register_static (G_TYPE_OBJECT,
					       "MonkeyMediaMusicbrainz",
					       &our_info, 0);
	}

	return type;
}

static void
monkey_media_musicbrainz_class_init (MonkeyMediaMusicbrainzClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = monkey_media_musicbrainz_finalize;
}

static void
monkey_media_musicbrainz_init (MonkeyMediaMusicbrainz *mb)
{
	mb->priv = g_new0 (MonkeyMediaMusicbrainzPrivate, 1);

	mb->priv->lock = g_mutex_new ();

	mb->priv->query_type = MONKEY_MEDIA_MUSICBRAINZ_QUERY_NONE;

	mb->priv->mb = mb_New ();

	mb_UseUTF8 (mb->priv->mb, 1);

	mb->priv->client = gconf_client_get_default ();

	gconf_client_add_dir (mb->priv->client,
			      CONF_PROXY_DIR,
			      GCONF_CLIENT_PRELOAD_NONE,
			      NULL);
	gconf_client_add_dir (mb->priv->client,
			      CONF_MONKEYMEDIA_DIR,
			      GCONF_CLIENT_PRELOAD_NONE,
			      NULL);

	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_PROXY_USE_AUTH,
				 (GConfClientNotifyFunc) proxy_settings_changed,
				 mb, NULL, NULL);
	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_PROXY_USE,
				 (GConfClientNotifyFunc) proxy_settings_changed,
				 mb, NULL, NULL);
	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_PROXY_HOST,
				 (GConfClientNotifyFunc) proxy_settings_changed,
				 mb, NULL, NULL);
	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_PROXY_PORT,
				 (GConfClientNotifyFunc) proxy_settings_changed,
				 mb, NULL, NULL);

	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_MUSICBRAINZ_SERVER,
				 (GConfClientNotifyFunc) server_settings_changed,
				 mb, NULL, NULL);
	gconf_client_notify_add (mb->priv->client,
				 CONF_KEY_MUSICBRAINZ_SERVER_PORT,
				 (GConfClientNotifyFunc) server_settings_changed,
				 mb, NULL, NULL);

	sync_proxy_settings (mb);
	sync_server_settings (mb);

	mb->priv->mb_dir = g_build_filename (monkey_media_get_dir (),
					     "cached_metadata",
					     NULL);
	mb->priv->album_dir = g_build_filename (mb->priv->mb_dir,
						"albums",
						NULL);

	/* ensure dir exists */
	if (g_file_test (mb->priv->mb_dir, G_FILE_TEST_IS_DIR) == FALSE)
	{
		monkey_media_mkdir (mb->priv->mb_dir);
	}
	if (g_file_test (mb->priv->album_dir, G_FILE_TEST_IS_DIR) == FALSE)
	{
		monkey_media_mkdir (mb->priv->album_dir);
	}
}

static void
monkey_media_musicbrainz_finalize (GObject *object)
{
	MonkeyMediaMusicbrainz *mb;

	mb = MONKEY_MEDIA_MUSICBRAINZ (object);

	g_mutex_free (mb->priv->lock);

	mb_Delete (mb->priv->mb);

	g_object_unref (G_OBJECT (mb->priv->client));

	free_info (mb);

	g_free (mb->priv->mb_dir);
	g_free (mb->priv->album_dir);

	g_free (mb->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

void
monkey_media_musicbrainz_unref_if_around (void)
{
	if (global_mb != NULL)
		g_object_unref (G_OBJECT (global_mb));
	if (global_mb_lock != NULL)
		g_mutex_free (global_mb_lock);
}

MonkeyMediaMusicbrainz *
monkey_media_musicbrainz_new (void)
{
	MonkeyMediaMusicbrainz *mb;

	if (global_mb_lock == NULL)
		global_mb_lock = g_mutex_new ();
	g_mutex_lock (global_mb_lock);

	/* we fake creating the object, since we need it internally as well
	 * and having multiple instances would just be a waste of memory. */
	if (global_mb != NULL)
	{
		g_mutex_unlock (global_mb_lock);
		return g_object_ref (G_OBJECT (global_mb));
	}

	mb = MONKEY_MEDIA_MUSICBRAINZ (g_object_new (MONKEY_MEDIA_TYPE_MUSICBRAINZ, NULL));

	g_return_val_if_fail (mb->priv != NULL, NULL);

	global_mb = mb;

	g_mutex_unlock (global_mb_lock);

	return g_object_ref (G_OBJECT (mb));
}

static void
free_track (TrackInfo *info)
{
	g_free (info->title);
	g_free (info->title_id);
	g_free (info->artist);
	g_free (info->artist_id);
	g_free (info->album);
	g_free (info->album_id);
	g_free (info);
}

static void
free_info (MonkeyMediaMusicbrainz *mb)
{
	int i;

	if (mb->priv->query_type == MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD &&
	    mb->priv->track_info.tracks != NULL)
	{
		for (i = 0; i < mb->priv->track_info.tracks->len; i++)
		{
			TrackInfo *info;

			info = g_ptr_array_index (mb->priv->track_info.tracks, i);

			free_track (info);
		}

		g_ptr_array_free (mb->priv->track_info.tracks, FALSE);
		mb->priv->track_info.tracks = NULL;
	}

	if (mb->priv->query_type == MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG &&
	    mb->priv->track_info.track != NULL)
	{
		free_track (mb->priv->track_info.track);
		mb->priv->track_info.track = NULL;
	}

	if (mb->priv->loaded_id != NULL)
	{
		g_free (mb->priv->loaded_id);
		mb->priv->loaded_id = NULL;
	}

	mb->priv->query_type = MONKEY_MEDIA_MUSICBRAINZ_QUERY_NONE;
}

static gboolean
load_album_file (MonkeyMediaMusicbrainz *mb,
		 const char *fn)
{
	xmlDocPtr doc;
	xmlNodePtr root, node;
	char *xml;

	doc = xmlParseFile (fn);

	if (doc == NULL)
		return FALSE;

	root = xmlDocGetRootElement (doc);
	xml = xmlGetProp (root, "version");
	if (xml == NULL || strcmp (xml, MM_CD_METADATA_XML_VERSION) != 0)
	{
		g_free (xml);
		xmlFreeDoc (doc);
		return FALSE;
	}

	mb->priv->query_type = MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD;
	mb->priv->track_info.tracks = g_ptr_array_new ();

	for (node = root->children; node != NULL; node = node->next)
	{
		if (strcmp (node->name, "track") == 0)
		{
			TrackInfo *info;

			info = g_new0 (TrackInfo, 1);

			g_ptr_array_add (mb->priv->track_info.tracks, info);
			
			info->title = xmlNodeGetContent (node);
			info->title_id = xmlGetProp (node, "id");
			info->artist = xmlGetProp (node, "artist");
			info->artist_id = xmlGetProp (node, "artist_id");
			info->album = xmlGetProp (node, "album");
			info->album_id = xmlGetProp (node, "album_id");
			if (mb->priv->loaded_id == NULL)
				mb->priv->loaded_id = g_strdup (info->album_id);
		}
	}

	xmlFreeDoc (doc);

	return TRUE;
}

static void
save_album_file (MonkeyMediaMusicbrainz *mb,
		 const char *fn)
{
	xmlDocPtr doc;
	xmlNodePtr root, node;
	int i;

	if (g_file_test (mb->priv->album_dir, G_FILE_TEST_IS_DIR) == FALSE)
		return;

	xmlIndentTreeOutput = TRUE;
	doc = xmlNewDoc ("1.0");

	root = xmlNewDocNode (doc, NULL, "audio_cd", NULL);
	xmlSetProp (root, "version", MM_CD_METADATA_XML_VERSION);
	xmlDocSetRootElement (doc, root);

	for (i = 0; i < mb->priv->track_info.tracks->len; i++)
	{
		TrackInfo *info;
		char *xml;

		info = g_ptr_array_index (mb->priv->track_info.tracks, i);
		
		node = xmlNewChild (root, NULL, "track", NULL);
		
		xml = g_strdup_printf ("%d", i + 1);
		xmlSetProp (node, "num", xml);
		g_free (xml);

		xmlSetProp (node, "id", info->title_id);

		xmlSetProp (node, "album", info->album);

		xmlSetProp (node, "album_id", info->album_id);

		xmlSetProp (node, "artist", info->artist);

		xmlSetProp (node, "artist_id", info->artist_id);

		xmlNodeSetContent (node, info->title);
	}

	xmlSaveFormatFileEnc (fn, doc, "UTF-8", 1);

	xmlFreeDoc (doc);
}

static gboolean
load_info_track (MonkeyMediaMusicbrainz *mb,
		 const char *id)
{
	gboolean ret = TRUE, res;
	char *args[2], data[256];
	TrackInfo *info;
	
	/* FIXME musicbrainz doesnt support proxy auth yet */
	if (mb->priv->use_auth == TRUE && mb->priv->use_proxy == TRUE)
	{
		return FALSE;
	}

	args[0] = (char *) id;
	args[1] = NULL;

	res = mb_QueryWithArgs (mb->priv->mb,
			        MBQ_GetTrackByTRMId,
			        args);
	if (res == FALSE)
	{
		return FALSE;
	}
	else
	{
		mb_Select1 (mb->priv->mb, MBS_SelectTrack, 1);
	}

	mb->priv->loaded_id = g_strdup (id);

	mb->priv->query_type = MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG;

	mb->priv->track_info.track = info = g_new0 (TrackInfo, 1);

	if (mb_GetResultData (mb->priv->mb,
			      MBE_TrackGetTrackName,
			      data, 256))
	{
		info->title = g_strdup (data);
	}

	info->title_id = g_strdup (id);

	if (mb_GetResultData (mb->priv->mb,
			      MBE_TrackGetArtistName,
			      data, 256))
	{
		info->artist = g_strdup (data);
	}

	if (mb_GetResultData (mb->priv->mb,
			      MBE_TrackGetArtistId,
			      data, 256))
	{
		info->artist_id = g_strdup (data);
	}

	if (mb_GetResultData (mb->priv->mb,
			      MBE_QuickGetAlbumName,
			      data, 256))
	{
		info->album = g_strdup (data);
	}

	if (mb_GetResultData (mb->priv->mb,
			      MBE_LookupGetAlbumId,
			      data, 256))
	{
		info->album_id = g_strdup (data);
	}
	
	info->track_number = mb_GetResultInt (mb->priv->mb,
			                      MBE_TrackGetTrackNum);
	if (info->track_number < 0)
		info->track_number = 0;
	
	args[0] = info->album_id;
	args[1] = NULL;
	
	res = mb_QueryWithArgs (mb->priv->mb,
			        MBQ_GetAlbumById,
			        args);
	if (res == FALSE)
	{
		return FALSE;
	}
	else
	{
		mb_Select1 (mb->priv->mb, MBS_SelectAlbum, 1);
	}

	info->max_track_number = mb_GetResultInt (mb->priv->mb,
						  MBE_AlbumGetNumTracks);
	if (info->track_number < 0)
		info->track_number = 0;

	return ret;
}

static gboolean
load_info_cd (MonkeyMediaMusicbrainz *mb,
	      const char *id)
{
	gboolean ret = TRUE, res;
	char *args[2], data[256], *fn, *disc_title = NULL;
	int n_tracks, i;
	
	fn = g_build_filename (mb->priv->album_dir, id, NULL);

	if (g_file_test (fn, G_FILE_TEST_EXISTS) == TRUE)
	{
		/* already have this info, load from cache */
		ret = load_album_file (mb, fn);

		if (ret == TRUE)
		{
			g_free (fn);

			return ret;
		}
	}

	/* don't have this info yet, retreive */

	/* FIXME musicbrainz doesnt support proxy auth yet */
	if (mb->priv->use_auth == TRUE && mb->priv->use_proxy == TRUE)
	{
		g_free (fn);

		return FALSE;
	}

	args[0] = (char *) id;
	args[1] = NULL;

	res = mb_QueryWithArgs (mb->priv->mb,
			        MBQ_GetCDInfoFromCDIndexId,
			        args);
	if (res == FALSE)
	{
		g_free (fn);

		return FALSE;
	}
	else
	{
		mb_Select1 (mb->priv->mb, MBS_SelectAlbum, 1);

		n_tracks = mb_GetResultInt (mb->priv->mb, MBE_AlbumGetNumTracks);
		if (n_tracks <= 0)
		{
			g_free (fn);

			return FALSE;
		}
	}

	mb->priv->loaded_id = g_strdup (id);

	if (mb_GetResultData (mb->priv->mb,
			      MBE_AlbumGetAlbumName,
			      data, 256))
	{
		disc_title = g_strdup (data);
	}

	mb->priv->query_type = MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD;
	mb->priv->track_info.tracks = g_ptr_array_new ();

	for (i = 1; i <= n_tracks; i++)
	{
		TrackInfo *info;

		info = g_new0 (TrackInfo, 1);

		g_ptr_array_add (mb->priv->track_info.tracks, info);

		if (mb_GetResultData1 (mb->priv->mb,
				       MBE_AlbumGetTrackName,
				       data, 256, i))
		{
			info->title = g_strdup (data);
		}

		if (mb_GetResultData1 (mb->priv->mb,
				       MBE_AlbumGetTrackId,
				       data, 256, i))
		{
			info->title_id = g_strdup (data);
		}

		if (mb_GetResultData1 (mb->priv->mb,
				       MBE_AlbumGetArtistName,
				       data, 256, i))
		{
			info->artist = g_strdup (data);
		}

		if (mb_GetResultData1 (mb->priv->mb,
				       MBE_AlbumGetArtistId,
				       data, 256, i))
		{
			info->artist_id = g_strdup (data);
		}

		info->album = g_strdup (disc_title);
		info->album_id = g_strdup (id);

		info->track_number = i;
		info->max_track_number = n_tracks;
	}

	g_free (disc_title);

	save_album_file (mb, fn);

	g_free (fn);

	return ret;
}

gboolean
monkey_media_musicbrainz_load_info (MonkeyMediaMusicbrainz *mb,
				    MonkeyMediaMusicbrainzQueryType type,
				    const char *id)
{
	gboolean ret = FALSE;
	
	g_return_val_if_fail (MONKEY_MEDIA_IS_MUSICBRAINZ (mb), FALSE);
	g_return_val_if_fail (id != NULL, FALSE);

	g_mutex_lock (mb->priv->lock);

	if (mb->priv->loaded_id != NULL && strcmp (mb->priv->loaded_id, id) == 0 &&
	    type == mb->priv->query_type)
	{
		g_mutex_unlock (mb->priv->lock);
		return TRUE;
	}

	free_info (mb);

	switch (type)
	{
	case MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD:
		ret = load_info_cd (mb, id);
		break;
	case MONKEY_MEDIA_MUSICBRAINZ_QUERY_SONG:
		ret = load_info_track (mb, id);
		break;
	case MONKEY_MEDIA_MUSICBRAINZ_QUERY_NONE:
		ret = FALSE;
		break;
	}

	g_mutex_unlock (mb->priv->lock);

	return ret;
}

gboolean
monkey_media_musicbrainz_query (MonkeyMediaMusicbrainz *mb,
			        MonkeyMediaStreamInfoField field,
				int tracknum,
			        GValue *value)
{
	gboolean ret;
	
	g_return_val_if_fail (MONKEY_MEDIA_IS_MUSICBRAINZ (mb), FALSE);

	g_mutex_lock (mb->priv->lock);

	if (mb->priv->query_type == MONKEY_MEDIA_MUSICBRAINZ_QUERY_NONE)
	{
		g_mutex_unlock (mb->priv->lock);

		return FALSE;
	}

	if (mb->priv->query_type == MONKEY_MEDIA_MUSICBRAINZ_QUERY_CD)
	{
		TrackInfo *info;

		if (mb->priv->track_info.tracks == NULL ||
		    tracknum > mb->priv->track_info.tracks->len)
		{
			g_mutex_unlock (mb->priv->lock);

			return FALSE;
		}

		info = g_ptr_array_index (mb->priv->track_info.tracks, tracknum - 1);

		switch (field)
		{
		case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
			ret = (info->title != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, info->title);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
			ret = (info->artist != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, info->artist);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
			ret = (info->album != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, info->album);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
			ret = (info->max_track_number > 0);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_INT);
				g_value_set_int (value, info->track_number);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
			ret = (info->max_track_number > 0);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_INT);
				g_value_set_int (value, info->max_track_number);
			}
			break;
		default:
			ret = FALSE;
			break;
		}
	}
	else
	{
		switch (field)
		{
		case MONKEY_MEDIA_STREAM_INFO_FIELD_TITLE:
			ret = (mb->priv->track_info.track->title != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, mb->priv->track_info.track->title);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_ARTIST:
			ret = (mb->priv->track_info.track->artist != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, mb->priv->track_info.track->artist);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_ALBUM:
			ret = (mb->priv->track_info.track->album != NULL);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_STRING);
				g_value_set_string (value, mb->priv->track_info.track->album);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_TRACK_NUMBER:
			ret = (mb->priv->track_info.track->track_number > 0);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_INT);
				g_value_set_int (value, mb->priv->track_info.track->track_number);
			}
			break;
		case MONKEY_MEDIA_STREAM_INFO_FIELD_MAX_TRACK_NUMBER:
			ret = (mb->priv->track_info.track->max_track_number > 0);
			if (ret == TRUE)
			{
				g_value_init (value, G_TYPE_INT);
				g_value_set_int (value, mb->priv->track_info.track->max_track_number);
			}
			break;
		default:
			ret = FALSE;
			break;
		}
	}

	g_mutex_unlock (mb->priv->lock);

	return ret;
}

static void
proxy_settings_changed (GConfClient *client, guint cnxn_id,
			GConfEntry *entry, MonkeyMediaMusicbrainz *mb)
{
	sync_proxy_settings (mb);
}

static void
server_settings_changed (GConfClient *client, guint cnxn_id,
			 GConfEntry *entry, MonkeyMediaMusicbrainz *mb)
{
	sync_server_settings (mb);
}

static void
sync_proxy_settings (MonkeyMediaMusicbrainz *mb)
{
	g_mutex_lock (mb->priv->lock);

	mb->priv->use_proxy = gconf_client_get_bool (mb->priv->client,
					             CONF_KEY_PROXY_USE,
					             NULL);

	mb->priv->use_auth = gconf_client_get_bool (mb->priv->client,
						    CONF_KEY_PROXY_USE_AUTH,
						    NULL);
	
	if (mb->priv->use_proxy == TRUE)
	{
		char *server_addr;
		int port;

		server_addr = gconf_client_get_string (mb->priv->client,
						       CONF_KEY_PROXY_HOST,
						       NULL);
		port = gconf_client_get_int (mb->priv->client,
					     CONF_KEY_PROXY_PORT,
					     NULL);
		
		mb_SetProxy (mb->priv->mb, server_addr, port);

		g_free (server_addr);
	}
	else
	{
		mb_SetProxy (mb->priv->mb, "", 0);
	}

	g_mutex_unlock (mb->priv->lock);
}

static void
sync_server_settings (MonkeyMediaMusicbrainz *mb)
{
	char *server_addr;
	int server_port;

	g_mutex_lock (mb->priv->lock);

	server_addr = gconf_client_get_string (mb->priv->client,
					       CONF_KEY_MUSICBRAINZ_SERVER,
					       NULL);
	server_port = gconf_client_get_int (mb->priv->client,
					    CONF_KEY_MUSICBRAINZ_SERVER_PORT,
					    NULL);

	if (server_addr == NULL)
		server_addr = g_strdup ("www.musicbrainz.org");

	if (server_port <= 0)
		server_port = 80;

	mb_SetServer (mb->priv->mb,
		      server_addr,
		      server_port);

	g_free (server_addr);

	g_mutex_unlock (mb->priv->lock);
}
