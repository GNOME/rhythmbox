/*
 *  arch-tag: Header file for various utility functions for using Bonobo
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

#include <bonobo/bonobo-ui-component.h>

#ifndef __RB_BONOBO_HELPERS_H
#define __RB_BONOBO_HELPERS_H

G_BEGIN_DECLS

typedef struct
{
	const char        *cname;
	BonoboUIListenerFn cb;
} RBBonoboUIListener;

#define RB_BONOBO_UI_LISTENER(name,cb) { (name), (cb) }
#define RB_BONOBO_UI_LISTENER_END      { NULL, NULL }

void     rb_bonobo_add_listener_list_with_data (BonoboUIComponent *component,
						const RBBonoboUIListener *list,
						gpointer user_data);

void     rb_bonobo_set_label     (BonoboUIComponent *component,
			          const char *path,
			          const char *label);
void     rb_bonobo_set_tip       (BonoboUIComponent *component,
			          const char *path,
			          const char *tip);
void     rb_bonobo_set_icon      (BonoboUIComponent *component,
			          const char *path,
			          const char *stock_icon);
void     rb_bonobo_set_verb      (BonoboUIComponent *component,
			          const char *path,
			          const char *verb);
void     rb_bonobo_set_sensitive (BonoboUIComponent *component,
			          const char *path,
			          gboolean sensitive);
gboolean rb_bonobo_get_sensitive (BonoboUIComponent *component,
			          const char *path);
void     rb_bonobo_set_active    (BonoboUIComponent *component,
			          const char *path,
			          gboolean active);
gboolean rb_bonobo_get_active    (BonoboUIComponent *component,
			          const char *path);
void     rb_bonobo_set_visible   (BonoboUIComponent *component,
				  const char *path,
				  gboolean visible);
gboolean rb_bonobo_get_visible   (BonoboUIComponent *component,
				  const char *path);
void     rb_bonobo_set_look      (BonoboUIComponent *component,
				  const char *path,
				  const char *look);

void	rb_bonobo_show_popup (GtkWidget *source, const char *path);

G_END_DECLS

#endif /* __RB_BONOBO_HELPERS_H */
