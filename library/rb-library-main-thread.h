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

#ifndef __RB_LIBRARY_MAIN_THREAD_H
#define __RB_LIBRARY_MAIN_THREAD_H

#include <glib-object.h>

#include "rb-library.h"

G_BEGIN_DECLS

#define RB_TYPE_LIBRARY_MAIN_THREAD         (rb_library_main_thread_get_type ())
#define RB_LIBRARY_MAIN_THREAD(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_LIBRARY_MAIN_THREAD, RBLibraryMainThread))
#define RB_LIBRARY_MAIN_THREAD_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_LIBRARY_MAIN_THREAD, RBLibraryMainThreadClass))
#define RB_IS_LIBRARY_MAIN_THREAD(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_LIBRARY_MAIN_THREAD))
#define RB_IS_LIBRARY_MAIN_THREAD_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_LIBRARY_MAIN_THREAD))
#define RB_LIBRARY_MAIN_THREAD_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_LIBRARY_MAIN_THREAD, RBLibraryMainThreadClass))

typedef struct RBLibraryMainThreadPrivate RBLibraryMainThreadPrivate;

typedef struct
{
	GObject parent;

	RBLibraryMainThreadPrivate *priv;
} RBLibraryMainThread;

typedef struct
{
	GObjectClass parent;
} RBLibraryMainThreadClass;

GType                rb_library_main_thread_get_type (void);

RBLibraryMainThread *rb_library_main_thread_new      (RBLibrary *library);

G_END_DECLS

#endif /* __RB_LIBRARY_MAIN_THREAD_H */
