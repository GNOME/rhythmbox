/*
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Library General Public License as
 *  published by the Free Software Foundation; either version 2, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <time.h>

#ifndef __RB_CUT_AND_PASTE_CODE_H
#define __RB_CUT_AND_PASTE_CODE_H

G_BEGIN_DECLS

GdkPixbuf *eel_create_colorized_pixbuf (GdkPixbuf *src,
					int red_value,
					int green_value,
					int blue_value);

char *     rb_utf_friendly_time        (time_t date);

char *     rb_make_valid_utf8 (const char *name, char substitute);

G_END_DECLS

#endif /* __RB_CUT_AND_PASTE_CODE_H */
