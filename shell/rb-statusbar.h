/*
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#include <gtk/gtk.h>

#include <sources/rb-display-page.h>
#include <rhythmdb/rhythmdb.h>
#include <shell/rb-track-transfer-queue.h>

#ifndef __RB_STATUSBAR_H
#define __RB_STATUSBAR_H

G_BEGIN_DECLS

#define RB_TYPE_STATUSBAR         (rb_statusbar_get_type ())
#define RB_STATUSBAR(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_STATUSBAR, RBStatusbar))
#define RB_STATUSBAR_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_STATUSBAR, RBStatusbarClass))
#define RB_IS_STATUSBAR(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_STATUSBAR))
#define RB_IS_STATUSBAR_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_STATUSBAR))
#define RB_STATUSBAR_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_STATUSBAR, RBStatusbarClass))

typedef struct _RBStatusbar RBStatusbar;
typedef struct _RBStatusbarClass RBStatusbarClass;

typedef struct RBStatusbarPrivate RBStatusbarPrivate;

struct _RBStatusbar
{
	GtkStatusbar parent;

	RBStatusbarPrivate *priv;
};

struct _RBStatusbarClass
{
	GtkStatusbarClass parent_class;
};

GType			rb_statusbar_get_type	(void);

RBStatusbar *		rb_statusbar_new	(RhythmDB *db);

void			rb_statusbar_set_page	(RBStatusbar *statusbar,
						 RBDisplayPage *page);

G_END_DECLS

#endif /* __RB_STATUSBAR_H */
