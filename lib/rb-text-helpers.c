/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Uri Sivan
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

#include <rb-text-helpers.h>

/**
 * SECTION:rbtexthelpers
 * @short_description: text direction (LTR/RTL) functions
 *
 * Provides some helper functions for constructing strings that
 * may include both left-to-right and right-to-left text.
 */

/* unicode direction markup characters
 * see http://unicode.org/reports/tr9/, in particular sections 2.1-2.4
 *
 * LRM = Left-to-Right Mark = invisible character with LTR direction
 * RLM = Right-to-Left Mark = invisible character with RTL direction
 * LRE = Left-to-Right Embedding = start of LTR "island" in RTL text
 * RLE = Right-to-Left Embedding = start of RTL "island" in LTR text
 * PDF = Pop Directional Format = close last LRE or RLE section
 *
 * the following constants are in UTF-8 encoding
 */
static const char *const UNICODE_LRM = "\xE2\x80\x8E";
static const char *const UNICODE_RLM = "\xE2\x80\x8F";
static const char *const UNICODE_LRE = "\xE2\x80\xAA";
static const char *const UNICODE_RLE = "\xE2\x80\xAB";
static const char *const UNICODE_PDF = "\xE2\x80\xAC";

static void
append_and_free (GString *str, char *text)
{
	g_string_append (str, text);
	g_free (text);
}

/**
 * rb_text_direction_conflict:
 * @dir1: direction A
 * @dir2: direction B
 *
 * Direction conflict here means the two directions are defined (non-neutral)
 * and they are different.
 *
 * Return value: %TRUE if the two directions conflict.
 */
gboolean
rb_text_direction_conflict (PangoDirection dir1, PangoDirection dir2)
{
	return (dir1 != dir2) &&
	       (dir1 != PANGO_DIRECTION_NEUTRAL) &&
	       (dir2 != PANGO_DIRECTION_NEUTRAL);
}

/**
 * rb_text_common_direction:
 * @first: first string
 * @...: rest of strings, terminated with %NULL
 *
 * This functions checks the direction of all given strings and:
 *
 * 1. If all strings are direction neutral, returns %PANGO_DIRECTION_NEUTRAL;
 *
 * 2. If all strings are either LTR or neutral, returns %PANGO_DIRECTION_LTR;
 *
 * 3. If all strings are either RTL or neutral, returns %PANGO_DIRECTION_RTL;
 *
 * 4. If at least one is RTL and one LTR, returns %PANGO_DIRECTION_NEUTRAL.
 *
 * Note: neutral (1) and mixed (4) are two very different situations,
 * they share a return code here only because they're the same for our
 * specific use.
 *
 * Return value: common direction of all strings, as defined above.
 */
PangoDirection
rb_text_common_direction (const char *first, ...)
{
	PangoDirection common_dir = PANGO_DIRECTION_NEUTRAL;
	PangoDirection text_dir;
	const char *text;
	va_list args;

	va_start (args, first);

	for (text = first; text; text = va_arg(args, const char *)) {
		if (!text[0])
			continue;

		text_dir = pango_find_base_dir (text, -1);

		if (rb_text_direction_conflict (text_dir, common_dir)) {
			/* mixed direction */
			common_dir = PANGO_DIRECTION_NEUTRAL;
			break;
		}

		common_dir = text_dir;
	}

	va_end (args);

	return common_dir;
}

/**
 * rb_text_cat:
 * @base_dir: direction of the result string.
 * @...: pairs of strings (content, format) terminated with %NULL.
 *
 * This function concatenates strings to a single string, preserving
 * each part's original direction (LTR or RTL) using unicode markup,
 * as detailed here: http://unicode.org/reports/tr9/.
 *
 * It is called like this:
 *
 * s = rb_text_cat(base_dir, str1, format1, ..., strN, formatN, %NULL)
 *
 * Format is a printf format with exactly one \%s. "\%s" or "" will
 * insert the string as is.
 *
 * Any string that is empty ("") will be skipped, its format must still be
 * passed.
 *
 * A space is inserted between strings.
 *
 * The algorithm:
 *
 * 1. Caller supplies the base direction of the result in base_dir.
 *
 * 2. Insert either LRM or RLM at the beginning of the string to set
 *    its base direction, according to base_dir.
 *
 * 3. Find the direction of each string using pango.
 *
 * 4. For strings that have the same direction as the base direction,
 *    just insert them in.
 *
 * 5. For strings that have the opposite direction than the base one,
 *    insert them surrounded with embedding codes RLE/LRE .. PDF.
 *
 * Return value: a new string containing the result.
 */
char *
rb_text_cat (PangoDirection base_dir, ...)
{
	PangoDirection text_dir;
	va_list args;
	const char *embed_start;
	const char *embed_stop = UNICODE_PDF;
	GString *result;
	int first_char;

	va_start (args, base_dir);

	result = g_string_sized_new (100);

	if (base_dir == PANGO_DIRECTION_LTR) {
		/* base direction LTR, embedded parts are RTL */
		g_string_append (result, UNICODE_LRM);
		embed_start = UNICODE_RLE;
	} else {
		/* base direction RTL, embedded parts are LTR */
		g_string_append (result, UNICODE_RLM);
		embed_start = UNICODE_LRE;
	}
	first_char = result->len;

	while (1) {
		const char *text = va_arg (args, const char *);
		const char *format;

		if (!text)
			break;

		format = va_arg (args, const char *);
		if (!text[0])
			continue;
		if (!format[0])
			format = "%s";

		if (result->len > first_char) {
			g_string_append (result, " ");
		}

		text_dir = pango_find_base_dir (text, -1);

		if (rb_text_direction_conflict (text_dir, base_dir)) {
			/* surround text with embed codes */
			g_string_append (result, embed_start);
			append_and_free (result, g_markup_printf_escaped (format, text));
			g_string_append (result, embed_stop);
		} else {
			append_and_free (result, g_markup_printf_escaped (format, text));
		}
	}

	va_end (args);

	return g_string_free (result, FALSE);
}

PangoAttrList *
rb_text_numeric_get_pango_attr_list (void)
{
	static PangoAttrList *attr_list = NULL;

	if (attr_list == NULL) {
		/*
		 * use tabular figures to ensure constant width for
		 * numeric data in text widgets / renderers. this
		 * ensures that numeric data which gets updated ( like
		 * track time label ) doesn't cause wiggle to the
		 * widgets / renderers that display them.
		 */
		attr_list = pango_attr_list_new ();
		pango_attr_list_insert (attr_list, pango_attr_font_features_new ("tnum=1"));
	}

	return attr_list;
}
