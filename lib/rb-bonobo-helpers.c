/*
 *  arch-tag: Implementation of various utility functions for using Bonobo
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

#include <stdlib.h>

#include "rb-bonobo-helpers.h"
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-window.h>
#include <gtk/gtk.h>

void
rb_bonobo_set_label (BonoboUIComponent *component,
		     const char *path,
		     const char *label)
{
	bonobo_ui_component_set_prop (component, path, "label", label, NULL);
}

void
rb_bonobo_set_tip (BonoboUIComponent *component,
		   const char *path,
	           const char *tip)
{
	bonobo_ui_component_set_prop (component, path, "tip", tip, NULL);
}

void
rb_bonobo_set_icon (BonoboUIComponent *component,
		    const char *path,
		    const char *stock_icon)
{
	bonobo_ui_component_set_prop (component, path, "pixname", stock_icon, NULL);
}

void
rb_bonobo_set_verb (BonoboUIComponent *component,
		    const char *path,
		    const char *verb)
{
	bonobo_ui_component_set_prop (component, path, "verb", verb, NULL);
}

void
rb_bonobo_set_sensitive (BonoboUIComponent *component,
			 const char *path,
			 gboolean sensitive)
{
	bonobo_ui_component_set_prop (component, path, "sensitive",
				      sensitive ? "1" : "0", NULL);
}

gboolean
rb_bonobo_get_sensitive (BonoboUIComponent *component,
			 const char *path)
{
	gboolean ret = FALSE;
	char *prop;

	prop = bonobo_ui_component_get_prop (component, path, "sensitive", NULL);
	if (prop != NULL)
		ret = atoi (prop);
	g_free (prop);

	return ret;
}

void
rb_bonobo_set_active (BonoboUIComponent *component,
		      const char *path,
		      gboolean active)
{
	bonobo_ui_component_set_prop (component, path, "state",
				      active ? "1" : "0", NULL);
}

gboolean
rb_bonobo_get_active (BonoboUIComponent *component,
		      const char *path)
{
	gboolean ret = FALSE;
	char *prop;

	prop = bonobo_ui_component_get_prop (component, path, "state", NULL);
	if (prop != NULL)
		ret = atoi (prop);
	g_free (prop);

	return ret;
}

void
rb_bonobo_set_visible (BonoboUIComponent *component,
		       const char *path,
		       gboolean visible)
{
	bonobo_ui_component_set_prop (component, path, "hidden",
				      visible ? "0" : "1", NULL);
}

gboolean
rb_bonobo_get_visible (BonoboUIComponent *component,
		       const char *path)
{
	gboolean ret = FALSE;
	char *prop;

	prop = bonobo_ui_component_get_prop (component, path, "hidden", NULL);
	if (prop != NULL)
		ret = atoi (prop);
	g_free (prop);

	return ret;
}

void
rb_bonobo_set_look (BonoboUIComponent *component,
		    const char *path,
		    const char *look)
{
	bonobo_ui_component_set_prop (component, path, "look",
				      look, NULL);
}

void
rb_bonobo_add_listener_list_with_data (BonoboUIComponent *component,
				       const RBBonoboUIListener *list,
				       gpointer user_data)
{
	const RBBonoboUIListener *l;

	g_return_if_fail (list != NULL);
	g_return_if_fail (BONOBO_IS_UI_COMPONENT (component));

	bonobo_object_ref (BONOBO_OBJECT (component));

	for (l = list; l != NULL && l->cname != NULL; l++)
	{
		bonobo_ui_component_add_listener (component,
						  l->cname,
						  l->cb,
						  user_data);
	}
	
	bonobo_object_unref (BONOBO_OBJECT (component));
}


void
rb_bonobo_show_popup (GtkWidget *source, const char *path)
{
	GtkWidget *menu;
	GtkWidget *window;
	
	window = gtk_widget_get_ancestor (GTK_WIDGET (source), 
					  BONOBO_TYPE_WINDOW);
	menu = gtk_menu_new ();
	gtk_widget_show (menu);
	
	bonobo_window_add_popup (BONOBO_WINDOW (window), GTK_MENU (menu), 
			         path);
		
	gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL,
			3, gtk_get_current_event_time ());

	gtk_object_sink (GTK_OBJECT (menu));
}
