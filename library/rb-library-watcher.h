/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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
 *  $Id$
 */

#ifndef __RB_LIBRARY_WATCHER_H
#define __RB_LIBRARY_WATCHER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_WATCHER         (rb_library_watcher_get_type ())
#define RB_LIBRARY_WATCHER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_WATCHER, RBLibraryWatcher))
#define RB_LIBRARY_WATCHER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_WATCHER, RBLibraryWatcherClass))
#define RB_IS_LIBRARY_WATCHER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_WATCHER))
#define RB_IS_LIBRARY_WATCHER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_WATCHER))
#define RB_LIBRARY_WATCHER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_WATCHER, RBLibraryWatcherClass))

typedef struct RBLibraryWatcherPrivate RBLibraryWatcherPrivate;

typedef struct
{
	GObject parent;

	RBLibraryWatcherPrivate *priv;
} RBLibraryWatcher;

typedef struct
{
	GObjectClass parent;

	void (*file_created) (RBLibraryWatcher *library, const char *file);
	void (*file_deleted) (RBLibraryWatcher *library, const char *file);
	void (*file_changed) (RBLibraryWatcher *library, const char *file);
} RBLibraryWatcherClass;

GType             rb_library_watcher_get_type  (void);

RBLibraryWatcher *rb_library_watcher_new       (void);

GList            *rb_library_watcher_get_files (RBLibraryWatcher *watcher);

G_END_DECLS

#endif /* __RB_LIBRARY_WATCHER_H */
