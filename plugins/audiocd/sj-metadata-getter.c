/*
 * sj-metadata.c
 * Copyright (C) 2003 Ross Burton <ross@burtonini.com>
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

#include "config.h"

#include <glib-object.h>
#include <glib/gi18n.h>
#include "sj-structures.h"
#include "sj-metadata-getter.h"
#include "sj-metadata-marshal.h"
#include "sj-metadata.h"
#ifdef HAVE_MUSICBRAINZ3
#include "sj-metadata-musicbrainz3.h"
#endif /* HAVE_MUSICBRAINZ3 */
#ifdef HAVE_MUSICBRAINZ
#include "sj-metadata-musicbrainz.h"
#endif /* HAVE_MUSICBRAINZ */
#ifdef HAVE_LIBCDIO
#include "sj-metadata-cdtext.h"
#endif /* HAVE_LIBCDIO */
#include "sj-metadata-gvfs.h"
#include "sj-error.h"

enum {
  METADATA,
  LAST_SIGNAL
};

struct SjMetadataGetterPrivate {
  char *url;
  char *cdrom;
  char *proxy_host;
  int proxy_port;
};

struct SjMetadataGetterSignal {
  SjMetadataGetter *mdg;
  SjMetadata *metadata;
  GList *albums;
  GError *error;
};

typedef struct SjMetadataGetterPrivate SjMetadataGetterPrivate;
typedef struct SjMetadataGetterSignal SjMetadataGetterSignal;

static int signals[LAST_SIGNAL] = { 0 };

static void sj_metadata_getter_finalize (GObject *object);
static void sj_metadata_getter_init (SjMetadataGetter *mdg);

G_DEFINE_TYPE(SjMetadataGetter, sj_metadata_getter, G_TYPE_OBJECT);

#define GETTER_PRIVATE(o)                                            \
  (G_TYPE_INSTANCE_GET_PRIVATE ((o), SJ_TYPE_METADATA_GETTER, SjMetadataGetterPrivate))

static void
sj_metadata_getter_class_init (SjMetadataGetterClass *klass)
{
  GObjectClass *object_class;
  object_class = (GObjectClass *)klass;

  g_type_class_add_private (klass, sizeof (SjMetadataGetterPrivate));

  object_class->finalize = sj_metadata_getter_finalize;

  /* Properties */
  signals[METADATA] = g_signal_new ("metadata",
				    G_TYPE_FROM_CLASS (object_class),
				    G_SIGNAL_RUN_LAST,
				    G_STRUCT_OFFSET (SjMetadataGetterClass, metadata),
				    NULL, NULL,
				    metadata_marshal_VOID__POINTER_POINTER,
				    G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
}

static void
sj_metadata_getter_finalize (GObject *object)
{
  SjMetadataGetterPrivate *priv = GETTER_PRIVATE (object);
  
  g_free (priv->url);
  g_free (priv->cdrom);
  g_free (priv->proxy_host);

  G_OBJECT_CLASS (sj_metadata_getter_parent_class)->finalize (object);
}

static void
sj_metadata_getter_init (SjMetadataGetter *mdg)
{
}

SjMetadataGetter *
sj_metadata_getter_new (void)
{
  return SJ_METADATA_GETTER (g_object_new (SJ_TYPE_METADATA_GETTER, NULL));
}

void
sj_metadata_getter_set_cdrom (SjMetadataGetter *mdg, const char* device)
{
  SjMetadataGetterPrivate *priv;

  priv = GETTER_PRIVATE (mdg);

  g_free (priv->cdrom);

#if defined (sun) && defined (__SVR4)
  if (g_str_has_prefix (device, "/dev/dsk/")) {
    priv->cdrom = g_strdup_printf ("/dev/rdsk/%s", device + strlen ("/dev/dsk/"));
    return;
  }
#endif
  priv->cdrom = g_strdup (device);
}

void
sj_metadata_getter_set_proxy (SjMetadataGetter *mdg, const char* proxy)
{
  SjMetadataGetterPrivate *priv;

  priv = GETTER_PRIVATE (mdg);

  if (priv->proxy_host)
    g_free (priv->proxy_host);
  priv->proxy_host = g_strdup (proxy);
}

void
sj_metadata_getter_set_proxy_port (SjMetadataGetter *mdg, const int proxy_port)
{
  SjMetadataGetterPrivate *priv;

  priv = GETTER_PRIVATE (mdg);

  priv->proxy_port = proxy_port;
}

static gboolean
fire_signal_idle (SjMetadataGetterSignal *signal)
{
  /* The callback is the sucker, and now owns the albums list */
  g_signal_emit_by_name (G_OBJECT (signal->mdg), "metadata",
  			 signal->albums, signal->error);

  if (signal->metadata)
    g_object_unref (signal->metadata);
  if (signal->error != NULL)
    g_error_free (signal->error);
  g_free (signal);

  return FALSE;
}

static gpointer
lookup_cd (SjMetadataGetter *mdg)
{
  SjMetadata *metadata;
  guint i;
  SjMetadataGetterPrivate *priv;
  GError *error = NULL;
  gboolean found = FALSE;
  GType types[] = {
#ifdef HAVE_MUSICBRAINZ3
    SJ_TYPE_METADATA_MUSICBRAINZ3,
#endif /* HAVE_MUSICBRAINZ3 */
#ifdef HAVE_MUSICBRAINZ
    SJ_TYPE_METADATA_MUSICBRAINZ,
#endif /* HAVE_MUSICBRAINZ */
#ifdef HAVE_LIBCDIO
    SJ_TYPE_METADATA_CDTEXT,
#endif /* HAVE_LIBCDIO */
    SJ_TYPE_METADATA_GVFS
  };

  priv = GETTER_PRIVATE (mdg);

  g_free (priv->url);
  priv->url = NULL;

  for (i = 0; i < G_N_ELEMENTS (types); i++) {
    GList *albums;

    metadata = g_object_new (types[i],
    			     "device", priv->cdrom,
    			     "proxy-host", priv->proxy_host,
    			     "proxy-port", priv->proxy_port,
    			     NULL);
    if (priv->url == NULL)
      albums = sj_metadata_list_albums (metadata, &priv->url, &error);
    else
      albums = sj_metadata_list_albums (metadata, NULL, &error);

    if (albums != NULL) {
      SjMetadataGetterSignal *signal;

      signal = g_new0 (SjMetadataGetterSignal, 1);
      signal->albums = albums;
      signal->mdg = mdg;
      signal->metadata = metadata;
      g_idle_add ((GSourceFunc)fire_signal_idle, signal);
      break;
    }

    g_object_unref (metadata);

    if (error != NULL) {
      SjMetadataGetterSignal *signal;

      g_assert (found == FALSE);

      signal = g_new0 (SjMetadataGetterSignal, 1);
      signal->error = error;
      signal->mdg = mdg;
      g_idle_add ((GSourceFunc)fire_signal_idle, signal);
      break;
    }
  }

  return NULL;
}

gboolean
sj_metadata_getter_list_albums (SjMetadataGetter *mdg, GError **error)
{
  GThread *thread;

  thread = g_thread_create ((GThreadFunc)lookup_cd, mdg, TRUE, error);
  if (thread == NULL) {
    g_set_error (error,
                 SJ_ERROR, SJ_ERROR_INTERNAL_ERROR,
                 _("Could not create CD lookup thread"));
    return FALSE;
  }

  return TRUE;
}

char *
sj_metadata_getter_get_submit_url (SjMetadataGetter *mdg)
{
  SjMetadataGetterPrivate *priv;

  priv = GETTER_PRIVATE (mdg);

  if (priv->url)
    return g_strdup (priv->url);
  return NULL;
}

