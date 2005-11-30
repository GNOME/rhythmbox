/*
 *  arch-tag: Header for totally random functions that didn't fit elsewhere
 *
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

#ifndef __RB_UTIL_H
#define __RB_UTIL_H

#include <stdarg.h>
#include <glib.h>
#include <gtk/gtkimage.h>
#include <gtk/gtkuimanager.h>

G_BEGIN_DECLS

gboolean rb_true_function (gpointer dummy);
gboolean rb_false_function (gpointer dummy);
gpointer rb_null_function (gpointer dummy);

gboolean rb_gvalue_compare (GValue *a, GValue *b);

int rb_compare_gtimeval (GTimeVal *a, GTimeVal *b);
char * rb_make_duration_string (guint duration);

void rb_gtk_action_popup_menu (GtkUIManager *uimanager, const char *path);

GtkWidget *rb_image_new_from_stock (const gchar *stock_id, GtkIconSize size);

gchar *rb_uri_get_mount_point (const char *uri);
gboolean rb_uri_is_mounted (const char *uri);


void rb_threads_init (void);
gboolean rb_is_main_thread (void);

gchar* rb_search_fold (const char *original);
gchar** rb_string_split_words (const gchar *string);

G_END_DECLS

#endif /* __RB_UTIL_H */
