/*
 *  RBSongDisplayBox is a hack of GtkHBox
 *  arch-tag: RBSongDisplayBox - display album/artist information (header)
 */ 

/* GTK - The GIMP Toolkit
 * Copyright (C) 1995-1997 Peter Mattis, Spencer Kimball and Josh MacDonald
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

/*
 * Modified by the GTK+ Team and others 1997-2000.  See the AUTHORS
 * file for a list of people on the GTK+ Team.  See the ChangeLog
 * files for a list of changes.  These files are distributed with
 * GTK+ at ftp://ftp.gtk.org/pub/gtk/. 
 */

#ifndef __RB_SONG_DISPLAY_BOX_H
#define __RB_SONG_DISPLAY_BOX_H

#include <gtk/gtklabel.h>
#include <gtk/gtkwidget.h>
#include <gtk/gtkbox.h>

#include <libgnomeui/libgnomeui.h>

#define RB_TYPE_SONG_DISPLAY_BOX	(rb_song_display_box_get_type ())
#define RB_SONG_DISPLAY_BOX(obj)	(GTK_CHECK_CAST ((obj), RB_TYPE_SONG_DISPLAY_BOX, RBSongDisplayBox))

typedef struct RBSongDisplayBoxPrivate	RBSongDisplayBoxPrivate;
typedef struct
{
	GtkBox box;

	RBSongDisplayBoxPrivate *priv;

	GnomeHRef *album;
	GnomeHRef *artist;
} RBSongDisplayBox;

typedef struct
{
	GtkBoxClass parent_class;
} RBSongDisplayBoxClass;

GType	   		rb_song_display_box_get_type (void) G_GNUC_CONST;
GtkWidget * 		rb_song_display_box_new	     (void);

#endif /* __RB_PLAYER_H */
