/*
 * rb-status-icon-plugin.c
 *
 *  Copyright (C) 2009  Jonathan Matthew  <jonathan@d14n.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * The Rhythmbox authors hereby grant permission for non-GPL compatible
 * GStreamer plugins to be used and distributed together with GStreamer
 * and Rhythmbox. This permission is above and beyond the permissions granted
 * by the GPL license by which Rhythmbox is covered. If you modify this code
 * you may extend this exception to your version of the code, but you are not
 * obligated to do so. If you do not wish to do so, delete this exception
 * statement from your version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301  USA.
 */

#include <config.h>

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <glib.h>
#include <glib-object.h>

#include <X11/Xatom.h>

#ifdef HAVE_NOTIFY
#include <libnotify/notify.h>
#endif

#include "rb-status-icon-plugin.h"
#include "rb-util.h"
#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "eel-gconf-extensions.h"
#include "rb-builder-helpers.h"

#if defined(USE_GTK_STATUS_ICON)
#include "rb-tray-icon-gtk.h"
#else
#include "rb-tray-icon.h"
#endif

#define TRAY_ICON_DEFAULT_TOOLTIP _("Music Player")

#define TOOLTIP_IMAGE_BORDER_WIDTH	1
#define PLAYING_ENTRY_NOTIFY_TIME 	4

#define CONF_PLUGIN_SETTINGS	"/apps/rhythmbox/plugins/status-icon"
#define CONF_NOTIFICATION_MODE	CONF_PLUGIN_SETTINGS "/notification-mode"
#define CONF_STATUS_ICON_MODE	CONF_PLUGIN_SETTINGS "/status-icon-mode"
#define CONF_WINDOW_VISIBILITY	CONF_PLUGIN_SETTINGS "/window-visible"

#define CONF_OLD_ICON_MODE	"/apps/rhythmbox/plugins/dontreallyclose/active"
#define CONF_OLD_NOTIFICATIONS	"/apps/rhythmbox/ui/show_notifications"
#define CONF_OLD_VISIBILITY	"/apps/rhythmbox/state/window_visible"

static void toggle_window_cmd (GtkAction *action, RBStatusIconPlugin *plugin);
static void show_window_cmd (GtkAction *action, RBStatusIconPlugin *plugin);
static void show_notifications_cmd (GtkAction *action, RBStatusIconPlugin *plugin);
static void update_status_icon_visibility (RBStatusIconPlugin *plugin, gboolean notifying);

struct _RBStatusIconPluginPrivate
{
	GtkActionGroup *action_group;
	guint ui_merge_id;

	RBTrayIcon *tray_icon;

	guint hide_main_window_id;
	guint gconf_notify_id;

	/* configuration */
	gboolean syncing_actions;
	gboolean syncing_config_widgets;
	enum {
		ICON_NEVER = 0,
		ICON_WITH_NOTIFY,
		ICON_ALWAYS,
		ICON_OWNS_WINDOW
	} icon_mode;
	enum {
		NOTIFY_NEVER = 0,
		NOTIFY_HIDDEN,
		NOTIFY_ALWAYS
	} notify_mode;

	/* current playing data */
	char *current_title;
	char *current_album_and_artist;	/* from _album_ by _artist_ */

	/* tooltip data */
	char *tooltip_markup;
	GdkPixbuf *tooltip_app_pixbuf;
	GdkPixbuf *tooltip_pixbuf;
	gboolean tooltips_suppressed;

	/* notification data */
	GdkPixbuf *notify_pixbuf;
#ifdef HAVE_NOTIFY
	NotifyNotification *notification;
	gboolean notify_supports_actions;
#endif

	GtkWidget *config_dialog;
	GtkWidget *notify_combo;
	GtkWidget *icon_combo;

	RBShellPlayer *shell_player;
	RBShell *shell;
	RhythmDB *db;
};

G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);

RB_PLUGIN_REGISTER(RBStatusIconPlugin, rb_status_icon_plugin)

static GtkActionEntry rb_status_icon_plugin_actions [] =
{
	{ "MusicClose", GTK_STOCK_CLOSE, N_("_Close"), "<control>W",
	  N_("Hide the music player window"),
	  G_CALLBACK (toggle_window_cmd) }
};

static GtkToggleActionEntry rb_status_icon_plugin_toggle_entries [] =
{
	{ "TrayShowWindow", NULL, N_("_Show Music Player"), NULL,
	  N_("Choose music to play"),
	  G_CALLBACK (show_window_cmd) },
	{ "TrayShowNotifications", NULL, N_("Show N_otifications"), NULL,
	  N_("Show notifications of song changes and other events"),
	  G_CALLBACK (show_notifications_cmd) },
};

static gchar *
markup_escape (const char *text)
{
	return (text == NULL) ? NULL : g_markup_escape_text (text, -1);
}

static GdkPixbuf *
create_tooltip_pixbuf (GdkPixbuf *pixbuf)
{
	GdkPixbuf *bordered;
	int w;
	int h;

	/* add a black border */
	w = gdk_pixbuf_get_width (pixbuf);
	h = gdk_pixbuf_get_height (pixbuf);
	bordered = gdk_pixbuf_new (gdk_pixbuf_get_colorspace (pixbuf),
				   gdk_pixbuf_get_has_alpha (pixbuf),
				   gdk_pixbuf_get_bits_per_sample (pixbuf),
				   w + (TOOLTIP_IMAGE_BORDER_WIDTH*2),
				   h + (TOOLTIP_IMAGE_BORDER_WIDTH*2));
	gdk_pixbuf_fill (bordered, 0xff);		/* opaque black */
	gdk_pixbuf_copy_area (pixbuf,
			      0, 0, w, h,
			      bordered,
			      TOOLTIP_IMAGE_BORDER_WIDTH,
			      TOOLTIP_IMAGE_BORDER_WIDTH);

	return bordered;
}

/* UI actions */

static void
toggle_window_cmd (GtkAction *action, RBStatusIconPlugin *plugin)
{
	rb_shell_toggle_visibility (plugin->priv->shell);
}

static void
show_window_cmd (GtkAction *action, RBStatusIconPlugin *plugin)
{
	if (plugin->priv->syncing_actions)
		return;

	g_object_set (plugin->priv->shell,
		      "visibility", gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action)),
		      NULL);
}

static void
show_notifications_cmd (GtkAction *action, RBStatusIconPlugin *plugin)
{
	gboolean active;
	int new_mode;

	if (plugin->priv->syncing_actions)
		return;

	/* we've only got on/off here, so map that to 'never' or 'only when hidden' */
	active = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	new_mode = active ? NOTIFY_HIDDEN : NOTIFY_NEVER;

	eel_gconf_set_integer (CONF_NOTIFICATION_MODE, new_mode);
}

void
rb_status_icon_plugin_scroll_event (RBStatusIconPlugin *plugin,
				  GdkEventScroll *event)
{
	gdouble adjust;

	switch (event->direction) {
	case GDK_SCROLL_UP:
		adjust = 0.02;
		break;
	case GDK_SCROLL_DOWN:
		adjust = -0.02;
		break;
	default:
		return;
	}

	rb_shell_player_set_volume_relative (plugin->priv->shell_player, adjust, NULL);
}

void
rb_status_icon_plugin_button_press_event (RBStatusIconPlugin *plugin,
					GdkEventButton *event)
{
	GtkWidget *popup;
	GtkUIManager *ui_manager;

	/* filter out double, triple clicks */
	if (event->type != GDK_BUTTON_PRESS)
		return;

	switch (event->button) {
	case 1:
		rb_shell_toggle_visibility (plugin->priv->shell);
		break;
	case 2:
		rb_shell_player_playpause (plugin->priv->shell_player, FALSE, NULL);
		break;
	case 3:
		g_object_get (plugin->priv->shell, "ui-manager", &ui_manager, NULL);
		popup = gtk_ui_manager_get_widget (GTK_UI_MANAGER (ui_manager),
						   "/RhythmboxTrayPopup");

		rb_tray_icon_menu_popup (plugin->priv->tray_icon, popup, 3);
		g_object_unref (ui_manager);
		break;
	}
}

static void
sync_actions (RBStatusIconPlugin *plugin)
{
	GtkAction *action;
	gboolean visible;

	plugin->priv->syncing_actions = TRUE;

	action = gtk_action_group_get_action (plugin->priv->action_group,
					      "TrayShowWindow");
	g_object_get (plugin->priv->shell, "visibility", &visible, NULL);
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), visible);

	action = gtk_action_group_get_action (plugin->priv->action_group,
					      "TrayShowNotifications");
#ifdef HAVE_NOTIFY
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
				      plugin->priv->notify_mode != NOTIFY_NEVER);
#else
	gtk_action_set_visible (action, FALSE);
#endif

	/* only show the 'close window' menu item if the icon owns the window */
	action = gtk_action_group_get_action (plugin->priv->action_group,
					      "MusicClose");
	gtk_action_set_visible (action, plugin->priv->icon_mode == ICON_OWNS_WINDOW);

	plugin->priv->syncing_actions = FALSE;
}

static void
visibility_changed_cb (RBShell *shell,
		       gboolean visible,
		       RBStatusIconPlugin *plugin)
{
	sync_actions (plugin);
}

/* notification popups */

#ifdef HAVE_NOTIFY
static void
notification_closed_cb (NotifyNotification *notification,
			RBStatusIconPlugin *plugin)
{
	rb_debug ("notification closed");
	plugin->priv->tooltips_suppressed = FALSE;
	rb_tray_icon_trigger_tooltip_query (plugin->priv->tray_icon);

	update_status_icon_visibility (plugin, FALSE);
}

static void
notification_next_cb (NotifyNotification *notification,
		      const char *action,
		      RBStatusIconPlugin *plugin)
{
	rb_debug ("notification action: %s", action);
	rb_shell_player_do_next (plugin->priv->shell_player, NULL);
}

static void
do_notify (RBStatusIconPlugin *plugin,
	   guint timeout,
	   const char *primary,
	   const char *secondary,
	   GdkPixbuf *pixbuf,
	   gboolean show_action)
{
	const char *icon_name;
	GError *error = NULL;

	if (notify_is_initted () == FALSE) {
		GList *caps;

		if (notify_init ("rhythmbox") == FALSE) {
			g_warning ("libnotify initialization failed");
			return;
		}

		/* ask the notification server if it supports actions */
		caps = notify_get_server_caps ();
		if (g_list_find_custom (caps, "actions", (GCompareFunc)g_strcmp0) != NULL) {
			rb_debug ("notification server supports actions");
			plugin->priv->notify_supports_actions = TRUE;
		} else {
			rb_debug ("notification server does not support actions");
		}
		rb_list_deep_free (caps);
	}

	update_status_icon_visibility (plugin, TRUE);

	if (primary == NULL)
		primary = "";

	if (secondary == NULL)
		secondary = "";

	if (pixbuf == NULL)
		icon_name = RB_APP_ICON;
	else
		icon_name = NULL;

	if (plugin->priv->notification == NULL) {
		plugin->priv->notification = notify_notification_new (primary, secondary, icon_name, NULL);

		g_signal_connect_object (plugin->priv->notification,
					 "closed",
					 G_CALLBACK (notification_closed_cb),
					 plugin, 0);
	} else {
		notify_notification_update (plugin->priv->notification, primary, secondary, icon_name);
	}

	switch (plugin->priv->icon_mode) {
	case ICON_NEVER:
		break;

	case ICON_WITH_NOTIFY:
	case ICON_ALWAYS:
	case ICON_OWNS_WINDOW:
		rb_tray_icon_attach_notification (plugin->priv->tray_icon,
						  plugin->priv->notification);
		break;

	default:
		g_assert_not_reached ();
	}

	notify_notification_set_timeout (plugin->priv->notification, timeout);

	if (pixbuf != NULL) {
		notify_notification_set_icon_from_pixbuf (plugin->priv->notification, pixbuf);
	}

	notify_notification_clear_actions (plugin->priv->notification);
	if (show_action && plugin->priv->notify_supports_actions) {
		notify_notification_add_action (plugin->priv->notification,
						"media-next",
						_("Next"),
						(NotifyActionCallback) notification_next_cb,
						plugin,
						NULL);
	}

	if (notify_notification_show (plugin->priv->notification, &error) == FALSE) {
		g_warning ("Failed to send notification (%s): %s", primary, error->message);
		g_error_free (error);
		update_status_icon_visibility (plugin, FALSE);
	} else {
		/* hide the tooltip while the notification is visible */
		plugin->priv->tooltips_suppressed = TRUE;
		rb_tray_icon_trigger_tooltip_query (plugin->priv->tray_icon);
	}
}

static gboolean
should_notify (RBStatusIconPlugin *plugin)
{
	gboolean visible;

	switch (plugin->priv->icon_mode) {
	case ICON_NEVER:
	case ICON_WITH_NOTIFY:
		break;

	case ICON_ALWAYS:
	case ICON_OWNS_WINDOW:
		if (rb_tray_icon_is_embedded (plugin->priv->tray_icon) == FALSE) {
			rb_debug ("status icon is not embedded, not notifying");
			return FALSE;
		}
		break;

	default:
		g_assert_not_reached ();
	}

	switch (plugin->priv->notify_mode) {
	case NOTIFY_NEVER:
		rb_debug ("notifications disabled, not notifying");
		return FALSE;

	case NOTIFY_HIDDEN:
		g_object_get (plugin->priv->shell, "visibility", &visible, NULL);
		if (visible) {
			rb_debug ("shell is visible, not notifying");
			return FALSE;
		}
		break;

	case NOTIFY_ALWAYS:
		break;

	default:
		g_assert_not_reached ();
	}

	return TRUE;
}

static void
notify_playing_entry (RBStatusIconPlugin *plugin, gboolean requested)
{
	if (requested == FALSE && should_notify (plugin) == FALSE) {
		return;
	}

	do_notify (plugin,
		   PLAYING_ENTRY_NOTIFY_TIME * 1000,
		   plugin->priv->current_title,
		   plugin->priv->current_album_and_artist,
		   plugin->priv->notify_pixbuf,
		   TRUE);
}

static void
notify_custom (RBStatusIconPlugin *plugin, guint timeout, const char *primary, const char *secondary, GdkPixbuf *pixbuf, gboolean requested)
{
	if (requested == FALSE && should_notify (plugin) == FALSE) {
		return;
	}

	do_notify (plugin, timeout, primary, secondary, pixbuf, FALSE);
}

static void
cleanup_notification (RBStatusIconPlugin *plugin)
{
	if (plugin->priv->notification != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->priv->notification,
						      G_CALLBACK (notification_closed_cb),
						      plugin);
		notify_notification_close (plugin->priv->notification, NULL);
		plugin->priv->notification = NULL;
	}
}

#else

/* lack of notification popups */

static void
notify_playing_entry (RBStatusIconPlugin *plugin, gboolean requested)
{
}

static void
notify_custom (RBStatusIconPlugin *plugin, guint timeout, const char *primary, const char *secondary, GdkPixbuf *pixbuf, gboolean requested)
{
}

static void
cleanup_notification (RBStatusIconPlugin *plugin)
{
}

#endif

static void
shell_notify_playing_cb (RBShell *shell, gboolean requested, RBStatusIconPlugin *plugin)
{
	notify_playing_entry (plugin, requested);
}

static void
shell_notify_custom_cb (RBShell *shell, guint timeout, const char *primary, const char *secondary, GdkPixbuf *pixbuf, gboolean requested, RBStatusIconPlugin *plugin)
{
	notify_custom (plugin, timeout, primary, secondary, pixbuf, requested);
}

/* tooltips */

static void
update_tooltip (RBStatusIconPlugin *plugin)
{
	gboolean playing;
	char *elapsed_string;
	GString *secondary;

	rb_shell_player_get_playing (plugin->priv->shell_player, &playing, NULL);
	elapsed_string = rb_shell_player_get_playing_time_string (plugin->priv->shell_player);

	secondary = g_string_sized_new (100);
	if (plugin->priv->current_album_and_artist != NULL) {
		g_string_append (secondary, plugin->priv->current_album_and_artist);
		if (secondary->len != 0)
			g_string_append_c (secondary, '\n');
	}
	if (plugin->priv->current_title == NULL) {
		g_string_append (secondary, _("Not playing"));
	} else if (!playing) {
		/* Translators: the %s is the elapsed and total time */
		g_string_append_printf (secondary, _("Paused, %s"), elapsed_string);
	} else {
		g_string_append (secondary, elapsed_string);
	}

	plugin->priv->tooltip_markup = g_string_free (secondary, FALSE);
	g_free (elapsed_string);

	rb_tray_icon_trigger_tooltip_query (plugin->priv->tray_icon);
}

gboolean
rb_status_icon_plugin_set_tooltip (GtkWidget        *widget,
				 gint              x,
				 gint              y,
				 gboolean          keyboard_tooltip,
				 GtkTooltip       *tooltip,
				 RBStatusIconPlugin *plugin)
{
	char *esc_primary;
	char *markup;

	if (plugin->priv->tooltips_suppressed)
		return FALSE;

	if (plugin->priv->tooltip_pixbuf != NULL) {
		gtk_tooltip_set_icon (tooltip, plugin->priv->tooltip_pixbuf);
	} else {
		gtk_tooltip_set_icon (tooltip, plugin->priv->tooltip_app_pixbuf);
	}

	if (plugin->priv->current_title != NULL) {
		esc_primary = g_markup_escape_text (plugin->priv->current_title, -1);
	} else {
		esc_primary = g_markup_escape_text (TRAY_ICON_DEFAULT_TOOLTIP, -1);
	}

	if (plugin->priv->tooltip_markup != NULL) {
		markup = g_strdup_printf ("<big><b>%s</b></big>\n\n%s",
					  esc_primary,
					  plugin->priv->tooltip_markup);
	} else {
		markup = g_strdup_printf ("<big><b>%s</b></big>", esc_primary);
	}

	gtk_tooltip_set_markup (tooltip, markup);

	g_free (esc_primary);
	g_free (markup);

	return TRUE;
}

/* information on current track */

static void
update_current_playing_data (RBStatusIconPlugin *plugin, RhythmDBEntry *entry)
{
	GValue *value;
	const char *stream_title = NULL;
	char *artist = NULL;
	char *album = NULL;
	char *title = NULL;
	GString *secondary;

	g_free (plugin->priv->current_title);
	g_free (plugin->priv->current_album_and_artist);
	plugin->priv->current_title = NULL;
	plugin->priv->current_album_and_artist = NULL;

	if (entry == NULL)
		return;

	secondary = g_string_sized_new (100);

	/* get artist, preferring streaming song details */
	value = rhythmdb_entry_request_extra_metadata (plugin->priv->db,
						       entry,
						       RHYTHMDB_PROP_STREAM_SONG_ARTIST);
	if (value != NULL) {
		artist = markup_escape (g_value_get_string (value));
		g_value_unset (value);
		g_free (value);
	} else {
		artist = markup_escape (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	}

	if (artist != NULL && artist[0] != '\0') {
		/* Translators: by Artist */
		g_string_append_printf (secondary, _("by <i>%s</i>"), artist);
	}
	g_free (artist);

	/* get album, preferring streaming song details */
	value = rhythmdb_entry_request_extra_metadata (plugin->priv->db,
						       entry,
						       RHYTHMDB_PROP_STREAM_SONG_ALBUM);
	if (value != NULL) {
		album = markup_escape (g_value_get_string (value));
		g_value_unset (value);
		g_free (value);
	} else {
		album = markup_escape (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	}

	if (album != NULL && album[0] != '\0') {
		if (secondary->len != 0)
			g_string_append_c (secondary, ' ');

		/* Translators: from Album */
		g_string_append_printf (secondary, _("from <i>%s</i>"), album);
	}
	g_free (album);

	/* get title and possibly stream name.
	 * if we have a streaming song title, the entry's title
	 * property is the stream name.
	 */
	value = rhythmdb_entry_request_extra_metadata (plugin->priv->db,
						       entry,
						       RHYTHMDB_PROP_STREAM_SONG_TITLE);
	if (value != NULL) {
		title = g_value_dup_string (value);
		g_value_unset (value);
		g_free (value);

		stream_title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);
	} else {
		title = g_strdup (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE));
	}

	if (stream_title != NULL && stream_title[0] != '\0') {
		char *escaped;

		escaped = markup_escape (stream_title);
		if (secondary->len == 0)
			g_string_append (secondary, escaped);
		else
			g_string_append_printf (secondary, " (%s)", escaped);
		g_free (escaped);
	}

	if (title != NULL)
		plugin->priv->current_title = title;
	else
		/* Translators: unknown track title */
		plugin->priv->current_title = g_strdup (_("Unknown"));

	plugin->priv->current_album_and_artist = g_string_free (secondary, FALSE);
}

static void
forget_pixbufs (RBStatusIconPlugin *plugin)
{
	if (plugin->priv->tooltip_pixbuf != NULL) {
		g_object_unref (plugin->priv->tooltip_pixbuf);
		plugin->priv->tooltip_pixbuf = NULL;
	}
	if (plugin->priv->notify_pixbuf != NULL) {
		g_object_unref (plugin->priv->notify_pixbuf);
		plugin->priv->notify_pixbuf = NULL;
	}
}

static void
playing_entry_changed_cb (RBShellPlayer *player,
			  RhythmDBEntry *entry,
			  RBStatusIconPlugin *plugin)
{
	forget_pixbufs (plugin);

	update_current_playing_data (plugin, entry);

	if (entry != NULL) {
		notify_playing_entry (plugin, FALSE);
	}
	update_tooltip (plugin);
}

static gboolean
is_playing_entry (RBStatusIconPlugin *plugin, RhythmDBEntry *entry)
{
	RhythmDBEntry *playing;

	playing = rb_shell_player_get_playing_entry (plugin->priv->shell_player);
	if (playing == NULL) {
		return FALSE;
	}

	rhythmdb_entry_unref (playing);
	return (entry == playing);
}

static void
db_art_metadata_cb (RhythmDB *db,
		    RhythmDBEntry *entry,
		    const char *field,
		    GValue *metadata,
		    RBStatusIconPlugin *plugin)
{
	guint time;

	if (is_playing_entry (plugin, entry) == FALSE)
		return;

	forget_pixbufs (plugin);

	if (G_VALUE_HOLDS (metadata, GDK_TYPE_PIXBUF)) {
		GdkPixbuf *pixbuf;

		pixbuf = GDK_PIXBUF (g_value_get_object (metadata));

		/* create a smallish copy for the tooltip */
		if (pixbuf != NULL) {
			GdkPixbuf *scaled;

			scaled = rb_scale_pixbuf_to_size (pixbuf, GTK_ICON_SIZE_DIALOG);
			plugin->priv->tooltip_pixbuf = create_tooltip_pixbuf (scaled);
			plugin->priv->notify_pixbuf = scaled;
		}

		/* probably keep the full size thing for notifications?  hmm. */
	}

	rb_tray_icon_trigger_tooltip_query (plugin->priv->tray_icon);

	if (rb_shell_player_get_playing_time (plugin->priv->shell_player, &time, NULL)) {
		if (time < PLAYING_ENTRY_NOTIFY_TIME) {
			notify_playing_entry (plugin, FALSE);
		}
	}
}

static void
db_stream_metadata_cb (RhythmDB *db,
		       RhythmDBEntry *entry,
		       const char *field,
		       GValue *metadata,
		       RBStatusIconPlugin *plugin)
{
	if (is_playing_entry (plugin, entry) == FALSE)
		return;

	update_current_playing_data (plugin, entry);
}


static void
elapsed_changed_cb (RBShellPlayer *player,
		    guint elapsed,
		    RBStatusIconPlugin *plugin)
{
	update_tooltip (plugin);
}

/* status icon visibility */

static void
update_status_icon_visibility (RBStatusIconPlugin *plugin, gboolean notifying)
{
	gboolean visible;

	switch (plugin->priv->icon_mode) {
	case ICON_NEVER:
		visible = FALSE;
		break;

	case ICON_WITH_NOTIFY:
		visible = notifying;
		break;

	case ICON_ALWAYS:
	case ICON_OWNS_WINDOW:
		visible = TRUE;
		break;

	default:
		g_assert_not_reached ();
	}

	rb_tray_icon_set_visible (plugin->priv->tray_icon, visible);
}

/* minimize/close to tray */

/* Based on a function found in wnck */
static void
set_icon_geometry  (GdkWindow *window,
		    int        x,
		    int        y,
		    int        width,
		    int        height)
{
	gulong data[4];
	Display *dpy = gdk_x11_drawable_get_xdisplay (window);

	data[0] = x;
	data[1] = y;
	data[2] = width;
	data[3] = height;

	XChangeProperty (dpy,
			 GDK_WINDOW_XID (window),
			 gdk_x11_get_xatom_by_name_for_display (gdk_drawable_get_display (window),
								"_NET_WM_ICON_GEOMETRY"),
			 XA_CARDINAL, 32, PropModeReplace,
			 (guchar *)&data, 4);
}

static gboolean
hide_main_window (GtkWidget *window)
{
	GDK_THREADS_ENTER ();

	gtk_widget_hide (window);
	g_object_unref (window);

	GDK_THREADS_LEAVE ();

	return FALSE;
}

static void
cancel_hide_main_window (RBStatusIconPlugin *plugin)
{
	/* FIXME - see below */
	if (plugin->priv->hide_main_window_id > 0)
		g_source_remove (plugin->priv->hide_main_window_id);
	plugin->priv->hide_main_window_id = 0;
}

static void
close_to_tray (RBStatusIconPlugin *plugin)
{
	int x, y, width, height;
	GtkWindow *window;

	cancel_hide_main_window (plugin);

	g_object_get (plugin->priv->shell, "window", &window, NULL);

	/* set the window's icon geometry to match the icon */
	rb_tray_icon_get_geom (plugin->priv->tray_icon,
			       &x, &y, &width, &height);
	if (GTK_WIDGET_REALIZED (window))
		set_icon_geometry (GTK_WIDGET (window)->window,
				   x, y, width, height);

	/* ask the tasklist not to show our window */
	gtk_window_set_skip_taskbar_hint (window, TRUE);

	/* FIXME - this is horribly evil racy workaround for a
	 * current bug in the tasklist not noticing our hint
	 * change
	 */
	plugin->priv->hide_main_window_id =
		g_timeout_add (250, (GSourceFunc) hide_main_window, g_object_ref (window));
}

static gboolean
visibility_changing_cb (RBShell *shell,
			gboolean initial,
			gboolean visible,
			RBStatusIconPlugin *plugin)
{

	switch (plugin->priv->icon_mode) {
	case ICON_NEVER:
	case ICON_WITH_NOTIFY:
	case ICON_ALWAYS:
		return visible;

	case ICON_OWNS_WINDOW:
		/* complicated stuff below */
		break;

	default:
		g_assert_not_reached ();
	}

	if (initial) {
		/* restore visibility from gconf setting */
		visible = eel_gconf_get_boolean (CONF_WINDOW_VISIBILITY);
		rb_debug ("setting initial visibility %d from gconf", visible);
		return visible;
	}

	cancel_hide_main_window (plugin);

	if (visible) {
		GtkWindow *window;

		g_object_get (shell, "window", &window, NULL);
		gtk_window_set_skip_taskbar_hint (window, FALSE);
		g_object_unref (window);
	} else {
		/* don't allow the window to be hidden if the icon is not embedded */
		if (rb_tray_icon_is_embedded (plugin->priv->tray_icon) == FALSE) {
			rb_debug ("status icon is not embedded, disallowing visibility change");
			visible = TRUE;
		} else {
			close_to_tray (plugin);
		}
	}

	return visible;
}

static gboolean
window_delete_event_cb (GtkWindow *window, GdkEvent *event, RBStatusIconPlugin *plugin)
{

	switch (plugin->priv->icon_mode) {
	case ICON_NEVER:
	case ICON_WITH_NOTIFY:
	case ICON_ALWAYS:
		return FALSE;

	case ICON_OWNS_WINDOW:
		rb_debug ("window deleted, but let's just hide it instead");
		close_to_tray (plugin);
		gtk_window_iconify (window);
		return TRUE;

	default:
		g_assert_not_reached ();
	}
}

static void
store_window_visibility (RBStatusIconPlugin *plugin)
{
	/* if in icon-owns-window mode, store current visibility in gconf */
	if (plugin->priv->icon_mode == ICON_OWNS_WINDOW) {
		gboolean visible;

		g_object_get (plugin->priv->shell, "visibility", &visible, NULL);
		eel_gconf_set_boolean (CONF_WINDOW_VISIBILITY, visible);
	}
}

#if !defined(USE_GTK_STATUS_ICON)

/* EggTrayIcon helpers */

#if 0
static void
tray_embedded_cb (GtkPlug *plug,
		  gpointer data)
{
	/* FIXME - this doesn't work */
	RBShell *shell = RB_SHELL (data);

	rb_debug ("got embedded signal");

	gdk_window_set_decorations (shell->priv->window->window,
				    GDK_DECOR_ALL | GDK_DECOR_MINIMIZE | GDK_DECOR_MAXIMIZE);
}
#endif

static gboolean
tray_destroy_cb (GtkObject *object, RBStatusIconPlugin *plugin)
{
	if (plugin->priv->tray_icon) {
		rb_debug ("caught destroy event for icon %p", object);
		g_object_ref_sink (object);
		plugin->priv->tray_icon = NULL;
		rb_debug ("finished sinking tray");
	}

	rb_debug ("creating new icon");
	plugin->priv->tray_icon = rb_tray_icon_new (plugin, plugin->priv->shell);
	g_signal_connect_object (plugin->priv->tray_icon, "destroy", G_CALLBACK (tray_destroy_cb), plugin, 0);
	/* g_signal_connect_object (plugin->priv->tray_icon, "embedded", G_CALLBACK (tray_embedded_cb), plugin, 0); */

	rb_debug ("done creating new icon %p", plugin->priv->tray_icon);
	return TRUE;
}

static void
cleanup_status_icon (RBStatusIconPlugin *plugin)
{
	g_signal_handlers_disconnect_by_func (plugin->priv->tray_icon,
					      G_CALLBACK (tray_destroy_cb),
					      plugin);

	gtk_widget_hide_all (GTK_WIDGET (plugin->priv->tray_icon));
	gtk_widget_destroy (GTK_WIDGET (plugin->priv->tray_icon));
}

static void
create_status_icon (RBStatusIconPlugin *plugin)
{
	tray_destroy_cb (NULL, plugin);
}

#else

/* boring equivalents for GtkStatusIcon */

static void
create_status_icon (RBStatusIconPlugin *plugin)
{
	plugin->priv->tray_icon = rb_tray_icon_new (plugin, plugin->priv->shell_player);
}

static void
cleanup_status_icon (RBStatusIconPlugin *plugin)
{
	g_object_unref (plugin->priv->tray_icon);
}


#endif

/* preferences dialog and gconf stuff */

static void
notification_config_changed_cb (GtkComboBox *widget, RBStatusIconPlugin *plugin)
{
	if (plugin->priv->syncing_config_widgets)
		return;

	eel_gconf_set_integer (CONF_NOTIFICATION_MODE, gtk_combo_box_get_active (widget));
}

static void
status_icon_config_changed_cb (GtkComboBox *widget, RBStatusIconPlugin *plugin)
{
	if (plugin->priv->syncing_config_widgets)
		return;

	eel_gconf_set_integer (CONF_STATUS_ICON_MODE, gtk_combo_box_get_active (widget));
}

static void
config_response_cb (GtkWidget *dialog, gint response, RBStatusIconPlugin *plugin)
{
	gtk_widget_hide (dialog);
}

static gboolean
should_upgrade (const char *from, const char *to)
{
	return (eel_gconf_is_default (to) && (eel_gconf_is_default (from) == FALSE));
}

static void
maybe_upgrade_preferences (RBStatusIconPlugin *plugin)
{
	/* dontreallyclose plugin enabled -> icon owns window mode, otherwise, icon always visible */
	if (should_upgrade (CONF_OLD_ICON_MODE, CONF_STATUS_ICON_MODE)) {
		int new_mode = eel_gconf_get_boolean (CONF_OLD_ICON_MODE) ? ICON_OWNS_WINDOW : ICON_ALWAYS;
		rb_debug ("using old gconf key " CONF_OLD_ICON_MODE " to set icon mode to %d", new_mode);
		eel_gconf_set_integer (CONF_STATUS_ICON_MODE, new_mode);
	}

	/* old show_notifications key maps to hidden mode if true, never if false */
	if (should_upgrade (CONF_OLD_NOTIFICATIONS, CONF_NOTIFICATION_MODE)) {
		int new_mode = eel_gconf_get_boolean (CONF_OLD_NOTIFICATIONS) ? NOTIFY_HIDDEN : NOTIFY_NEVER;
		rb_debug ("using old gconf key " CONF_OLD_NOTIFICATIONS  " to set notify mode to %d", new_mode);
		eel_gconf_set_integer (CONF_NOTIFICATION_MODE, new_mode);
	}

	/* apply old window visibility key */
	if (should_upgrade (CONF_OLD_VISIBILITY, CONF_WINDOW_VISIBILITY)) {
		gboolean visible = eel_gconf_get_boolean (CONF_OLD_VISIBILITY);
		rb_debug ("using old gconf key " CONF_OLD_VISIBILITY  " to set window visibility to %d", visible);
		eel_gconf_set_boolean (CONF_WINDOW_VISIBILITY, visible);
	}
}

static void
config_notify_cb (GConfClient *client, guint connection_id, GConfEntry *entry, RBStatusIconPlugin *plugin)
{
	if (g_str_equal (gconf_entry_get_key (entry), CONF_STATUS_ICON_MODE)) {

		plugin->priv->icon_mode = gconf_value_get_int (gconf_entry_get_value (entry));
		rb_debug ("icon mode changed to %d", plugin->priv->icon_mode);

		update_status_icon_visibility (plugin, FALSE);	/* maybe should remember if we're notifying.. */
		sync_actions (plugin);

		if (plugin->priv->icon_combo != NULL) {
			plugin->priv->syncing_config_widgets = TRUE;
			gtk_combo_box_set_active (GTK_COMBO_BOX (plugin->priv->icon_combo), plugin->priv->icon_mode);
			plugin->priv->syncing_config_widgets = FALSE;
		}

	} else if (g_str_equal (gconf_entry_get_key (entry), CONF_NOTIFICATION_MODE)) {
		plugin->priv->notify_mode = gconf_value_get_int (gconf_entry_get_value (entry));
		rb_debug ("notify mode changed to %d", plugin->priv->notify_mode);

		sync_actions (plugin);

		if (plugin->priv->notify_combo != NULL) {
			plugin->priv->syncing_config_widgets = TRUE;
			gtk_combo_box_set_active (GTK_COMBO_BOX (plugin->priv->notify_combo), plugin->priv->notify_mode);
			plugin->priv->syncing_config_widgets = FALSE;
		}
	}
}


/* plugin infrastructure */

static GtkWidget *
impl_get_config_widget (RBPlugin *bplugin)
{
	RBStatusIconPlugin *plugin;
	GtkBuilder *builder;
	char *builderfile;

	plugin = RB_STATUS_ICON_PLUGIN (bplugin);
	if (plugin->priv->config_dialog != NULL) {
		gtk_widget_show_all (plugin->priv->config_dialog);
		return plugin->priv->config_dialog;
	}

	builderfile = rb_plugin_find_file (bplugin, "status-icon-preferences.ui");
	if (builderfile == NULL) {
		g_warning ("can't find status-icon-preferences.ui");
		return NULL;
	}

	builder = rb_builder_load (builderfile, NULL);
	g_free (builderfile);

	rb_builder_boldify_label (builder, "headerlabel");

	plugin->priv->config_dialog = GTK_WIDGET (gtk_builder_get_object (builder, "statusiconpreferences"));
	gtk_widget_hide_on_delete (plugin->priv->config_dialog);

	/* connect signals and stuff */
	g_signal_connect_object (plugin->priv->config_dialog, "response", G_CALLBACK (config_response_cb), plugin, 0);

	plugin->priv->icon_combo = GTK_WIDGET (gtk_builder_get_object (builder, "statusiconmode"));
	plugin->priv->notify_combo = GTK_WIDGET (gtk_builder_get_object (builder, "notificationmode"));
	g_signal_connect_object (plugin->priv->notify_combo,
				 "changed",
				 G_CALLBACK (notification_config_changed_cb),
				 plugin, 0);
	g_signal_connect_object (plugin->priv->icon_combo,
				 "changed",
				 G_CALLBACK (status_icon_config_changed_cb),
				 plugin, 0);
	gtk_combo_box_set_active (GTK_COMBO_BOX (plugin->priv->notify_combo), plugin->priv->notify_mode);
	gtk_combo_box_set_active (GTK_COMBO_BOX (plugin->priv->icon_combo), plugin->priv->icon_mode);

	g_object_unref (builder);
	return plugin->priv->config_dialog;
}

static void
impl_activate (RBPlugin *bplugin,
	       RBShell *shell)
{
	RBStatusIconPlugin *plugin;
	GtkUIManager *ui_manager;
	RhythmDBEntry *entry;
	GtkWindow *window;
	char *uifile;

	rb_debug ("activating status icon plugin");

	plugin = RB_STATUS_ICON_PLUGIN (bplugin);
	g_object_get (shell,
		      "shell-player", &plugin->priv->shell_player,
		      "db", &plugin->priv->db,
		      "ui-manager", &ui_manager,
		      "window", &window,
		      NULL);
	plugin->priv->shell = g_object_ref (shell);

	/* create action group for the tray menu */
	plugin->priv->action_group = gtk_action_group_new ("StatusIconActions");
	gtk_action_group_set_translation_domain (plugin->priv->action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_actions (plugin->priv->action_group,
				      rb_status_icon_plugin_actions,
				      G_N_ELEMENTS (rb_status_icon_plugin_actions),
				      plugin);
	gtk_action_group_add_toggle_actions (plugin->priv->action_group,
					     rb_status_icon_plugin_toggle_entries,
					     G_N_ELEMENTS (rb_status_icon_plugin_toggle_entries),
					     plugin);
	sync_actions (plugin);

	gtk_ui_manager_insert_action_group (ui_manager, plugin->priv->action_group, 0);

	/* add icon menu UI */
	uifile = rb_plugin_find_file (bplugin, "status-icon-ui.xml");
	if (uifile != NULL) {
		plugin->priv->ui_merge_id = gtk_ui_manager_add_ui_from_file (ui_manager, uifile, NULL);
		g_free (uifile);
	}

	/* connect various things */
	g_signal_connect_object (plugin->priv->shell, "visibility-changed", G_CALLBACK (visibility_changed_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->shell, "visibility-changing", G_CALLBACK (visibility_changing_cb), plugin, G_CONNECT_AFTER);
	g_signal_connect_object (plugin->priv->shell, "notify-playing-entry", G_CALLBACK (shell_notify_playing_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->shell, "notify-custom", G_CALLBACK (shell_notify_custom_cb), plugin, 0);

	g_signal_connect_object (plugin->priv->shell_player, "playing-song-changed", G_CALLBACK (playing_entry_changed_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->shell_player, "elapsed-changed", G_CALLBACK (elapsed_changed_cb), plugin, 0);

	g_signal_connect_object (plugin->priv->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_COVER_ART,
				 G_CALLBACK (db_art_metadata_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_TITLE,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_ARTIST,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);
	g_signal_connect_object (plugin->priv->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_ALBUM,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);

	g_signal_connect_object (window, "delete-event", G_CALLBACK (window_delete_event_cb), plugin, 0);

	/* read config */
	eel_gconf_monitor_add (CONF_PLUGIN_SETTINGS);
	plugin->priv->gconf_notify_id =
		eel_gconf_notification_add (CONF_PLUGIN_SETTINGS,
					    (GConfClientNotifyFunc) config_notify_cb,
					    plugin);

	maybe_upgrade_preferences (plugin);

	plugin->priv->icon_mode = eel_gconf_get_integer (CONF_STATUS_ICON_MODE);
	plugin->priv->notify_mode = eel_gconf_get_integer (CONF_NOTIFICATION_MODE);

	/* create status icon */
	create_status_icon (plugin);
	update_status_icon_visibility (plugin, FALSE);

	/* update everything in case we're already playing something */
	entry = rb_shell_player_get_playing_entry (plugin->priv->shell_player);
	if (entry != NULL) {
		update_current_playing_data (plugin, entry);
		rhythmdb_entry_unref (entry);
	}
	update_tooltip (plugin);

	g_object_unref (ui_manager);
	g_object_unref (window);
}

static void
impl_deactivate	(RBPlugin *bplugin,
		 RBShell *shell)
{
	RBStatusIconPlugin *plugin;
	GtkUIManager *ui_manager;
	GtkWindow *window;

	plugin = RB_STATUS_ICON_PLUGIN (bplugin);
	g_object_get (plugin->priv->shell, "ui-manager", &ui_manager, NULL);

	store_window_visibility (plugin);

	/* stop watching for config changes */
	if (plugin->priv->gconf_notify_id != 0) {
		eel_gconf_notification_remove (plugin->priv->gconf_notify_id);
		eel_gconf_monitor_remove (CONF_PLUGIN_SETTINGS);
		plugin->priv->gconf_notify_id = 0;
	}

	/* remove UI bits */
	if (plugin->priv->ui_merge_id != 0) {
		gtk_ui_manager_remove_ui (ui_manager, plugin->priv->ui_merge_id);
		plugin->priv->ui_merge_id = 0;
	}

	if (plugin->priv->action_group != NULL) {
		gtk_ui_manager_remove_action_group (ui_manager, plugin->priv->action_group);

		g_object_unref (plugin->priv->action_group);
		plugin->priv->action_group = NULL;
	}

	/* remove notification popups */
	cleanup_notification (plugin);

	/* remove icon */
	if (plugin->priv->tray_icon != NULL) {
		cleanup_status_icon (plugin);
		plugin->priv->tray_icon = NULL;
	}

	/* disconnect signal handlers used to update the icon */
	if (plugin->priv->shell_player != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->priv->shell_player, playing_entry_changed_cb, plugin);
		g_signal_handlers_disconnect_by_func (plugin->priv->shell_player, elapsed_changed_cb, plugin);

		g_object_unref (plugin->priv->shell_player);
		plugin->priv->shell_player = NULL;
	}

	if (plugin->priv->db != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->priv->db, db_art_metadata_cb, plugin);
		g_signal_handlers_disconnect_by_func (plugin->priv->db, db_stream_metadata_cb, plugin);

		g_object_unref (plugin->priv->db);
		plugin->priv->db = NULL;
	}

	if (plugin->priv->config_dialog != NULL) {
		gtk_widget_destroy (plugin->priv->config_dialog);
		plugin->priv->config_dialog = NULL;
	}

	g_object_unref (ui_manager);

	g_object_get (plugin->priv->shell, "window", &window, NULL);
	g_signal_handlers_disconnect_by_func (window, window_delete_event_cb, plugin);
	g_object_unref (window);

	g_signal_handlers_disconnect_by_func (plugin->priv->shell, visibility_changed_cb, plugin);
	g_signal_handlers_disconnect_by_func (plugin->priv->shell, visibility_changing_cb, plugin);
	g_signal_handlers_disconnect_by_func (plugin->priv->shell, shell_notify_playing_cb, plugin);
	g_signal_handlers_disconnect_by_func (plugin->priv->shell, shell_notify_custom_cb, plugin);
	g_object_unref (plugin->priv->shell);
	plugin->priv->shell = NULL;

	/* forget what's playing */
	g_free (plugin->priv->current_title);
	g_free (plugin->priv->current_album_and_artist);
	g_free (plugin->priv->tooltip_markup);
	plugin->priv->current_title = NULL;
	plugin->priv->current_album_and_artist = NULL;
	plugin->priv->tooltip_markup = NULL;

	forget_pixbufs (plugin);
}

static void
rb_status_icon_plugin_init (RBStatusIconPlugin *plugin)
{
	GtkIconTheme *theme;
	gint size;

	rb_debug ("RBStatusIconPlugin initialising");

	plugin->priv = G_TYPE_INSTANCE_GET_PRIVATE (plugin,
						    RB_TYPE_STATUS_ICON_PLUGIN,
						    RBStatusIconPluginPrivate);

	theme = gtk_icon_theme_get_default ();

	gtk_icon_size_lookup (GTK_ICON_SIZE_DIALOG, &size, NULL);
	plugin->priv->tooltip_app_pixbuf = gtk_icon_theme_load_icon (theme, RB_APP_ICON, size, 0, NULL);
}


static void
rb_status_icon_plugin_class_init (RBStatusIconPluginClass *klass)
{
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;

	plugin_class->create_configure_dialog = impl_get_config_widget;

	g_type_class_add_private (klass, sizeof (RBStatusIconPluginPrivate));
}

