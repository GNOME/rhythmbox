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

#include "rhythmdb.h"
#include "rb-daap-record-factory.h"
#include "rb-daap-record.h"

DMAPRecord *
rb_daap_record_factory_create  (DMAPRecordFactory *factory,
				gpointer user_data)
{
	DAAPRecord *record;

	record = DAAP_RECORD (rb_daap_record_new ((RhythmDBEntry *) user_data));

	return (DMAP_RECORD (record));
}

static void
rb_daap_record_factory_init (RBDAAPRecordFactory *factory)
{
}

static void
rb_daap_record_factory_class_init (RBDAAPRecordFactoryClass *klass)
{
}

static void
rb_daap_record_factory_interface_init (gpointer iface, gpointer data)
{
	DMAPRecordFactoryInterface *factory = iface;

	g_assert (G_TYPE_FROM_INTERFACE (factory) == TYPE_DMAP_RECORD_FACTORY);

	factory->create = rb_daap_record_factory_create;
}

G_DEFINE_TYPE_WITH_CODE (RBDAAPRecordFactory, rb_daap_record_factory, G_TYPE_OBJECT, 
			 G_IMPLEMENT_INTERFACE (TYPE_DMAP_RECORD_FACTORY,
					        rb_daap_record_factory_interface_init))

RBDAAPRecordFactory *
rb_daap_record_factory_new (void)
{
	RBDAAPRecordFactory *factory;

	factory = RB_DAAP_RECORD_FACTORY (g_object_new (RB_TYPE_DAAP_RECORD_FACTORY, NULL));

	return factory;
}
