/*
 *  arch-tag: Header for main song information database object
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@rhythmbox.org>
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

#ifndef __RB_LIBRARY_H
#define __RB_LIBRARY_H

#include "rhythmdb.h"
#include <libgnomevfs/gnome-vfs-file-info.h>

G_BEGIN_DECLS

#define RB_LIBRARY_XML_VERSION "2.1"

#define RB_TYPE_LIBRARY         (rb_library_get_type ())
#define RB_LIBRARY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY, RBLibrary))
#define RB_LIBRARY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY, RBLibraryClass))
#define RB_IS_LIBRARY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY))
#define RB_IS_LIBRARY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY))
#define RB_LIBRARY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY, RBLibraryClass))

typedef struct RBLibraryPrivate RBLibraryPrivate;

typedef struct
{
	GObject parent;

	RBLibraryPrivate *priv;
} RBLibrary;

typedef struct
{
	GObjectClass parent;

	/* signals */
	void	(*error)	(const char *uri, const char *msg);
	void	(*progress)	(double val);
	void	(*status_changed)();
	void	(*legacy_load_complete)();
} RBLibraryClass;

GType			rb_library_get_type		(void);

RBLibrary *		rb_library_new                  (RhythmDB *db);

void			rb_library_add_uri_async	(RBLibrary *library,
							 const char *uri);
/* These methods are called asynchronously by the library main thread. */
void			rb_library_add_uri		(RBLibrary *library,
							 const char *uri,
							 GError **error);
void			rb_library_update_entry		(RBLibrary *library,
							 RhythmDBEntry *entry, GError **error);

void			rb_library_load_legacy		(RBLibrary *library);
RhythmDBEntry *		rb_library_legacy_id_to_entry	(RBLibrary *library, guint id);

void			rb_library_release_brakes       (RBLibrary *library);
void			rb_library_shutdown		(RBLibrary *library);

GAsyncQueue *		rb_library_get_main_queue       (RBLibrary *library);

GAsyncQueue *		rb_library_get_add_queue	(RBLibrary *library);

char *			rb_library_compute_status_normal(gint count, glong duration,
							 GnomeVFSFileSize size);

G_END_DECLS

#endif /* __RB_LIBRARY_H */
