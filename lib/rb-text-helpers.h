/*
 *  Copyright (C) 2010  Uri Sivan
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

#ifndef __RB_TEXT_HELPERS_H
#define __RB_TEXT_HELPERS_H

#include <glib.h>
#include <pango/pango-bidi-type.h>
#include <pango/pango-attributes.h>

G_BEGIN_DECLS

gboolean rb_text_direction_conflict (PangoDirection dir1, PangoDirection dir2);

PangoDirection rb_text_common_direction (const char *first, ...);

char *rb_text_cat (PangoDirection base_dir, ...);

PangoAttrList *rb_text_numeric_get_pango_attr_list (void);

G_END_DECLS

#endif /* __RB_TEXT_HELPERS_H */
