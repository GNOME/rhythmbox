/*
 *  arch-tag: Implementation of Rhythmbox Glade XML utility functions
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

#include <gmodule.h>

#include "rb-glade-helpers.h"
#include "rb-file-helpers.h"

static void glade_signal_connect_func (const gchar *cb_name, GObject *obj, 
			               const gchar *signal_name, const gchar *signal_data,
			               GObject *conn_obj, gboolean conn_after,
			               gpointer user_data);

GladeXML *
rb_glade_xml_new (const char *file,
	          const char *root,
		  gpointer user_data)
{
	GladeXML *xml;

	xml = glade_xml_new (rb_file (file), root, NULL);

	glade_xml_signal_autoconnect_full (xml,
					   (GladeXMLConnectFunc) glade_signal_connect_func,
					   user_data);

	return xml;
}

static void
glade_signal_connect_func (const gchar *cb_name, GObject *obj, 
			   const gchar *signal_name, const gchar *signal_data,
			   GObject *conn_obj, gboolean conn_after,
			   gpointer user_data)
{
	/** Module with all the symbols of the program */
	static GModule *mod_self = NULL;
	gpointer handler_func;

 	/* initialize gmodule */
	if (mod_self == NULL)
	{
		mod_self = g_module_open (NULL, 0);
		g_assert (mod_self != NULL);
	}

	if (g_module_symbol (mod_self, cb_name, &handler_func))
	{
		/* found callback */
		if (conn_obj)
		{
			if (conn_after)
			{
				g_signal_connect_object
                                        (obj, signal_name, 
                                         handler_func, conn_obj,
                                         G_CONNECT_AFTER);
			}
			else
			{
				g_signal_connect_object
                                        (obj, signal_name, 
                                         handler_func, conn_obj,
                                         G_CONNECT_SWAPPED);
			}
		}
		else
		{
			/* no conn_obj; use standard connect */
			gpointer data = NULL;
			
			data = user_data;
			
			if (conn_after)
			{
				g_signal_connect_after
					(obj, signal_name, 
					 handler_func, data);
			}
			else
			{
				g_signal_connect
					(obj, signal_name, 
					 handler_func, data);
			}
		}
	}
	else
	{
		g_warning("callback function not found: %s", cb_name);
	}
}
