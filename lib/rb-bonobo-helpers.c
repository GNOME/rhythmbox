/*
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
 *  $Id$
 */

#include <stdlib.h>

#include "rb-bonobo-helpers.h"

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
