/*
 *  arch-tag: Interface for audio backends capable of dynamic plugging of
 *            gstreamer data tee elements
 *
 *  Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *
 */

#ifndef __RB_PLAYER_GST_DATA_TEE_H
#define __RB_PLAYER_GST_DATA_TEE_H

#include <glib-object.h>
#include <gst/gstelement.h>

G_BEGIN_DECLS


#define RB_TYPE_PLAYER_GST_DATA_TEE         (rb_player_gst_data_tee_get_type ())
#define RB_PLAYER_GST_DATA_TEE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYER_GST_DATA_TEE, RBPlayerGstDataTee))
#define RB_IS_PLAYER_GST_DATA_TEE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYER_GST_DATA_TEE))
#define RB_PLAYER_GST_DATA_TEE_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_PLAYER_GST_DATA_TEE, RBPlayerGstDataTeeIface))

typedef struct _RBPlayerGstDataTee RBPlayerGstDataTee;

typedef struct
{
	GTypeInterface	g_iface;

	/* virtual functions */
	gboolean	(*add_data_tee)		(RBPlayerGstDataTee *player, GstElement *element);
	gboolean	(*remove_data_tee)		(RBPlayerGstDataTee *player, GstElement *element);
} RBPlayerGstDataTeeIface;

GType		rb_player_gst_data_tee_get_type   (void);

gboolean	rb_player_gst_data_tee_add_data_tee (RBPlayerGstDataTee *player, GstElement *element);
gboolean	rb_player_gst_data_tee_remove_data_tee (RBPlayerGstDataTee *player, GstElement *element);

G_END_DECLS

#endif /* __RB_PLAYER_GST_DATA_TEE_H */



