/*
 *  arch-tag: Header for Rhythmbox Bonobo remoting
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
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 *  
 */

#ifndef __RB_REMOTE_BONOBO_H
#define __RB_REMOTE_BONOBO_H

#include "bonobo/Rhythmbox.h"
#include "rb-remote-proxy.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-object.h>

G_BEGIN_DECLS

#define RB_REMOTE_BONOBO_OAFIID "OAFIID:GNOME_Rhythmbox"
#define RB_FACTORY_OAFIID "OAFIID:GNOME_Rhythmbox_Factory"

#define RB_TYPE_REMOTE_BONOBO (rb_remote_bonobo_get_type ())
#define RB_REMOTE_BONOBO(o) (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_REMOTE_BONOBO, RBRemoteBonobo))
#define RB_REMOTE_BONOBO_CLASS(k) (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_REMOTE_BONOBO, RBRemoteBonoboClass))
#define RB_IS_REMOTE_BONOBO(o) (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_REMOTE_BONOBO))
#define RB_IS_REMOTE_BONOBO_CLASS(k) (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_REMOTE_BONOBO))
#define RB_REMOTE_BONOBO_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_REMOTE_BONOBO, RBRemoteBonoboClass))

typedef struct RBRemoteBonoboPrivate RBRemoteBonoboPrivate;

typedef struct
{
        BonoboObject parent;

	RBRemoteBonoboPrivate *priv;
} RBRemoteBonobo;

typedef struct
{
        BonoboObjectClass parent_class;

        POA_GNOME_Rhythmbox__epv epv;
} RBRemoteBonoboClass;

typedef enum
{
	RB_REMOTE_BONOBO_ERROR_SERVICE_UNAVAILABLE,
	RB_REMOTE_BONOBO_ERROR_ACQUISITION_FAILURE,
	RB_REMOTE_BONOBO_ERROR_ACTIVATION_FAILURE
} RBRemoteBonoboError;

#define RB_REMOTE_BONOBO_ERROR rb_remote_bonobo_error_quark ()

GQuark rb_remote_bonobo_error_quark (void);

GType rb_remote_bonobo_get_type	(void);

void  rb_remote_bonobo_preinit (void);


RBRemoteBonobo * rb_remote_bonobo_new (void);

gboolean rb_remote_bonobo_activate (RBRemoteBonobo *bonobo);

gboolean rb_remote_bonobo_acquire (RBRemoteBonobo *bonobo,
				   RBRemoteProxy *proxy,
				   GError **error);

G_END_DECLS

#endif /* __RB_REMOTE_BONOBO_H */
