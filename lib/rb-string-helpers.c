/*
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
 *  $Id$
 */

#include <glib.h>

#include "rb-string-helpers.h"

char *
rb_string_compress (const char *string,
	            int target_length)
{
	int length;
	char *ret;

	length = g_utf8_strlen (string, -1);

	if (length > target_length)
	{
		char *part1 = g_strndup (string, (target_length / 2));
		char *part2 = g_strndup (string + (target_length / 2), (target_length / 2) - 3);
		ret = g_strdup_printf ("%s...%s", part1, part2);
		g_free (part1);
		g_free (part2);
	}
	else
		ret = g_strdup (string);

	return ret;
}
