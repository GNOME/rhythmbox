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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <glib.h>
#include <string.h>

#include "rb-string-helpers.h"

char *
rb_prefix_to_suffix (const char *string)
{
	/* comma separated list of prefixes that are to
	 * be appended as suffix */
	static const char *prefix_to_suffix = N_("THE ,DJ ");
	char **items;
	char *foldedname = g_utf8_casefold (string, -1);
	int i;
	char *str = NULL;

	items = g_strsplit (_(prefix_to_suffix), ",", 0);
	for (i = 0; items[i] != NULL; i++)
	{
		char *foldedprefix = g_utf8_casefold (items[i], -1);
		
		if (strncmp (foldedname, foldedprefix, strlen (foldedprefix)) == 0)
		{
			char *tmp = g_strndup (string, strlen (items[i]));
			tmp = g_strchomp (tmp);
			str = g_strdup_printf (_("%s, %s"), string + strlen (items[i]), tmp);
			g_free (tmp);
			g_free (foldedprefix);
			break;
		}
		g_free (foldedprefix);
	}
	g_strfreev (items);

	g_free (foldedname);

	if (str == NULL)
		str = g_strdup (string);

	return str;
}
