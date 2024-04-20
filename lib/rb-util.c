/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2003 Colin Walters <walters@verbum.org>
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

/**
 * SECTION:rbutil
 * @short_description: assorted utility functions
 *
 * This is a dumping ground for utility functions that may or may not
 * be generally useful in Rhythmbox or elsewhere.  Things end up here
 * if they're clever or if they're used all over the place.
 */

#include "config.h"

#include <string.h>
#include <stdarg.h>
#include <stdlib.h>

#include <gtk/gtk.h>
#include <glib/gi18n.h>
#include <gobject/gvaluecollector.h>
#include <gio/gio.h>

#include "rb-util.h"
#include "rb-debug.h"

static GPrivate private_is_primary_thread;

/**
 * rb_true_function: (skip)
 * @dummy: unused
 *
 * Just returns %TRUE, useful as a callback function.
 *
 * Return value: %TRUE
 */
gboolean
rb_true_function (gpointer dummy)
{
	return TRUE;
}

/**
 * rb_false_function: (skip)
 * @dummy: unused
 *
 * Just returns %FALSE, useful as a callback function.
 *
 * Return value: %FALSE
 */
gboolean
rb_false_function (gpointer dummy)
{
	return FALSE;
}

/**
 * rb_null_function: (skip)
 * @dummy: unused
 *
 * Just returns NULL.  Useful as a callback function.
 *
 * Return value: NULL
 */
gpointer
rb_null_function (gpointer dummy)
{
	return NULL;
}

/**
 * rb_copy_function: (skip)
 * @data: generic argument
 *
 * Just returns its first argument.  Useful as a callback function.
 *
 * Return value: @data
 */
gpointer
rb_copy_function (gpointer data)
{
	return data;
}


/**
 * rb_gvalue_compare: (skip)
 * @a: left hand side
 * @b: right hand size
 *
 * Compares @a and @b for sorting.  @a and @b must contain the same value
 * type for the comparison to be valid.  Comparisons for some value types
 * are not particularly useful.
 *
 * Return value: -1 if @a < @b, 0 if @a == @b, 1 if @a > @b
 */
int
rb_gvalue_compare (GValue *a, GValue *b)
{
	int retval;
	const char *stra, *strb;

	if (G_VALUE_TYPE (a) != G_VALUE_TYPE (b))
		return -1;
	
	switch (G_VALUE_TYPE (a))
	{
	case G_TYPE_BOOLEAN:
		if (g_value_get_int (a) < g_value_get_int (b))
			retval = -1;
		else if (g_value_get_int (a) == g_value_get_int (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_CHAR:
		if (g_value_get_schar (a) < g_value_get_schar (b))
			retval = -1;
		else if (g_value_get_schar (a) == g_value_get_schar (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UCHAR:
		if (g_value_get_uchar (a) < g_value_get_uchar (b))
			retval = -1;
		else if (g_value_get_uchar (a) == g_value_get_uchar (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_INT:
		if (g_value_get_int (a) < g_value_get_int (b))
			retval = -1;
		else if (g_value_get_int (a) == g_value_get_int (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UINT:
		if (g_value_get_uint (a) < g_value_get_uint (b))
			retval = -1;
		else if (g_value_get_uint (a) == g_value_get_uint (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_LONG:
		if (g_value_get_long (a) < g_value_get_long (b))
			retval = -1;
		else if (g_value_get_long (a) == g_value_get_long (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_ULONG:
		if (g_value_get_ulong (a) < g_value_get_ulong (b))
			retval = -1;
		else if (g_value_get_ulong (a) == g_value_get_ulong (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_INT64:
		if (g_value_get_int64 (a) < g_value_get_int64 (b))
			retval = -1;
		else if (g_value_get_int64 (a) == g_value_get_int64 (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_UINT64:
		if (g_value_get_uint64 (a) < g_value_get_uint64 (b))
			retval = -1;
		else if (g_value_get_uint64 (a) == g_value_get_uint64 (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_ENUM:
		/* this is somewhat bogus. */
		if (g_value_get_enum (a) < g_value_get_enum (b))
			retval = -1;
		else if (g_value_get_enum (a) == g_value_get_enum (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_FLAGS:
		/* this is even more bogus. */
		if (g_value_get_flags (a) < g_value_get_flags (b))
			retval = -1;
		else if (g_value_get_flags (a) == g_value_get_flags (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_FLOAT:
		if (g_value_get_float (a) < g_value_get_float (b))
			retval = -1;
		else if (g_value_get_float (a) == g_value_get_float (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_DOUBLE:
		if (g_value_get_double (a) < g_value_get_double (b))
			retval = -1;
		else if (g_value_get_double (a) == g_value_get_double (b))
			retval = 0;
		else
			retval = 1;
		break;
	case G_TYPE_STRING:
		stra = g_value_get_string (a);
		strb = g_value_get_string (b);
		if (stra == NULL) stra = "";
		if (strb == NULL) strb = "";
		retval = g_utf8_collate (stra, strb);
		break;
	case G_TYPE_POINTER:
		retval = (g_value_get_pointer (a) != g_value_get_pointer (b));
		break;
	case G_TYPE_BOXED:
		retval = (g_value_get_boxed (a) != g_value_get_boxed (b));
		break;
	case G_TYPE_OBJECT:
		retval = (g_value_get_object (a) != g_value_get_object (b));
		break;
	default:
		g_assert_not_reached ();
		retval = 0;
		break;
	}
	return retval;
}

/**
 * rb_compare_gtimeval:
 * @a: left hand side
 * @b: right hand size
 *
 * Compares two #GTimeVal structures for sorting.
 *
 * Return value: -1 if @a < @b, 0 if @a == @b, 1 if @a > @b
 */
int
rb_compare_gtimeval (GTimeVal *a, GTimeVal *b)
{
	if (a->tv_sec == b->tv_sec)
		/* It's quite unlikely that microseconds are equal,
		 * so just ignore that case, we don't need a lot
		 * of precision.
		 */
		return a->tv_usec > b->tv_usec ? 1 : -1;
	else if (a->tv_sec > b->tv_sec)
		return 1;
	else
		return -1;
}

/* this is obsoleted by g_strcmp0, don't use it */
int
rb_safe_strcmp (const char *a,
                const char *b)
{
	return (!a && !b) ? 0 : (a && !b) || (!a && b) ? 1 : strcmp (a, b);
}

/**
 * rb_is_main_thread:
 *
 * Checks if currently executing on the main thread.
 *
 * Return value: %TRUE if on the main thread
 */
gboolean
rb_is_main_thread (void)
{
	return GPOINTER_TO_UINT(g_private_get (&private_is_primary_thread)) == 1;
}

static gboolean
purge_useless_threads (gpointer data)
{
	g_thread_pool_stop_unused_threads ();
	return TRUE;
}

static gboolean mutex_recurses;

/**
 * rb_assert_locked: (skip)
 * @mutex: a #GMutex
 *
 * Asserts that @mutex is currently locked.  Does not work with all
 * mutex implementations.
 */
void
rb_assert_locked (GMutex *mutex)
{
	if (!mutex_recurses)
		g_assert (!g_mutex_trylock (mutex));
}

/**
 * rb_threads_init: (skip)
 *
 * Initializes various thread helpers.  Must be called on startup.
 */
void
rb_threads_init (void)
{
	GMutex m;

	g_private_set (&private_is_primary_thread, GUINT_TO_POINTER (1));

	g_mutex_init (&m);
	g_mutex_lock (&m);
	mutex_recurses = g_mutex_trylock (&m);
	if (mutex_recurses)
		g_mutex_unlock (&m);
	g_mutex_unlock (&m);

	rb_debug ("GMutex %s recursive", mutex_recurses ? "is" : "isn't");

	/* purge useless thread-pool threads occasionally */
	g_timeout_add_seconds (30, purge_useless_threads, NULL);
}

/**
 * rb_string_split_words:
 * @string: the string to split
 *
 * Splits @string on word boundaries using Unicode character definitions.
 *
 * Return value: (array zero-terminated=1) (transfer full): NULL-terminated array of strings
 */
gchar **
rb_string_split_words (const gchar *string)
{
	/*return g_slist_prepend (NULL, g_strdup (string));*/

	GSList *words, *current;
	gunichar *unicode, *cur_write, *cur_read;
	gchar **ret;
	gchar *normalized;
	gint i, wordcount = 1;
	gboolean new_word = TRUE;

	g_return_val_if_fail (string != NULL, NULL);

	normalized = g_utf8_normalize(string, -1, G_NORMALIZE_DEFAULT);
	cur_write = cur_read = unicode = g_utf8_to_ucs4_fast (normalized, -1, NULL);

	/* we may fail here, we expect valid utf-8 */
	g_return_val_if_fail (unicode != NULL, NULL);

	words = g_slist_prepend (NULL, unicode);

	/* now normalize this text */
	while (*cur_read) {
		switch (g_unichar_type (*cur_read)) {
		case G_UNICODE_UNASSIGNED:
			rb_debug ("unassigned unicode character type found");
			/* fall through */
		case G_UNICODE_CONTROL:
		case G_UNICODE_FORMAT:
		case G_UNICODE_PRIVATE_USE:

		case G_UNICODE_SURROGATE:
		case G_UNICODE_LINE_SEPARATOR:
		case G_UNICODE_PARAGRAPH_SEPARATOR:
		case G_UNICODE_SPACE_SEPARATOR:
			/* remove these and start a new word */
			if (!new_word) {
				/* end current word if it isn't ended yet */
				*cur_write++ = 0;
				new_word = TRUE;
			}

			break;
		case G_UNICODE_SPACING_MARK:
		case G_UNICODE_ENCLOSING_MARK:
		case G_UNICODE_NON_SPACING_MARK:
		case G_UNICODE_CONNECT_PUNCTUATION:
		case G_UNICODE_DASH_PUNCTUATION:
		case G_UNICODE_CLOSE_PUNCTUATION:
		case G_UNICODE_FINAL_PUNCTUATION:
		case G_UNICODE_INITIAL_PUNCTUATION:
		case G_UNICODE_OTHER_PUNCTUATION:
		case G_UNICODE_OPEN_PUNCTUATION:
			/* remove these */
			/*break;*/
		case G_UNICODE_LOWERCASE_LETTER:
		case G_UNICODE_MODIFIER_LETTER:
		case G_UNICODE_OTHER_LETTER:
		case G_UNICODE_TITLECASE_LETTER:
		case G_UNICODE_UPPERCASE_LETTER:
		case G_UNICODE_DECIMAL_NUMBER:
		case G_UNICODE_LETTER_NUMBER:
		case G_UNICODE_OTHER_NUMBER:
		case G_UNICODE_CURRENCY_SYMBOL:
		case G_UNICODE_MODIFIER_SYMBOL:
		case G_UNICODE_MATH_SYMBOL:
		case G_UNICODE_OTHER_SYMBOL:
			/* keep these unchanged */
			*cur_write = *cur_read;
			if (new_word) {
				if (cur_write != unicode) {/* first insert has been done above */
					words = g_slist_prepend (words, cur_write);
					wordcount++;
				}
				new_word = FALSE;
			}
			cur_write++;
			break;    
		default:
			g_warning ("unknown unicode character type found");
			break;
		}
		cur_read++;
	}

	if (!new_word) {
		*cur_write++ = 0;
	}

	ret = g_new (gchar *, wordcount + 1); 
	current = words;
	for (i = wordcount - 1; i >= 0; i--) {
		ret[i] = g_ucs4_to_utf8 (current->data, -1, NULL, NULL, NULL);
		current = g_slist_next (current);
	}
	ret[wordcount] = NULL;

	g_slist_free (words);
	g_free (unicode);
	g_free (normalized);

	return ret;
}

/**
 * rb_search_fold:
 * @original: the string to fold
 *
 * Returns a case-folded and punctuation-stripped version of @original, useful
 * for performing text searches.
 *
 * Return value: (transfer full): case-folded string
 */
gchar*
rb_search_fold (const char *original)
{
	GString *string;
	gchar *normalized;
	gunichar *unicode, *cur;
	
	g_return_val_if_fail (original != NULL, NULL);

	/* old behaviour is equivalent to: return g_utf8_casefold (original, -1); */
	
	string = g_string_new (NULL);
	normalized = g_utf8_normalize(original, -1, G_NORMALIZE_DEFAULT);
	unicode = g_utf8_to_ucs4_fast (normalized, -1, NULL);
	

	for (cur = unicode; *cur != 0; cur++) {
		switch (g_unichar_type (*cur)) {
		case G_UNICODE_SPACING_MARK:
		case G_UNICODE_ENCLOSING_MARK:
		case G_UNICODE_NON_SPACING_MARK:
		case G_UNICODE_CONNECT_PUNCTUATION:
		case G_UNICODE_DASH_PUNCTUATION:
		case G_UNICODE_CLOSE_PUNCTUATION:
		case G_UNICODE_FINAL_PUNCTUATION:
		case G_UNICODE_INITIAL_PUNCTUATION:
		case G_UNICODE_OTHER_PUNCTUATION:
		case G_UNICODE_OPEN_PUNCTUATION:
			/* remove these */
			break;

		case G_UNICODE_LOWERCASE_LETTER:
		case G_UNICODE_MODIFIER_LETTER:
		case G_UNICODE_OTHER_LETTER:
		case G_UNICODE_TITLECASE_LETTER:
		case G_UNICODE_UPPERCASE_LETTER:
			/* convert to lower case */
			*cur = g_unichar_tolower (*cur);
			/* ... and fall through */\
		case G_UNICODE_DECIMAL_NUMBER:
		case G_UNICODE_LETTER_NUMBER:
		case G_UNICODE_OTHER_NUMBER:
		/* should be keep symbols? */
		case G_UNICODE_CURRENCY_SYMBOL:
		case G_UNICODE_MODIFIER_SYMBOL:
		case G_UNICODE_MATH_SYMBOL:
		case G_UNICODE_OTHER_SYMBOL:
			g_string_append_unichar (string, *cur);
			break;

		case G_UNICODE_UNASSIGNED:
			rb_debug ("unassigned unicode character type found");
			/* fall through */

		default:
			/* leave these in */
			g_string_append_unichar (string, *cur);
		}
	}
	
	g_free (unicode);
	g_free (normalized);
			
	return g_string_free (string, FALSE);
}

/**
 * rb_make_time_string:
 * @seconds: time in seconds
 *
 * Constructs a string describing the specified time.
 *
 * Return value: (transfer full): time string
 */
char *
rb_make_time_string (guint nseconds)
{
	int hours, minutes, seconds;

	hours = nseconds / (60 * 60);
	minutes = (nseconds - (hours * 60 * 60)) / 60;
	seconds = nseconds % 60;

	if (hours == 0)
		return g_strdup_printf (_("%d:%02d"), minutes, seconds);
	else
		return g_strdup_printf (_("%d:%02d:%02d"), hours, minutes, seconds);
}


/**
 * rb_make_duration_string:
 * @duration: duration in seconds
 *
 * Constructs a string describing the specified duration.  The string
 * describes hours, minutes, and seconds, and its format is localised.
 *
 * Return value: (transfer full): duration string
 */
char *
rb_make_duration_string (guint duration)
{
	if (duration == 0)
		return g_strdup (_("Unknown"));
	else
		return rb_make_time_string (duration);
}

/**
 * rb_make_elapsed_time_string:
 * @elapsed: elapsed time (in seconds)
 * @duration: duration (in seconds)
 * @show_remaining: if %TRUE, show the remaining time, otherwise show elapsed time
 *
 * Constructs a string describing a playback position.  The string describes hours,
 * minutes, and seconds, and its format is localised.  The string can describe either
 * the elapsed time or the time remaining.
 *
 * Return value: (transfer full): elapsed/remaining time string
 */
char *
rb_make_elapsed_time_string (guint elapsed, guint duration, gboolean show_remaining)
{
	int seconds = 0, minutes = 0, hours = 0;
	int seconds2 = 0, minutes2 = 0, hours2 = 0;

	if (duration == 0)
		return rb_make_time_string (elapsed);

	if (duration > 0) {
		hours2 = duration / (60 * 60);
		minutes2 = (duration - (hours2 * 60 * 60)) / 60;
		seconds2 = duration % 60;
	}

	if (elapsed > 0) {
		hours = elapsed / (60 * 60);
		minutes = (elapsed - (hours * 60 * 60)) / 60;
		seconds = elapsed % 60;
	}

	if (show_remaining) {
		int remaining = duration - elapsed;
		int remaining_hours = remaining / (60 * 60);
		int remaining_minutes = (remaining - (remaining_hours * 60 * 60)) / 60;
		/* remaining could conceivably be negative. This would
		 * be a bug, but the elapsed time will display right
		 * with the abs(). */
		int remaining_seconds = abs (remaining % 60);
		if (hours2 == 0)
			return g_strdup_printf (_("%d:%02d of %d:%02d remaining"),
						remaining_minutes, remaining_seconds,
						minutes2, seconds2);
		else
			return g_strdup_printf (_("%d:%02d:%02d of %d:%02d:%02d remaining"),
						remaining_hours, remaining_minutes, remaining_seconds,
						hours2, minutes2, seconds2);
	} else {
		if (hours == 0 && hours2 == 0)
			return g_strdup_printf (_("%d:%02d of %d:%02d"),
						minutes, seconds,
						minutes2, seconds2);
		else
			return g_strdup_printf (_("%d:%02d:%02d of %d:%02d:%02d"),
						hours, minutes, seconds,
						hours2, minutes2, seconds2);
	}
}

/**
 * rb_string_list_equal: (skip)
 * @a: (element-type utf8): list of strings to compare
 * @b: (element-type utf8): other list of strings to compare
 *
 * Checks if @a and @b contain exactly the same set of strings,
 * regardless of order.
 *
 * Return value: %TRUE if the lists contain all the same strings
 */
gboolean
rb_string_list_equal (GList *a, GList *b)
{
	GList *sorted_a_keys;
	GList *sorted_b_keys;
	GList *a_ptr, *b_ptr;
	gboolean ret = TRUE;

	if (a == b)
		return TRUE;

	if (g_list_length (a) != g_list_length (b))
		return FALSE;

	for (sorted_a_keys = NULL; a; a = a->next) {
		sorted_a_keys = g_list_prepend (sorted_a_keys,
					       g_utf8_collate_key (a->data, -1));
	}
	for (sorted_b_keys = NULL; b; b = b->next) {
		sorted_b_keys = g_list_prepend (sorted_b_keys,
					       g_utf8_collate_key (b->data, -1));
	}
	sorted_a_keys = g_list_sort (sorted_a_keys, (GCompareFunc) strcmp);
	sorted_b_keys = g_list_sort (sorted_b_keys, (GCompareFunc) strcmp);
	
	for (a_ptr = sorted_a_keys, b_ptr = sorted_b_keys;
	     a_ptr && b_ptr; a_ptr = a_ptr->next, b_ptr = b_ptr->next) {
		if (strcmp (a_ptr->data, b_ptr->data)) {
			ret = FALSE;
			break;
		}
	}
	g_list_foreach (sorted_a_keys, (GFunc) g_free, NULL);
	g_list_foreach (sorted_b_keys, (GFunc) g_free, NULL);
	g_list_free (sorted_a_keys);
	g_list_free (sorted_b_keys);
	return ret;
}

static void
list_copy_cb (const char *s, GList **list)
{
	*list = g_list_prepend (*list, g_strdup (s));
}

/**
 * rb_string_list_copy: (skip)
 * @list: (element-type utf8): list of strings to copy
 *
 * Creates a deep copy of @list.
 *
 * Return value: (element-type utf8) (transfer full): copied list
 */
GList *
rb_string_list_copy (GList *list)
{
	GList *copy = NULL;
	
	if (list == NULL)
		return NULL;

	g_list_foreach (list, (GFunc)list_copy_cb, &copy);
	copy = g_list_reverse (copy);

	return copy;
}

/**
 * rb_string_list_contains: (skip)
 * @list: (element-type utf8): list to check
 * @s: string to check for
 *
 * Checks if @list contains the string @s.
 *
 * Return value: %TRUE if found
 */
gboolean
rb_string_list_contains (GList *list, const char *s)
{
	GList *l;

	for (l = list; l != NULL; l = g_list_next (l)) {
		if (strcmp ((const char *)l->data, s) == 0)
			return TRUE;
	}

	return FALSE;
}

/**
 * rb_list_destroy_free: (skip)
 * @list: list to destroy
 * @destroyer: function to call to free elements of @list
 *
 * Calls @destroyer for each element in @list, then frees @list.
 */
void
rb_list_destroy_free (GList *list, GDestroyNotify destroyer)
{
	g_list_foreach (list, (GFunc)destroyer, NULL);
	g_list_free (list);
}

/**
 * rb_list_deep_free: (skip)
 * @list: (element-type any) (transfer full): list to free
 *
 * Frees each element of @list and @list itself.
 */
void
rb_list_deep_free (GList *list)
{
	rb_list_destroy_free (list, (GDestroyNotify)g_free);
}

/**
 * rb_slist_deep_free: (skip)
 * @list: (element-type any) (transfer full): list to free
 *
 * Frees each element of @list and @list itself.
 */
void
rb_slist_deep_free (GSList *list)
{
	g_slist_foreach (list, (GFunc)g_free, NULL);
	g_slist_free (list);
}

static void
collate_keys_cb (gpointer key, gpointer value, GList **list)
{
	*list = g_list_prepend (*list, key);
}

static void
collate_values_cb (gpointer key, gpointer value, GList **list)
{
	*list = g_list_prepend (*list, value);
}

/**
 * rb_collate_hash_table_keys: (skip)
 * @table: #GHashTable to collate
 *
 * Returns a #GList containing all keys from @table.  The keys are
 * not copied.
 *
 * Return value: (element-type any) (transfer container): #GList of keys
 */
GList*
rb_collate_hash_table_keys (GHashTable *table)
{
	GList *list = NULL;

	g_hash_table_foreach (table, (GHFunc)collate_keys_cb, &list);
	list = g_list_reverse (list);

	return list;
}

/**
 * rb_collate_hash_table_values: (skip)
 * @table: #GHashTable to collate
 *
 * Returns a #GList containing all values from @table.  The values are
 * not copied.
 *
 * Return value: (element-type any) (transfer container): #GList of values
 */
GList*
rb_collate_hash_table_values (GHashTable *table)
{
	GList *list = NULL;

	g_hash_table_foreach (table, (GHFunc)collate_values_cb, &list);
	list = g_list_reverse (list);

	return list;
}

/**
 * rb_uri_list_parse:
 * @uri_list: string containing URIs to parse
 *
 * Converts a single string containing a list of URIs into
 * a #GList of URI strings.
 *
 * Return value: (element-type utf8) (transfer full): #GList of URI strings
 */
GList *
rb_uri_list_parse (const char *uri_list)
{
	const gchar *p, *q;
	gchar *retval;
	GList *result = NULL;

	g_return_val_if_fail (uri_list != NULL, NULL);

	p = uri_list;

	while (p != NULL) {
		while (g_ascii_isspace (*p))
			p++;

		q = p;
		while ((*q != '\0')
		       && (*q != '\n')
		       && (*q != '\r'))
			q++;

		if (q > p) {
			q--;
			while (q > p
			       && g_ascii_isspace (*q))
				q--;

			retval = g_malloc (q - p + 2);
			strncpy (retval, p, q - p + 1);
			retval[q - p + 1] = '\0';

			if (retval != NULL)
				result = g_list_prepend (result, retval);
		}
		p = strchr (p, '\n');
		if (p != NULL)
			p++;
	}

	return g_list_reverse (result);
}

/**
 * rb_signal_accumulator_object_handled: (skip)
 * @hint: a #GSignalInvocationHint
 * @return_accu: holds the accumulated return value
 * @handler_return: holds the return value to be accumulated
 * @dummy: user data (unused)
 *
 * A #GSignalAccumulator that aborts the signal emission after the
 * first handler to return a value, and returns the value returned by
 * that handler.  This is the opposite behaviour from what you get when
 * no accumulator is specified, where the last signal handler wins.
 *
 * Return value: %FALSE to abort signal emission, %TRUE to continue
 */
gboolean
rb_signal_accumulator_object_handled (GSignalInvocationHint *hint,
				      GValue *return_accu,
				      const GValue *handler_return,
				      gpointer dummy)
{
	if (handler_return == NULL ||
	    !G_VALUE_HOLDS_OBJECT (handler_return) ||
	    g_value_get_object (handler_return) == NULL)
		return TRUE;

	g_value_unset (return_accu);
	g_value_init (return_accu, G_VALUE_TYPE (handler_return));
	g_value_copy (handler_return, return_accu);

	return FALSE;
}

/**
 * rb_signal_accumulator_value_handled: (skip)
 * @hint: a #GSignalInvocationHint
 * @return_accu: holds the accumulated return value
 * @handler_return: holds the return value to be accumulated
 * @dummy: user data (unused)
 *
 * A #GSignalAccumulator that aborts the signal emission after the
 * first handler to return a value, and returns the value returned by
 * that handler.  This is the opposite behaviour from what you get when
 * no accumulator is specified, where the last signal handler wins.
 *
 * Return value: %FALSE to abort signal emission, %TRUE to continue
 */
gboolean
rb_signal_accumulator_value_handled (GSignalInvocationHint *hint,
				     GValue *return_accu,
				     const GValue *handler_return,
				     gpointer dummy)
{
	if (handler_return == NULL ||
	    !G_VALUE_HOLDS (handler_return, G_TYPE_VALUE) ||
	    g_value_get_boxed (handler_return) == NULL)
		return TRUE;

	g_value_unset (return_accu);
	g_value_init (return_accu, G_VALUE_TYPE (handler_return));
	g_value_copy (handler_return, return_accu);

	return FALSE;
}

/**
 * rb_signal_accumulator_value_array: (skip)
 * @hint: a #GSignalInvocationHint
 * @return_accu: holds the accumulated return value
 * @handler_return: holds the return value to be accumulated
 * @dummy: user data (unused)
 *
 * A #GSignalAccumulator used to combine all returned values into
 * a #GArray of #GValue instances.
 *
 * Return value: %FALSE to abort signal emission, %TRUE to continue
 */
gboolean
rb_signal_accumulator_value_array (GSignalInvocationHint *hint,
				   GValue *return_accu,
				   const GValue *handler_return,
				   gpointer dummy)
{
	GArray *a;
	GArray *b;
	int i;

	if (handler_return == NULL)
		return TRUE;

	a = g_array_sized_new (FALSE, TRUE, sizeof (GValue), 1);
	g_array_set_clear_func (a, (GDestroyNotify) g_value_unset);
	if (G_VALUE_HOLDS_BOXED (return_accu)) {
		b = g_value_get_boxed (return_accu);
		if (b != NULL) {
			g_array_append_vals (a, b->data, b->len);
		}
	}

	if (G_VALUE_HOLDS_BOXED (handler_return)) {
		b = g_value_get_boxed (handler_return);
		for (i=0; i < b->len; i++) {
			a = g_array_append_val (a, g_array_index (b, GValue, i));
		}
	}

	g_value_unset (return_accu);
	g_value_init (return_accu, G_TYPE_ARRAY);
	g_value_set_boxed (return_accu, a);
	return TRUE;
}

/**
 * rb_signal_accumulator_boolean_or: (skip)
 * @hint: a #GSignalInvocationHint
 * @return_accu: holds the accumulated return value
 * @handler_return: holds the return value to be accumulated
 * @dummy: user data (unused)
 *
 * A #GSignalAccumulator used to return the boolean OR of all
 * returned (boolean) values.
 *
 * Return value: %FALSE to abort signal emission, %TRUE to continue
 */
gboolean
rb_signal_accumulator_boolean_or (GSignalInvocationHint *hint,
				  GValue *return_accu,
				  const GValue *handler_return,
				  gpointer dummy)
{
	if (handler_return != NULL && G_VALUE_HOLDS_BOOLEAN (handler_return)) {
		if (G_VALUE_HOLDS_BOOLEAN (return_accu) == FALSE ||
		    g_value_get_boolean (return_accu) == FALSE) {
			g_value_unset (return_accu);
			g_value_init (return_accu, G_TYPE_BOOLEAN);
			g_value_set_boolean (return_accu, g_value_get_boolean (handler_return));
		}
	}

	return TRUE;
}

/**
 * rb_value_array_append_data: (skip)
 * @array: #GArray to append to
 * @type: #GType of the value being appended
 * @...: value to append
 *
 * Appends a single value to @array, collecting it from @Varargs.
 */
void
rb_value_array_append_data (GArray *array, GType type, ...)
{
	GValue val = {0,};
	va_list va;
	gchar *err = NULL;

	va_start (va, type);

	g_value_init (&val, type);
	G_VALUE_COLLECT (&val, va, 0, &err);
	g_array_append_val (array, val);
	g_value_unset (&val);

	if (err)
		rb_debug ("unable to collect GValue: %s", err);

	va_end (va);
}

/**
 * rb_value_free: (skip)
 * @val: (transfer full): a #GValue
 *
 * Unsets and frees @val.  @val must have been allocated using
 * @g_slice_new or @g_slice_new0.
 */
void
rb_value_free (GValue *val)
{
	g_value_unset (val);
	g_slice_free (GValue, val);
}

/**
 * rb_str_in_strv: (skip)
 * @needle: string to search for
 * @haystack: array of strings to search
 *
 * Checks if @needle is present in the NULL-terminated string
 * array @haystack.
 *
 * Return value: %TRUE if found
 */
gboolean
rb_str_in_strv (const char *needle, const char **haystack)
{
	int i;

	if (needle == NULL || haystack == NULL)
		return FALSE;

	for (i = 0; haystack[i] != NULL; i++) {
		if (strcmp (needle, haystack[i]) == 0)
			return TRUE;
	}

	return FALSE;
}

/**
 * rb_set_tree_view_column_fixed_width:
 * @treeview: the #GtkTreeView containing the column
 * @column: the #GtkTreeViewColumn to size
 * @renderer: the #GtkCellRenderer used in the column
 * @strings: (array zero-terminated=1): a NULL-terminated set of strings to base the size on
 * @padding: a small amount of extra padding for the column
 *
 * Sets a fixed size for a tree view column based on
 * a set of strings to be displayed in the column.
 */
void
rb_set_tree_view_column_fixed_width (GtkWidget  *treeview,
				     GtkTreeViewColumn *column,
				     GtkCellRenderer *renderer,
				     const char **strings,
				     int padding)
{
	int max_width = 0;
	int i = 0;

	/* Take into account the header button width */
	GtkWidget *widget = gtk_tree_view_column_get_button (column);
	if (widget != NULL) {
		GtkRequisition natural_size;
		gtk_widget_get_preferred_size (widget, NULL, &natural_size);
		max_width = natural_size.width;
	}

	while (strings[i] != NULL) {
		GtkRequisition natural_size;
		g_object_set (renderer, "text", strings[i], NULL);
		/* XXX should we use minimum size instead? */
		gtk_cell_renderer_get_preferred_size (renderer,
						      GTK_WIDGET (treeview),
						      NULL,
						      &natural_size);

		if (natural_size.width > max_width)
			max_width = natural_size.width;

		i++;
	}

	gtk_tree_view_column_set_fixed_width (column, max_width + padding);
}

/**
 * rb_scale_pixbuf_to_size:
 * @pixbuf: the #GdkPixbuf containing the original image
 * @size: a stock icon size
 *
 * Creates a new #GdkPixbuf from the original one, for a target of
 * size, respecting the aspect ratio of the image.
 *
 * Return value: (transfer full): scaled #GdkPixbuf
 */
GdkPixbuf *
rb_scale_pixbuf_to_size (GdkPixbuf *pixbuf, GtkIconSize size)
{
	int icon_size;
	int width, height;
	int d_width, d_height;

	g_return_val_if_fail (GDK_IS_PIXBUF (pixbuf), NULL);

	if (gtk_icon_size_lookup (size, &icon_size, NULL) == FALSE)
		return NULL;

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);

	if (width > height) {
		d_width = icon_size;
		d_height = d_width * height / width;
	} else {
		d_height = icon_size;
		d_width = d_height * width / height;
	}

	return gdk_pixbuf_scale_simple (pixbuf, d_width, d_height, GDK_INTERP_BILINEAR);
}

#define DELAYED_SYNC_ITEM "rb-delayed-sync"
#define DELAYED_SYNC_FUNC_ITEM "rb-delayed-sync-func"
#define DELAYED_SYNC_DATA_ITEM "rb-delayed-sync-data"


static gboolean
do_delayed_apply (GSettings *settings)
{
	gpointer data;
	RBDelayedSyncFunc sync_func;

	data = g_object_get_data (G_OBJECT (settings), DELAYED_SYNC_DATA_ITEM);
	sync_func = g_object_get_data (G_OBJECT (settings), DELAYED_SYNC_FUNC_ITEM);
	if (sync_func != NULL) {
		sync_func (settings, data);
	}

	g_object_set_data (G_OBJECT (settings), DELAYED_SYNC_ITEM, GUINT_TO_POINTER (0));
	g_object_set_data (G_OBJECT (settings), DELAYED_SYNC_FUNC_ITEM, NULL);
	g_object_set_data (G_OBJECT (settings), DELAYED_SYNC_DATA_ITEM, NULL);
	return FALSE;
}

static void
remove_delayed_sync (gpointer data)
{
	g_source_remove (GPOINTER_TO_UINT (data));
}

/**
 * rb_settings_delayed_sync:
 * @settings: #GSettings instance
 * @sync_func: (allow-none): function to call
 * @data: (allow-none): data to pass to @func
 * @destroy: (allow-none): function to use to free @data
 *
 * Synchronizes settings in the @settings instance after 500ms has elapsed
 * with no further changes.
 */
void
rb_settings_delayed_sync (GSettings *settings, RBDelayedSyncFunc sync_func, gpointer data, GDestroyNotify destroy)
{
	if (sync_func == NULL) {
		do_delayed_apply (settings);
	} else {
		guint id = g_timeout_add (500, (GSourceFunc) do_delayed_apply, settings);
		g_object_set_data_full (G_OBJECT (settings), DELAYED_SYNC_ITEM, GUINT_TO_POINTER (id), remove_delayed_sync);
		g_object_set_data (G_OBJECT (settings), DELAYED_SYNC_FUNC_ITEM, sync_func);
		g_object_set_data_full (G_OBJECT (settings), DELAYED_SYNC_DATA_ITEM, data, destroy);
	}
}

/**
 * rb_menu_update_link:
 * @menu: menu to update
 * @link_attr: attribute indicating the menu link to update
 * @target: new menu link target
 *
 * Updates a submenu link to point to the specified target menu.
 */
void
rb_menu_update_link (GMenu *menu, const char *link_attr, GMenuModel *target)
{
	GMenuModel *mm = G_MENU_MODEL (menu);
	int i;

	for (i = 0; i < g_menu_model_get_n_items (mm); i++) {
		const char *link;
		const char *label;
		GMenuModel *section;

		/* only recurse into sections, not submenus */
		section = g_menu_model_get_item_link (mm, i, G_MENU_LINK_SECTION);
		if (section != NULL && G_IS_MENU (section)) {
			rb_menu_update_link (G_MENU (section), link_attr, target);
		}

		if (g_menu_model_get_item_attribute (mm, i, link_attr, "s", &link)) {
			GMenuItem *item;

			g_menu_model_get_item_attribute (mm, i, "label", "s", &label);
			g_menu_remove (menu, i);

			item = g_menu_item_new (label, NULL);
			g_menu_item_set_attribute (item, link_attr, "s", "hi");
			if (target) {
				g_menu_item_set_link (item, G_MENU_LINK_SUBMENU, target);
			} else {
				/* set a nonexistant action name so it gets disabled */
				g_menu_item_set_detailed_action (item, "nonexistant-action");
			}

			g_menu_insert_item (menu, i, item);
		}
	}
}
