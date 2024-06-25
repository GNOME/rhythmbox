/*
 *  Copyright (C) 2004 Colin Walters <walters@redhat.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <config.h>

#include <glib.h>
#include <string.h>
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"
#include "rb-refstring.h"

GHashTable *rb_refstrings;
GMutex rb_refstrings_mutex;

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

/**
 * rb_refstring_system_init:
 *
 * Sets up the refstring system.  Called on startup.
 */
void
rb_refstring_system_init (void)
{
	rb_refstrings = g_hash_table_new_full (g_str_hash, g_str_equal,
					       NULL, (GDestroyNotify) rb_refstring_free);
}

/**
 * rb_refstring_new:
 * @init: string to intern
 *
 * Returns an #RBRefString for the specified string.
 * If one already exists, its reference count is incremented and it is
 * returned.  Otherwise, a new #RBRefString is created and returned.
 *
 * Return value: #RBRefString for @init
 */
RBRefString *
rb_refstring_new (const char *init)
{
	RBRefString *ret;

	g_mutex_lock (&rb_refstrings_mutex);
	ret = g_hash_table_lookup (rb_refstrings, init);

	if (ret) {
		rb_refstring_ref (ret);
		g_mutex_unlock (&rb_refstrings_mutex);
		return ret;
	}

	ret = g_malloc (sizeof (RBRefString) + strlen (init));

	g_strlcpy (ret->value, init, strlen (init) + 1);
	g_atomic_int_set (&ret->refcount, 1);
	ret->folded = NULL;
	ret->sortkey = NULL;

	g_hash_table_insert (rb_refstrings, ret->value, ret);
	g_mutex_unlock (&rb_refstrings_mutex);
	return ret;
}

/**
 * rb_refstring_find:
 * @init: string to find
 *
 * Returns an existing #RBRefString for @init if one exists,
 * otherwise returns NULL.  If an existing refstring is found,
 * its reference count is increased.
 *
 * Return value: existing #RBRefString, or NULL
 */
RBRefString *
rb_refstring_find (const char *init)
{
	RBRefString *ret;

	g_mutex_lock (&rb_refstrings_mutex);
	ret = g_hash_table_lookup (rb_refstrings, init);

	if (ret)
		rb_refstring_ref (ret);

	g_mutex_unlock (&rb_refstrings_mutex);
	return ret;
}

/**
 * rb_refstring_unref:
 * @val: #RBRefString to unref
 *
 * Drops a reference to an #RBRefString.  If this is the last
 * reference, the string will be freed.
 */
void
rb_refstring_unref (RBRefString *val)
{
	if (val == NULL)
		return;

	g_return_if_fail (g_atomic_int_get (&val->refcount) > 0);

	if (g_atomic_int_dec_and_test (&val->refcount)) {
		g_mutex_lock (&rb_refstrings_mutex);
		/* ensure it's still not referenced, as something may have called
		 * rb_refstring_new since we decremented the count */
		if (g_atomic_int_get (&val->refcount) == 0)
			g_hash_table_remove (rb_refstrings, val->value);
		g_mutex_unlock (&rb_refstrings_mutex);
	}
}

/**
 * rb_refstring_system_shutdown:
 *
 * Frees all data associated with the refstring system.
 * Only called on shutdown.
 */
void
rb_refstring_system_shutdown (void)
{
	g_hash_table_destroy (rb_refstrings);
}

/**
 * rb_refstring_ref:
 * @val: a #RBRefString to reference
 *
 * Increases the reference count for an existing #RBRefString.
 * The refstring is returned for convenience.
 *
 * Return value: the same refstring
 */
RBRefString *
rb_refstring_ref (RBRefString *val)
{
	if (val == NULL)
		return NULL;

	g_return_val_if_fail (g_atomic_int_get (&val->refcount) > 0, NULL);

	g_atomic_int_inc (&val->refcount);
	return val;
}

/**
 * rb_refstring_get:
 * @val: an #RBRefString
 *
 * Returns the actual string for a #RBRefString.
 *
 * Return value: underlying string, must not be freed
 */
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

/**
 * rb_refstring_get_folded:
 * @val: an #RBRefString
 *
 * Returns the case-folded version of the string underlying @val.
 * The case-folded string is cached inside the #RBRefString for
 * speed.  See @rb_search_fold for information on case-folding
 * strings.
 *
 * Return value: case-folded string, must not be freed
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

/**
 * rb_refstring_get_sort_key:
 * @val: an #RBRefString
 *
 * Returns the sort key version of the string underlying @val.
 * The sort key string is cached inside the #RBRefString for speed.
 * Sort key strings are not generally human readable, so don't display
 * them anywhere.  See @g_utf8_collate_key_for_filename for information
 * on sort keys.
 *
 * Return value: sort key string, must not be freed.
 */
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
		char *s;

		s = g_utf8_casefold (val->value, -1);
		newstring = g_utf8_collate_key_for_filename (s, -1);
		g_free (s);

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

/**
 * rb_refstring_hash:
 * @p: an #RBRefString
 *
 * Hash function suitable for use with @GHashTable.
 *
 * Return value: hash value for the string underlying @p
 */
guint
rb_refstring_hash (gconstpointer p)
{
	const RBRefString *ref = p;
	return g_str_hash (rb_refstring_get (ref));
}

/**
 * rb_refstring_equal:
 * @ap: an #RBRefString
 * @bp: another #RBRefstring
 *
 * Key equality function suitable for use with #GHashTable.
 * Equality checks for #RBRefString are just pointer comparisons,
 * since there can only be one refstring for a given string.
 *
 * Return value: %TRUE if the strings are the same
 */
gboolean
rb_refstring_equal (gconstpointer ap, gconstpointer bp)
{
	return (ap == bp);
}

GType
rb_refstring_get_type (void)
{
	static GType type = 0;

	if (G_UNLIKELY (type == 0)) {
		type = g_boxed_type_register_static ("RBRefString",
						     (GBoxedCopyFunc)rb_refstring_ref,
						     (GBoxedFreeFunc)rb_refstring_unref);
	}

	return type;
}
