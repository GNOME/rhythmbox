/*
 *  Copyright (C) 2006 James Livingston <doclivingston@gmail.com>
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

#ifndef __RB_PLAYER_GST_TEE_H
#define __RB_PLAYER_GST_TEE_H

#include <glib-object.h>
#include <gst/gstelement.h>

G_BEGIN_DECLS


#define RB_TYPE_PLAYER_GST_TEE         (rb_player_gst_tee_get_type ())
#define RB_PLAYER_GST_TEE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_PLAYER_GST_TEE, RBPlayerGstTee))
#define RB_IS_PLAYER_GST_TEE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_PLAYER_GST_TEE))
#define RB_PLAYER_GST_TEE_GET_IFACE(o) (G_TYPE_INSTANCE_GET_INTERFACE ((o), RB_TYPE_PLAYER_GST_TEE, RBPlayerGstTeeIface))

typedef struct _RBPlayerGstTee RBPlayerGstTee;
typedef struct _RBPlayerGstTeeIface RBPlayerGstTeeIface;

struct _RBPlayerGstTeeIface
{
	GTypeInterface	g_iface;

	/* virtual functions */
	gboolean	(*add_tee)		(RBPlayerGstTee *player, GstElement *element);
	gboolean	(*remove_tee)		(RBPlayerGstTee *player, GstElement *element);

	/* signals */
	void		(*tee_inserted)		(RBPlayerGstTee *player, GstElement *tee);
	void		(*tee_pre_remove)	(RBPlayerGstTee *player, GstElement *tee);
};

GType		rb_player_gst_tee_get_type   (void);

gboolean	rb_player_gst_tee_add_tee (RBPlayerGstTee *player, GstElement *element);
gboolean	rb_player_gst_tee_remove_tee (RBPlayerGstTee *player, GstElement *element);

/* only to be called by implementing classes */
void _rb_player_gst_tee_emit_tee_inserted (RBPlayerGstTee *player, GstElement *tee);
void _rb_player_gst_tee_emit_tee_pre_remove (RBPlayerGstTee *player, GstElement *tee);

G_END_DECLS

#endif /* __RB_PLAYER_GST_TEE_H */



