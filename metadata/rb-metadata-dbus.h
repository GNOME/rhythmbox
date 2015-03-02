/*
 *  Copyright (C) 2006 Jonathan Matthew <jonathan@kaolin.hn.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

/*
 * Common definitions and functions for out-of-process metadata reader.
 */

#ifndef __RB_METADATA_DBUS_H
#define __RB_METADATA_DBUS_H

G_BEGIN_DECLS

#define RB_METADATA_DBUS_NAME		"org.gnome.Rhythmbox3.Metadata"
#define RB_METADATA_DBUS_OBJECT_PATH	"/org/gnome/Rhythmbox3/MetadataService"
#define RB_METADATA_DBUS_INTERFACE	"org.gnome.Rhythmbox3.Metadata"

/* Timeouts in milliseconds.  If a metadata operation takes longer than this,
 * the metadata process will be killed and the operation will fail.  We use a
 * longer timeout for save operations because they involve reading and writing
 * the entire file being modified, which can take some time on slow devices.
 */
#define RB_METADATA_DBUS_TIMEOUT	(15000)
#define RB_METADATA_SAVE_DBUS_TIMEOUT	(120000)

extern const char *rb_metadata_iface_xml;

GVariantBuilder *rb_metadata_dbus_get_variant_builder (RBMetaData *md);

G_END_DECLS

#endif /* __RB_METADATA_DBUS_H */
