/*
 * arch-tag: Nautilus context menu constructor
 * Copyright (C) 2000, 2001 Eazel, Inc
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 *
 * Author: Maciej Stachowiak
 */

/* libmain.c - object activation infrastructure for shared library
   version of tree view. */

#include <config.h>
#include <string.h>
#include "rb-nautilus-context-menu.h"
#include <bonobo.h>
#include <bonobo-activation/bonobo-activation.h>

#define RB_NAUTILUS_CONTEXT_MENU_IID  "OAFIID:Rhythmbox_Nautilus_Context_Menu_Item"


static CORBA_Object
rhythmbox_shlib_make_object (PortableServer_POA poa,
			 const char *iid,
			 gpointer impl_ptr,
			 CORBA_Environment *ev)
{
	RbNautilusContextMenu *rncm;

	if (strcmp (iid, RB_NAUTILUS_CONTEXT_MENU_IID) != 0) {
                return CORBA_OBJECT_NIL;
        }

	rncm = RB_NAUTILUS_CONTEXT_MENU (g_object_new (TYPE_RB_NAUTILUS_CONTEXT_MENU, NULL));

	bonobo_activation_plugin_use (poa, impl_ptr);

	return CORBA_Object_duplicate (BONOBO_OBJREF (rncm), ev);
}

static const BonoboActivationPluginObject plugin_list[] = {
	{ RB_NAUTILUS_CONTEXT_MENU_IID, rhythmbox_shlib_make_object },
	{ NULL }
};

const BonoboActivationPlugin Bonobo_Plugin_info = {
	plugin_list,
	"Rhythmbox Nautilus Context Menu"
};
