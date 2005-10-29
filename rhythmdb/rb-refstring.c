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
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 */

#include <config.h>

#include <glib.h>
#include <string.h>
#include "rb-util.h"
#include "rb-cut-and-paste-code.h"

GHashTable *rb_refstrings;
GMutex *rb_refstrings_mutex;

#define	G_IMPLEMENT_INLINES 1
#define	__RB_REFSTRING_C__
#include "rb-refstring.h"
#undef G_IMPLEMENT_INLINES

static void
rb_refstring_free (RBRefString *refstr)
{
	refstr->refcount = 0xdeadbeef;
	g_free (refstr->folded);
	g_free (refstr->sortkey);
	g_free (refstr);
}

void
rb_refstring_system_init (void)
{
	rb_refstrings_mutex = g_mutex_new ();

	rb_refstrings = g_hash_table_new_full (g_str_hash, g_str_equal,
					       NULL, (GDestroyNotify) rb_refstring_free);
}

RBRefString *
rb_refstring_new_full (const char *init, gboolean compute_sortdata)
{
	RBRefString *ret;

	g_mutex_lock (rb_refstrings_mutex);
	ret = g_hash_table_lookup (rb_refstrings, init);
	g_mutex_unlock (rb_refstrings_mutex);

	if (ret) {
		rb_refstring_ref (ret);
		return ret;
	}
	
	ret = g_malloc (sizeof (RBRefString) + strlen (init));
	
	ret->refcount = 1;
	
	if (compute_sortdata) {
		ret->folded = rb_search_fold (init);
		ret->sortkey = rb_utf8_collate_key_for_filename (ret->folded, -1);
	} else {
		ret->folded = NULL;
		ret->sortkey = NULL;
	}
	
	strcpy (ret->value, init);

	g_mutex_lock (rb_refstrings_mutex);
	g_hash_table_insert (rb_refstrings, ret->value, ret);
	g_mutex_unlock (rb_refstrings_mutex);
	return ret;
}

void
rb_refstring_unref (RBRefString *val)
{
	if (!val)
		return;

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
	
