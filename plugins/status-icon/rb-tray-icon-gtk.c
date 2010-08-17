/* -*- Mode: C; tab-width: 8; indent-tabs-mode: t; c-basic-offset: 8 -*-
 *
 *  Copyright (C) 2009 Jonathan Matthew  <jonathan@d14n.org>
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

#include "rb-tray-icon-gtk.h"
#include "rb-stock-icons.h"
#include "rb-debug.h"
#include "rb-shell-player.h"
#include "rb-util.h"



/*
 * SECTION:rb-tray-icon
 * @short_description: Notification area icon
 *
 * The tray icon handles a few different forms of input:
 * <itemizedlist>
 *   <listitem>left clicking hides and shows the main window</listitem>
 *   <listitem>right clicking brings up a popup menu</listitem>
 *   <listitem>scroll events change the playback volume</listitem>
 * </itemizedlist>
 *
 * The tooltip for the tray icon consists of an image, the primary text
 * (displayed in bold large type), and the secondary text (which can
 * contain markup).
 */

static void rb_tray_icon_class_init (RBTrayIconClass *klass);
static void rb_tray_icon_init (RBTrayIcon *tray);
static void rb_tray_icon_constructed (GObject *object);
static void rb_tray_icon_dispose (GObject *object);
static void rb_tray_icon_set_property (GObject *object,
					  guint prop_id,
					  const GValue *value,
					  GParamSpec *pspec);
static void rb_tray_icon_get_property (GObject *object,
					  guint prop_id,
					  GValue *value,
					  GParamSpec *pspec);
static void rb_tray_icon_button_press_event_cb (GtkStatusIcon *status_icon,
						GdkEventButton *event,
						RBTrayIcon *tray);
static void rb_tray_icon_scroll_event_cb (GtkStatusIcon *status_icon,
					  GdkEventScroll *event,
					  RBTrayIcon *tray);

struct _RBTrayIconPrivate
{
	RBStatusIconPlugin *plugin;

	GtkStatusIcon *icon;

	RBShellPlayer *shell_player;
};

enum
{
	PROP_0,
	PROP_PLUGIN,
	PROP_SHELL_PLAYER
};

enum
{
	LAST_SIGNAL,
};

G_DEFINE_TYPE (RBTrayIcon, rb_tray_icon, G_TYPE_OBJECT)

static void
rb_tray_icon_class_init (RBTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_tray_icon_dispose;
	object_class->constructed = rb_tray_icon_constructed;

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
	 * RBTrayIcon:shell-player:
	 *
	 * #RBShellPlayer instance
	 */
	g_object_class_install_property (object_class,
					 PROP_SHELL_PLAYER,
					 g_param_spec_object ("shell-player",
							      "RBShellPlayer",
							      "RBShellPlayer object",
							      RB_TYPE_SHELL_PLAYER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	g_type_class_add_private (klass, sizeof (RBTrayIconPrivate));
}

static void
rb_tray_icon_init (RBTrayIcon *tray)
{
	rb_debug ("setting up tray icon");

	tray->priv = G_TYPE_INSTANCE_GET_PRIVATE ((tray), RB_TYPE_TRAY_ICON, RBTrayIconPrivate);

	tray->priv->icon = gtk_status_icon_new_from_icon_name (RB_STOCK_TRAY_ICON);
	gtk_status_icon_set_visible (tray->priv->icon, FALSE);
	gtk_status_icon_set_title (GTK_STATUS_ICON (tray->priv->icon), _("Rhythmbox"));

	g_signal_connect_object (tray->priv->icon, "button-press-event",
				 G_CALLBACK (rb_tray_icon_button_press_event_cb),
				 tray, 0);
	g_signal_connect_object (tray->priv->icon,
				 "scroll_event",
				 G_CALLBACK (rb_tray_icon_scroll_event_cb),
				 tray, 0);

}

static void
rb_tray_icon_constructed (GObject *object)
{
	RBTrayIcon *tray;

	RB_CHAIN_GOBJECT_METHOD (rb_tray_icon_parent_class, constructed, object);
	tray = RB_TRAY_ICON (object);

	gtk_status_icon_set_has_tooltip (tray->priv->icon, TRUE);
	g_signal_connect_object (tray->priv->icon, "query-tooltip",
				 G_CALLBACK (rb_status_icon_plugin_set_tooltip),
				 tray->priv->plugin, 0);
}

static void
rb_tray_icon_dispose (GObject *object)
{
	RBTrayIcon *tray;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TRAY_ICON (object));

	tray = RB_TRAY_ICON (object);

	g_return_if_fail (tray->priv != NULL);

	if (tray->priv->icon != NULL) {
		g_object_unref (tray->priv->icon);
		tray->priv->icon = NULL;
	}

	G_OBJECT_CLASS (rb_tray_icon_parent_class)->dispose (object);
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
	case PROP_SHELL_PLAYER:
		tray->priv->shell_player = g_value_get_object (value);
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
	case PROP_SHELL_PLAYER:
		g_value_set_object (value, tray->priv->shell_player);
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
 * @shell_player: the #RBShellPlayer
 *
 * Return value: the #RBTrayIcon
 */
RBTrayIcon *
rb_tray_icon_new (RBStatusIconPlugin *plugin,
		  RBShellPlayer *shell_player)
{
	return g_object_new (RB_TYPE_TRAY_ICON,
			     "plugin", plugin,
			     "shell-player", shell_player,
			     NULL);
}

static void
rb_tray_icon_button_press_event_cb (GtkStatusIcon *status_icon,
				    GdkEventButton *event,
				    RBTrayIcon *tray)
{
	rb_status_icon_plugin_button_press_event (tray->priv->plugin, event);
}

static void
rb_tray_icon_scroll_event_cb (GtkStatusIcon *status_icon,
			      GdkEventScroll *event,
			      RBTrayIcon *tray)
{
	rb_status_icon_plugin_scroll_event (tray->priv->plugin, event);
}

void
rb_tray_icon_menu_popup (RBTrayIcon *tray, GtkWidget *popup, gint button)
{
	gtk_menu_set_screen (GTK_MENU (popup), gtk_status_icon_get_screen (tray->priv->icon));
	gtk_menu_popup (GTK_MENU (popup), NULL, NULL,
			gtk_status_icon_position_menu, tray->priv->icon, button,
			gtk_get_current_event_time ());
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
rb_tray_icon_get_geom (RBTrayIcon *tray, int *x, int *y, int *width, int *height)
{
	GdkRectangle area;

	if (gtk_status_icon_get_geometry (tray->priv->icon, NULL, &area, NULL)) {
		*x = area.x;
		*y = area.y;
		*width = area.width;
		*height = area.height;
	}
}

void
rb_tray_icon_trigger_tooltip_query (RBTrayIcon *tray)
{
	GdkScreen *screen;
	GdkDisplay *display;

	screen = gtk_status_icon_get_screen (tray->priv->icon);
	display = gdk_screen_get_display (screen);
	gtk_tooltip_trigger_tooltip_query (display);
}


gboolean
rb_tray_icon_is_embedded (RBTrayIcon *tray)
{
	return gtk_status_icon_is_embedded (tray->priv->icon);
}

#if defined(HAVE_NOTIFY)
void
rb_tray_icon_attach_notification (RBTrayIcon *tray, NotifyNotification *notification)
{
	notify_notification_attach_to_status_icon (notification, tray->priv->icon);
}
#endif

void
rb_tray_icon_set_visible (RBTrayIcon *tray, gboolean visible)
{
	gtk_status_icon_set_visible (tray->priv->icon, visible);
}
