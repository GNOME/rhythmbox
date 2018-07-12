/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * rhythmbox
 * Copyright (C) Alexandre Rosenfeld 2010 <alexandre.rosenfeld@gmail.com>
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

#ifndef _RB_DACP_PLAYER_H_
#define _RB_DACP_PLAYER_H_

#include <glib-object.h>
#include <libdmapsharing/dmap.h>

G_BEGIN_DECLS

#define RB_TYPE_DACP_PLAYER             (rb_dacp_player_get_type ())
#define RB_DACP_PLAYER(obj)             (G_TYPE_CHECK_INSTANCE_CAST ((obj), RB_TYPE_DACP_PLAYER, RBDACPPlayer))
#define RB_DACP_PLAYER_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST ((klass), RB_TYPE_DACP_PLAYER, RBDACPPlayerClass))
#define RB_IS_DACP_PLAYER(obj)          (G_TYPE_CHECK_INSTANCE_TYPE ((obj), RB_TYPE_DACP_PLAYER))
#define RB_IS_DACP_PLAYER_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE ((klass), RB_TYPE_DACP_PLAYER))
#define RB_DACP_PLAYER_GET_CLASS(obj)   (G_TYPE_INSTANCE_GET_CLASS ((obj), RB_TYPE_DACP_PLAYER, RBDACPPlayerClass))
#define RB_DACP_PLAYER_GET_PRIVATE(o)   (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_DACP_PLAYER, RBDACPPlayerPrivate))

typedef struct _RBDACPPlayerClass RBDACPPlayerClass;
typedef struct _RBDACPPlayer RBDACPPlayer;

typedef struct _RBDACPPlayerPrivate RBDACPPlayerPrivate;

struct _RBDACPPlayerClass
{
	GObjectClass parent_class;

	void (*player_updated) (DmapControlPlayer *player);
};

struct _RBDACPPlayer
{
	GObject parent_instance;

	RBDACPPlayerPrivate *priv;
};

GType rb_dacp_player_get_type (void) G_GNUC_CONST;

RBDACPPlayer *rb_dacp_player_new (RBShell *shell);

void _rb_dacp_player_register_type (GTypeModule *module);

G_END_DECLS

#endif /* _RB_DACP_PLAYER_H_ */
