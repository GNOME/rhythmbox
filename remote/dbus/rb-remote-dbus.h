/*
 *  arch-tag: Header for Rhythmbox DBus remoting
 *
 *  Copyright (C) 2004 Colin Walters <walters@verbum.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2, or (at your option)
 *  any later version.
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

#ifndef __RB_REMOTE_DBUS_H
#define __RB_REMOTE_DBUS_H

#include "rb-remote-proxy.h"

G_BEGIN_DECLS

#define RB_TYPE_REMOTE_DBUS (rb_remote_dbus_get_type ())
#define RB_REMOTE_DBUS(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOTE_DBUS, RBRemoteDBus))
#define RB_REMOTE_DBUS_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOTE_DBUS, RBRemoteDBusClass))
#define RB_IS_REMOTE_DBUS(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOTE_DBUS))
#define RB_IS_REMOTE_DBUS_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOTE_DBUS))
#define RB_REMOTE_DBUS_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOTE_DBUS, RBRemoteDBusClass))

typedef struct RBRemoteDBusPrivate RBRemoteDBusPrivate;

typedef struct
{
        GObject parent;

	RBRemoteDBusPrivate *priv;
} RBRemoteDBus;

typedef struct
{
        GObjectClass parent_class;
} RBRemoteDBusClass;

typedef enum
{
	RB_REMOTE_DBUS_ERROR_SERVICE_UNAVAILABLE,
	RB_REMOTE_DBUS_ERROR_ACQUISITION_FAILURE,
	RB_REMOTE_DBUS_ERROR_ACTIVATION_FAILURE
} RBRemoteDBusError;

#define RB_REMOTE_DBUS_ERROR rb_remote_dbus_error_quark ()

GQuark rb_remote_dbus_error_quark (void);

GType rb_remote_dbus_get_type (void);

RBRemoteDBus * rb_remote_dbus_new (void);

gboolean rb_remote_dbus_activate (RBRemoteDBus *bonobo);

G_END_DECLS

#endif /* __RB_REMOTE_DBUS_H */
