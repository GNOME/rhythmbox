/*
 * arch-tag: Nautilus context menu main implementation
 * Copyright (C) 2003 Yann Rouillard
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more av.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author:  Yann Rouillard  <y.rouillard@laposte.net>
 */

#include <config.h>

#include <string.h>
#include <gtk/gtk.h>
#include <libgnomevfs/gnome-vfs-utils.h>
#include <libbonobo.h>
#include "rb-nautilus-context-menu.h"
#include "../corba/Rhythmbox.h"

#include <stdlib.h>


static void
impl_Bonobo_Listener_event (PortableServer_Servant servant,
			    const CORBA_char *event_name,
			    const CORBA_any *args,
			    CORBA_Environment *ev)
{
	RbNautilusContextMenu *rncm;
	const CORBA_sequence_CORBA_string *list;
	gint i;
	CORBA_Object obj;

	rncm = RB_NAUTILUS_CONTEXT_MENU (bonobo_object_from_servant (servant));

	if (!CORBA_TypeCode_equivalent (args->_type, TC_CORBA_sequence_CORBA_string, ev))
		return;

	list = (CORBA_sequence_CORBA_string *) args->_value;

	g_return_if_fail (rncm != NULL);
	g_return_if_fail (list != NULL);

	obj = bonobo_activation_activate ("repo_ids.has ('IDL:GNOME/Rhythmbox:1.0')", NULL, 0, NULL, ev);

	if (ev->_major != CORBA_NO_EXCEPTION)
		return;

	g_return_if_fail (obj != CORBA_OBJECT_NIL);

	for (i = 0; i < list->_length; i++)
		GNOME_Rhythmbox_addToLibrary(obj, list->_buffer[i], ev);

	CORBA_Object_release (obj, ev);
}


/* initialize the class */
static void
rb_nautilus_context_menu_class_init (RbNautilusContextMenuClass *class)
{
	POA_Bonobo_Listener__epv *epv = &class->epv;
	epv->event = impl_Bonobo_Listener_event;
}


static void
rb_nautilus_context_menu_init (RbNautilusContextMenu *rncm)
{
}


BONOBO_TYPE_FUNC_FULL (RbNautilusContextMenu,
		       Bonobo_Listener,
		       BONOBO_TYPE_OBJECT,
		       rb_nautilus_context_menu);
