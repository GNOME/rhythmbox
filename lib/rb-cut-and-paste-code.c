/*
 *  arch-tag: File containing code cut and pasted from elsewhere
 *
 *  Copyright (C) 2002 Jorn Baayen
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

#include <config.h>
#include <libgnome/gnome-i18n.h>
#include <string.h>

#include "rb-cut-and-paste-code.h"

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

/* Legal conversion specifiers, as specified in the C standard. */
#define C_STANDARD_STRFTIME_CHARACTERS "aAbBcdHIjmMpSUwWxXyYZ"
#define C_STANDARD_NUMERIC_STRFTIME_CHARACTERS "dHIjmMSUwWyY"

/**
 * eel_strdup_strftime:
 *
 * Cover for standard date-and-time-formatting routine strftime that returns
 * a newly-allocated string of the correct size. The caller is responsible
 * for g_free-ing the returned string.
 *
 * Besides the buffer management, there are two differences between this
 * and the library strftime:
 *
 *   1) The modifiers "-" and "_" between a "%" and a numeric directive
 *      are defined as for the GNU version of strftime. "-" means "do not
 *      pad the field" and "_" means "pad with spaces instead of zeroes".
 *   2) Non-ANSI extensions to strftime are flagged at runtime with a
 *      warning, so it's easy to notice use of the extensions without
 *      testing with multiple versions of the library.
 *
 * @format: format string to pass to strftime. See strftime documentation
 * for details.
 * @time_pieces: date/time, in struct format.
 * 
 * Return value: Newly allocated string containing the formatted time.
 **/
char *
eel_strdup_strftime (const char *format, struct tm *time_pieces)
{
	GString *string;
	const char *remainder, *percent;
	char code[3], buffer[512];
	char *piece, *result, *converted;
	size_t string_length;
	gboolean strip_leading_zeros, turn_leading_zeros_to_spaces;

	/* Format could be translated, and contain UTF-8 chars,
	 * so convert to locale encoding which strftime uses */
	converted = g_locale_from_utf8 (format, -1, NULL, NULL, NULL);
	g_return_val_if_fail (converted != NULL, NULL);
	
	string = g_string_new ("");
	remainder = converted;

	/* Walk from % character to % character. */
	for (;;) {
		percent = strchr (remainder, '%');
		if (percent == NULL) {
			g_string_append (string, remainder);
			break;
		}
		g_string_append_len (string, remainder,
				     percent - remainder);

		/* Handle the "%" character. */
		remainder = percent + 1;
		switch (*remainder) {
		case '-':
			strip_leading_zeros = TRUE;
			turn_leading_zeros_to_spaces = FALSE;
			remainder++;
			break;
		case '_':
			strip_leading_zeros = FALSE;
			turn_leading_zeros_to_spaces = TRUE;
			remainder++;
			break;
		case '%':
			g_string_append_c (string, '%');
			remainder++;
			continue;
		case '\0':
			g_warning ("Trailing %% passed to eel_strdup_strftime");
			g_string_append_c (string, '%');
			continue;
		default:
			strip_leading_zeros = FALSE;
			turn_leading_zeros_to_spaces = FALSE;
			break;
		}
		
		if (strchr (C_STANDARD_STRFTIME_CHARACTERS, *remainder) == NULL) {
			g_warning ("eel_strdup_strftime does not support "
				   "non-standard escape code %%%c",
				   *remainder);
		}

		/* Convert code to strftime format. We have a fixed
		 * limit here that each code can expand to a maximum
		 * of 512 bytes, which is probably OK. There's no
		 * limit on the total size of the result string.
		 */
		code[0] = '%';
		code[1] = *remainder;
		code[2] = '\0';
		string_length = strftime (buffer, sizeof (buffer),
					  code, time_pieces);
		if (string_length == 0) {
			/* We could put a warning here, but there's no
			 * way to tell a successful conversion to
			 * empty string from a failure.
			 */
			buffer[0] = '\0';
		}

		/* Strip leading zeros if requested. */
		piece = buffer;
		if (strip_leading_zeros || turn_leading_zeros_to_spaces) {
			if (strchr (C_STANDARD_NUMERIC_STRFTIME_CHARACTERS, *remainder) == NULL) {
				g_warning ("eel_strdup_strftime does not support "
					   "modifier for non-numeric escape code %%%c%c",
					   remainder[-1],
					   *remainder);
			}
			if (*piece == '0') {
				do {
					piece++;
				} while (*piece == '0');
				if (!g_ascii_isdigit (*piece)) {
				    piece--;
				}
			}
			if (turn_leading_zeros_to_spaces) {
				memset (buffer, ' ', piece - buffer);
				piece = buffer;
			}
		}
		remainder++;

		/* Add this piece. */
		g_string_append (string, piece);
	}
	
	/* Convert the string back into utf-8. */
	result = g_locale_to_utf8 (string->str, -1, NULL, NULL, NULL);

	g_string_free (string, TRUE);
	g_free (converted);

	return result;
}
