/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2010  Jonathan Matthew <jonathan@d14n.org>
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

#ifndef RB_VISUALIZER_PAGE_H
#define RB_VISUALIZER_PAGE_H

#include <shell/rb-shell.h>
#include <sources/rb-display-page.h>

#include <clutter/clutter.h>
#include <clutter-gst/clutter-gst.h>
#include <clutter-gtk/clutter-gtk.h>

G_BEGIN_DECLS

typedef struct _RBVisualizerPage RBVisualizerPage;
typedef struct _RBVisualizerPageClass RBVisualizerPageClass;

struct _RBVisualizerPage
{
	RBDisplayPage parent;

	GtkWidget *embed;

	GstElement *sink;
	ClutterActor *texture;

	GtkWidget *fullscreen;

	GtkWidget *popup;
	GtkToggleAction *fullscreen_action;
	gboolean setting_state;
};

struct _RBVisualizerPageClass
{
	RBDisplayPageClass parent_class;
};

GType          rb_visualizer_page_get_type    (void);
void           _rb_visualizer_page_register_type (GTypeModule *module);

#define RB_TYPE_VISUALIZER_PAGE		(rb_visualizer_page_get_type ())
#define RB_VISUALIZER_PAGE(o)		(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VISUALIZER_PAGE, RBVisualizerPage))
#define RB_IS_VISUALIZER_PAGE(o)	(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VISUALIZER_PAGE))
#define RB_VISUALIZER_PAGE_CLASS(k) 	(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VISUALIZER_PAGE, RBVisualizerPageClass))
#define RB_IS_VISUALIZER_PAGE_CLASS(k)  (G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VISUALIZER_PAGE))
#define RB_VISUALIZER_PAGE_GET_CLASS(o) (G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VISUALIZER_PAGE, RBVisualizerPageClass))

RBVisualizerPage        *rb_visualizer_page_new              (GObject *plugin,
							      RBShell *shell,
							      GtkToggleAction *fullscreen,
							      GtkWidget *popup);

G_END_DECLS

#endif /* RB_VISUALIZER_PAGE_H */
