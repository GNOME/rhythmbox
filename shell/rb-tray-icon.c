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
#include "eel-gconf-extensions.h"
#include "rb-preferences.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-util.h"

#define TRAY_ICON_DEFAULT_TOOLTIP _("Music Player")
#define DEFAULT_TOOLTIP_ICON "" /*"gnome-media-player"*/

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
 *
 * The tooltip is shown after the mouse pointer has been within the tray
 * icon area for half a second.  This delay is only reset once the pointer
 * has been outside the tray icon area for more than one second.
 *
 * The tray icon can also display notification bubbles (using libnotify).
 * These contain the same set of elements as the tooltip.  The tooltip is
 * suppressed while a notification bubble is being displayed.  A GConf setting
 * allows the user to disable notification bubbles.
 */

static void rb_tray_icon_class_init (RBTrayIconClass *klass);
static void rb_tray_icon_init (RBTrayIcon *shell_player);
static GObject *rb_tray_icon_constructor (GType type, guint n_construct_properties,
					  GObjectConstructParam *construct_properties);
static void rb_tray_icon_dispose (GObject *object);
static void rb_tray_icon_finalize (GObject *object);
static void rb_tray_icon_sync_action (RBShell *shell,
				      gboolean visible,
				      RBTrayIcon *tray);
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
static void rb_tray_icon_scroll_event_cb (GtkWidget *ebox, GdkEvent *event,
						RBTrayIcon *icon);
static void rb_tray_icon_show_window_changed_cb (GtkAction *action,
						 RBTrayIcon *icon);
static void rb_tray_icon_show_notifications_changed_cb (GtkAction *action,
							RBTrayIcon *icon);
static void rb_tray_icon_drop_cb (GtkWidget *widget,
				  GdkDragContext *context,
				  gint x,
				  gint y,
				  GtkSelectionData *data,
				  guint info,
				  guint time,
				  RBTrayIcon *icon);
static void rb_tray_icon_suppress_tooltips (RBTrayIcon *icon, guint duration);
static GdkPixbuf* rb_tray_icon_create_blank_image (RBTrayIcon *icon);
static gboolean tray_icon_tooltip_cb (GtkWidget  *widget,
				      gint        x,
				      gint        y,
				      gboolean    keyboard_tooltip,
				      GtkTooltip *tooltip,
				      RBTrayIcon *icon);

struct RBTrayIconPrivate
{
	GtkUIManager *ui_manager;
	GtkActionGroup *actiongroup;

	GtkWidget *playing_image;
	GtkWidget *not_playing_image;
	GtkWidget *ebox;

	char *primary_text;
	char *secondary_markup;
	GdkPixbuf *pixbuf;

	gboolean tooltips_suppressed;
	guint tooltip_unsuppress_id;

	RBShell *shell;
	RBShellPlayer *shell_player;

	gboolean show_notifications;
};

#define RB_TRAY_ICON_GET_PRIVATE(o) (G_TYPE_INSTANCE_GET_PRIVATE ((o), RB_TYPE_TRAY_ICON, RBTrayIconPrivate))

enum
{
	PROP_0,
	PROP_UI_MANAGER,
	PROP_ACTION_GROUP,
	PROP_SHELL
};

enum
{
	LAST_SIGNAL,
};

static GtkToggleActionEntry rb_tray_icon_toggle_entries [] =
{
	{ "TrayShowWindow", NULL, N_("_Show Music Player"), NULL,
	  N_("Choose music to play"),
	  G_CALLBACK (rb_tray_icon_show_window_changed_cb) },
	{ "TrayShowNotifications", NULL, N_("Show N_otifications"), NULL,
	  N_("Show notifications of song changes and other events"),
	  G_CALLBACK (rb_tray_icon_show_notifications_changed_cb) }
};
static guint rb_tray_icon_n_toggle_entries = G_N_ELEMENTS (rb_tray_icon_toggle_entries);

static const GtkTargetEntry target_uri [] = {{ "text/uri-list", 0, 0 }};

G_DEFINE_TYPE (RBTrayIcon, rb_tray_icon, EGG_TYPE_TRAY_ICON)

static void
rb_tray_icon_class_init (RBTrayIconClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);

	object_class->dispose = rb_tray_icon_dispose;
	object_class->finalize = rb_tray_icon_finalize;
	object_class->constructor = rb_tray_icon_constructor;

	object_class->set_property = rb_tray_icon_set_property;
	object_class->get_property = rb_tray_icon_get_property;

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
	/**
	 * RBTrayIcon:ui-manager:
	 *
	 * #GtkUIManager instance
	 */
	g_object_class_install_property (object_class,
					 PROP_UI_MANAGER,
					 g_param_spec_object ("ui-manager",
							      "GtkUIManager",
							      "GtkUIManager object",
							      GTK_TYPE_UI_MANAGER,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	/**
	 * RBTrayIcon:action-group:
	 *
	 * The #GtkActionGroup containing action specific to the tray icon
	 */
	g_object_class_install_property (object_class,
					 PROP_ACTION_GROUP,
					 g_param_spec_object ("action-group",
							      "GtkActionGroup",
							      "GtkActionGroup object",
							      GTK_TYPE_ACTION_GROUP,
							      G_PARAM_READABLE));

	g_type_class_add_private (klass, sizeof (RBTrayIconPrivate));
}

static void
rb_tray_icon_init (RBTrayIcon *icon)
{
	rb_debug ("setting up tray icon");

	icon->priv = RB_TRAY_ICON_GET_PRIVATE (icon);

	g_object_set (icon, "has-tooltip", TRUE, NULL);
	g_signal_connect_object (icon, "query-tooltip",
				 G_CALLBACK (tray_icon_tooltip_cb),
				 icon, 0);

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

#ifdef HAVE_NOTIFY
	tray->priv->show_notifications = eel_gconf_get_boolean (CONF_UI_SHOW_NOTIFICATIONS);
#endif

	tray->priv->actiongroup = gtk_action_group_new ("TrayActions");
	gtk_action_group_set_translation_domain (tray->priv->actiongroup,
						 GETTEXT_PACKAGE);
	gtk_action_group_add_toggle_actions (tray->priv->actiongroup,
					     rb_tray_icon_toggle_entries,
					     rb_tray_icon_n_toggle_entries,
					     tray);
	rb_tray_icon_sync_action (NULL, FALSE, tray);

	gtk_ui_manager_insert_action_group (tray->priv->ui_manager, tray->priv->actiongroup, 0);

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

	if (tray->priv->actiongroup != NULL) {
		if (tray->priv->ui_manager != NULL) {
			gtk_ui_manager_remove_action_group (tray->priv->ui_manager,
							    tray->priv->actiongroup);
		}
		g_object_unref (tray->priv->actiongroup);
		tray->priv->actiongroup = NULL;
	}

	G_OBJECT_CLASS (rb_tray_icon_parent_class)->dispose (object);
}

static void
rb_tray_icon_finalize (GObject *object)
{
	RBTrayIcon *tray;

	g_return_if_fail (object != NULL);
	g_return_if_fail (RB_IS_TRAY_ICON (object));

	tray = RB_TRAY_ICON (object);

	rb_debug ("tray icon %p finalizing", object);

	g_return_if_fail (tray->priv != NULL);

	if (tray->priv->tooltip_unsuppress_id > 0)
		g_source_remove (tray->priv->tooltip_unsuppress_id);

	G_OBJECT_CLASS (rb_tray_icon_parent_class)->finalize (object);
}

static void
rb_tray_icon_sync_action (RBShell *shell, gboolean visible, RBTrayIcon *tray)
{
	GtkAction *action;

	if ((tray->priv->actiongroup != NULL) && (tray->priv->shell != NULL)) {
		gboolean visible;

		action = gtk_action_group_get_action (tray->priv->actiongroup,
						      "TrayShowWindow");
		g_object_get (tray->priv->shell, "visibility", &visible, NULL);
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), visible);

		action = gtk_action_group_get_action (tray->priv->actiongroup,
						      "TrayShowNotifications");
#ifdef HAVE_NOTIFY
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
					      tray->priv->show_notifications);
#else
		gtk_action_set_visible (action, FALSE);
#endif
	}
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
		g_signal_connect_object (G_OBJECT (tray->priv->shell),
					 "visibility_changed",
					 G_CALLBACK (rb_tray_icon_sync_action),
					 tray, 0);
		g_object_get (tray->priv->shell,
			      "shell-player", &tray->priv->shell_player,
			      NULL);
		g_signal_connect_object (tray->priv->shell_player,
					 "playing-changed",
					 G_CALLBACK (rb_tray_icon_playing_changed_cb),
					 tray, 0);
		rb_tray_icon_sync_action (NULL, FALSE, tray);
		break;
	case PROP_UI_MANAGER:
		tray->priv->ui_manager = g_value_get_object (value);
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
	case PROP_UI_MANAGER:
		g_value_set_object (value, tray->priv->ui_manager);
		break;
	case PROP_ACTION_GROUP:
		g_value_set_object (value, tray->priv->actiongroup);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

/**
 * rb_tray_icon_new:
 * @mgr: the #GtkUIManager
 * @shell: the #RBShell
 * 
 * Return value: the #RBTrayIcon
 */
RBTrayIcon *
rb_tray_icon_new (GtkUIManager *mgr,
		  RBShell *shell)
{
	return g_object_new (RB_TYPE_TRAY_ICON,
			     "title", "Rhythmbox tray icon",
			     "ui-manager", mgr,
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

static void
rb_tray_icon_button_press_event_cb (GtkWidget *ebox, GdkEventButton *event,
				    RBTrayIcon *icon)
{
	GtkWidget *popup;

	/* filter out double, triple clicks */
	if (event->type != GDK_BUTTON_PRESS)
		return;

	rb_debug ("tray button press");

	switch (event->button) {
	case 1:
		rb_shell_toggle_visibility (icon->priv->shell);
		break;
	case 2:
		rb_shell_player_playpause (icon->priv->shell_player, FALSE, NULL);
		break;
	case 3:
		popup = gtk_ui_manager_get_widget (GTK_UI_MANAGER (icon->priv->ui_manager),
						   "/RhythmboxTrayPopup");
		gtk_menu_set_screen (GTK_MENU (popup), gtk_widget_get_screen (GTK_WIDGET (icon)));
		gtk_menu_popup (GTK_MENU (popup), NULL, NULL,
				tray_popup_position_menu, ebox, 2,
				gtk_get_current_event_time ());
		break;
	}
}

static void
rb_tray_icon_scroll_event_cb (GtkWidget *ebox, GdkEvent *event,
				    RBTrayIcon *icon)
{
	gdouble adjust;

	switch (event->scroll.direction) {
	case GDK_SCROLL_UP:
		adjust = 0.02;
		break;
	case GDK_SCROLL_DOWN:
		adjust = -0.02;
		break;
	default:
		return;
	}

	rb_shell_player_set_volume_relative (icon->priv->shell_player, adjust, NULL);
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

static void
rb_tray_icon_show_window_changed_cb (GtkAction *action,
				     RBTrayIcon *icon)
{
	rb_debug ("show window clicked for %p", icon);
	g_object_set (G_OBJECT (icon->priv->shell),
		      "visibility", gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)),
		      NULL);
}

static void
rb_tray_icon_show_notifications_changed_cb (GtkAction *action,
					    RBTrayIcon *icon)
{
	rb_debug ("show notifications clicked for %p", icon);
	icon->priv->show_notifications = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	eel_gconf_set_boolean (CONF_UI_SHOW_NOTIFICATIONS, icon->priv->show_notifications);
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

static void
rb_tray_icon_update_tooltip_visibility (RBTrayIcon *icon)
{
	gtk_widget_trigger_tooltip_query (GTK_WIDGET (icon));
}

/**
 * rb_tray_icon_set_tooltip_primary_text:
 * @icon: the #RBTrayIcon
 * @primary_text: the new primary text
 *
 * Updates the primary (large) text in the tray icon.
 */
void
rb_tray_icon_set_tooltip_primary_text (RBTrayIcon *icon,
				       const char *primary_text)
{
	g_free (icon->priv->primary_text);
	icon->priv->primary_text = g_strdup (primary_text);

	gtk_widget_trigger_tooltip_query (GTK_WIDGET (icon));
}



/**
 * rb_tray_icon_set_tooltip_icon:
 * @icon: the #RBTrayIcon
 * @pixbuf: a #GdkPixbuf to display as the icon in the tooltip
 *
 * Updates the icon in the tooltip.
 */
void
rb_tray_icon_set_tooltip_icon (RBTrayIcon *icon, GdkPixbuf *pixbuf)
{
	if (icon->priv->pixbuf != NULL) {
		g_object_unref (icon->priv->pixbuf);
	}
	if (pixbuf != NULL) {
		icon->priv->pixbuf = g_object_ref (pixbuf);
	} else {
		icon->priv->pixbuf = rb_tray_icon_create_blank_image (icon);
	}

	gtk_widget_trigger_tooltip_query (GTK_WIDGET (icon));
}

/**
 * rb_tray_icon_set_tooltip_secondary_markup:
 * @icon: the #RBTrayIcon
 * @secondary_markup: markup text to display as the secondary text in the toolip
 *
 * Updates the secondary text in the tooltip.  The secondary text can contain
 * simple markup.
 */
void
rb_tray_icon_set_tooltip_secondary_markup (RBTrayIcon *icon,
					   const char *secondary_markup)
{
	g_free (icon->priv->secondary_markup);
	icon->priv->secondary_markup = g_strdup (secondary_markup);

	gtk_widget_trigger_tooltip_query (GTK_WIDGET (icon));
}

/**
 * rb_tray_icon_notify:
 * @icon: the #RBTrayIcon
 * @timeout: how long the notification should be displayed (in milliseconds)
 * @primary_markup: primary markup to display in the notification
 * @pixbuf: #GdkPixbuf to display as the image in the notification
 * @secondary_markup: secondary markup to display in the notification
 * @requested: if %TRUE, the notification was directly requested by the user
 *
 * Displays a notification bubble attached to the tray icon.  If the user has
 * disabled notifications, the notification is only displayed if @requested
 * is %TRUE.
 */
void
rb_tray_icon_notify (RBTrayIcon *icon,
		     guint timeout,
		     const char *primary_markup,
		     GdkPixbuf *pixbuf,
		     const char *secondary_markup,
		     gboolean requested)
{
	if (!egg_tray_icon_have_manager (EGG_TRAY_ICON (icon))) {
		rb_debug ("not showing notification: %s", primary_markup);
		return;
	}
	if ((requested || icon->priv->show_notifications) == FALSE) {
		rb_debug ("ignoring notification: %s", primary_markup);
		return;
	}

	rb_debug ("doing notify: %s", primary_markup);
	if (timeout > 0)
		rb_tray_icon_suppress_tooltips (icon, timeout);
	if (pixbuf == NULL)
		pixbuf = rb_tray_icon_create_blank_image (icon);
	egg_tray_icon_notify (EGG_TRAY_ICON (icon), timeout,
			      primary_markup, pixbuf, secondary_markup);
}

/**
 * rb_tray_icon_cancel_notify:
 * @icon: the #RBTrayIcon
 *
 * Cancels any existing notification bubble.
 */
void
rb_tray_icon_cancel_notify (RBTrayIcon *icon)
{
	egg_tray_icon_cancel_message (EGG_TRAY_ICON (icon), 1);
}

static gboolean
tray_icon_tooltip_cb (GtkWidget  *widget,
		      gint        x,
		      gint        y,
		      gboolean    keyboard_tooltip,
		      GtkTooltip *tooltip,
		      RBTrayIcon *icon)
{
	char *esc_primary;
	char *markup;

	if (icon->priv->tooltips_suppressed)
		return FALSE;

	gtk_tooltip_set_icon (tooltip, icon->priv->pixbuf);

	if (icon->priv->primary_text != NULL) {
		esc_primary = g_markup_escape_text (icon->priv->primary_text, -1);
	} else {
		esc_primary = g_markup_escape_text (TRAY_ICON_DEFAULT_TOOLTIP, -1);
	}

	if (icon->priv->secondary_markup != NULL) {
		markup = g_strdup_printf ("<big><b>%s</b></big>\n\n%s",
					  esc_primary,
					  icon->priv->secondary_markup);
	} else {
		markup = g_strdup_printf ("<big><b>%s</b></big>", esc_primary);
	}

	gtk_tooltip_set_markup (tooltip, markup);

	g_free (esc_primary);
	g_free (markup);

	return TRUE;
}

static gboolean
rb_tray_icon_unsuppress_cb (RBTrayIcon *icon)
{
	gdk_threads_enter ();
	icon->priv->tooltips_suppressed = FALSE;
	rb_tray_icon_update_tooltip_visibility (icon);
	icon->priv->tooltip_unsuppress_id = 0;
	gdk_threads_leave ();
	return FALSE;
}

static void
rb_tray_icon_suppress_tooltips (RBTrayIcon *icon, guint duration)
{
	g_return_if_fail (duration > 0);

	icon->priv->tooltips_suppressed = TRUE;
	rb_tray_icon_update_tooltip_visibility (icon);

	if (icon->priv->tooltip_unsuppress_id > 0)
		g_source_remove (icon->priv->tooltip_unsuppress_id);
	icon->priv->tooltip_unsuppress_id = g_timeout_add (duration, (GSourceFunc) rb_tray_icon_unsuppress_cb, icon);
}

static GdkPixbuf *
rb_tray_icon_create_blank_image (RBTrayIcon *icon)
{
	int width;
	GdkPixbuf *pixbuf;

	gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &width, NULL);
	pixbuf = gdk_pixbuf_new (GDK_COLORSPACE_RGB, TRUE, 8, width, width);
	gdk_pixbuf_fill (pixbuf, 0); /* transparent */

	return pixbuf;
}

