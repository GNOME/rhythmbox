/*  RhythmBox
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *                     Marco Pesenti Gritti <marco@it.gnome.org>
 *                     Bastien Nocera <hadess@hadess.net>
 *                     Seth Nickell <snickell@stanford.edu>
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

#ifndef __FILE_WATCHER_H
#define __FILE_WATCHER_H

#include <glib-object.h>

G_BEGIN_DECLS

#define TYPE_FILE_WATCHER            (file_watcher_get_type ())
#define FILE_WATCHER(obj)	     (G_TYPE_CHECK_INSTANCE_CAST ((obj), TYPE_FILE_WATCHER, FileWatcher))
#define FILE_WATCHER_CLASS(klass)    (G_TYPE_CHECK_CLASS_CAST ((klass), TYPE_FILE_WATCHER, FileWatcherClass))
#define IS_FILE_WATCHER(obj)	     (G_TYPE_CHECK_INSTANCE_TYPE ((obj), TYPE_FILE_WATCHER))
#define IS_FILE_WATCHER_CLASS(klass) (G_TYPE_CHECK_CLASS_TYPE ((klass), TYPE_FILE_WATCHER))
#define FILE_WATCHER_GET_CLASS(obj)  (G_TYPE_INSTANCE_GET_CLASS ((obj), TYPE_FILE_WATCHER, FileWatcherClass))

typedef struct _FileWatcher        	FileWatcher;
typedef struct _FileWatcherClass 	FileWatcherClass;

typedef struct _FileWatcherPrivate 	FileWatcherPrivate;

struct _FileWatcher
{
	GObject base;
	
	FileWatcherPrivate *priv;
};

struct _FileWatcherClass
{
	GObjectClass parent_class;

	/* Signals */
	void (*file_created) (FileWatcher *w, const gchar *fn);
	void (*file_deleted) (FileWatcher *w, const gchar *fn);
	void (*file_changed) (FileWatcher *w, const gchar *fn);
};

GType        file_watcher_get_type       (void) G_GNUC_CONST;

FileWatcher *file_watcher_new            (void);

void         file_watcher_release_brakes (FileWatcher *w);

/* prefs */
#define CONF_FILE_WATCHER_URIS   "/apps/rhythmbox/FileWatcher/uris"

G_END_DECLS

#endif /* __FILE_WATCHER_H */
