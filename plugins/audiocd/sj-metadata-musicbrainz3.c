/*
 * sj-metadata-musicbrainz3.c
 * Copyright (C) 2008 Ross Burton <ross@burtonini.com>
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <string.h>
#include <stdlib.h>
#include <glib.h>
#include <glib-object.h>
#include <gconf/gconf-client.h>
#include <musicbrainz3/mb_c.h>

#include "sj-metadata-musicbrainz3.h"
#include "sj-structures.h"
#include "sj-error.h"

#define GET(field, function, obj) {						\
	function (obj, buffer, sizeof (buffer));				\
	if (field)								\
		g_free (field);							\
	if (*buffer == '\0')							\
		field = NULL;							\
	else									\
		field = g_strdup (buffer);					\
}

#if HAVE_MB_EXTRACT_UUID
#define GET_ID(field, function, obj) {						\
	char uuid_buffer[37];							\
        function (obj, buffer, sizeof (buffer));				\
	mb_extract_uuid (buffer, uuid_buffer, sizeof (uuid_buffer));		\
	if (field)								\
		g_free (field);							\
	if (*uuid_buffer == '\0')						\
		field = NULL;							\
	else									\
		field = g_strdup (uuid_buffer);					\
}
#else
#define GET_ID(field, function, obj) {						\
	if (field)								\
		g_free (field);							\
	field = NULL;								\
}
#endif /* HAVE_MB_EXTRACT_UUID */

#define GCONF_MUSICBRAINZ_SERVER "/apps/sound-juicer/musicbrainz_server"
#define GCONF_PROXY_USE_PROXY "/system/http_proxy/use_http_proxy"
#define GCONF_PROXY_HOST "/system/http_proxy/host"
#define GCONF_PROXY_PORT "/system/http_proxy/port"
#define GCONF_PROXY_USE_AUTHENTICATION "/system/http_proxy/use_authentication"
#define GCONF_PROXY_USERNAME "/system/http_proxy/authentication_user"
#define GCONF_PROXY_PASSWORD "/system/http_proxy/authentication_password"

typedef struct {
  MbWebService mb;
  MbDisc disc;
  char *cdrom;
  /* Proxy */
  char *http_proxy;
  int http_proxy_port;
} SjMetadataMusicbrainz3Private;

#define GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SJ_TYPE_METADATA_MUSICBRAINZ3, SjMetadataMusicbrainz3Private))

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_USE_PROXY,
  PROP_PROXY_HOST,
  PROP_PROXY_PORT,
};

static void metadata_interface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SjMetadataMusicbrainz3,
                         sj_metadata_musicbrainz3,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SJ_TYPE_METADATA,
                                                metadata_interface_init));


/*
 * Private methods
 */

static AlbumDetails *
make_album_from_release (MbRelease *release)
{
  AlbumDetails *album;
  char buffer[512];
  MbArtist artist;
  char *new_title;
  int i;

  g_assert (release);

  album = g_new0 (AlbumDetails, 1);

  GET_ID (album->album_id, mb_release_get_id, release);
  GET (album->title, mb_release_get_title, release);
  new_title = sj_metadata_helper_scan_disc_number (album->title, &album->disc_number);
  if (new_title) {
    g_free (album->title);
    album->title = new_title;
  }

  artist = mb_release_get_artist (release);
  GET_ID (album->artist_id, mb_artist_get_id, artist);
  GET (album->artist, mb_artist_get_name, artist);
  GET (album->artist_sortname, mb_artist_get_sortname, artist);

  if (mb_release_get_num_release_events (release) >= 1) {
    MbReleaseEvent event;
    char *date = NULL;

    event = mb_release_get_release_event (release, 0);
    GET (date, mb_release_event_get_date, event);
    album->release_date = sj_metadata_helper_scan_date (date);
    g_free (date);
  }

  album->number = mb_release_get_num_tracks (release);
  GET (album->asin, mb_release_get_asin, release);

  for (i = 0; i < mb_release_get_num_relations (release); i++) {
    MbRelation relation;
    char *type = NULL;

    relation = mb_release_get_relation (release, i);
    GET(type, mb_relation_get_type, relation);
    if (type && g_str_equal (type, "http://musicbrainz.org/ns/rel-1.0#Wikipedia")) {
      GET (album->wikipedia, mb_relation_get_target_id, relation);
    } else if (type && g_str_equal (type, "http://musicbrainz.org/ns/rel-1.0#Discogs")) {
      GET (album->discogs, mb_relation_get_target_id, relation);
      continue;
    }
    g_free (type);
  }

  for (i = 0; i < mb_release_get_num_types (release); i++) {
    mb_release_get_type (release, i, buffer, sizeof(buffer));

    if (g_str_has_suffix (buffer, "#Spokenword")
    	|| g_str_has_suffix (buffer, "#Interview")
    	|| g_str_has_suffix (buffer, "#Audiobook")) {
      album->is_spoken_word = TRUE;
      break;
    }
  }

  for (i = 0; i < album->number; i++) {
    MbTrack mbt;
    TrackDetails *track;

    mbt = mb_release_get_track (release, i);
    track = g_new0 (TrackDetails, 1);

    track->album = album;

    track->number = i + 1;
    GET_ID (track->track_id, mb_track_get_id, mbt);

    GET (track->title, mb_track_get_title, mbt);
    track->duration = mb_track_get_duration (mbt) / 1000;

    artist = mb_track_get_artist (mbt);
    if (artist == NULL)
      artist = mb_release_get_artist (release);
    GET_ID (track->artist_id, mb_artist_get_id, artist);
    GET (track->artist, mb_artist_get_name, artist);
    GET (track->artist_sortname, mb_artist_get_sortname, artist);

    album->tracks = g_list_append (album->tracks, track);
  }

  return album;
}

static void
fill_empty_durations (MbDisc *disc, AlbumDetails *album)
{
  if (disc == NULL)
    return;
}

static MbReleaseIncludes
get_release_includes (void)
{
    MbReleaseIncludes includes;

    includes = mb_release_includes_new ();
    includes = mb_release_includes_artist (includes);
    includes = mb_release_includes_tracks (includes);
    includes = mb_artist_includes_release_events (includes);
    includes = mb_track_includes_url_relations (includes);

    return includes;
}

/**
 * Virtual methods
 */

static GList *
mb_list_albums (SjMetadata *metadata, char **url, GError **error)
{
  SjMetadataMusicbrainz3Private *priv;
  GList *albums = NULL;
  MbQuery query;
  MbReleaseFilter filter;
  MbResultList results;
  MbRelease release;
  char *id = NULL;
  char buffer[1024];
  int i;
  g_return_val_if_fail (SJ_IS_METADATA_MUSICBRAINZ3 (metadata), NULL);

  priv = GET_PRIVATE (metadata);

  if (sj_metadata_helper_check_media (priv->cdrom, error) == FALSE) {
    return NULL;
  }

  priv->disc = mb_read_disc (priv->cdrom);
  if (priv->disc == NULL)
    return NULL;

  if (url != NULL) {
    mb_get_submission_url (priv->disc, NULL, 0, buffer, sizeof (buffer));
    *url = g_strdup (buffer);
  }

  if (g_getenv("MUSICBRAINZ_FORCE_DISC_ID")) {
    id = g_strdup (g_getenv("MUSICBRAINZ_FORCE_DISC_ID"));
  } else {
    GET(id, mb_disc_get_id, priv->disc);
  }

  query = mb_query_new (priv->mb, "sound-juicer");
  filter = mb_release_filter_new ();
  filter = mb_release_filter_disc_id (filter, id);
  results = mb_query_get_releases (query, filter);
  mb_release_filter_free (filter);
  g_free (id);

  if (results == NULL) {
    mb_query_free (query);
    return NULL;
  }

  if (mb_result_list_get_size (results) == 0) {
    mb_result_list_free (results);
    mb_query_free (query);
    return NULL;
  }

  for (i = 0; i < mb_result_list_get_size (results); i++) {
    AlbumDetails *album;
    MbReleaseIncludes includes;
    char buffer[512];

    release = mb_result_list_get_release (results, i);
    if(release) {
      mb_release_get_id (release, buffer, sizeof (buffer));
      includes = get_release_includes ();
      release = mb_query_get_release_by_id (query, buffer, includes);
      if(release) {
        mb_release_includes_free (includes);
        album = make_album_from_release (release);
        album->metadata_source = SOURCE_MUSICBRAINZ;
        fill_empty_durations (priv->disc, album);
        albums = g_list_append (albums, album);
        mb_release_free (release);
      }
    }
  }
  mb_result_list_free (results);
  mb_query_free (query);

  return albums;
}

/*
 * GObject methods
 */

static void
metadata_interface_init (gpointer g_iface, gpointer iface_data)
{
  SjMetadataClass *klass = (SjMetadataClass*)g_iface;

  klass->list_albums = mb_list_albums;
}

static void
sj_metadata_musicbrainz3_init (SjMetadataMusicbrainz3 *self)
{
  GConfClient *gconf_client;
  gchar *server_name;

  SjMetadataMusicbrainz3Private *priv;

  priv = GET_PRIVATE (self);

  priv->mb = mb_webservice_new ();

  gconf_client = gconf_client_get_default ();

  server_name = gconf_client_get_string (gconf_client, GCONF_MUSICBRAINZ_SERVER, NULL);

  if (server_name && strcmp (server_name, "") != 0) {
    mb_webservice_set_host (priv->mb, server_name);
  }

  g_free (server_name);

  /* Set the HTTP proxy */
  if (gconf_client_get_bool (gconf_client, GCONF_PROXY_USE_PROXY, NULL)) {
    char *proxy_host;
    int port;

    proxy_host = gconf_client_get_string (gconf_client, GCONF_PROXY_HOST, NULL);
    mb_webservice_set_proxy_host (priv->mb, proxy_host);
    g_free (proxy_host);

    port = gconf_client_get_int (gconf_client, GCONF_PROXY_PORT, NULL);
    mb_webservice_set_proxy_port (priv->mb, port);

    if (gconf_client_get_bool (gconf_client, GCONF_PROXY_USE_AUTHENTICATION, NULL)) {
      char *username, *password;

      username = gconf_client_get_string (gconf_client, GCONF_PROXY_USERNAME, NULL);
      mb_webservice_set_proxy_username (priv->mb, username);
      g_free (username);

      password = gconf_client_get_string (gconf_client, GCONF_PROXY_PASSWORD, NULL);
      mb_webservice_set_proxy_password (priv->mb, password);
      g_free (password);
    }
  }

  g_object_unref (gconf_client);
}

static void
sj_metadata_musicbrainz3_get_property (GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
  SjMetadataMusicbrainz3Private *priv = GET_PRIVATE (object);
  g_assert (priv);

  switch (property_id) {
  case PROP_DEVICE:
    g_value_set_string (value, priv->cdrom);
    break;
  case PROP_PROXY_HOST:
    g_value_set_string (value, priv->http_proxy);
    break;
  case PROP_PROXY_PORT:
    g_value_set_int (value, priv->http_proxy_port);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
sj_metadata_musicbrainz3_set_property (GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec)
{
  SjMetadataMusicbrainz3Private *priv = GET_PRIVATE (object);
  g_assert (priv);

  switch (property_id) {
  case PROP_DEVICE:
    if (priv->cdrom)
      g_free (priv->cdrom);
    priv->cdrom = g_value_dup_string (value);
    break;
  case PROP_PROXY_HOST:
    if (priv->http_proxy) {
      g_free (priv->http_proxy);
    }
    priv->http_proxy = g_value_dup_string (value);
    /* TODO: check this unsets the proxy if NULL, or should we pass "" ? */
    mb_webservice_set_proxy_host (priv->mb, priv->http_proxy);
    break;
  case PROP_PROXY_PORT:
    priv->http_proxy_port = g_value_get_int (value);
    mb_webservice_set_proxy_port (priv->mb, priv->http_proxy_port);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
sj_metadata_musicbrainz3_finalize (GObject *object)
{
  SjMetadataMusicbrainz3Private *priv;
  
  priv = GET_PRIVATE (object);

  if (priv->mb != NULL) {
    mb_webservice_free (priv->mb);
    priv->mb = NULL;
  }
  g_free (priv->cdrom);

  G_OBJECT_CLASS (sj_metadata_musicbrainz3_parent_class)->finalize (object);
}

static void
sj_metadata_musicbrainz3_class_init (SjMetadataMusicbrainz3Class *class)
{
  GObjectClass *object_class = (GObjectClass*)class;

  g_type_class_add_private (class, sizeof (SjMetadataMusicbrainz3Private));

  object_class->get_property = sj_metadata_musicbrainz3_get_property;
  object_class->set_property = sj_metadata_musicbrainz3_set_property;
  object_class->finalize = sj_metadata_musicbrainz3_finalize;

  g_object_class_override_property (object_class, PROP_DEVICE, "device");
  g_object_class_override_property (object_class, PROP_PROXY_HOST, "proxy-host");
  g_object_class_override_property (object_class, PROP_PROXY_PORT, "proxy-port");
}


/*
 * Public methods.
 */

GObject *
sj_metadata_musicbrainz3_new (void)
{
  return g_object_new (SJ_TYPE_METADATA_MUSICBRAINZ3, NULL);
}
