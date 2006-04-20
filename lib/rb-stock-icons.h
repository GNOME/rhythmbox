/*
 *  arch-tag: Header with definitions for Rhythmbox icon loading
 *
 *  Copyright (C) 2002 Jorn Baayen
 *  Copyright (C) 2004 Colin Walters <walters@verbum.org>
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

#ifndef __RB_STOCK_ICONS_H
#define __RB_STOCK_ICONS_H

G_BEGIN_DECLS

extern const char RB_STOCK_TRAY_ICON[];
extern const char RB_STOCK_SET_STAR[];
extern const char RB_STOCK_UNSET_STAR[];
extern const char RB_STOCK_NO_STAR[];
extern const char RB_STOCK_PODCAST[];
extern const char RB_STOCK_BROWSER[];
extern const char GNOME_MEDIA_SHUFFLE[];
extern const char GNOME_MEDIA_REPEAT[];
extern const char GNOME_MEDIA_PLAYLIST[];
extern const char GNOME_MEDIA_AUTO_PLAYLIST[];
extern const char GNOME_MEDIA_EJECT[];

void	rb_stock_icons_init	(void);

void	rb_stock_icons_shutdown (void);


G_END_DECLS

#endif /* __RB_STOCK_ICONS_H */
