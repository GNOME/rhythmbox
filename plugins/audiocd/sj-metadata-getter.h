/*
 * sj-metadata-getter.h
 * Copyright (C) 2008 Bastien Nocera
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

#ifndef SJ_METADATA_GETTER_H
#define SJ_METADATA_GETTER_H

#include <glib-object.h>
#include <glib.h>

G_BEGIN_DECLS

#define SJ_TYPE_METADATA_GETTER            (sj_metadata_getter_get_type ())
#define SJ_METADATA_GETTER(obj)            (G_TYPE_CHECK_INSTANCE_CAST ((obj), SJ_TYPE_METADATA_GETTER, SjMetadataGetter))
#define SJ_METADATA_GETTER_CLASS(vtable)    (G_TYPE_CHECK_CLASS_CAST ((vtable), SJ_TYPE_METADATA_GETTER, SjMetadataGetterClass))
#define SJ_IS_METADATA_GETTER(obj)         (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SJ_TYPE_METADATA_GETTER))
#define SJ_IS_METADATA_GETTER_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), SJ_TYPE_METADATA_GETTER))
#define SJ_METADATA_GETTER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_INTERFACE ((obj), SJ_TYPE_METADATA_GETTER, SjMetadataGetterClass))

struct _SjMetadataGetter
{
  GObject parent;
};

typedef struct _SjMetadataGetter SjMetadataGetter;
typedef struct _SjMetadataGetterClass SjMetadataGetterClass;

struct _SjMetadataGetterClass
{
  GObjectClass parent;

  /* Signals */
  void         (*metadata) (SjMetadataGetter *mdg, GList *albums, GError *error);
};

GType sj_metadata_getter_get_type (void);
SjMetadataGetter *sj_metadata_getter_new (void);
void sj_metadata_getter_set_cdrom (SjMetadataGetter *mdg, const char* device);
void sj_metadata_getter_set_proxy (SjMetadataGetter *mdg, const char* proxy);
void sj_metadata_getter_set_proxy_port (SjMetadataGetter *mdg, const int proxy_port);
gboolean sj_metadata_getter_list_albums (SjMetadataGetter *mdg, GError **error);
char *sj_metadata_getter_get_submit_url (SjMetadataGetter *mdg);

G_END_DECLS

#endif /* SJ_METADATA_GETTER_H */
