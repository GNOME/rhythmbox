/*
 *  arch-tag: Header for various string-related utility functions
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

#ifndef __RB_STRING_HELPERS_H
#define __RB_STRING_HELPERS_H

G_BEGIN_DECLS

void	rb_string_helpers_init		(void);
void	rb_string_helpers_shutdown	(void);

char *	rb_unicodify			(const char *str);

int	rb_utf8_strncasecmp		(gconstpointer a, gconstpointer b);

char *	rb_get_sort_key			(const char *string);

G_END_DECLS

#endif /* __RB_STRING_HELPERS_H */
