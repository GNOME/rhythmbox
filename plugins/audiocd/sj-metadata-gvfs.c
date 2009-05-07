/*
 * sj-metadata-gvfs.c
 * Copyright (C) 2005 Ross Burton <ross@burtonini.com>
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
#include <glib-object.h>
#include <glib/gi18n.h>
#include <glib.h>
#include <gio/gio.h>

#include "sj-metadata-gvfs.h"
#include "sj-structures.h"
#include "sj-error.h"

struct SjMetadataGvfsPrivate {
  char *cdrom;
  char *uri;
};

#define GET_PRIVATE(o)  \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SJ_TYPE_METADATA_GVFS, SjMetadataGvfsPrivate))

enum {
  PROP_0,
  PROP_DEVICE,
  PROP_PROXY_HOST,
  PROP_PROXY_PORT,
};

static void metadata_iface_init (gpointer g_iface, gpointer iface_data);

G_DEFINE_TYPE_WITH_CODE (SjMetadataGvfs, sj_metadata_gvfs,
                         G_TYPE_OBJECT,
                         G_IMPLEMENT_INTERFACE (SJ_TYPE_METADATA, metadata_iface_init));


/**
 * Private methods
 */

static char *
device_to_cdda_uri (const char *device)
{
  if (g_str_has_prefix (device, "/dev/") == FALSE)
    return NULL;
  return g_strdup_printf ("cdda://%s", device + strlen ("/dev/"));
}

static GList *
gvfs_list_albums (SjMetadata *metadata, char **url, GError **error)
{
  SjMetadataGvfsPrivate *priv;
  GList *albums = NULL;
  AlbumDetails *album;
  GError *my_error = NULL;
  GFile *file = NULL;
  GFileInfo *info;
  GFileEnumerator *e;
  guint i = 0;

  g_return_val_if_fail (SJ_IS_METADATA_GVFS (metadata), NULL);

  priv = SJ_METADATA_GVFS (metadata)->priv;

  if (priv->uri == NULL) {
    g_set_error (error, SJ_ERROR, SJ_ERROR_INTERNAL_ERROR, _("Cannot access CD"));
    goto bail;
  }

  file = g_file_new_for_uri (priv->uri);

  info = g_file_query_info (file, "xattr::org.gnome.audio",
  			    G_FILE_QUERY_INFO_NONE, NULL, &my_error);
  if (info == NULL)
    goto bail;

  album = g_new0(AlbumDetails, 1);

  /* Get the album metadata */
  if (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.title") != NULL) {
    album->metadata_source = SOURCE_CDTEXT;
    album->title = g_strdup (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.title"));
  } else {
    album->metadata_source = SOURCE_FALLBACK;
    album->title = g_strdup (_("Unknown Title"));
  }
  album->artist = g_strdup (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.artist"));
  if (album->artist == NULL)
    album->artist = g_strdup (_("Unknown Artist"));
  album->genre = g_strdup (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.genre"));

  g_object_unref (info);

  /* Get tracks metadata */
  e = g_file_enumerate_children (file, "xattr::org.gnome.audio",
  				 G_FILE_QUERY_INFO_NONE, NULL, &my_error);
  if (e == NULL)
    goto bail;

  for (info = g_file_enumerator_next_file (e, NULL, NULL) ;
       info != NULL ;
       info = g_file_enumerator_next_file (e, NULL, NULL)) {
    TrackDetails *track;

    track = g_new0 (TrackDetails, 1);
    track->number = i++;
    track->title = g_strdup (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.title"));
    if (track->title == NULL)
      track->title = g_strdup_printf (_("Track %d"), i);
    track->artist = g_strdup (g_file_info_get_attribute_string (info, "xattr::org.gnome.audio.artist"));
    if (track->artist == NULL)
      track->artist = g_strdup (_("Unknown Artist"));
    track->duration = g_file_info_get_attribute_uint64 (info, "xattr::org.gnome.audio.duration");
    album->number++;
    g_object_unref (info);

    album->tracks = g_list_append (album->tracks, track);
  }
  g_object_unref (e);

  albums = g_list_append (albums, album);

  return albums;

bail:

  if (file)
    g_object_unref (file);
  if (my_error) {
    g_set_error (error, SJ_ERROR, SJ_ERROR_INTERNAL_ERROR, _("Cannot access CD: %s"), my_error->message);
    g_error_free (my_error);
  }
  return NULL;
}

/**
 * GObject methods
 */

static void
sj_metadata_gvfs_get_property (GObject *object, guint property_id,
                                      GValue *value, GParamSpec *pspec)
{
  SjMetadataGvfsPrivate *priv = SJ_METADATA_GVFS (object)->priv;
  g_assert (priv);

  switch (property_id) {
  case PROP_DEVICE:
    g_value_set_string (value, priv->cdrom);
    break;
  case PROP_PROXY_HOST:
    /* Do nothing */
    g_value_set_string (value, "");
    break;
  case PROP_PROXY_PORT:
    /* Do nothing */
    g_value_set_int (value, 0);
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
sj_metadata_gvfs_set_property (GObject *object, guint property_id,
                                      const GValue *value, GParamSpec *pspec)
{
  SjMetadataGvfsPrivate *priv = SJ_METADATA_GVFS (object)->priv;
  g_assert (priv);

  switch (property_id) {
  case PROP_DEVICE:
    g_free (priv->cdrom);
    priv->cdrom = g_value_dup_string (value);
    priv->uri = device_to_cdda_uri (priv->cdrom);
    break;
  case PROP_PROXY_HOST:
  case PROP_PROXY_PORT:
    /* Do nothing */
    break;
  default:
    G_OBJECT_WARN_INVALID_PROPERTY_ID (object, property_id, pspec);
  }
}

static void
sj_metadata_gvfs_finalize (GObject *object)
{
  SjMetadataGvfsPrivate *priv = SJ_METADATA_GVFS (object)->priv;
  g_free (priv->cdrom);
  g_free (priv->uri);
}

static void
sj_metadata_gvfs_init (SjMetadataGvfs *gvfs)
{
  gvfs->priv = GET_PRIVATE (gvfs);
}

static void
metadata_iface_init (gpointer g_iface, gpointer iface_data)
{
  SjMetadataClass *klass = (SjMetadataClass*)g_iface;
  
  klass->list_albums = gvfs_list_albums;
}

static void
sj_metadata_gvfs_class_init (SjMetadataGvfsClass *class)
{
  GObjectClass *object_class = (GObjectClass*) class;

  g_type_class_add_private (class, sizeof (SjMetadataGvfsPrivate));

  object_class->get_property = sj_metadata_gvfs_get_property;
  object_class->set_property = sj_metadata_gvfs_set_property;
  object_class->finalize = sj_metadata_gvfs_finalize;

  g_object_class_override_property (object_class, PROP_DEVICE, "device");
  g_object_class_override_property (object_class, PROP_PROXY_HOST, "proxy-host");
  g_object_class_override_property (object_class, PROP_PROXY_PORT, "proxy-port");
}


/*
 * Public methods
 */

GObject *
sj_metadata_gvfs_new (void)
{
  return g_object_new (sj_metadata_gvfs_get_type (), NULL);
}
