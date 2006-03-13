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

typedef struct RBRefString RBRefString;


void		rb_refstring_system_init (void);
void		rb_refstring_system_shutdown (void);

RBRefString *	rb_refstring_new (const char *init);

RBRefString *	rb_refstring_ref (RBRefString *val);
void		rb_refstring_unref (RBRefString *val);


const char *	rb_refstring_get (const RBRefString *val);
const char *	rb_refstring_get_folded (RBRefString *val);
const char *	rb_refstring_get_sort_key (RBRefString *val);

guint rb_refstring_hash (gconstpointer p);
gboolean rb_refstring_equal (gconstpointer ap, gconstpointer bp);

#endif
