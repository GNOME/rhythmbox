/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include "rb-player-gst-filter.h"
#include "rb-marshal.h"

enum {
	FILTER_INSERTED,
	FILTER_PRE_REMOVE,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = { 0 };

static void
rb_player_gst_filter_interface_init (RBPlayerGstFilterIface *iface)
{
	/**
	 * RBPlayerGstFilter::tee-inserted
	 * @filter: the element which has been inserted
	 *
	 * The 'filter-inserted' signal is emitted when the tee element has been
	 * inserted into the pipeline and fully linked
	 **/
	signals[FILTER_INSERTED] =
		g_signal_new ("filter-inserted",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerGstFilterIface, filter_inserted),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBPlayerGstFilter::tee-pre-remove
	 * @filter: the element which is about to be removed
	 *
	 * The 'filter-pre-remove' signal is emitted immediately before the element
	 * is unlinked and removed from the pipeline
	 **/
	signals[FILTER_PRE_REMOVE] =
		g_signal_new ("filter-pre-remove",
			      G_TYPE_FROM_INTERFACE (iface),
			      G_SIGNAL_RUN_LAST | G_SIGNAL_NO_RECURSE,
			      G_STRUCT_OFFSET (RBPlayerGstFilterIface, filter_pre_remove),
			      NULL, NULL,
			      g_cclosure_marshal_VOID__OBJECT,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

}

GType
rb_player_gst_filter_get_type (void)
{
	static GType our_type = 0;

	if (!our_type) {
		static const GTypeInfo our_info = {
			sizeof (RBPlayerGstFilterIface),
			NULL,	/* base_init */
			NULL,	/* base_finalize */
			(GClassInitFunc)rb_player_gst_filter_interface_init,
			NULL,	/* class_finalize */
			NULL,	/* class_data */
			0,
			0,
			NULL
		};

		our_type = g_type_register_static (G_TYPE_INTERFACE, "RBPlayerGstFilter", &our_info, 0);
	}

	return our_type;
}

gboolean
rb_player_gst_filter_add_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGstFilterIface *iface = RB_PLAYER_GST_FILTER_GET_IFACE (player);

	return iface->add_filter (player, element);
}

gboolean
rb_player_gst_filter_remove_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGstFilterIface *iface = RB_PLAYER_GST_FILTER_GET_IFACE (player);

	return iface->remove_filter (player, element);
}

void
_rb_player_gst_filter_emit_filter_inserted (RBPlayerGstFilter *player, GstElement *filter)
{
	g_signal_emit (player, signals[FILTER_INSERTED], 0, filter);
}

void
_rb_player_gst_filter_emit_filter_pre_remove (RBPlayerGstFilter *player, GstElement *filter)
{
	g_signal_emit (player, signals[FILTER_PRE_REMOVE], 0, filter);
}

