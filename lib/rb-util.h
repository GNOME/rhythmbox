/*
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

#ifndef __RB_UTIL_H
#define __RB_UTIL_H

#include <stdarg.h>
#include <glib.h>
#include <gio/gio.h>
#include <gtk/gtk.h>

G_BEGIN_DECLS

/* Offsets for seeking forward, and rewinding in seconds */
#define FFWD_OFFSET 20
#define RWD_OFFSET 10

#define RB_CHAIN_GOBJECT_METHOD(klass, method, instance) \
	if (G_OBJECT_CLASS (klass)->method != NULL) \
		G_OBJECT_CLASS (klass)->method (instance);

gboolean rb_true_function (gpointer dummy);
gboolean rb_false_function (gpointer dummy);
gpointer rb_null_function (gpointer dummy);
gpointer rb_copy_function (gpointer data);

int rb_gvalue_compare (GValue *a, GValue *b);

int rb_compare_gtimeval (GTimeVal *a, GTimeVal *b);
int rb_safe_strcmp (const char *a, const char *b);
char *rb_make_time_string (guint seconds);
char *rb_make_duration_string (guint duration);
char *rb_make_elapsed_time_string (guint elapsed, guint duration, gboolean show_remaining);

void rb_threads_init (void);
gboolean rb_is_main_thread (void);

gchar* rb_search_fold (const char *original);
gchar** rb_string_split_words (const gchar *string);

gboolean rb_string_list_equal (GList *a, GList *b);
gboolean rb_string_list_contains (GList *list, const char *s);
GList* rb_string_list_copy (GList *list);

void rb_list_deep_free (GList *list);
void rb_list_destroy_free (GList *list, GDestroyNotify destroyer);
void rb_slist_deep_free (GSList *list);

gboolean rb_str_in_strv (const char *needle, const char **haystack);

GList* rb_collate_hash_table_keys (GHashTable *table);
GList* rb_collate_hash_table_values (GHashTable *table);

GList* rb_uri_list_parse (const char *uri_list);

gboolean rb_signal_accumulator_object_handled (GSignalInvocationHint *hint,
					       GValue *return_accu,
					       const GValue *handler_return,
					       gpointer dummy);
gboolean rb_signal_accumulator_value_handled (GSignalInvocationHint *hint,
					      GValue *return_accu,
					      const GValue *handler_return,
					      gpointer dummy);
gboolean rb_signal_accumulator_boolean_or (GSignalInvocationHint *hint,
					   GValue *return_accu,
					   const GValue *handler_return,
					   gpointer dummy);
gboolean rb_signal_accumulator_value_array (GSignalInvocationHint *hint,
					    GValue *return_accu,
					    const GValue *handler_return,
					    gpointer dummy);
void rb_value_array_append_data (GArray *array, GType type, ...);
void rb_value_free (GValue *val); /* g_value_unset, g_slice_free */

void rb_assert_locked (GMutex *mutex);

void rb_set_tree_view_column_fixed_width (GtkWidget *treeview,
					  GtkTreeViewColumn *column,
					  GtkCellRenderer *renderer,
					  const char **strings,
					  int padding);

GdkPixbuf *rb_scale_pixbuf_to_size (GdkPixbuf *pixbuf,
				    GtkIconSize size);

typedef void (*RBDelayedSyncFunc)(GSettings *settings, gpointer data);

void rb_settings_delayed_sync (GSettings *settings, RBDelayedSyncFunc sync_func, gpointer data, GDestroyNotify destroy);

void rb_menu_update_link (GMenu *menu, const char *link_attr, GMenuModel *target);

G_END_DECLS

#endif /* __RB_UTIL_H */
