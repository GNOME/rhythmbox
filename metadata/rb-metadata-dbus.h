/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grants permission for non-GPL compatible
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

/*
 * Common definitions and functions for out-of-process metadata reader.
 */

#ifndef __RB_METADATA_DBUS_H
#define __RB_METADATA_DBUS_H

#include <dbus/dbus.h>

G_BEGIN_DECLS

#define RB_METADATA_DBUS_NAME		"org.gnome.rhythmbox.Metadata"
#define RB_METADATA_DBUS_OBJECT_PATH	"/org/gnome/rhythmbox/MetadataService"
#define RB_METADATA_DBUS_INTERFACE	"org.gnome.rhythmbox.Metadata"

/* Timeout in milliseconds.  If a metadata operation takes longer than this,
 * the metadata process will be killed and the operation will fail.
 */
#define RB_METADATA_DBUS_TIMEOUT	(15000)

gboolean	rb_metadata_dbus_get_boolean (DBusMessageIter *iter,
					      gboolean *value);
gboolean	rb_metadata_dbus_get_uint32 (DBusMessageIter *iter,
					     guint32 *value);
gboolean	rb_metadata_dbus_get_string (DBusMessageIter *iter,
					     gchar **value);
gboolean	rb_metadata_dbus_get_strv (DBusMessageIter *iter,
					   char ***strv);

gboolean	rb_metadata_dbus_add_strv (DBusMessageIter *iter,
					   char **strv);

gboolean	rb_metadata_dbus_add_to_message (RBMetaData *md,
						 DBusMessageIter *iter);
gboolean	rb_metadata_dbus_read_from_message (RBMetaData *md,
						    GHashTable *metadata,
						    DBusMessageIter *iter);

G_END_DECLS

#endif /* __RB_METADATA_DBUS_H */
