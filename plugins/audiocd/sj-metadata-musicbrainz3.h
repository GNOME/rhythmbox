/*
 * sj-metadata-musicbrainz3.h
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

#ifndef SJ_METADATA_MUSICBRAINZ3_H
#define SJ_METADATA_MUSICBRAINZ3_H

#include <glib-object.h>
#include "sj-metadata.h"

G_BEGIN_DECLS

#define SJ_TYPE_METADATA_MUSICBRAINZ3           (sj_metadata_musicbrainz3_get_type ())
#define SJ_METADATA_MUSICBRAINZ3(obj)           (G_TYPE_CHECK_INSTANCE_CAST ((obj), SJ_TYPE_METADATA_MUSICBRAINZ3, SjMetadataMusicbrainz3))
#define SJ_METADATA_MUSICBRAINZ3_CLASS(vtable)  (G_TYPE_CHECK_CLASS_CAST ((vtable), SJ_TYPE_METADATA_MUSICBRAINZ3, SjMetadataMusicbrainz3Class))
#define SJ_IS_METADATA_MUSICBRAINZ3(obj)        (G_TYPE_CHECK_INSTANCE_TYPE ((obj), SJ_TYPE_METADATA_MUSICBRAINZ3))
#define SJ_IS_METADATA_MUSICBRAINZ3_CLASS(vtable) (G_TYPE_CHECK_CLASS_TYPE ((vtable), SJ_TYPE_METADATA_MUSICBRAINZ3))
#define SJ_METADATA_MUSICBRAINZ3_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS ((obj), SJ_TYPE_METADATA_MUSICBRAINZ3, SjMetadataMusicbrainz3Class))

typedef struct _SjMetadataMusicbrainz3 SjMetadataMusicbrainz3;
typedef struct _SjMetadataMusicbrainz3Class SjMetadataMusicbrainz3Class;

struct _SjMetadataMusicbrainz3
{
  GObject parent;
};

struct _SjMetadataMusicbrainz3Class
{
  GObjectClass parent;
};

GType sj_metadata_musicbrainz3_get_type (void);

GObject *sj_metadata_musicbrainz3_new (void);

G_END_DECLS

#endif /* SJ_METADATA_MUSICBRAINZ3_H */
