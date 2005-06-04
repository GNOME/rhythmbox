/*
 *  arch-tag: Header for reference-counted string functions 
 *
 *  Copyright (C) 2004 Colin Walters <walters@verbum.org>
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

#include <glib.h>

#ifndef __RB_REFSTRING_H
#define __RB_REFSTRING_H

typedef struct
{
	guint32 refcount;
	char *folded;
	char *sortkey;
	char value[1];
} RBRefString;

void				rb_refstring_system_init (void);

G_INLINE_FUNC RBRefString *	rb_refstring_new (const char *init);

RBRefString *			rb_refstring_new_full (const char *init, gboolean compute_sortdata);

G_INLINE_FUNC RBRefString *	rb_refstring_ref (RBRefString *val);

G_INLINE_FUNC void		rb_refstring_unref (RBRefString *val);

G_INLINE_FUNC const char *	rb_refstring_get (const RBRefString *val);

G_INLINE_FUNC const char *	rb_refstring_get_folded (const RBRefString *val);

G_INLINE_FUNC const char *	rb_refstring_get_sort_key (const RBRefString *val);

G_INLINE_FUNC guint		rb_refstring_hash (gconstpointer a);

G_INLINE_FUNC gboolean		rb_refstring_equal (gconstpointer a, gconstpointer b);

void				rb_refstring_system_shutdown (void);

#if defined (G_CAN_INLINE) || defined (G_HAVE_INLINE ) || defined (__RB_REFSTRING_C__)

#ifndef __RB_REFSTRING_C__
extern GHashTable *rb_refstrings;
#endif

G_INLINE_FUNC RBRefString *
rb_refstring_new (const char *init)
{
	return rb_refstring_new_full (init, TRUE);
}

G_INLINE_FUNC RBRefString *
rb_refstring_ref (RBRefString *val)
{
	val->refcount++;
	return val;
}

G_INLINE_FUNC void
rb_refstring_unref (RBRefString *val)
{
	if (--val->refcount == 0)
		g_hash_table_remove (rb_refstrings, val);
}

G_INLINE_FUNC const char *
rb_refstring_get (const RBRefString *val)
{
	return val->value;
}

G_INLINE_FUNC const char *
rb_refstring_get_folded (const RBRefString *val)
{
	return val->folded;
}

G_INLINE_FUNC const char *
rb_refstring_get_sort_key (const RBRefString *val)
{
	return val->sortkey;
}

G_INLINE_FUNC guint
rb_refstring_hash (gconstpointer p)
{
	const RBRefString *ref = p;
	return g_str_hash (rb_refstring_get (ref));
}

G_INLINE_FUNC gboolean
rb_refstring_equal (gconstpointer ap, gconstpointer bp)
{
	const RBRefString *a = ap;
	const RBRefString *b = bp;
	return g_str_equal (rb_refstring_get (a),
			    rb_refstring_get (b));
}

#endif

#endif
