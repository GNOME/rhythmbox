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

#include <glib-object.h>
#include "sj-metadata.h"
#include "sj-metadata-marshal.h"

enum {
  METADATA,
  LAST_SIGNAL
};

static int signals[LAST_SIGNAL] = { 0 };

static void
sj_metadata_base_init (gpointer g_class)
{
  static gboolean initialized = FALSE;
  if (!initialized) {
    signals[METADATA] = g_signal_new ("metadata",
                                      G_TYPE_FROM_CLASS (g_class),
                                      G_SIGNAL_RUN_LAST,
                                      G_STRUCT_OFFSET (SjMetadataClass, metadata),
                                      NULL, NULL,
                                      metadata_marshal_VOID__POINTER_POINTER,
                                      G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
    initialized = TRUE;
  }
}

GType
sj_metadata_get_type (void)
{
  static GType type = 0;
  if (type == 0) {
    static const GTypeInfo info = {
      sizeof (SjMetadataClass), /* class_size */
      sj_metadata_base_init,   /* base_init */
      NULL,           /* base_finalize */
      NULL,
      NULL,           /* class_finalize */
      NULL,           /* class_data */
      0,
      0,              /* n_preallocs */
      NULL,
      NULL
    };
    
    type = g_type_register_static (G_TYPE_INTERFACE, "SjMetadata", &info, 0);
    g_type_interface_add_prerequisite (type, G_TYPE_OBJECT);
  }
  return type;
}

GError *
sj_metadata_get_new_error (SjMetadata *metadata)
{
  return SJ_METADATA_GET_CLASS (metadata)->get_new_error (metadata);
}

void
sj_metadata_set_cdrom (SjMetadata *metadata, const char* device)
{
  SJ_METADATA_GET_CLASS (metadata)->set_cdrom (metadata, device);
}

void
sj_metadata_set_proxy (SjMetadata *metadata, const char* proxy)
{
  SJ_METADATA_GET_CLASS (metadata)->set_proxy (metadata, proxy);
}

void
sj_metadata_set_proxy_port (SjMetadata *metadata, const int proxy_port)
{
  SJ_METADATA_GET_CLASS (metadata)->set_proxy_port (metadata, proxy_port);
}

void
sj_metadata_list_albums (SjMetadata *metadata, GError **error)
{
  SJ_METADATA_GET_CLASS (metadata)->list_albums (metadata, error);
}

char *
sj_metadata_get_submit_url (SjMetadata *metadata)
{
  if (SJ_METADATA_GET_CLASS (metadata)->get_submit_url)
    return SJ_METADATA_GET_CLASS (metadata)->get_submit_url (metadata);
  else
    return NULL;
}
