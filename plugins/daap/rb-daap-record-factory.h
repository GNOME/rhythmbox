/*
 *  RBDAAPRecord factory class
 *
 *  Copyright (C) 2008 W. Michael Petullo <mike@flyn.org>
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

#ifndef __RB_DAAP_RECORD_FACTORY
#define __RB_DAAP_RECORD_FACTORY

#include <libdmapsharing/dmap.h>

G_BEGIN_DECLS

#define RB_TYPE_DAAP_RECORD_FACTORY         (rb_daap_record_factory_get_type ())
#define RB_DAAP_RECORD_FACTORY(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), \
				               RB_TYPE_DAAP_RECORD_FACTORY, RBDAAPRecordFactory))
#define RB_DAAP_RECORD_FACTORY_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), \
				               RB_TYPE_DAAP_RECORD_FACTORY, RBDAAPRecordFactoryClass))
#define RB_IS_DAAP_RECORD_FACTORY(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), \
				               RB_TYPE_DAAP_RECORD_FACTORY))
#define RB_IS_DAAP_RECORD_FACTORY_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), \
				               RB_TYPE_DAAP_RECORD_FACTORY_CLASS))
#define RB_DAAP_RECORD_FACTORY_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), \
				               RB_TYPE_DAAP_RECORD_FACTORY, RBDAAPRecordFactoryClass))

typedef struct RBDAAPRecordFactoryPrivate RBDAAPRecordFactoryPrivate;

typedef struct {
	GObject parent;
} RBDAAPRecordFactory;

typedef struct {
	GObjectClass parent;
} RBDAAPRecordFactoryClass;

GType                  rb_daap_record_factory_get_type (void);

RBDAAPRecordFactory *rb_daap_record_factory_new      (void);

DmapRecord            *rb_daap_record_factory_create   (DmapRecordFactory *factory, gpointer user_data, GError **error);

void                   _rb_daap_record_factory_register_type (GTypeModule *module);

#endif /* __RB_DAAP_RECORD_FACTORY */

G_END_DECLS
