/*
 *  arch-tag: Implementation of various string-related utility functions
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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <glib.h>
#include <string.h>
#include <stdlib.h>

#include "rb-string-helpers.h"

static GHashTable *encodings;

/* stolen from gnome-desktop-item.c */
static gboolean
check_locale (const char *locale)
{
	GIConv cd = g_iconv_open ("UTF-8", locale);
	if ((GIConv)-1 == cd)
		return FALSE;
	g_iconv_close (cd);
	return TRUE;
}

/* stolen from gnome-desktop-item.c */
static void
insert_locales (GHashTable *encodings, char *enc, ...)
{
	va_list args;
	char *s;

	va_start (args, enc);
	for (;;) {
		s = va_arg (args, char *);
		if (s == NULL)
			break;
		g_hash_table_insert (encodings, s, enc);
	}
	va_end (args);
}

/* stolen from gnome-desktop-item.c */
void
rb_string_helpers_init (void)
{
/* make a standard conversion table from the desktop standard spec */
	encodings = g_hash_table_new (g_str_hash, g_str_equal);

	/* "C" is plain ascii */
	insert_locales (encodings, "ASCII", "C", NULL);

	insert_locales (encodings, "ARMSCII-8", "by", NULL);
	insert_locales (encodings, "BIG5", "zh_TW", NULL);
	insert_locales (encodings, "CP1251", "be", "bg", NULL);
	if (check_locale ("EUC-CN")) {
		insert_locales (encodings, "EUC-CN", "zh_CN", NULL);
	} else {
		insert_locales (encodings, "GB2312", "zh_CN", NULL);
	}
	insert_locales (encodings, "EUC-JP", "ja", NULL);
	insert_locales (encodings, "UHC", "ko", NULL);
	/*insert_locales (encodings, "GEORGIAN-ACADEMY", NULL);*/
	insert_locales (encodings, "GEORGIAN-PS", "ka", NULL);
	insert_locales (encodings, "ISO-8859-1", "br", "ca", "da", "de", "en", "es", "eu", "fi", "fr", "gl", "it", "nl", "wa", "no", "pt", "pt", "sv", NULL);
	insert_locales (encodings, "ISO-8859-2", "cs", "hr", "hu", "pl", "ro", "sk", "sl", "sq", "sr", NULL);
	insert_locales (encodings, "ISO-8859-3", "eo", NULL);
	insert_locales (encodings, "ISO-8859-5", "mk", "sp", NULL);
	insert_locales (encodings, "ISO-8859-7", "el", NULL);
	insert_locales (encodings, "ISO-8859-9", "tr", NULL);
	insert_locales (encodings, "ISO-8859-13", "lt", "lv", "mi", NULL);
	insert_locales (encodings, "ISO-8859-14", "ga", "cy", NULL);
	insert_locales (encodings, "ISO-8859-15", "et", NULL);
	insert_locales (encodings, "KOI8-R", "ru", NULL);
	insert_locales (encodings, "KOI8-U", "uk", NULL);
	if (check_locale ("TCVN-5712")) {
		insert_locales (encodings, "TCVN-5712", "vi", NULL);
	} else {
		insert_locales (encodings, "TCVN", "vi", NULL);
	}
	insert_locales (encodings, "TIS-620", "th", NULL);
	/*insert_locales (encodings, "VISCII", NULL);*/
}

void
rb_string_helpers_shutdown (void)
{
	g_hash_table_destroy (encodings);
}

/* stolen from gnome-desktop-item.c */
static const char *
get_encoding_from_locale (const char *locale)
{
	char lang[3];
	const char *encoding;

	if (locale == NULL)
		return NULL;

	/* if locale includes encoding (that isn't UTF-8), use it */
	encoding = strchr (locale, '.');
	if (encoding != NULL && strncmp (encoding, ".UTF-8", 6)) {
		return encoding+1;
	}

	/* first try the entire locale (at this point ll_CC) */
	encoding = g_hash_table_lookup (encodings, locale);
	if (encoding != NULL)
		return encoding;

	/* Try just the language */
	strncpy (lang, locale, 2);
	lang[2] = '\0';
	return g_hash_table_lookup (encodings, lang);
}

char *
rb_unicodify (const char *str)
{
	char *ret = NULL;
	const char *char_encoding;

	/* Try validating it as UTF-8 first */
	if (g_utf8_validate (str, -1, NULL))
		return g_strdup (str);

	/* Failing that, try the legacy encoding associated
	   with the locale. */
	char_encoding = get_encoding_from_locale (getenv ("LANG"));
	if (char_encoding == NULL)
		ret = NULL;
	else
		ret = g_convert (str, -1, "UTF-8", char_encoding,
				 NULL, NULL, NULL);
	/* Failing that, try ISO-8859-1. */
	if (!ret)
		ret = g_convert (str, -1, "UTF-8", "ISO-8859-1",
				 NULL, NULL, NULL);

	return ret;
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
rb_get_sort_key (const char *string)
{
	char *collated, *folded;
	folded = g_utf8_casefold (string, -1);
	collated = g_utf8_collate_key (folded, -1);
	g_free (folded);
	return collated;
}

