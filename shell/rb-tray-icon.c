/*
 *  arch-tag: Implementation of Rhythmbox tray icon object
 *
 *  Copyright (C) 2003 Colin Walters <walters@debian.org>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
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

#include <gtk/gtk.h>
#include <config.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libgnome/gnome-i18n.h>
#include <bonobo/bonobo-ui-component.h>
#include <bonobo/bonobo-ui-util.h>
#include <bonobo/bonobo-main.h>
#include <bonobo/bonobo-context.h>
#include <bonobo/bonobo-exception.h>
#include <bonobo/bonobo-window.h>
#include <bonobo/bonobo-control-frame.h>
#include <bonobo/bonobo-control.h>
#include <libgnomevfs/gnome-vfs.h>
#include <libgnomevfs/gnome-vfs-mime-utils.h>

#include "disclosure-widget.h"
#include "rb-tray-icon.h"
#include "rb-stock-icons.h"
#include "rb-bonobo-helpers.h"
#include "rb-debug.h"
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"

static void rb_tray_icon_class_init (RBTrayIconClass *klass);
static void rb_tray_icon_init (RBTrayIcon *shell_player);
static void rb_tray_icon_finalize (GObject *object);
static void rb_tray_icon_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_tray_icon_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_tray_set_visibility (RBTrayIcon *tray, int state);
static void rb_tray_icon_button_press_event_cb (GtkWidget *ebox, GdkEventButton *event,
						RBTrayIcon *icon);
static void rb_tray_icon_scroll_event_cb (GtkWidget *ebox, GdkEvent *event,
						RBTrayIcon *icon);
static void sync_menu (RBTrayIcon *tray);
static void rb_tray_icon_show_window_changed_cb (BonoboUIComponent *component,
					const char *path,
					Bonobo_UIComponent_EventType type,
					const char *state,
					RBTrayIcon *icon);
static void rb_tray_icon_drop_cb (GtkWidget *widget,
				  GdkDragContext *context,
				  gint x,
				  gint y,
				  GtkSelectionData *data,
				  guint info,
				  guint time,
				  RBTrayIcon *icon);

#define CMD_PATH_SHOW_WINDOW    "/commands/ShowWindow"

struct RBTrayIconPrivate
{
	GtkTooltips *tooltips;

	BonoboUIComponent *main_component;
	BonoboUIComponent *tray_component;

	BonoboUIContainer *container;
	BonoboControl *control;

	GtkWindow *main_window;
	GtkWidget *ebox;

	RhythmDB *db;

	int window_x;
	int window_y;
	int window_w;
	int window_h;
	gboolean visible;
};

enum
{
	PROP_0,
	PROP_CONTAINER,
	PROP_COMPONENT,
	PROP_DB,
	PROP_TRAY_COMPONENT,
	PROP_WINDOW,
};

enum
{
	VISIBILITY_HIDDEN,
	VISIBILITY_VISIBLE,
	VISIBILITY_SYNC,
	VISIBILITY_TOGGLE
};

enum
{
	LAST_SIGNAL,
};

static RBBonoboUIListener rb_tray_icon_listeners[] =
{
	RB_BONOBO_UI_LISTENER ("ShowWindow", (BonoboUIListenerFn) rb_tray_icon_show_window_changed_cb),
	RB_BONOBO_UI_LISTENER_END
};

static const GtkTargetEntry target_uri [] = {{ "text/uri-list", 0, 0 }};

static GObjectClass *parent_class = NULL;

GType
rb_tray_icon_get_type (void)
{
	static GType rb_tray_icon_type = 0;

	if (rb_tray_icon_type == 0)
	{
		static const GTypeInfo our_info =
		{
			sizeof (RBTrayIconClass),
			NULL,
			NULL,
			(GClassInitFunc) rb_tray_icon_class_init,
			NULL,
			NULL,
			sizeof (RBTrayIcon),
			0,
			(GInstanceInitFunc) rb_tray_icon_init
		};

		rb_tray_icon_type = g_type_register_static (EGG_TYPE_TRAY_ICON,
							    "RBTrayIcon",
							    &our_info, 0);
	}

	return rb_tray_icon_type;
}

static void
rb_tray_icon_class_init (RBTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	parent_class = g_type_class_peek_parent (klass);

	object_class->finalize = rb_tray_icon_finalize;

	object_class->set_property = rb_tray_icon_set_property;
	object_class->get_property = rb_tray_icon_get_property;

	g_object_class_install_property (object_class,
					 PROP_WINDOW,
					 g_param_spec_object ("window",
							      "GtkWindow",
							      "main GtkWindo",
							      GTK_TYPE_WINDOW,
							      G_PARAM_READWRITE));
	g_object_class_install_property (object_class,
					 PROP_CONTAINER,
					 g_param_spec_object ("container",
							      "BonoboUIContainer",
							      "BonoboUIContainer object",
							      BONOBO_TYPE_UI_CONTAINER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_COMPONENT,
					 g_param_spec_object ("component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_DB,
					 g_param_spec_object ("db",
							      "RhythmDB",
							      "RhythmDB object",
							      RHYTHMDB_TYPE,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_object_class_install_property (object_class,
					 PROP_TRAY_COMPONENT,
					 g_param_spec_object ("tray_component",
							      "BonoboUIComponent",
							      "BonoboUIComponent object",
							      BONOBO_TYPE_UI_COMPONENT,
							      G_PARAM_READABLE));
}

static void
rb_tray_icon_init (RBTrayIcon *icon)
{
	GtkWidget *image;

	rb_debug ("setting up tray icon");

	icon->priv = g_new0 (RBTrayIconPrivate, 1);

	icon->priv->tooltips = gtk_tooltips_new ();

	gtk_tooltips_set_tip (icon->priv->tooltips, GTK_WIDGET (icon),
			      _("Not playing"), NULL);

	icon->priv->ebox = gtk_event_box_new ();
	g_signal_connect_object (G_OBJECT (icon->priv->ebox),
				 "button_press_event",
				 G_CALLBACK (rb_tray_icon_button_press_event_cb),
				 icon, 0);
	g_signal_connect_object (G_OBJECT (icon->priv->ebox),
				 "scroll_event",
				 G_CALLBACK (rb_tray_icon_scroll_event_cb),
				 icon, 0);
	gtk_drag_dest_set (icon->priv->ebox, GTK_DEST_DEFAULT_ALL, target_uri, 1, GDK_ACTION_COPY);
	g_signal_connect_object (G_OBJECT (icon->priv->ebox), "drag_data_received",
				 G_CALLBACK (rb_tray_icon_drop_cb), icon, 0);

	image = gtk_image_new_from_stock (RB_STOCK_TRAY_ICON,
					  GTK_ICON_SIZE_SMALL_TOOLBAR);
	gtk_container_add (GTK_CONTAINER (icon->priv->ebox), image);
	
	icon->priv->control = bonobo_control_new (icon->priv->ebox);
	icon->priv->tray_component = bonobo_control_get_popup_ui_component (icon->priv->control);

	icon->priv->window_x = -1;
	icon->priv->window_y = -1;
	icon->priv->window_w = -1;
	icon->priv->window_h = -1;
	icon->priv->visible = TRUE;

	rb_bonobo_add_listener_list_with_data (icon->priv->tray_component,
						rb_tray_icon_listeners,
						icon);
}

static void
rb_tray_icon_finalize (GObject *object)
{
	RBTrayIcon *tray;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TRAY_ICON (object));

	tray = RB_TRAY_ICON (object);

	g_return_if_fail (tray->priv != NULL);

	g_free (tray->priv);

	G_OBJECT_CLASS (parent_class)->finalize (object);
}

static void
rb_tray_icon_set_property (GObject *object,
			      guint prop_id,
			      const GValue *value,
			      GParamSpec *pspec)
{
	RBTrayIcon *tray = RB_TRAY_ICON (object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		tray->priv->main_window = g_value_get_object (value);
		break;
	case PROP_CONTAINER:
	{
		BonoboControlFrame *frame;
		tray->priv->container = g_value_get_object (value);

		frame = bonobo_control_frame_new (BONOBO_OBJREF (tray->priv->container));
		bonobo_control_frame_bind_to_control (frame, BONOBO_OBJREF (tray->priv->control), NULL);
		gtk_container_add (GTK_CONTAINER (tray), bonobo_control_frame_get_widget (frame));
		gtk_widget_show_all (GTK_WIDGET (tray->priv->ebox));
		break;
	}
	case PROP_COMPONENT:
		tray->priv->main_component = g_value_get_object (value);
		break;
	case PROP_DB:
		tray->priv->db = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void 
rb_tray_icon_get_property (GObject *object,
			      guint prop_id,
			      GValue *value,
			      GParamSpec *pspec)
{
	RBTrayIcon *tray = RB_TRAY_ICON (object);

	switch (prop_id)
	{
	case PROP_WINDOW:
		g_value_set_object (value, tray->priv->main_window);
		break;
	case PROP_CONTAINER:
		g_value_set_object (value, tray->priv->container);
		break;
	case PROP_COMPONENT:
		g_value_set_object (value, tray->priv->main_component);
		break;
	case PROP_TRAY_COMPONENT:
		g_value_set_object (value, tray->priv->tray_component);
		break;
	case PROP_DB:
		g_value_set_object (value, tray->priv->db);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

RBTrayIcon *
rb_tray_icon_new (BonoboUIContainer *container,
		  BonoboUIComponent *component,
		  RhythmDB *db,
		  GtkWindow *window)
{
	RBTrayIcon *tray = g_object_new (RB_TYPE_TRAY_ICON,
					 "title", "Rhythmbox tray icon",
					 "container", container,
					 "component", component,
					 "db", db,
					 "window", window,
					 NULL);
	
	g_return_val_if_fail (tray->priv != NULL, NULL);

	rb_tray_set_visibility (tray, VISIBILITY_SYNC);

	return tray;
}

static void
rb_tray_icon_button_press_event_cb (GtkWidget *ebox, GdkEventButton *event,
				    RBTrayIcon *icon)
{
	/* filter out double, triple clicks */
	if (event->type != GDK_BUTTON_PRESS)
		return;

	rb_debug ("tray button press");

	switch (event->button) {
	case 1:
		rb_tray_set_visibility (icon, VISIBILITY_TOGGLE);
		break;

	case 3:
		/* contextmenu */
		sync_menu (icon);
		bonobo_control_do_popup (icon->priv->control, event->button,
								event->time);
		break;

	default:
		break;
	}
}

static void
rb_tray_icon_scroll_event_cb (GtkWidget *ebox, GdkEvent *event,
				    RBTrayIcon *icon)
{
	float volume;
	rb_debug ("tray button scroll");
	volume = eel_gconf_get_float (CONF_STATE_VOLUME);
	switch(event->scroll.direction) {
	case GDK_SCROLL_UP:
		volume += 0.1;
		if (volume > 1.0)
			volume = 1.0;
		break;
	case GDK_SCROLL_DOWN:
		volume -= 0.1;
		if (volume < 0)
			volume = 0;
		break;
	case GDK_SCROLL_LEFT:
	case GDK_SCROLL_RIGHT:
		break;
	}
	
	rb_debug ("got scroll, setting volume to %f", volume);
	eel_gconf_set_float (CONF_STATE_VOLUME, volume);
}

static void
rb_tray_icon_drop_cb (GtkWidget *widget,
		      GdkDragContext *context,
		      gint x,
		      gint y,
		      GtkSelectionData *data,
		      guint info,
		      guint time,
		      RBTrayIcon *icon)
{
	GList *list, *uri_list, *i;
	GtkTargetList *tlist;
	gboolean ret;

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	list = gnome_vfs_uri_list_parse (data->data);

	if (list == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	uri_list = NULL;

	for (i = list; i != NULL; i = g_list_next (i))
		uri_list = g_list_append (uri_list, gnome_vfs_uri_to_string ((const GnomeVFSURI *) i->data, 0));

	gnome_vfs_uri_list_free (list);

	if (uri_list == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = uri_list; i != NULL; i = i->next) {
		char *uri = i->data;

		if (uri != NULL)
			rhythmdb_add_uri_async (icon->priv->db, uri);

		g_free (uri);
	}

	g_list_free (uri_list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}


static void
sync_menu (RBTrayIcon *tray)
{
	BonoboUIComponent *pcomp;
	BonoboUINode *node;

	pcomp = bonobo_control_get_popup_ui_component (tray->priv->control);
	
	bonobo_ui_component_set (pcomp, "/", "<popups></popups>", NULL);

	node = bonobo_ui_component_get_tree (tray->priv->main_component, "/popups/TrayPopup", TRUE, NULL);

	bonobo_ui_node_set_attr (node, "name", "button3");

	bonobo_ui_component_set_tree (pcomp, "/popups", node, NULL);
	bonobo_ui_node_free (node);

	node = bonobo_ui_component_get_tree (tray->priv->main_component, "/commands", TRUE, NULL);
	bonobo_ui_component_set_tree (pcomp, "/", node, NULL);
	bonobo_ui_node_free (node);
}

static void
rb_tray_icon_show_window_changed_cb (BonoboUIComponent *component,
				const char *path,
				Bonobo_UIComponent_EventType type,
				const char *state,
				RBTrayIcon *icon)
{
	rb_debug ("show window clicked");
	rb_tray_set_visibility (icon, atoi (state));
}

void
rb_tray_icon_set_tooltip (RBTrayIcon *icon, const char *tooltip)
{
	gtk_tooltips_set_tip (icon->priv->tooltips,
			      GTK_WIDGET (icon),
			      tooltip, NULL);
}

static void
rb_tray_restore_main_window (RBTrayIcon *icon)
{
	if ((icon->priv->window_x >= 0 && icon->priv->window_y >= 0) || (icon->priv->window_h >= 0 && icon->priv->window_w >=0 ))
	{
		gtk_widget_realize (GTK_WIDGET (icon->priv->main_window));
		gdk_flush ();

		if (icon->priv->window_x >= 0 && icon->priv->window_y >= 0)
		{
			gtk_window_move (icon->priv->main_window,
					icon->priv->window_x,
					icon->priv->window_y);                                }
		if (icon->priv->window_w >= 0 && icon->priv->window_y >=0)
		{
			gtk_window_resize (icon->priv->main_window,
					icon->priv->window_w,
					icon->priv->window_h);
		}
	}
}

static void
rb_tray_set_visibility (RBTrayIcon *icon, int state)
{
	switch (state)
	{
	case VISIBILITY_HIDDEN:
       	case VISIBILITY_VISIBLE:
		if (icon->priv->visible != state)
			rb_tray_set_visibility (icon, VISIBILITY_TOGGLE);
		break;
        case VISIBILITY_TOGGLE:
		icon->priv->visible = !icon->priv->visible;

		if (icon->priv->visible == TRUE)
		{
			rb_tray_restore_main_window (icon);
			gtk_widget_show (GTK_WIDGET (icon->priv->main_window));
		} else {
			gtk_window_get_position (icon->priv->main_window,
						 &icon->priv->window_x,
						 &icon->priv->window_y);
			gtk_window_get_size (icon->priv->main_window,
					     &icon->priv->window_w,
					     &icon->priv->window_h);
			gtk_widget_hide (GTK_WIDGET (icon->priv->main_window));
		}

	case VISIBILITY_SYNC:
		rb_bonobo_set_active (icon->priv->main_component,
				      CMD_PATH_SHOW_WINDOW,
				      icon->priv->visible);
	}
}
