/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-stock-icons.h"

static GtkIconFactory *factory = NULL;

const char RB_APP_ICON[] = "rhythmbox";
const char RB_STOCK_TRAY_ICON[] = "rhythmbox-tray-icon";
const char RB_STOCK_SET_STAR[] = "rhythmbox-set-star";
const char RB_STOCK_UNSET_STAR[] = "rhythmbox-unset-star";
const char RB_STOCK_NO_STAR[] = "rhythmbox-no-star";
const char RB_STOCK_PODCAST[] = "rhythmbox-podcast";
const char RB_STOCK_BROWSER[] = "stock_music-library";
const char GNOME_MEDIA_SHUFFLE[] = "stock_shuffle";
const char GNOME_MEDIA_REPEAT[] = "stock_repeat";
const char GNOME_MEDIA_PLAYLIST[] = "stock_playlist";
const char GNOME_MEDIA_AUTO_PLAYLIST[] = "stock_smart-playlist";
const char GNOME_MEDIA_EJECT[] = "media-eject";

void
rb_stock_icons_init (void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default ();
	int i;
	int icon_size;

	static const char* items[] =
	{
		/* Rhythmbox custom icons */
		RB_STOCK_TRAY_ICON,
		RB_STOCK_SET_STAR,
		RB_STOCK_UNSET_STAR,
		RB_STOCK_PODCAST,
		RB_STOCK_NO_STAR,
		
		/* gnome-icon-theme icons */
		GNOME_MEDIA_SHUFFLE,
		GNOME_MEDIA_REPEAT,
		GNOME_MEDIA_PLAYLIST,
		GNOME_MEDIA_AUTO_PLAYLIST,
		GNOME_MEDIA_EJECT,
		RB_STOCK_BROWSER
	};

	g_return_if_fail (factory == NULL);

	factory = gtk_icon_factory_new ();
	gtk_icon_factory_add_default (factory);

	/* we should really add all the sizes */
	gtk_icon_size_lookup (GTK_ICON_SIZE_LARGE_TOOLBAR, &icon_size, NULL);

	for (i = 0; i < (int) G_N_ELEMENTS (items); i++) {
		GtkIconSet *icon_set;
		GdkPixbuf *pixbuf;

		pixbuf = gtk_icon_theme_load_icon (theme,
						   items[i],
						   icon_size,
						   0,
						   NULL);
		if (pixbuf == NULL) {
			char *fn;
			const char *path;

			fn = g_strconcat (items[i], ".png", NULL);
			path = rb_file (fn);
			if (path != NULL) {
				pixbuf = gdk_pixbuf_new_from_file (path, NULL);
			}
			g_free (fn);
		}

		if (pixbuf) {
			icon_set = gtk_icon_set_new_from_pixbuf (pixbuf);
			gtk_icon_factory_add (factory, items[i], icon_set);
			gtk_icon_set_unref (icon_set);
			
			g_object_unref (G_OBJECT (pixbuf));
		} else {
			g_warning ("Unable to load icon %s", items[i]);
		}
	}

	/* register the app icon as a builtin if the theme can't find it */
	if (!gtk_icon_theme_has_icon (theme, RB_APP_ICON)) {
		int i;
		GdkPixbuf *pixbuf;
		char *path;
		static char *search_paths[] = {
#ifdef SHARE_UNINSTALLED_DIR
			SHARE_UNINSTALLED_DIR "/",
#endif
			DATADIR "/icons/hicolor/48x48/apps/",
		};

		for (i = 0; i < (int) G_N_ELEMENTS (search_paths); i++) {
			path = g_strconcat (search_paths[i], RB_APP_ICON, ".png", NULL);
			if (g_file_test (path, G_FILE_TEST_EXISTS) == TRUE)
				break;
			g_free (path);
			path = NULL;
		}

		if (path) {
			pixbuf = gdk_pixbuf_new_from_file (path, NULL);
			if (pixbuf) {
				gtk_icon_theme_add_builtin_icon (RB_APP_ICON, icon_size, pixbuf);
			}
		}
		g_free (path);
	}
}


void
rb_stock_icons_shutdown (void)
{
	g_return_if_fail (factory != NULL);

	gtk_icon_factory_remove_default (factory);

	g_object_unref (G_OBJECT (factory));
}
