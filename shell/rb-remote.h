/*
 *  arch-tag: Header for Rhythmbox LIRC remote control object
 *
 *  Copyright (C) 2002 James Willcox  <jwillcox@gnome.org>
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

#include <glib.h>
#include <glib-object.h>

#ifndef __RB_REMOTE_H
#define __RB_REMOTE_H

G_BEGIN_DECLS

#define RB_TYPE_REMOTE         (rb_remote_get_type ())
#define RB_REMOTE(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOTE, RBRemote))
#define RB_REMOTE_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOTE, RBRemoteClass))
#define RB_IS_REMOTE(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOTE))
#define RB_IS_REMOTE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOTE))
#define RB_REMOTE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOTE, RBRemoteClass))

typedef struct _RBRemote RBRemote;

typedef enum {
	RB_REMOTE_COMMAND_UNKNOWN,
	RB_REMOTE_COMMAND_PLAY,
	RB_REMOTE_COMMAND_PAUSE,
	RB_REMOTE_COMMAND_SHUFFLE,
	RB_REMOTE_COMMAND_REPEAT,
	RB_REMOTE_COMMAND_NEXT,
	RB_REMOTE_COMMAND_PREVIOUS,
	RB_REMOTE_COMMAND_SEEK_FORWARD,
	RB_REMOTE_COMMAND_SEEK_BACKWARD,
	RB_REMOTE_COMMAND_VOLUME_UP,
	RB_REMOTE_COMMAND_VOLUME_DOWN,
	RB_REMOTE_COMMAND_MUTE,
	RB_REMOTE_COMMAND_QUIT
} RBRemoteCommand;

typedef struct
{
	GObjectClass parent_class;

	void (* button_pressed) (RBRemote *remote, RBRemoteCommand cmd);
} RBRemoteClass;

GType		 rb_remote_get_type   (void);

RBRemote	*rb_remote_new (void);

G_END_DECLS

#endif /* __RB_REMOTE_H */
