/*
 *  Copyright (C) 2011 Jonathan Matthew  <jonathan@d14n.org>
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

#ifndef RB_DEVICE_SOURCE_H
#define RB_DEVICE_SOURCE_H

#include <glib-object.h>

#include "rb-source.h"

G_BEGIN_DECLS

#define RB_TYPE_DEVICE_SOURCE         (rb_device_source_get_type ())
#define RB_DEVICE_SOURCE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_DEVICE_SOURCE, RBDeviceSource))
#define RB_IS_DEVICE_SOURCE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_DEVICE_SOURCE))
#define RB_DEVICE_SOURCE_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_DEVICE_SOURCE, RBDeviceSourceInterface))

typedef struct _RBDeviceSource RBDeviceSource;
typedef struct _RBDeviceSourceInterface RBDeviceSourceInterface;

struct _RBDeviceSourceInterface
{
	GTypeInterface g_iface;

	gboolean	(*can_eject)		(RBDeviceSource *source);
	void		(*eject)		(RBDeviceSource *source);
};

GType		rb_device_source_get_type	(void);

gboolean	rb_device_source_can_eject	(RBDeviceSource *source);
void		rb_device_source_eject		(RBDeviceSource *source);

guint		rb_device_source_want_uri	(RBSource *source, const char *uri);
gboolean	rb_device_source_uri_is_source	(RBSource *source, const char *uri);

void		rb_device_source_set_display_details (RBDeviceSource *source);

void		rb_device_source_default_eject	(RBDeviceSource *source);

G_END_DECLS

#endif  /* RB_DEVICE_SOURCE_H */
