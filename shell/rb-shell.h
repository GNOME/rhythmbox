/*
 *  arch-tag: Header for main Rhythmbox shell
 *
 *  Copyright (C) 2002 Jorn Baayen
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

#ifndef __RB_SHELL_H
#define __RB_SHELL_H

#include "Rhythmbox.h"

#include <bonobo/bonobo-control.h>
#include <bonobo/bonobo-object.h>

G_BEGIN_DECLS

#define RB_SHELL_OAFIID "OAFIID:GNOME_Rhythmbox"
#define RB_FACTORY_OAFIID "OAFIID:GNOME_Rhythmbox_Factory"

#define RB_TYPE_SHELL         (rb_shell_get_type ())
#define RB_SHELL(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_SHELL, RBShell))
#define RB_SHELL_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_SHELL, RBShellClass))
#define RB_IS_SHELL(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_SHELL))
#define RB_IS_SHELL_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_SHELL))
#define RB_SHELL_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_SHELL, RBShellClass))

typedef struct RBShellPrivate RBShellPrivate;

typedef struct
{
        BonoboObject parent;

	RBShellPrivate *priv;
} RBShell;

typedef struct
{
        BonoboObjectClass parent_class;

        POA_GNOME_Rhythmbox__epv epv;
} RBShellClass;

GType		rb_shell_get_type  (void);

RBShell *	rb_shell_new       (void);

void		rb_shell_construct (RBShell *shell);

/* utilities */

char *		rb_shell_corba_exception_to_string (CORBA_Environment *ev);

G_END_DECLS

#endif /* __RB_SHELL_H */
