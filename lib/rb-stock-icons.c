/*
 *  arch-tag: Implementation of Rhythmbox icon loading
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
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

static GtkIconFactory *factory = NULL;

const char RB_STOCK_TRAY_ICON[] = "rhythmbox-tray-icon";
const char RB_STOCK_SET_STAR[] = "rhythmbox-set-star";
const char RB_STOCK_UNSET_STAR[] = "rhythmbox-unset-star";
const char RB_STOCK_NO_STAR[] = "rhythmbox-no-star";
const char RB_STOCK_PODCAST[] = "rhythmbox-podcast";
const char GNOME_MEDIA_SHUFFLE[] = "stock_shuffle";
const char GNOME_MEDIA_REPEAT[] = "stock_repeat";
const char GNOME_MEDIA_PLAYLIST[] = "stock_playlist";
const char GNOME_MEDIA_AUTO_PLAYLIST[] = "stock_smart-playlist";
const char GNOME_MEDIA_EJECT[] = "player_eject";

typedef struct {
	const char *name;
	gboolean custom;
} RBStockIcon;

void
rb_stock_icons_init (void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default ();
	int i;

	static const RBStockIcon items[] =
	{
		/* Rhythmbox custom icons */
		{RB_STOCK_TRAY_ICON, TRUE},
		{RB_STOCK_SET_STAR, TRUE},
		{RB_STOCK_UNSET_STAR, TRUE},
		{RB_STOCK_PODCAST, TRUE},
		{RB_STOCK_NO_STAR, TRUE},
		
		/* gnome-icon-theme icons */
		{GNOME_MEDIA_SHUFFLE, FALSE},
		{GNOME_MEDIA_REPEAT, FALSE},
		{GNOME_MEDIA_PLAYLIST, FALSE},
		{GNOME_MEDIA_AUTO_PLAYLIST, FALSE},
		{GNOME_MEDIA_EJECT, FALSE}
	};

	g_return_if_fail (factory == NULL);

	factory = gtk_icon_factory_new ();
	gtk_icon_factory_add_default (factory);

	for (i = 0; i < (int) G_N_ELEMENTS (items); i++) {
		GtkIconSet *icon_set;
		GdkPixbuf *pixbuf;

		if (items[i].custom) {
			char *fn = g_strconcat (items[i].name, ".png", NULL);
			pixbuf = gdk_pixbuf_new_from_file (rb_file (fn), NULL);
			g_free (fn);
		} else {
			/* we should really add all the sizes */
			gint size;
			gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &size, NULL);
			pixbuf = gtk_icon_theme_load_icon (theme,
							   items[i].name,
							   size,
							   0,
							   NULL);
		}

		if (pixbuf) {
			icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
			gtk_icon_factory_add (factory, items[i].name, icon_set);
			gtk_icon_set_unref (icon_set);
			
			g_object_unref (G_OBJECT (pixbuf));
		} else {
			g_warning ("Unable to load icon %s", items[i].name);
		}
	}
}


void
rb_stock_icons_shutdown (void)
{
	g_return_if_fail (factory != NULL);

	gtk_icon_factory_remove_default (factory);

	g_object_unref (G_OBJECT (factory));
}
