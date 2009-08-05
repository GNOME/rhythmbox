/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of totally random functions that didn't fit elsewhere
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

static GPrivate * private_is_primary_thread;

gboolean
rb_true_function (gpointer dummy)
{
	return TRUE;
}

gboolean
rb_false_function (gpointer dummy)
{
	return FALSE;
}

gpointer
rb_null_function (gpointer dummy)
{
	return NULL;
}

gpointer
rb_copy_function (gpointer data)
{
	return data;
}


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
		if (g_value_get_char (a) < g_value_get_char (b))
			retval = -1;
		else if (g_value_get_char (a) == g_value_get_char (b))
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

int
rb_safe_strcmp (const char *a,
                const char *b)
{
	return (!a && !b) ? 0 : (a && !b) || (!a && b) ? 1 : strcmp (a, b);
}

/* Taken from totem/video-utils.c CVS HEAD 2004-04-22 */
static void
totem_pixbuf_mirror (GdkPixbuf *pixbuf)
{
	int i, j, rowstride, offset, right;
	guchar *pixels;
	int width, height, size;
	guint32 tmp;

	pixels = gdk_pixbuf_get_pixels (pixbuf);
	g_return_if_fail (pixels != NULL);

	width = gdk_pixbuf_get_width (pixbuf);
	height = gdk_pixbuf_get_height (pixbuf);
	rowstride = gdk_pixbuf_get_rowstride (pixbuf);
	size = height * width * sizeof (guint32);

	for (i = 0; i < size; i += rowstride)
	{
		for (j = 0; j < rowstride; j += sizeof(guint32))
		{
			offset = i + j;
			right = i + (((width - 1) * sizeof(guint32)) - j);

			if (right <= offset)
				break;

			memcpy (&tmp, pixels + offset, sizeof(guint32));
			memcpy (pixels + offset, pixels + right,
					sizeof(guint32));
			memcpy (pixels + right, &tmp, sizeof(guint32));
		}
	}
}



/* Same as gtk_image_new_from_stock except that it mirrors the icons for RTL 
 * languages
 */
GtkWidget *
rb_image_new_from_stock (const gchar *stock_id, GtkIconSize size)
{
	if (gtk_widget_get_default_direction () == GTK_TEXT_DIR_LTR) {
		return gtk_image_new_from_stock (stock_id, size);
	} else {

		GtkWidget *image;
		GdkPixbuf *pixbuf;
		GdkPixbuf *mirror;
		
		image = gtk_image_new ();
		
		if (image == NULL) {
			return NULL;
		}
		
		pixbuf = gtk_widget_render_icon (image, stock_id, size, NULL);
		g_assert (pixbuf != NULL);
		
		
		mirror = gdk_pixbuf_copy (pixbuf);
		g_object_unref (pixbuf);

		if (!mirror)
			return NULL;

		totem_pixbuf_mirror (mirror);
		gtk_image_set_from_pixbuf (GTK_IMAGE (image), mirror);
		g_object_unref (mirror);

		return image;
	}

	return NULL;
}

void
rb_gtk_action_popup_menu (GtkUIManager *uimanager, const char *path)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (uimanager, path);
	if (menu == NULL) {
		g_warning ("Couldn't get menu widget for %s", path);
	} else {
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, 
				gtk_get_current_event_time ());
	}
}


gboolean
rb_is_main_thread (void)
{
	if (g_thread_supported()) {
		return GPOINTER_TO_UINT(g_private_get (private_is_primary_thread)) == 1;
	} else {
		return TRUE;
	}
}

static gboolean
purge_useless_threads (gpointer data)
{
	g_thread_pool_stop_unused_threads ();
	return TRUE;
}


static GStaticRecMutex rb_gdk_mutex;
static gboolean mutex_recurses;

static void
_threads_enter (void)
{
	g_static_rec_mutex_lock (&rb_gdk_mutex);
}

static void
_threads_leave (void)
{
	g_static_rec_mutex_unlock (&rb_gdk_mutex);
}


void
rb_assert_locked (GMutex *m)
{
	if (!mutex_recurses)
		g_assert (!g_mutex_trylock (m));
}

void
rb_threads_init (void)
{
	GMutex *m;

	private_is_primary_thread = g_private_new (NULL);
	g_private_set (private_is_primary_thread, GUINT_TO_POINTER (1));

	g_static_rec_mutex_init (&rb_gdk_mutex);
	gdk_threads_set_lock_functions (_threads_enter, _threads_leave);
	gdk_threads_init ();

	m = g_mutex_new ();

	g_mutex_lock (m);
	mutex_recurses = g_mutex_trylock (m);
	if (mutex_recurses)
		g_mutex_unlock (m);
	g_mutex_unlock (m);
	g_mutex_free (m);

	rb_debug ("GMutex %s recursive", mutex_recurses ? "is" : "isn't");

	/* purge useless thread-pool threads occasionally */
	g_timeout_add_seconds (30, purge_useless_threads, NULL);
}

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
		case G_UNICODE_COMBINING_MARK:
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
		case G_UNICODE_COMBINING_MARK:
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

char *
rb_make_duration_string (guint duration)
{
	char *str;
	int hours, minutes, seconds;

	hours = duration / (60 * 60);
	minutes = (duration - (hours * 60 * 60)) / 60;
	seconds = duration % 60;

	if (hours == 0 && minutes == 0 && seconds == 0)
		str = g_strdup (_("Unknown"));
	else if (hours == 0)
		str = g_strdup_printf (_("%d:%02d"), minutes, seconds);
	else
		str = g_strdup_printf (_("%d:%02d:%02d"), hours, minutes, seconds);

	return str;
}

char *
rb_make_elapsed_time_string (guint elapsed, guint duration, gboolean show_remaining)
{
	int seconds = 0, minutes = 0, hours = 0;
	int seconds2 = 0, minutes2 = 0, hours2 = 0;

	if (duration == 0)
		return rb_make_duration_string (elapsed);

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

void
rb_list_destroy_free (GList *list, GDestroyNotify destroyer)
{
	g_list_foreach (list, (GFunc)destroyer, NULL);
	g_list_free (list);

}

void
rb_list_deep_free (GList *list)
{
	rb_list_destroy_free (list, (GDestroyNotify)g_free);
}

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

GList*
rb_collate_hash_table_keys (GHashTable *table)
{
	GList *list = NULL;

	g_hash_table_foreach (table, (GHFunc)collate_keys_cb, &list);
	list = g_list_reverse (list);

	return list;
}

GList*
rb_collate_hash_table_values (GHashTable *table)
{
	GList *list = NULL;

	g_hash_table_foreach (table, (GHFunc)collate_values_cb, &list);
	list = g_list_reverse (list);

	return list;
}

/*
 * hacked up version of gnome_vfs_uri_list_parse,
 * that it doesn't strip #s and and returns strings. 
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

char*
rb_mime_get_friendly_name (const char *mime_type)
{
	gchar *name = NULL;
	
	if (name == NULL && mime_type)
		name = g_content_type_get_description (mime_type);
	if (name == NULL)
		name = _("Unknown");

	return name;
}

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

gboolean
rb_signal_accumulator_value_array (GSignalInvocationHint *hint,
				   GValue *return_accu,
				   const GValue *handler_return,
				   gpointer bleh)
{
	GValueArray *a;
	GValueArray *b;
	int i;

	if (handler_return == NULL)
		return TRUE;

	a = NULL;
	if (G_VALUE_HOLDS_BOXED (return_accu)) {
		a = g_value_get_boxed (return_accu);
		if (a != NULL) {
			a = g_value_array_copy (a);
		}
	}

	if (a == NULL) {
		a = g_value_array_new (1);
	}

	if (G_VALUE_HOLDS_BOXED (handler_return)) {
		b = g_value_get_boxed (handler_return);
		for (i=0; i < b->n_values; i++) {
			GValue *z = g_value_array_get_nth (b, i);
			a = g_value_array_append (a, z);
		}
	}

	g_value_unset (return_accu);
	g_value_init (return_accu, G_TYPE_VALUE_ARRAY);
	g_value_set_boxed (return_accu, a);
	return TRUE;
}

void
rb_value_array_append_data (GValueArray *array, GType type, ...)
{
	GValue val = {0,};
	va_list va;
	gchar *err = NULL;

	va_start (va, type);

	g_value_init (&val, type);
	G_VALUE_COLLECT (&val, va, 0, &err);
	g_value_array_append (array, &val);
	g_value_unset (&val);

	if (err)
		rb_debug ("unable to collect GValue: %s", err);

	va_end (va);
}

void
rb_value_free (GValue *val)
{
	g_value_unset (val);
	g_slice_free (GValue, val);
}

gboolean
rb_str_in_strv (const char *needle, char **haystack)
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
 * @strings: a NULL-terminated set of strings to base the size on
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

	while (strings[i] != NULL) {
		gint width;
		g_object_set (renderer, "text", strings[i], NULL);
		gtk_cell_renderer_get_size (renderer,
					    GTK_WIDGET (treeview),
					    NULL,
					    NULL, NULL,
					    &width, NULL);

		if (width > max_width)
			max_width = width;

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

