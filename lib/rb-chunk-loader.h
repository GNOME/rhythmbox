/*
 *  Copyright (C) 2012 Jonathan Matthew  <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_CHUNK_LOADER_H
#define __RB_CHUNK_LOADER_H

#include <gio/gio.h>

G_BEGIN_DECLS

#define RB_TYPE_CHUNK_LOADER           (rb_chunk_loader_get_type ())
#define RB_CHUNK_LOADER(o)             (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_CHUNK_LOADER, RBChunkLoader))
#define RB_CHUNK_LOADER_CLASS(k)       (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_CHUNK_LOADER, RBChunkLoaderClass))
#define RB_IS_CHUNK_LOADER(o)          (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_CHUNK_LOADER))
#define RB_IS_CHUNK_LOADER_CLASS(k)    (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_CHUNK_LOADER))
#define RB_CHUNK_LOADER_GET_CLASS(o)   (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_CHUNK_LOADER, RBChunkLoaderClass))

typedef struct _RBChunkLoader RBChunkLoader;
typedef struct _RBChunkLoaderClass RBChunkLoaderClass;
typedef struct _RBChunkLoaderPrivate RBChunkLoaderPrivate;

typedef void (*RBChunkLoaderCallback) (RBChunkLoader *loader, GBytes *data, goffset total, gpointer user_data);

struct _RBChunkLoader
{
	GObject parent;
	RBChunkLoaderPrivate *priv;
};

struct _RBChunkLoaderClass
{
	GObjectClass parent_class;
};

GType			rb_chunk_loader_get_type	(void);

RBChunkLoader *		rb_chunk_loader_new		(void);

void			rb_chunk_loader_set_callback	(RBChunkLoader *loader,
							 RBChunkLoaderCallback callback,
							 gpointer user_data,
							 GDestroyNotify destroy_data);

GError *		rb_chunk_loader_get_error	(RBChunkLoader *loader);

void			rb_chunk_loader_start		(RBChunkLoader *loader,
							 const char *uri,
							 gssize chunk_size);

void			rb_chunk_loader_cancel		(RBChunkLoader *loader);

G_END_DECLS

#endif /* __RB_CHUNK_LOADER_H */

