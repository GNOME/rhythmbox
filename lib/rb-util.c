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
#include <string.h>

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
GtkWidget *rb_image_new_from_stock (const gchar *stock_id, GtkIconSize size)
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
