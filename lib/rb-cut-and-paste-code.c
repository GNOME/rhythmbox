/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2000 Eazel, Inc.
 *  Copyright (C) 2002 Jorn Baayen
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU Library General Public License
 *  as published by the Free Software Foundation; either version 2, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this program; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 *  Authors: John Sullivan <sullivan@eazel.com>
 *           Jorn Baayen
 */

#include <config.h>

#include <string.h>
#include <glib/gi18n.h>

#include "rb-cut-and-paste-code.h"

/*LGPL'd*/
GdkPixbuf *
eel_create_colorized_pixbuf (GdkPixbuf *src,
    			     int red_value,
			     int green_value,
			     int blue_value)
{
	int i, j;
	int width, height, has_alpha, src_row_stride, dst_row_stride;
	guchar *target_pixels;
	guchar *original_pixels;
	guchar *pixsrc;
	guchar *pixdest;
	GdkPixbuf *dest;

	g_return_val_if_fail (gdk_pixbuf_get_colorspace (src) == GDK_COLORSPACE_RGB, NULL);
	g_return_val_if_fail ((!gdk_pixbuf_get_has_alpha (src)
		       	      && gdk_pixbuf_get_n_channels (src) == 3)
			      || (gdk_pixbuf_get_has_alpha (src)
			      && gdk_pixbuf_get_n_channels (src) == 4), NULL);
	g_return_val_if_fail (gdk_pixbuf_get_bits_per_sample (src) == 8, NULL);

	dest = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (src),
			       gdk_pixbuf_get_has_alpha (src),
			       gdk_pixbuf_get_bits_per_sample (src),
			       gdk_pixbuf_get_width (src),
			       gdk_pixbuf_get_height (src));

	has_alpha = gdk_pixbuf_get_has_alpha (src);
	width = gdk_pixbuf_get_width (src);
	height = gdk_pixbuf_get_height (src);
	src_row_stride = gdk_pixbuf_get_rowstride (src);
	dst_row_stride = gdk_pixbuf_get_rowstride (dest);
	target_pixels = gdk_pixbuf_get_pixels (dest);
	original_pixels = gdk_pixbuf_get_pixels (src);

	for (i = 0; i < height; i++) {
		pixdest = target_pixels + i*dst_row_stride;
		pixsrc = original_pixels + i*src_row_stride;
		for (j = 0; j < width; j++) {
			*pixdest++ = (*pixsrc++ * red_value) >> 8;
			*pixdest++ = (*pixsrc++ * green_value) >> 8;
			*pixdest++ = (*pixsrc++ * blue_value) >> 8;
			if (has_alpha) {
				*pixdest++ = *pixsrc++;
			}
		}
	}
	return dest;
}

/* Based on evolution/mail/message-list.c:filter_date() */
char *
rb_utf_friendly_time (time_t date)
{
	GDateTime *datetime, *now, *yesterday;
	int d, m, y;
	int nd, nm, ny;
	int yd, ym, yy;
	const char *format = NULL;
	char *str = NULL;

	if (date == 0) {
		return g_strdup (_("Never"));
	}

	now = g_date_time_new_now_local ();
	datetime = g_date_time_new_from_unix_local (date);

	g_date_time_get_ymd (datetime, &y, &m, &d);
	g_date_time_get_ymd (now, &ny, &nm, &nd);

	if (y == ny && m == nm && d == nd) {
		/* Translators: "friendly time" string for the current day, strftime format. like "Today 12:34 am" */
		format = _("Today %I:%M %p");
	}

	if (format == NULL) {
		yesterday = g_date_time_add_days (now, -1);

		g_date_time_get_ymd (yesterday, &yy, &ym, &yd);
		if (y == yy && m == ym && d == yd) {
			/* Translators: "friendly time" string for the previous day,
			 * strftime format. e.g. "Yesterday 12:34 am"
			 */
			format = _("Yesterday %I:%M %p");
		}
		g_date_time_unref (yesterday);
	}

	if (format == NULL) {
		int i;
		for (i = 2; i < 7; i++) {
			yesterday = g_date_time_add_days (now, -i);
			g_date_time_get_ymd (yesterday, &yy, &ym, &yd);
			if (y == yy && m == ym && d == yd) {
				/* Translators: "friendly time" string for a day in the current week,
				 * strftime format. e.g. "Wed 12:34 am"
				 */
				format = _("%a %I:%M %p");
				g_date_time_unref (yesterday);
				break;
			}
			g_date_time_unref (yesterday);
		}
	}

	if (format == NULL) {
		if (y == ny) {
			/* Translators: "friendly time" string for a day in the current year,
			 * strftime format. e.g. "Feb 12 12:34 am"
			 */
			format = _("%b %d %I:%M %p");
		} else {
			/* Translators: "friendly time" string for a day in a different year,
			 * strftime format. e.g. "Feb 12 1997"
			 */
			format = _("%b %d %Y");
		}
	}

	if (format != NULL) {
		str = g_date_time_format (datetime, format);
	}

	if (str == NULL) {
		/* impossible time or broken locale settings */
		str = g_strdup (_("Unknown"));
	}

	g_date_time_unref (datetime);
	g_date_time_unref (now);

	return str;
}

/* Copied from eel-vfs-extensions.c from eel CVS HEAD on 2004-05-09
 * This function is (C) 1999, 2000 Eazel, Inc.
 */
char *
rb_make_valid_utf8 (const char *name, char substitute)
{
	GString *string;
	const char *remainder, *invalid;
	int remaining_bytes, valid_bytes;

	string = NULL;
	remainder = name;
	remaining_bytes = strlen (name);

	while (remaining_bytes != 0) {
		if (g_utf8_validate (remainder, remaining_bytes, &invalid)) {
			break;
		}
		valid_bytes = invalid - remainder;

		if (string == NULL) {
			string = g_string_sized_new (remaining_bytes);
		}
		g_string_append_len (string, remainder, valid_bytes);
		g_string_append_c (string, substitute);

		remaining_bytes -= valid_bytes + 1;
		remainder = invalid + 1;
	}

	if (string == NULL) {
		return g_strdup (name);
	}

	g_string_append (string, remainder);
	g_assert (g_utf8_validate (string->str, -1, NULL));

	return g_string_free (string, FALSE);
}
