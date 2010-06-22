/*
 *  Copyright (C) 2004 Colin Walters <walters@verbum.org>
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

#include <glib.h>
#include <glib-object.h>

#ifndef __RB_REFSTRING_H
#define __RB_REFSTRING_H

#define RB_TYPE_REFSTRING      (rb_refstring_get_type ())

typedef struct RBRefString RBRefString;

void		rb_refstring_system_init (void);
void		rb_refstring_system_shutdown (void);

RBRefString *	rb_refstring_new (const char *init);
RBRefString *	rb_refstring_find (const char *init);

RBRefString *	rb_refstring_ref (RBRefString *val);
void		rb_refstring_unref (RBRefString *val);

const char *	rb_refstring_get (const RBRefString *val);
const char *	rb_refstring_get_folded (RBRefString *val);
const char *	rb_refstring_get_sort_key (RBRefString *val);

guint rb_refstring_hash (gconstpointer p);
gboolean rb_refstring_equal (gconstpointer ap, gconstpointer bp);

GType rb_refstring_get_type (void);

#endif
