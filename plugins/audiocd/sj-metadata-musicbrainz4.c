/*
 * sj-metadata-musicbrainz4.c
 * Copyright (C) 2008 Ross Burton <ross@burtonini.com>
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2011 Christophe Fergeau <cfergeau@redhat.com>
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
#include <discid/discid.h>
#include <musicbrainz4/mb4_c.h>

#include "sj-metadata-musicbrainz4.h"
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

#define GCONF_MUSICBRAINZ_SERVER "/apps/sound-juicer/musicbrainz_server"
#define GCONF_PROXY_USE_PROXY "/system/http_proxy/use_http_proxy"
#define GCONF_PROXY_HOST "/system/http_proxy/host"
#define GCONF_PROXY_PORT "/system/http_proxy/port"
#define GCONF_PROXY_USE_AUTHENTICATION "/system/http_proxy/use_authentication"
#define GCONF_PROXY_USERNAME "/system/http_proxy/authentication_user"
#define GCONF_PROXY_PASSWORD "/system/http_proxy/authentication_password"
#define SJ_MUSICBRAINZ_USER_AGENT "libjuicer-"VERSION

typedef struct {
  Mb4Query mb;
  DiscId *disc;
  char *cdrom;
  /* Proxy */
  char *http_proxy;
  int http_proxy_port;
} SjMetadataMusicbrainz4Private;

#define GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SJ_TYPE_METADATA_MUSICBRAINZ4, SjMetadataMusicbrainz4Private))

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_USE_PROXY,
  PROP_PROXY_HOST,
  PROP_PROXY_PORT,
};

static void metadata_interface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SjMetadataMusicbrainz4,
                         sj_metadata_musicbrainz4,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SJ_TYPE_METADATA,
                                                metadata_interface_init));


/*
 * Private methods
 */
#ifdef DUMP_DETAILS
static void
sj_mb4_album_details_dump (AlbumDetails *details)
{
  if (details->country)
    g_print ("Country: %s\n", details->country);
  if (details->type)
    g_print ("Type: %s\n", details->type);
  if (details->lyrics_url)
    g_print ("Lyrics URL: %s\n", details->lyrics_url);
}
#else
#define sj_mb4_album_details_dump(...)
#endif

static GList *
get_artist_list (Mb4ArtistCredit credit)
{
  Mb4NameCreditList name_list;
  GList *artists;
  unsigned int i;
  char buffer[512]; /* for the GET macro */

  if (credit == NULL)
    return NULL;

  name_list = mb4_artistcredit_get_namecreditlist (credit);
  if (name_list == NULL) {
    return NULL;
  }

  artists = NULL;
  for (i = 0; i < mb4_namecredit_list_size (name_list); i++) {
    Mb4NameCredit name_credit;
    Mb4Artist artist;
    ArtistDetails *details;

    name_credit = mb4_namecredit_list_item (name_list, i);
    details = g_new0 (ArtistDetails, 1);
    GET (details->joinphrase, mb4_namecredit_get_joinphrase, name_credit);
    artists = g_list_prepend (artists, details);
    artist = mb4_namecredit_get_artist (name_credit);
    if (!artist) {
      g_warning ("no Mb4Artist associated with Mb4NameCredit, falling back to Mb4NameCredit::name");
      GET (details->name, mb4_namecredit_get_name, name_credit);
      continue;
    }

    GET (details->id, mb4_artist_get_id, artist);
    GET (details->name, mb4_artist_get_name, artist);
    GET (details->sortname, mb4_artist_get_sortname, artist);
    GET (details->disambiguation, mb4_artist_get_disambiguation, artist);
    GET (details->gender, mb4_artist_get_gender, artist);
    GET (details->country, mb4_artist_get_country, artist);
  }

  return g_list_reverse(artists);
}

static void
get_artist_info (GList *artists, char **name, char **sortname, char **id)
{
  GString *artist_name;
  GList *it;
  unsigned int artist_count;

  artist_name = g_string_new (NULL);
  artist_count = 0;
  for (it = artists; it != NULL; it = it->next) {
    ArtistDetails *details = (ArtistDetails *)it->data;
    artist_count++;
    g_string_append (artist_name, details->name);
    if (details->joinphrase != NULL)
      g_string_append (artist_name, details->joinphrase);
  }

  if (artist_count != 1) {
      g_warning ("multiple artists");
      if (sortname != NULL)
        *sortname = NULL;
      if (id != NULL)
        *id = NULL;
  } else {
      ArtistDetails *details = (ArtistDetails *)artists->data;
      if (sortname != NULL)
        *sortname = g_strdup (details->sortname);
      if (id != NULL)
        *id = g_strdup (details->id);
  }

  if (name != NULL)
    *name = artist_name->str;

  g_string_free (artist_name, FALSE);
}


static void
fill_relations (Mb4RelationList relations, AlbumDetails *album)
{
  unsigned int i;

  for (i = 0; i < mb4_relation_list_size (relations); i++) {
    Mb4Relation relation;
    char buffer[512]; /* for the GET() macro */
    char *type = NULL;

    relation = mb4_relation_list_item (relations, i);
    if (relation == NULL)
      continue;

    GET (type, mb4_relation_get_type, relation);
    if (type == NULL) {
      continue;
    }
    if (g_str_equal (type, "wikipedia")) {
      char *wikipedia = NULL;
      GET (wikipedia, mb4_relation_get_target, relation);
      if (wikipedia != NULL) {
          g_free (album->wikipedia);
          album->wikipedia = wikipedia;
      }
    } else if (g_str_equal (type, "discogs")) {
      char *discogs = NULL;
      GET (discogs, mb4_relation_get_target, relation);
      if (discogs != NULL) {
          g_free (album->discogs);
          album->discogs = discogs;
      }
    } else if (g_str_equal (type, "lyrics")) {
      char *lyrics = NULL;
      GET (lyrics, mb4_relation_get_target, relation);
      if (lyrics != NULL) {
          g_free (album->lyrics_url);
          album->lyrics_url = lyrics;
      }
    }
    g_free (type);
  }
}

static void
fill_tracks_from_medium (Mb4Medium medium, AlbumDetails *album)
{
  Mb4TrackList track_list;
  GList *tracks;
  unsigned int i;
  char buffer[512]; /* for the GET() macro */

  track_list = mb4_medium_get_tracklist (medium);
  if (!track_list)
    return;

  album->number = mb4_track_list_size (track_list);

  tracks = NULL;

  for (i = 0; i < mb4_track_list_size (track_list); i++) {
    Mb4Track mbt;
    Mb4ArtistCredit credit;
    Mb4Recording recording;
    TrackDetails *track;

    mbt = mb4_track_list_item (track_list, i);
    if (!mbt)
      continue;

    track = g_new0 (TrackDetails, 1);

    track->album = album;

    track->number = mb4_track_get_position (mbt);
    recording = mb4_track_get_recording (mbt);
    if (recording != NULL) {
      GET (track->title, mb4_recording_get_title, recording);
      GET (track->track_id, mb4_recording_get_id, recording);
      track->duration = mb4_recording_get_length (recording) / 1000;
      credit = mb4_recording_get_artistcredit (recording);
    } else {
      GET (track->title, mb4_track_get_title, mbt);
      track->duration = mb4_track_get_length (mbt) / 1000;
      credit = mb4_track_get_artistcredit (mbt);
    }

    if (credit) {
      GList *artists;
      artists = get_artist_list (credit);
      if (artists) {
        get_artist_info (artists, &track->artist,
                         &track->artist_sortname,
                         &track->artist_id);
      }
      track->artists = artists;
    }
    if (track->artist == NULL)
      track->artist = g_strdup (album->artist);
    if (track->artist_sortname == NULL)
      track->artist_sortname = g_strdup (album->artist_sortname);
    if (track->artist_id == NULL)
      track->artist_id = g_strdup (album->artist_id);

    tracks = g_list_prepend (tracks, track);
  }
  album->tracks = g_list_reverse (tracks);
}

static AlbumDetails *
make_album_from_release (Mb4ReleaseGroup group,
                         Mb4Release release,
                         Mb4Medium medium)
{
  AlbumDetails *album;
  Mb4ArtistCredit credit;
  GList *artists;
  char *date = NULL;
  char buffer[512]; /* for the GET macro */

  g_assert (release);
  g_return_val_if_fail (medium != NULL, NULL);

  album = g_new0 (AlbumDetails, 1);

  GET (album->album_id, mb4_release_get_id, release);
  GET (album->title, mb4_medium_get_title, medium);
  if (album->title == NULL)
    GET (album->title, mb4_release_get_title, release);

  credit = mb4_release_get_artistcredit (release);

  artists = get_artist_list (credit);
  if (artists) {
    get_artist_info (artists, &album->artist,
                     &album->artist_sortname,
                     &album->artist_id);
  }
  album->artists = artists;

  GET (date, mb4_release_get_date, release);
  album->release_date = sj_metadata_helper_scan_date (date);
  g_free (date);

  GET (album->asin, mb4_release_get_asin, release);
  GET (album->country, mb4_release_get_country, release);
  if (group) {
    GET (album->type, mb4_releasegroup_get_type, group);
    if (g_str_has_suffix (album->type, "Spokenword")
        || g_str_has_suffix (album->type, "Interview")
        || g_str_has_suffix (album->type, "Audiobook")) {
      album->is_spoken_word = TRUE;
    }
    fill_relations (mb4_releasegroup_get_relationlist(group), album);
  }

  album->disc_number = mb4_medium_get_position (medium);
  fill_tracks_from_medium (medium, album);
  fill_relations (mb4_release_get_relationlist (release), album);

  sj_mb4_album_details_dump (album);
  return album;
}

/*
 * Virtual methods
 */
static GList *
mb4_list_albums (SjMetadata *metadata, char **url, GError **error)
{
  SjMetadataMusicbrainz4Private *priv;
  GList *albums = NULL;
  Mb4ReleaseList releases;
  Mb4Release release;
  const char *discid = NULL;
  char buffer[1024];
  int i;
  g_return_val_if_fail (SJ_IS_METADATA_MUSICBRAINZ4 (metadata), NULL);

  priv = GET_PRIVATE (metadata);

  if (sj_metadata_helper_check_media (priv->cdrom, error) == FALSE) {
    return NULL;
  }

  priv->disc = discid_new ();
  if (priv->disc == NULL)
    return NULL;
  if (discid_read (priv->disc, priv->cdrom) == 0)
    return NULL;

  if (url != NULL)
    *url = g_strdup (discid_get_submission_url (priv->disc));

  if (g_getenv("MUSICBRAINZ_FORCE_DISC_ID")) {
    discid = g_getenv("MUSICBRAINZ_FORCE_DISC_ID");
  } else {
    discid = discid_get_id (priv->disc);
  }

  releases = mb4_query_lookup_discid(priv->mb, discid);

  if (releases == NULL) {
    return NULL;
  }

  if (mb4_release_list_size (releases) == 0) {
    return NULL;
  }

  for (i = 0; i < mb4_release_list_size (releases); i++) {
    AlbumDetails *album;

    release = mb4_release_list_item (releases, i);
    if (release) {
      char *releaseid = NULL;
      Mb4Release full_release;

      releaseid = NULL;
      GET(releaseid, mb4_release_get_id, release);

      full_release = mb4_query_lookup_release (priv->mb, releaseid);
      g_free (releaseid);
      if (full_release) {
        Mb4MediumList media;
        Mb4Metadata metadata = NULL;
        Mb4ReleaseGroup group;
        unsigned int j;

        group = mb4_release_get_releasegroup (full_release);
        if (group) {
          /* The release-group information we can extract from the
           * lookup_release query doesn't have the url relations for the
           * release-group, so run a separate query to get these urls
           */
          char *releasegroupid = NULL;
          char *params_names[] = { "inc" };
          char *params_values[] = { "artists url-rels" };

          GET (releasegroupid, mb4_releasegroup_get_id, group);
          metadata = mb4_query_query (priv->mb, "release-group", releasegroupid, "",
                                      1, params_names, params_values);
          g_free (releasegroupid);
        }

        if (metadata && mb4_metadata_get_releasegroup (metadata))
          group = mb4_metadata_get_releasegroup (metadata);

        media = mb4_release_media_matching_discid (full_release, discid);
        for (j = 0; j < mb4_medium_list_size (media); j++) {
          Mb4Medium medium;
          medium = mb4_medium_list_item (media, j);
          if (medium) {
            album = make_album_from_release (group, full_release, medium);
            album->metadata_source = SOURCE_MUSICBRAINZ;
            albums = g_list_append (albums, album);
          }
        }
        mb4_metadata_delete (metadata);
        mb4_medium_list_delete (media);
        mb4_release_delete (full_release);
      }
    }
  }
  mb4_release_list_delete (releases);
  return albums;
}

/*
 * GObject methods
 */

static void
metadata_interface_init (gpointer g_iface, gpointer iface_data)
{
  SjMetadataClass *klass = (SjMetadataClass*)g_iface;

  klass->list_albums = mb4_list_albums;
}

static void
sj_metadata_musicbrainz4_init (SjMetadataMusicbrainz4 *self)
{
  GConfClient *gconf_client;
  gchar *server_name;

  SjMetadataMusicbrainz4Private *priv;

  priv = GET_PRIVATE (self);

  gconf_client = gconf_client_get_default ();

  server_name = gconf_client_get_string (gconf_client, GCONF_MUSICBRAINZ_SERVER, NULL);

  if (server_name && (*server_name == '\0')) {
    g_free (server_name);
    server_name = NULL;
  }

  priv->mb = mb4_query_new (SJ_MUSICBRAINZ_USER_AGENT, server_name, 0);
  g_free (server_name);


  /* Set the HTTP proxy */
  if (gconf_client_get_bool (gconf_client, GCONF_PROXY_USE_PROXY, NULL)) {
    char *proxy_host;
    int port;

    proxy_host = gconf_client_get_string (gconf_client, GCONF_PROXY_HOST, NULL);
    mb4_query_set_proxyhost (priv->mb, proxy_host);
    g_free (proxy_host);

    port = gconf_client_get_int (gconf_client, GCONF_PROXY_PORT, NULL);
    mb4_query_set_proxyport (priv->mb, port);

    if (gconf_client_get_bool (gconf_client, GCONF_PROXY_USE_AUTHENTICATION, NULL)) {
      char *username, *password;

      username = gconf_client_get_string (gconf_client, GCONF_PROXY_USERNAME, NULL);
      mb4_query_set_proxyusername (priv->mb, username);
      g_free (username);

      password = gconf_client_get_string (gconf_client, GCONF_PROXY_PASSWORD, NULL);
      mb4_query_set_proxypassword (priv->mb, password);
      g_free (password);
    }
  }

  g_object_unref (gconf_client);
}

static void
sj_metadata_musicbrainz4_get_property (GObject *object, guint property_id,
                                       GValue *value, GParamSpec *pspec)
{
  SjMetadataMusicbrainz4Private *priv = GET_PRIVATE (object);
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
sj_metadata_musicbrainz4_set_property (GObject *object, guint property_id,
                                       const GValue *value, GParamSpec *pspec)
{
  SjMetadataMusicbrainz4Private *priv = GET_PRIVATE (object);
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
    mb4_query_set_proxyhost (priv->mb, priv->http_proxy);
    break;
  case PROP_PROXY_PORT:
    priv->http_proxy_port = g_value_get_int (value);
    mb4_query_set_proxyport (priv->mb, priv->http_proxy_port);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
sj_metadata_musicbrainz4_finalize (GObject *object)
{
  SjMetadataMusicbrainz4Private *priv;

  priv = GET_PRIVATE (object);

  if (priv->mb != NULL) {
    mb4_query_delete (priv->mb);
    priv->mb = NULL;
  }
  if (priv->disc != NULL) {
      discid_free (priv->disc);
      priv->disc = NULL;
  }
  g_free (priv->cdrom);

  G_OBJECT_CLASS (sj_metadata_musicbrainz4_parent_class)->finalize (object);
}

static void
sj_metadata_musicbrainz4_class_init (SjMetadataMusicbrainz4Class *class)
{
  GObjectClass *object_class = (GObjectClass*)class;

  g_type_class_add_private (class, sizeof (SjMetadataMusicbrainz4Private));

  object_class->get_property = sj_metadata_musicbrainz4_get_property;
  object_class->set_property = sj_metadata_musicbrainz4_set_property;
  object_class->finalize = sj_metadata_musicbrainz4_finalize;

  g_object_class_override_property (object_class, PROP_DEVICE, "device");
  g_object_class_override_property (object_class, PROP_PROXY_HOST, "proxy-host");
  g_object_class_override_property (object_class, PROP_PROXY_PORT, "proxy-port");
}


/*
 * Public methods.
 */

GObject *
sj_metadata_musicbrainz4_new (void)
{
  return g_object_new (SJ_TYPE_METADATA_MUSICBRAINZ4, NULL);
}
