/*
 *  arch-tag: Implementation of Rhythmbox icon loading
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-stock-icons.h"

void
rb_stock_icons_init (void)
{
	GtkIconFactory *factory;
	int i;

	static const char *items[] =
	{
		RB_STOCK_PLAY,
		RB_STOCK_PAUSE,
		RB_STOCK_STOP,
		RB_STOCK_PREVIOUS,
		RB_STOCK_NEXT,
		RB_STOCK_REWIND,
		RB_STOCK_SHUFFLE,
		RB_STOCK_REPEAT,
		RB_STOCK_VISUALS,
		RB_STOCK_PLAYLIST,
		RB_STOCK_PLAYLIST_MENU,
		RB_STOCK_LIBRARY,
		RB_STOCK_IRADIO,
		RB_STOCK_PLAYING,
		RB_STOCK_PAUSED,
		RB_STOCK_ALBUM,
		RB_STOCK_VOLUME_ZERO,
		RB_STOCK_VOLUME_MIN,
		RB_STOCK_VOLUME_MEDIUM,
		RB_STOCK_VOLUME_MAX,
		RB_STOCK_VOLUME_MUTE,
		RB_STOCK_TRAY_ICON,
		RB_STOCK_SET_STAR,
		RB_STOCK_AUTOMATIC_PLAYLIST,
		RB_STOCK_UNSET_STAR,
		RB_STOCK_NO_STAR,
		RB_STOCK_DND_ICON,
                RB_STOCK_AUDIOCD
	};

	factory = gtk_icon_factory_new ();
	gtk_icon_factory_add_default (factory);

	for (i = 0; i < (int) G_N_ELEMENTS (items); i++) {
		GtkIconSet *icon_set;
		GdkPixbuf *pixbuf;
		char *fn;

		fn = g_strconcat (items[i], ".png", NULL);
		pixbuf = gdk_pixbuf_new_from_file (rb_file (fn), NULL);
		g_free (fn);

		icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
		gtk_icon_factory_add (factory, items[i], icon_set);
		gtk_icon_set_unref (icon_set);
		
		g_object_unref (G_OBJECT (pixbuf));
	}

	g_object_unref (G_OBJECT (factory));
}
