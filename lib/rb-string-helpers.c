/*
 *  arch-tag: Implementation of various string-related utility functions
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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <glib.h>
#include <string.h>

#include "rb-string-helpers.h"

char *
rb_prefix_to_suffix (const char *string)
{
	/* comma separated list of prefixes that are to
	 * be appended as suffix, NOTE: notice the spaces placement */
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

int
rb_utf8_strncasecmp (gconstpointer a, gconstpointer b)
{
	char *al = g_utf8_casefold ((const char *) a, -1);
	char *bl = g_utf8_casefold ((const char *) b, -1);
	int ret = g_utf8_collate (al, bl);
	g_free (al);
	g_free (bl);
	return ret;
}

char *
rb_unicodify (const char *str, gboolean try_iso1_first)
{
	char *ret;
	int bytes_read, bytes_written;

	if (g_utf8_validate (str, -1, NULL))
		return g_strdup (str);

	/* A lot of stuff we get over the network is ISO-8859-1. */
	if (try_iso1_first)
		ret = g_convert (str, strlen (str), "UTF-8", "ISO-8859-1",
				 &bytes_read, &bytes_written, NULL);
	else
		ret = NULL;

	/* Failing that, try the locale's encoding. */
	if (!ret)
		ret = g_locale_to_utf8 (str, strlen (str), &bytes_read, &bytes_written, NULL);
	if (!ret)
		ret = g_convert (str, strlen (str), "UTF-8", "ISO-8859-1",
				 &bytes_read, &bytes_written, NULL);

	return ret;
}

char *
rb_get_sort_key (const char *string)
{
	char *collated, *folded;
	folded = g_utf8_casefold (string, -1);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);
	return collated;
}

