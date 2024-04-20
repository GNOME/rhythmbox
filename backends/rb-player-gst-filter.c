/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2006  James Livingston  <doclivingston@gmail.com>
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
 *  along with this program; if not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <config.h>

#include "rb-player-gst-filter.h"

/**
 * SECTION:rbplayergstfilter
 * @short_description: player interface for inserting filter elements
 * @include: rb-player-gst-filter.h
 *
 * This interface allows a caller to add filter elements to the GStreamer playback
 * pipeline.
 */

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
	 * RBPlayerGstFilter::filter-inserted:
	 * @player: the #RBPlayerGstFilter implementation
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
			      NULL,
			      G_TYPE_NONE,
			      1, G_TYPE_OBJECT);

	/**
	 * RBPlayerGstFilter::filter-pre-remove:
	 * @player: the #RBPlayerGstFilter implementation
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
			      NULL,
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

/**
 * rb_player_gst_filter_add_filter:
 * @player: #RBPlayerGstFilter implementation
 * @element: new filter element (or bin) to add
 *
 * Adds a new filter to the playback pipeline.  The filter may not be
 * inserted immediately.  The 'filter-inserted' signal will be emitted
 * when this actually happens.
 *
 * Return value: TRUE if the filter will be added
 */
gboolean
rb_player_gst_filter_add_filter (RBPlayerGstFilter *player, GstElement *element)
{
	RBPlayerGstFilterIface *iface = RB_PLAYER_GST_FILTER_GET_IFACE (player);

	return iface->add_filter (player, element);
}

/**
 * rb_player_gst_filter_remove_filter:
 * @player: #RBPlayerGstFilter implementation
 * @element: the filter element (or bin) to remove
 *
 * Removes a filter from the playback pipeline.  The filter may not be
 * removed immediately.  The 'filter-pre-remove' signal will be emitted
 * immediately before this actually happens.
 *
 * Return value: TRUE if the filter was found and will be removed
 */
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

