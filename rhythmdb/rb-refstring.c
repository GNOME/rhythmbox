/*
 *  arch-tag: Implementation of reference-counted string
 *
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <config.h>

#include <glib.h>
#include <string.h>
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-refstring.h"

GHashTable *rb_refstrings;
GMutex *rb_refstrings_mutex;

struct RBRefString
{
	gint refcount;
	gpointer folded;
	gpointer sortkey;
	char value[1];
};

static void
rb_refstring_free (RBRefString *refstr)
{
	refstr->refcount = 0xdeadbeef;
	g_free (refstr->folded);
	refstr->folded = NULL;
	g_free (refstr->sortkey);
	refstr->sortkey = NULL;
	g_free (refstr);
}

void
rb_refstring_system_init ()
{
	rb_refstrings_mutex = g_mutex_new ();

	rb_refstrings = g_hash_table_new_full (g_str_hash, g_str_equal,
					       NULL, (GDestroyNotify) rb_refstring_free);
}

RBRefString *
rb_refstring_new (const char *init)
{
	RBRefString *ret;

	g_mutex_lock (rb_refstrings_mutex);
	ret = g_hash_table_lookup (rb_refstrings, init);

	if (ret) {
		rb_refstring_ref (ret);
		g_mutex_unlock (rb_refstrings_mutex);
		return ret;
	}

	ret = g_malloc (sizeof (RBRefString) + strlen (init));

	strcpy (ret->value, init);
	ret->refcount = 1;
	ret->folded = NULL;
	ret->sortkey = NULL;

	g_hash_table_insert (rb_refstrings, ret->value, ret);
	g_mutex_unlock (rb_refstrings_mutex);
	return ret;
}

RBRefString *
rb_refstring_find (const char *init)
{
	RBRefString *ret;

	g_mutex_lock (rb_refstrings_mutex);
	ret = g_hash_table_lookup (rb_refstrings, init);

	if (ret)
		rb_refstring_ref (ret);

	g_mutex_unlock (rb_refstrings_mutex);
	return ret;
}

void
rb_refstring_unref (RBRefString *val)
{
	if (val == NULL)
		return;

	g_return_if_fail (val->refcount > 0);

	if (g_atomic_int_dec_and_test (&val->refcount)) {
		g_mutex_lock (rb_refstrings_mutex);
		g_hash_table_remove (rb_refstrings, val->value);
		g_mutex_unlock (rb_refstrings_mutex);
	}
}

void
rb_refstring_system_shutdown (void)
{
	g_hash_table_destroy (rb_refstrings);
	g_mutex_free (rb_refstrings_mutex);
}

RBRefString *
rb_refstring_ref (RBRefString *val)
{
	if (val == NULL)
		return NULL;

	g_return_val_if_fail (val->refcount > 0, NULL);

	g_atomic_int_inc (&val->refcount);
	return val;
}

const char *
rb_refstring_get (const RBRefString *val)
{
	return val ? val->value : NULL;
}

/*
 * The next two functions will compute the values if they haven't
 * been already done. Using g_atomic_* is much more efficient than
 * using mutexes (since mutexes may require kernel calls) and these
 * get called often.
 */

const char *
rb_refstring_get_folded (RBRefString *val)
{
	gpointer *ptr;
	const char *string;

	if (val == NULL)
		return NULL;

	ptr = &val->folded;
	string = (const char*)g_atomic_pointer_get (ptr);
	if (string == NULL) {
		char *newstring;

		newstring = rb_search_fold (rb_refstring_get (val));
		if (g_atomic_pointer_compare_and_exchange (ptr, NULL, newstring)) {
			string = newstring;
		} else {
			g_free (newstring);
			string = (const char *)g_atomic_pointer_get (ptr);
			g_assert (string);
		}
	}

	return string;
}

const char *
rb_refstring_get_sort_key (RBRefString *val)
{
	gpointer *ptr;
	const char *string;

	if (val == NULL)
		return NULL;

	ptr = &val->sortkey;
	string = (const char *)g_atomic_pointer_get (ptr);
	if (string == NULL) {
		char *newstring;
		const char *s;

		s = rb_refstring_get_folded (val);
		newstring = rb_utf8_collate_key_for_filename (s, -1);

		if (g_atomic_pointer_compare_and_exchange (ptr, NULL, newstring)) {
			string = newstring;
		} else {
			g_free (newstring);
			string = (const char*)g_atomic_pointer_get (ptr);
			g_assert (string);
		}
	}

	return string;
}

guint
rb_refstring_hash (gconstpointer p)
{
	const RBRefString *ref = p;
	return g_str_hash (rb_refstring_get (ref));
}

gboolean
rb_refstring_equal (gconstpointer ap, gconstpointer bp)
{
	return (ap == bp);
}
