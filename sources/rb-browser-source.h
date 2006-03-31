/*
 *  arch-tag: Header for browser-using source object
 *
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
 */

#ifndef __RB_BROWSER_SOURCE_H
#define __RB_BROWSER_SOURCE_H

#include <gtk/gtkwindow.h>
#include <gtk/gtkactiongroup.h>

#include "rb-shell.h"
#include "rb-source.h"
#include "rhythmdb.h"

G_BEGIN_DECLS

#define RB_TYPE_BROWSER_SOURCE         (rb_browser_source_get_type ())
#define RB_BROWSER_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_BROWSER_SOURCE, RBBrowserSource))
#define RB_BROWSER_SOURCE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_BROWSER_SOURCE, RBBrowserSourceClass))
#define RB_IS_BROWSER_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_BROWSER_SOURCE))
#define RB_IS_BROWSER_SOURCE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_BROWSER_SOURCE))
#define RB_BROWSER_SOURCE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_BROWSER_SOURCE, RBBrowserSourceClass))


typedef struct RBBrowserSourcePrivate RBBrowserSourcePrivate;

typedef struct
{
	RBSource parent;

	RBBrowserSourcePrivate *priv;
} RBBrowserSource;

typedef struct
{
	RBSourceClass parent;

	const char *	(*impl_get_paned_key)		(RBBrowserSource *source);
	gboolean	(*impl_has_first_added_column)	(RBBrowserSource *source);
	gboolean	(*impl_has_drop_support)	(RBBrowserSource *source);
} RBBrowserSourceClass;

typedef gboolean	(*RBBrowserSourceFeatureFunc) (RBBrowserSource *source);
typedef const char*	(*RBBrowserSourceStringFunc) (RBBrowserSource *source);

GType		rb_browser_source_get_type		(void);

void            rb_browser_source_class_add_actions	(RBShell *shell, 
							GtkActionGroup *uimgr);

const char *	rb_browser_source_get_paned_key		(RBBrowserSource *source);
gboolean	rb_browser_source_has_first_added_column (RBBrowserSource *source);
gboolean	rb_browser_source_has_drop_support	(RBBrowserSource *source);


G_END_DECLS

#endif /* __RB_BROWSER_SOURCE_H */
