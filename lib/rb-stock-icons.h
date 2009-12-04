/*
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@verbum.org>
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

#ifndef __RB_STOCK_ICONS_H
#define __RB_STOCK_ICONS_H

G_BEGIN_DECLS

extern const char RB_APP_ICON[];
extern const char RB_STOCK_TRAY_ICON_PLAYING[];
extern const char RB_STOCK_TRAY_ICON_NOT_PLAYING[];
extern const char RB_STOCK_SET_STAR[];
extern const char RB_STOCK_UNSET_STAR[];
extern const char RB_STOCK_NO_STAR[];
extern const char RB_STOCK_PODCAST[];
extern const char RB_STOCK_PODCAST_NEW[];
extern const char RB_STOCK_BROWSER[];
extern const char RB_STOCK_PLAYLIST[];
extern const char RB_STOCK_PLAYLIST_NEW[];
extern const char RB_STOCK_AUTO_PLAYLIST[];
extern const char RB_STOCK_AUTO_PLAYLIST_NEW[];
extern const char GNOME_MEDIA_SHUFFLE[];
extern const char GNOME_MEDIA_REPEAT[];
extern const char GNOME_MEDIA_EJECT[];

void	rb_stock_icons_init	(void);

void	rb_stock_icons_shutdown (void);


G_END_DECLS

#endif /* __RB_STOCK_ICONS_H */
