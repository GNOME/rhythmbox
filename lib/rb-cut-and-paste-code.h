/*
 *  arch-tag: Header file for code cut and pasted from elsewhere
 *
 *  Copyright (C) 2002 Jorn Baayen
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

#include <gdk-pixbuf/gdk-pixbuf.h>
#include <time.h>

#ifndef __RB_CUT_AND_PASTE_CODE_H
#define __RB_CUT_AND_PASTE_CODE_H

G_BEGIN_DECLS

char      *eel_strdup_strftime         (const char *format,
			                struct tm *time_pieces);

GdkPixbuf *eel_create_colorized_pixbuf (GdkPixbuf *src,
					int red_value,
					int green_value,
					int blue_value);


G_END_DECLS

#endif /* __RB_CUT_AND_PASTE_CODE_H */
