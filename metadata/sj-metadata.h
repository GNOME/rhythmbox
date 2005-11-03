/*
 * sj-metadata.h
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

#ifndef SJ_METADATA_H
#define SJ_METADATA_H

#include <glib-object.h>
#include <glib/gerror.h>

G_BEGIN_DECLS

#define SJ_TYPE_METADATA            (sj_metadata_get_type ())
#define SJ_METADATA(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SJ_TYPE_METADATA, SjMetadata))
#define SJ_METADATA_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), SJ_TYPE_METADATA, SjMetadataClass))
#define SJ_IS_METADATA(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SJ_TYPE_METADATA))
#define SJ_IS_METADATA_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), SJ_TYPE_METADATA))
#define SJ_METADATA_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SJ_TYPE_METADATA, SjMetadataClass))

typedef struct _SjMetadata SjMetadata; /* dummy object */
typedef struct _SjMetadataClass SjMetadataClass;

struct _SjMetadataClass
{
  GTypeInterface g_iface;

  /* Signals */
  void         (*metadata) (GList *albums, GError *error);

  /* Virtual Table */
  GError* (*get_new_error) (SjMetadata *metadata);
  void (*set_cdrom) (SjMetadata *metadata, const char* device);
  void (*set_proxy) (SjMetadata *metadata, const char* proxy);
  void (*set_proxy_port) (SjMetadata *metadata, int proxy_port);
  void (*list_albums) (SjMetadata *metadata, GError **error);
};

GType sj_metadata_get_type (void);
GError *sj_metadata_get_new_error (SjMetadata *metadata);
void sj_metadata_set_cdrom (SjMetadata *metadata, const char* device);
void sj_metadata_set_proxy (SjMetadata *metadata, const char* proxy);
void sj_metadata_set_proxy_port (SjMetadata *metadata, const int proxy_port);
void sj_metadata_list_albums (SjMetadata *metadata, GError **error);

G_END_DECLS

#endif /* SJ_METADATA_H */
