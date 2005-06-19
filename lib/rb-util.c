/*
 *  arch-tag: Implementation of totally random functions that didn't fit elsewhere
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

#include "rb-util.h"
#include <gtk/gtk.h>
#include <string.h>
#include <libgnomevfs/gnome-vfs.h>

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

int
rb_gvalue_compare (GValue *a, GValue *b)
{
	int retval;
	const char *stra, *strb;
	
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
	case G_TYPE_BOXED:
	case G_TYPE_OBJECT:
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
		gdk_pixbuf_unref (pixbuf);

		if (!mirror)
			return NULL;

		totem_pixbuf_mirror (mirror);
		gtk_image_set_from_pixbuf (GTK_IMAGE (image), mirror);
		gdk_pixbuf_unref (mirror);

		return image;
	}

	return NULL;
}

void
rb_gtk_action_popup_menu (GtkUIManager *uimanager, const char *path)
{
	GtkWidget *menu;

	menu = gtk_ui_manager_get_widget (uimanager, path);
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, 
			gtk_get_current_event_time ());
}

static GList *
get_mount_points (void)
{
	GnomeVFSVolumeMonitor *monitor;
	GList *volumes;
	GList *it;
	GList *mount_points = NULL;

	monitor = gnome_vfs_get_volume_monitor ();
	/* FIXME: should also get the list of connected drivers (network
	 * shares I assume)
	 */
	volumes = gnome_vfs_volume_monitor_get_mounted_volumes (monitor);

	for (it = volumes; it != NULL; it = it->next) {
		gchar *uri;
		GnomeVFSVolume *volume;

		volume = GNOME_VFS_VOLUME (it->data);
		uri = gnome_vfs_volume_get_activation_uri (volume);
		g_assert (uri != NULL);
		mount_points = g_list_prepend (mount_points, uri);
	}

	g_list_foreach (volumes, (GFunc)gnome_vfs_volume_ref, NULL);
	g_list_free (volumes);

	return mount_points;
}


gchar *
rb_uri_get_mount_point (const char *uri)
{
	GList *mount_points = get_mount_points ();
	GList *it;
	gchar *mount_point = NULL;

	for (it = mount_points; it != NULL; it = it->next) {
		if (g_str_has_prefix (uri, it->data)) {
			if ((mount_point == NULL) || (strlen (mount_point) < strlen (it->data))) {
				g_free (mount_point);
				mount_point = g_strdup (it->data);
			}
		}
	}
	g_list_foreach (mount_points, (GFunc)g_free, NULL);
	g_list_free (mount_points);

	return mount_point;
}

gboolean
rb_uri_is_mounted (const char *uri)
{
	GList *mount_points = get_mount_points ();
	GList *it;
	gboolean found = FALSE;

	if ((uri == NULL) || (*uri == '\0')) {
		return TRUE;
	}

	for (it = mount_points; it != NULL; it = it->next) {
		if (strcmp (it->data, uri) == 0) {
			found = TRUE;
			break;
		}
	}
	g_list_foreach (mount_points, (GFunc)g_free, NULL);
	g_list_free (mount_points);

/*	if (found == FALSE) {
		g_print ("%s not mounted\n", uri);
		}*/

	return found;
}

/* hack to use a recursive mutex for gdk locking */
static GStaticRecMutex _rb_threads_mutex = G_STATIC_REC_MUTEX_INIT;

static void
_rb_threads_lock (void)
{
	g_static_rec_mutex_lock (&_rb_threads_mutex);
}

static void
_rb_threads_unlock (void)
{
	g_static_rec_mutex_unlock (&_rb_threads_mutex);
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


void
rb_threads_init (void)
{
	private_is_primary_thread = g_private_new (NULL);
	g_private_set (private_is_primary_thread, GUINT_TO_POINTER (1));

	gdk_threads_set_lock_functions (G_CALLBACK (_rb_threads_lock),
					G_CALLBACK (_rb_threads_unlock));

	/* not really necessary, but in case it does something besides
	 * set up lock functions some day..
	 */
	gdk_threads_init ();
}
