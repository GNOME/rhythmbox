/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2003,2004 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#include "config.h"

#include <gtk/gtk.h>
#include <glib.h>
#include <stdio.h>
#include <string.h>

#include "rb-file-helpers.h"
#include "rb-stock-icons.h"

const char RB_APP_ICON[] = "org.gnome.Rhythmbox3";
const char RB_STOCK_SET_STAR[] = "rhythmbox-set-star";
const char RB_STOCK_UNSET_STAR[] = "rhythmbox-unset-star";
const char RB_STOCK_NO_STAR[] = "rhythmbox-no-star";
const char RB_STOCK_MISSING_ARTWORK[] = "rhythmbox-missing-artwork";

/**
 * rb_stock_icons_init:
 *
 * Initializes the stock icons, adding the necessary filesystem
 * locations to the GTK icon search path.  Must be called on startup.
 */
void
rb_stock_icons_init (void)
{
	GtkIconTheme *theme = gtk_icon_theme_get_default ();
	char *dot_icon_dir;

	/* add our icon search paths */
	dot_icon_dir = g_build_filename (rb_user_data_dir (), "icons", NULL);
	gtk_icon_theme_append_search_path (theme, dot_icon_dir);
	g_free (dot_icon_dir);

	gtk_icon_theme_append_search_path (theme, SHARE_DIR G_DIR_SEPARATOR_S "icons");

	/* add resource icons */
	gtk_icon_theme_add_resource_path (theme, "/org/gnome/Rhythmbox/icons/hicolor");
}

/**
 * rb_stock_icons_shutdown:
 *
 * If anything was necessary to clean up the stock icons, this function
 * would do it.  Doesn't do anything, but should be called on shutdown
 * anyway.
 */
void
rb_stock_icons_shutdown (void)
{
	/* do nothing */
}
