/*
 *  arch-tag: Header for main song information display widget
 *
 *  Copyright (C) 2002 Jorn Baayen <jorn@nl.linux.org>
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

#ifndef __RB_HEADER_H
#define __RB_HEADER_H

#include <gtk/gtkhbox.h>

#include "rhythmdb.h"
#include "rb-player.h"

G_BEGIN_DECLS

#define RB_TYPE_HEADER         (rb_header_get_type ())
#define RB_HEADER(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_HEADER, RBHeader))
#define RB_HEADER_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_HEADER, RBHeaderClass))
#define RB_IS_HEADER(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_HEADER))
#define RB_IS_HEADER_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_HEADER))
#define RB_HEADER_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_HEADER, RBHeaderClass))

typedef struct RBHeaderPrivate RBHeaderPrivate;

typedef struct
{
	GtkHBox parent;

	RBHeaderPrivate *priv;
} RBHeader;

typedef struct
{
	GtkHBoxClass parent;
} RBHeaderClass;

GType		rb_header_get_type		(void);

RBHeader *	rb_header_new			(RBPlayer *player);

void		rb_header_set_playing_entry	(RBHeader *player,
						 RhythmDBEntry *entry);

void		rb_header_set_title		(RBHeader *player,
						 const char *title);

void		rb_header_set_show_artist_album	(RBHeader *player,
						 gboolean show);

void		rb_header_set_urldata		(RBHeader *player,
						 const char *urltext,
						 const char *urllink);

void		rb_header_sync			(RBHeader *player);

gboolean	rb_header_sync_time		(RBHeader *player);

char *		rb_header_get_elapsed_string	(RBHeader *player);

G_END_DECLS

#endif /* __RB_HEADER_H */
