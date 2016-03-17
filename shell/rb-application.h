/*
 *  Copyright (C) 2012  Jonathan Matthew <jonathan@d14n.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The Rhythmbox authors hereby grant permission for non-GPL compatible
 *  GStreamer plugins to be used and distributed together with GStreamer
 *  and Rhythmbox. This permission is above and beyond the permissions granted
 *  by the GPL license by which Rhythmbox is covered. If you modify this code
 *  you may extend this exception to your version of the code, but you are not
 *  obligated to do so. If you do not wish to do so, delete this exception
 *  statement from your version.
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

#include <gtk/gtk.h>

#ifndef RB_APPLICATION_H
#define RB_APPLICATION_H

#define RB_TYPE_APPLICATION         (rb_application_get_type ())
#define RB_APPLICATION(o)           (G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_APPLICATION, RBApplication))
#define RB_APPLICATION_CLASS(k)     (G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_APPLICATION, RBApplicationClass))
#define RB_IS_APPLICATION(o)        (G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_APPLICATION))
#define RB_IS_APPLICATION_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_APPLICATION))
#define RB_APPLICATION_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_APPLICATION, RBApplicationClass))

typedef struct _RBApplication RBApplication;
typedef struct _RBApplicationClass RBApplicationClass;
typedef struct _RBApplicationPrivate RBApplicationPrivate;

struct _RBApplication
{
	GtkApplication parent;

	RBApplicationPrivate *priv;
};

struct _RBApplicationClass
{
	GtkApplicationClass parent_class;
};

GType		rb_application_get_type (void);

GApplication *	rb_application_new (void);

int		rb_application_run (RBApplication *app, int argc, char **argv);

void		rb_application_link_shared_menus (RBApplication *app, GMenu *menu);
void		rb_application_set_menu_accelerators (RBApplication *app, GMenuModel *menu, gboolean enable);

void		rb_application_add_shared_menu (RBApplication *app, const char *name, GMenuModel *menu);
GMenuModel *	rb_application_get_shared_menu (RBApplication *app, const char *name);

GMenuModel *	rb_application_get_plugin_menu (RBApplication *app, const char *menu);
void		rb_application_add_plugin_menu_item (RBApplication *app, const char *menu, const char *id, GMenuItem *item);
void		rb_application_remove_plugin_menu_item (RBApplication *app, const char *menu, const char *id);

void		rb_application_add_accelerator (RBApplication *app, const char *accel, const char *action, GVariant *parameter);
gboolean	rb_application_activate_key (RBApplication *app, GdkEventKey *event);

G_END_DECLS

#endif /* RB_APPLICATION_H */
