/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  arch-tag: Implementation of Rhythmbox tray icon object
 *
 *  Copyright (C) 2003,2004 Colin Walters <walters@redhat.com>
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

#include <config.h>

#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <glib/gi18n.h>
#include <gtk/gtk.h>

#include "rb-tray-icon.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-util.h"



/**
 * SECTION:rb-tray-icon
 * @short_description: Notification area icon
 *
 * The tray icon handles a few different forms of input:
 * <itemizedlist>
 *   <listitem>left clicking hides and shows the main window</listitem>
 *   <listitem>right clicking brings up a popup menu</listitem>
 *   <listitem>dropping files on the icon adds them to the library</listitem>
 *   <listitem>scroll events change the playback volume</listitem>
 * </itemizedlist>
 *
 * The tooltip for the tray icon consists of an image, the primary text
 * (displayed in bold large type), and the secondary text (which can
 * contain markup).
 */

static void rb_tray_icon_class_init (RBTrayIconClass *klass);
static void rb_tray_icon_init (RBTrayIcon *icon);
static GObject *rb_tray_icon_constructor (GType type, guint n_construct_properties,
					  GObjectConstructParam *construct_properties);
static void rb_tray_icon_dispose (GObject *object);
static void rb_tray_icon_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_tray_icon_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_tray_icon_button_press_event_cb (GtkWidget *ebox, GdkEventButton *event,
						RBTrayIcon *icon);
static void rb_tray_icon_scroll_event_cb (GtkWidget *ebox,
					  GdkEventScroll *event,
					  RBTrayIcon *icon);
static void rb_tray_icon_drop_cb (GtkWidget *widget,
				  GdkDragContext *context,
				  gint x,
				  gint y,
				  GtkSelectionData *data,
				  guint info,
				  guint time,
				  RBTrayIcon *icon);
static void rb_tray_icon_playing_changed_cb (RBShellPlayer *player,
					     gboolean playing,
					     RBTrayIcon *tray);

struct _RBTrayIconPrivate
{
	RBStatusIconPlugin *plugin;

	GtkWidget *playing_image;
	GtkWidget *not_playing_image;
	GtkWidget *ebox;

	RBShell *shell;
	RBShellPlayer *shell_player;
};

#define RB_TRAY_ICON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_TRAY_ICON, RBTrayIconPrivate))

enum
{
	PROP_0,
	PROP_PLUGIN,
	PROP_SHELL
};

enum
{
	LAST_SIGNAL,
};

static const GtkTargetEntry target_uri [] = {{ "text/uri-list", 0, 0 }};

G_DEFINE_TYPE (RBTrayIcon, rb_tray_icon, EGG_TYPE_TRAY_ICON)

static void
rb_tray_icon_class_init (RBTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_tray_icon_dispose;
	object_class->constructor = rb_tray_icon_constructor;

	object_class->set_property = rb_tray_icon_set_property;
	object_class->get_property = rb_tray_icon_get_property;

	/**
	 * RBTrayIcon:plugin:
	 *
	 * #RBStatusIconPlugin instance
	 */
	g_object_class_install_property (object_class,
					 PROP_PLUGIN,
					 g_param_spec_object ("plugin",
							      "RBStatusIconPlugin",
							      "RBStatusIconPlugin object",
							      RB_TYPE_STATUS_ICON_PLUGIN,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrayIcon:shell:
	 *
	 * #RBShell instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL,
					 g_param_spec_object ("shell",
							      "RBShell",
							      "RBShell object",
							      RB_TYPE_SHELL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBTrayIconPrivate));
}

static void
rb_tray_icon_init (RBTrayIcon *icon)
{
	rb_debug ("setting up tray icon");

	icon->priv = G_TYPE_INSTANCE_GET_PRIVATE ((icon), RB_TYPE_TRAY_ICON, RBTrayIconPrivate);

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

	icon->priv->playing_image = gtk_image_new_from_icon_name (RB_STOCK_TRAY_ICON_PLAYING,
								  GTK_ICON_SIZE_SMALL_TOOLBAR);
	icon->priv->not_playing_image = gtk_image_new_from_icon_name (RB_STOCK_TRAY_ICON_NOT_PLAYING,
								      GTK_ICON_SIZE_SMALL_TOOLBAR);
	g_object_ref (icon->priv->playing_image);
	g_object_ref (icon->priv->not_playing_image);

	gtk_container_add (GTK_CONTAINER (icon->priv->ebox), icon->priv->not_playing_image);

	gtk_container_add (GTK_CONTAINER (icon), icon->priv->ebox);
	gtk_widget_show_all (GTK_WIDGET (icon->priv->ebox));
}

static GObject *
rb_tray_icon_constructor (GType type, guint n_construct_properties,
			  GObjectConstructParam *construct_properties)
{
	RBTrayIcon *tray;
	RBTrayIconClass *klass;

	klass = RB_TRAY_ICON_CLASS (g_type_class_peek (RB_TYPE_TRAY_ICON));

	tray = RB_TRAY_ICON (G_OBJECT_CLASS (rb_tray_icon_parent_class)->constructor
				(type, n_construct_properties,
				 construct_properties));

	g_object_get (tray->priv->shell,
		      "shell-player", &tray->priv->shell_player,
		      NULL);
	g_signal_connect_object (tray->priv->shell_player,
				 "playing-changed",
				 G_CALLBACK (rb_tray_icon_playing_changed_cb),
				 tray, 0);

	g_object_set (tray, "has-tooltip", TRUE, NULL);
	g_signal_connect_object (tray, "query-tooltip",
				 G_CALLBACK (rb_status_icon_plugin_set_tooltip),
				 tray->priv->plugin, 0);

	return G_OBJECT (tray);
}

static void
rb_tray_icon_dispose (GObject *object)
{
	RBTrayIcon *tray;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TRAY_ICON (object));

	tray = RB_TRAY_ICON (object);

	g_return_if_fail (tray->priv != NULL);

	if (tray->priv->shell_player != NULL) {
		g_object_unref (tray->priv->shell_player);
		tray->priv->shell_player = NULL;
	}

	if (tray->priv->playing_image != NULL) {
		g_object_unref (tray->priv->playing_image);
		tray->priv->playing_image = NULL;
	}

	if (tray->priv->not_playing_image != NULL) {
		g_object_unref (tray->priv->not_playing_image);
		tray->priv->not_playing_image = NULL;
	}

	G_OBJECT_CLASS (rb_tray_icon_parent_class)->dispose (object);
}

static void
rb_tray_icon_playing_changed_cb (RBShellPlayer *player, gboolean playing, RBTrayIcon *tray)
{
	GtkWidget *image;

	if (playing)
		image = tray->priv->playing_image;
	else
		image = tray->priv->not_playing_image;

	gtk_container_remove (GTK_CONTAINER (tray->priv->ebox),
			      gtk_bin_get_child (GTK_BIN (tray->priv->ebox)));
	gtk_container_add (GTK_CONTAINER (tray->priv->ebox), image);
	gtk_widget_show_all (GTK_WIDGET (tray->priv->ebox));
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
	case PROP_SHELL:
		tray->priv->shell = g_value_get_object (value);
		break;
	case PROP_PLUGIN:
		tray->priv->plugin = g_value_get_object (value);
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
	case PROP_SHELL:
		g_value_set_object (value, tray->priv->shell);
		break;
	case PROP_PLUGIN:
		g_value_set_object (value, tray->priv->plugin);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_tray_icon_new:
 * @plugin: the #RBStatusIconPlugin
 * @shell: the #RBShell
 *
 * Return value: the #RBTrayIcon
 */
RBTrayIcon *
rb_tray_icon_new (RBStatusIconPlugin *plugin,
		  RBShell *shell)
{
	return g_object_new (RB_TYPE_TRAY_ICON,
			     "title", "Rhythmbox tray icon",
			     "plugin", plugin,
			     "shell", shell,
			     NULL);
}

static void
tray_popup_position_menu (GtkMenu *menu,
			  int *x,
			  int *y,
			  gboolean *push_in,
			  gpointer user_data)
{
        GtkWidget *widget;
        GtkRequisition requisition;
        gint menu_xpos;
        gint menu_ypos;

        widget = GTK_WIDGET (user_data);

        gtk_widget_size_request (GTK_WIDGET (menu), &requisition);

        gdk_window_get_origin (widget->window, &menu_xpos, &menu_ypos);

        menu_xpos += widget->allocation.x;
        menu_ypos += widget->allocation.y;

	if (menu_ypos > gdk_screen_get_height (gtk_widget_get_screen (widget)) / 2)
		menu_ypos -= requisition.height + widget->style->ythickness;
	else
		menu_ypos += widget->allocation.height + widget->style->ythickness;

        *x = menu_xpos;
        *y = menu_ypos;
        *push_in = TRUE;
}

void
rb_tray_icon_menu_popup (RBTrayIcon *tray, GtkWidget *popup, gint button)
{
	gtk_menu_set_screen (GTK_MENU (popup), gtk_widget_get_screen (GTK_WIDGET (tray)));
	gtk_menu_popup (GTK_MENU (popup), NULL, NULL,
			tray_popup_position_menu, tray->priv->ebox, button,
			gtk_get_current_event_time ());
}

static void
rb_tray_icon_button_press_event_cb (GtkWidget *ebox,
				    GdkEventButton *event,
				    RBTrayIcon *tray)
{
	rb_status_icon_plugin_button_press_event (tray->priv->plugin, event);
}

static void
rb_tray_icon_scroll_event_cb (GtkWidget *ebox,
			      GdkEventScroll *event,
			      RBTrayIcon *tray)
{
	rb_status_icon_plugin_scroll_event (tray->priv->plugin, event);
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
	GList *list, *i;
	GtkTargetList *tlist;
	gboolean ret;

	tlist = gtk_target_list_new (target_uri, 1);
	ret = (gtk_drag_dest_find_target (widget, context, tlist) != GDK_NONE);
	gtk_target_list_unref (tlist);

	if (ret == FALSE)
		return;

	list = rb_uri_list_parse ((char *) data->data);

	if (list == NULL) {
		gtk_drag_finish (context, FALSE, FALSE, time);
		return;
	}

	for (i = list; i != NULL; i = i->next) {
		char *uri = i->data;
		if (uri != NULL)
			rb_shell_load_uri (icon->priv->shell, uri, FALSE, NULL);

		g_free (uri);
	}

	g_list_free (list);

	gtk_drag_finish (context, TRUE, FALSE, time);
}

/**
 * rb_tray_icon_get_geom:
 * @icon: the #RBTrayIcon
 * @x: returns the x position of the tray icon
 * @y: returns the y position of the tray icon
 * @width: returns the width of the tray icon
 * @height: returns the height of the tray icon
 *
 * Retrieves the current position and size of the tray icon.
 */
void
rb_tray_icon_get_geom (RBTrayIcon *icon, int *x, int *y, int *width, int *height)
{
	GtkWidget *widget;
	GtkRequisition requisition;

	widget = GTK_WIDGET (icon->priv->ebox);

	gtk_widget_size_request (widget, &requisition);

	gdk_window_get_origin (widget->window, x, y);

	*width = widget->allocation.x;
	*height = widget->allocation.y;
}

void
rb_tray_icon_trigger_tooltip_query (RBTrayIcon *icon)
{
	gtk_widget_trigger_tooltip_query (GTK_WIDGET (icon));
}


gboolean
rb_tray_icon_is_embedded (RBTrayIcon *icon)
{
	return (GTK_PLUG (icon)->socket_window != NULL);
}

void
rb_tray_icon_attach_notification (RBTrayIcon *icon, NotifyNotification *notification)
{
	notify_notification_attach_to_widget (notification, GTK_WIDGET (icon));
}


void
rb_tray_icon_set_visible (RBTrayIcon *icon, gboolean visible)
{
	if (visible)
		gtk_widget_show_all (GTK_WIDGET (icon));
	else
		gtk_widget_hide_all (GTK_WIDGET (icon));
}

