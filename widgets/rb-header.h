/*
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
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

#ifndef __RB_HEADER_H
#define __RB_HEADER_H

#include <gtk/gtk.h>

#include <rhythmdb/rhythmdb.h>
#include <shell/rb-shell-player.h>

G_BEGIN_DECLS

#define RB_TYPE_HEADER         (rb_header_get_type ())
#define RB_HEADER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_HEADER, RBHeader))
#define RB_HEADER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_HEADER, RBHeaderClass))
#define RB_IS_HEADER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_HEADER))
#define RB_IS_HEADER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_HEADER))
#define RB_HEADER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_HEADER, RBHeaderClass))

typedef struct _RBHeader RBHeader;
typedef struct _RBHeaderClass RBHeaderClass;

typedef struct RBHeaderPrivate RBHeaderPrivate;

struct _RBHeader
{
	GtkGrid parent;

	RBHeaderPrivate *priv;
};

struct _RBHeaderClass
{
	GtkGridClass parent;
};

GType		rb_header_get_type		(void);

RBHeader *	rb_header_new			(RBShellPlayer *shell_player,
						 RhythmDB *db);

G_END_DECLS

#endif /* __RB_HEADER_H */
