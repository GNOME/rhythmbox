/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
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

#ifndef __RB_LIBRARY_SOURCE_H
#define __RB_LIBRARY_SOURCE_H

#include <gtk/gtk.h>

#include "rb-shell.h"
#include "rb-browser-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_SOURCE         (rb_library_source_get_type ())
#define RB_LIBRARY_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySource))
#define RB_LIBRARY_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourceClass))
#define RB_IS_LIBRARY_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_SOURCE))
#define RB_IS_LIBRARY_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_SOURCE))
#define RB_LIBRARY_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_SOURCE, RBLibrarySourceClass))

typedef struct _RBLibrarySource RBLibrarySource;
typedef struct _RBLibrarySourceClass RBLibrarySourceClass;

typedef struct RBLibrarySourcePrivate RBLibrarySourcePrivate;

struct _RBLibrarySource
{
	RBBrowserSource parent;

	RBLibrarySourcePrivate *priv;
};

struct _RBLibrarySourceClass
{
	RBBrowserSourceClass parent;
};

GType		rb_library_source_get_type		(void);

RBSource *      rb_library_source_new			(RBShell *shell);

G_END_DECLS

#endif /* __RB_LIBRARY_SOURCE_H */
