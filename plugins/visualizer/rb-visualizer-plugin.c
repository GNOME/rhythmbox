/*
 * rb-visualizer-plugin.c
 *
 * Copyright (C) 2006  Jonathan Matthew
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

/*
 * visualizer plugin.
 * - assumes we're going to continue using playbin
 *     (it would be nice if any replacement pipeline had equivalent
 *      hooks for a visualization->video output branch..)
 * - does nasty tricks using a fake visualizer to convince
 *   playbin to set up the machinery for visualization so we can
 *   enable it at any time without even pausing the pipeline
 *     (it would be nice if any replacement pipeline had the
 *      ability to splice in a visualization->video output
 *      branch at any time)
 * - does further nasty tricks to make sure the video sink always
 *     has a valid window ID to scribble on, so it doesn't go
 *     creating its own window
 *
 * things to do:
 * - libvisual opengl actors may require some work here too
 * - maybe mangle caps filter when the output window size changes?
 *    do something with aspect ratios?
 * - screensaver hack (theme?) mode
 *    I think this would just be a dbus method to set the window ID
 *    and enable visualization.  the screensaver hack/theme would
 *    just call this and pass in the correct window ID.  right?
 *    what would this do for multiple screens?
 * - store fullscreenness in gconf?
 * - possibly display notifications somehow?
 *
 * crack-related things to do:
 * - add effects elements (effectv stuff) into the video sink
 *
 * things to fix:
 */


#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <string.h>
#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <gdk/gdkx.h>
#include <gdk/gdkkeysyms.h>
#include <glib.h>
#include <glib-object.h>
#include <glade/glade.h>

#include <gst/gst.h>
#include <gst/gstutils.h>
#include <gst/interfaces/xoverlay.h>

#include "rb-plugin.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-player.h"
#include "rb-player-gst.h"
#include "rb-dialog.h"
#include "rb-file-helpers.h"
#include "rb-preferences.h"
#include "eel-gconf-extensions.h"

#ifdef WITH_DBUS
#include <dbus/dbus-glib.h>
#endif

#include "rb-vis-widget.h"

/* not going to create a new header just for this */
extern GType rb_fake_vis_get_type (void);

/* preferences */
#define CONF_VIS_PREFIX  CONF_PREFIX "/plugins/visualizer"
#define CONF_VIS_ELEMENT CONF_VIS_PREFIX "/element"
#define CONF_VIS_QUALITY CONF_VIS_PREFIX "/quality"
#define CONF_VIS_SCREEN  CONF_VIS_PREFIX "/screen"
#define CONF_VIS_MONITOR CONF_VIS_PREFIX "/monitor"

/* defaults */
#define DEFAULT_VIS_ELEMENT	"goom"
#define DEFAULT_VIS_QUALITY	1

/* plugin */
#define RB_TYPE_VISUALIZER_PLUGIN		(rb_visualizer_plugin_get_type ())
#define RB_VISUALIZER_PLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPlugin))
#define RB_VISUALIZER_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPluginClass))
#define RB_IS_VISUALIZER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VISUALIZER_PLUGIN))
#define RB_IS_VISUALIZER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VISUALIZER_PLUGIN))
#define RB_VISUALIZER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPluginClass))

#define HIDE_CONTROLS_TIMEOUT	5 * 1000

#define VISUALIZER_DBUS_PATH	"/org/gnome/Rhythmbox/Visualizer"

typedef struct {
	const char *name;
	const char *displayname;
} VisPluginInfo;

typedef struct {
	const char *name;
	int width;
	int height;
	gint fps_n;
	gint fps_d;
} VisualizerQuality;

typedef enum {
	EMBEDDED = 0,		/* stuck in main UI window */
	FULLSCREEN,		/* separate window, fullscreen */
	REMOTE_WINDOW		/* drawing on a remote window (not done yet) */
} VisualizerMode;

typedef struct
{
	RBPlugin parent;
	RBShell *shell;
	RBShellPlayer *shell_player;
	RBPlayer *player;

	/* visualizer and output window */
	GtkWidget *vis_window;	/* for fullscreen */
	GtkWidget *vis_shell;	/* notebook page */
	GtkWidget *vis_box;
	GtkWidget *vis_widget;
	GstElement *visualizer;
	GstElement *video_sink;
	GstElement *playbin;
	GstXOverlay *xoverlay;
	gint bus_sync_id;
	GdkWindow *fake_window;
	gboolean window_id_set;

	gboolean enable_deferred;
	gboolean active;
	VisualizerMode mode;
	gboolean controls_shown;
	gboolean screen_controls_shown;
	gint hide_controls_id;
	unsigned long remote_window;

	/* signal handler IDs */
	gulong playbin_notify_id;
	gulong selected_source_notify_id;
	gulong shell_visibility_change_id;
	gulong playing_song_changed_id;
	gulong playing_changed_id;
	gulong window_title_change_id;

	/* ui */
	gint merge_id;
	GtkActionGroup *action_group;
	gboolean has_desktop_manager;

	/* control ui */
	GtkWidget *control_widget;
	GtkWidget *root_window_button;
	GtkWidget *fullscreen_button;
	GtkWidget *leave_fullscreen_button;
	GtkWidget *screen_label;
	GtkWidget *screen_combo;
	GtkWidget *element_combo;
	GtkWidget *quality_combo;
	GList *vis_plugin_list;

	GtkWidget *play_control_widget;
	GtkWidget *song_info_label;
	GtkWidget *play_button;
	gboolean syncing_play;

#ifdef WITH_DBUS
	gboolean dbus_interface_registered;
#endif
	gboolean plugin_enabled;

} RBVisualizerPlugin;

typedef struct
{
	RBPluginClass parent_class;
} RBVisualizerPluginClass;



G_MODULE_EXPORT GType register_rb_plugin (GTypeModule *module);
GType	rb_visualizer_plugin_get_type		(void) G_GNUC_CONST;

static void rb_visualizer_plugin_cmd_toggle (GtkAction *action,
					     RBVisualizerPlugin *pi);

static void create_controls (RBVisualizerPlugin *pi);
static void enable_visualization (RBVisualizerPlugin *pi);
static gboolean disable_visualization (RBVisualizerPlugin *pi);
static void update_window (RBVisualizerPlugin *plugin, VisualizerMode mode, int screen, int monitor);

#ifdef WITH_DBUS
gboolean rb_visualizer_start_remote (RBVisualizerPlugin *plugin, unsigned long window_id, GError **error);
gboolean rb_visualizer_stop_remote (RBVisualizerPlugin *plugin, GError **error);

#include "rb-visualizer-glue.h"
#endif

static GtkToggleActionEntry rb_visualizer_plugin_toggle_actions [] =
{
	{ "ToggleVisualizer", NULL, N_("Visualization"), NULL,
	  N_("Start or stop visualization"),
	  G_CALLBACK (rb_visualizer_plugin_cmd_toggle) },
};

/* these match totem's settings */
static const VisualizerQuality vis_quality[] = {
	{ N_("Small"),		200,	150,	10,	1 },
	{ N_("Normal"),		320,	240,	20,	1 },
	{ N_("Large"),		640,	480,	25,	1 },
	{ N_("Extra Large"),	800,	600,	30,	1 },
};

/* "quality" to use for fake visualization; should be small and
 * low framerate, but not so slow it makes a noticeable difference
 * to playback start time.  the video sink needs to preroll too,
 * so if the framerate is 1/10, that will take 10 seconds.
 */
static const VisualizerQuality fake_vis_quality = { "", 60, 60, 1, 1 };

RB_PLUGIN_REGISTER(RBVisualizerPlugin, rb_visualizer_plugin)

static void
rb_visualizer_plugin_init (RBVisualizerPlugin *plugin)
{
	rb_debug ("RBVisualizerPlugin initialising");
}

static void
rb_visualizer_plugin_dispose (GObject *object)
{
	RBVisualizerPlugin *plugin = RB_VISUALIZER_PLUGIN (object);
	rb_debug ("RBVisualizerPlugin disposing");

	if (plugin->play_control_widget != NULL) {
		g_object_unref (plugin->play_control_widget);
		plugin->play_control_widget = NULL;
	}

	if (plugin->control_widget != NULL) {
		g_object_unref (plugin->control_widget);
		plugin->control_widget = NULL;
	}

	if (plugin->vis_widget != NULL) {
		g_object_unref (plugin->vis_widget);
		plugin->vis_widget = NULL;
	}

	if (plugin->vis_box != NULL) {
		g_object_unref (plugin->vis_box);
		plugin->vis_box = NULL;
	}

	if (plugin->vis_window != NULL) {
		g_object_unref (plugin->vis_window);
		plugin->vis_window = NULL;
	}

	if (plugin->shell_player != NULL) {
		g_object_unref (plugin->shell_player);
		plugin->shell_player = NULL;
	}

	if (plugin->player != NULL) {
		g_object_unref (plugin->player);
		plugin->player = NULL;
	}

	G_OBJECT_CLASS (rb_visualizer_plugin_parent_class)->dispose (object);
}

static void
rb_visualizer_plugin_finalize (GObject *object)
{
	rb_debug ("RBVisualizerPlugin finalising");

	G_OBJECT_CLASS (rb_visualizer_plugin_parent_class)->finalize (object);
}

static gboolean
check_desktop_manager (RBVisualizerPlugin *plugin, int screen)
{
	char *selection_name;
	GdkDisplay *display;
	GdkAtom selection_atom;

	if (screen == -1)
		screen = 0;
	display = gdk_display_get_default ();

	selection_name = g_strdup_printf ("_NET_DESKTOP_MANAGER_S%d", screen);
	selection_atom = gdk_atom_intern (selection_name, FALSE);
	g_free (selection_name);

	if (XGetSelectionOwner (GDK_DISPLAY_XDISPLAY (display),
				gdk_x11_atom_to_xatom_for_display (display, selection_atom)) != None) {
		return TRUE;
	}
	return FALSE;
}

static gboolean
find_xoverlay (RBVisualizerPlugin *plugin)
{
	/* (re-)locate xoverlay */
	if (plugin->xoverlay) {
		g_object_unref (plugin->xoverlay);
		plugin->xoverlay = NULL;
	}

	if (GST_IS_BIN (plugin->video_sink)) {
		GstElement *overlay;

		overlay = gst_bin_get_by_interface (GST_BIN (plugin->video_sink),
						    GST_TYPE_X_OVERLAY);
		plugin->xoverlay = GST_X_OVERLAY (overlay);
		rb_debug ("found xoverlay in video bin");
	} else if (GST_IS_X_OVERLAY (plugin->video_sink)) {
		plugin->xoverlay = GST_X_OVERLAY (plugin->video_sink);
		g_object_ref (G_OBJECT (plugin->xoverlay));
		rb_debug ("found video_sink implementing xoverlay");
	}

	if (!plugin->xoverlay) {
		g_warning ("Couldn't find an x overlay");
		return FALSE;
	}

	return TRUE;
}

static void
bus_sync_message_cb (GstBus *bus, GstMessage *msg, RBVisualizerPlugin *plugin)
{
	XID window;
	if (msg->structure == NULL ||
	    gst_structure_has_name (msg->structure, "prepare-xwindow-id") == FALSE) {
		return;
	}

	/* update our idea of where the xoverlay is */
	find_xoverlay (plugin);

	/* feed it the window xid */
	switch (plugin->mode) {
	case EMBEDDED:
	case FULLSCREEN:
		if (plugin->vis_widget != NULL) {
			g_object_get (plugin->vis_widget, "window-xid", &window, NULL);
			if (window == 0) {
				window = GDK_WINDOW_XWINDOW (plugin->fake_window);
				rb_debug ("setting fake window id %lu", window);
			} else {
				rb_debug ("setting window id %lu in prepare-xwindow-id handler", window);
			}
		}
		break;
	case REMOTE_WINDOW:
		window = plugin->remote_window;
		rb_debug ("setting remote window id %lu", window);
		break;
	}

	gst_x_overlay_set_xwindow_id (plugin->xoverlay, window);
	plugin->window_id_set = TRUE;
}


static void
fixate_vis_caps (RBVisualizerPlugin *pi, GstElement *vis_element, GstElement *capsfilter, int quality)
{
	GstPad *pad;
	GstCaps *caps = NULL;
	const VisualizerQuality *q;

	if (quality < 0)
		quality = eel_gconf_get_integer (CONF_VIS_QUALITY);

	if (quality < 0 || quality > G_N_ELEMENTS (vis_quality))
		quality = DEFAULT_VIS_QUALITY;

	if (pi->active)
		q = &vis_quality[quality];
	else
		q = &fake_vis_quality;

	pad = gst_element_get_pad (vis_element, "src");
	caps = gst_pad_get_allowed_caps (pad);
	gst_object_unref (pad);

	if (caps && !gst_caps_is_fixed (caps)) {
		guint i;
		char *dbg;

		caps = gst_caps_make_writable (caps);
		for (i = 0; i < gst_caps_get_size (caps); i++) {
			GstStructure *s = gst_caps_get_structure (caps, i);

			gst_structure_fixate_field_nearest_int (s, "width", q->width);
			gst_structure_fixate_field_nearest_int (s, "height", q->height);
			gst_structure_fixate_field_nearest_fraction (s, "framerate", q->fps_n, q->fps_d);
		}

		dbg = gst_caps_to_string (caps);
		rb_debug ("setting fixed caps on capsfilter: %s", dbg);
		g_free (dbg);

		g_object_set (capsfilter, "caps", caps, NULL);
	} else if (caps) {
		char *dbg = gst_caps_to_string (caps);
		rb_debug ("vis element caps already fixed: %s", dbg);
		g_free (dbg);
	} else {
		rb_debug ("vis element has no caps");
	}

	if (GST_IS_CAPS (caps))
		gst_caps_unref (caps);
}

static void
update_visualizer (RBVisualizerPlugin *plugin, const char *vis_override, int quality)
{
	GstElement *capsfilter;
	GstPad *pad;
	char *vis_element_name = NULL;
	GstElement *vis_plugin;

	if (plugin->playbin == NULL)
		return;

	if (plugin->visualizer)
		g_object_unref (plugin->visualizer);

	plugin->visualizer = gst_bin_new (NULL);

	/* set up capsfilter */
	capsfilter = gst_element_factory_make ("capsfilter", NULL);
	gst_bin_add (GST_BIN (plugin->visualizer), capsfilter);

	pad = gst_element_get_pad (capsfilter, "src");
	gst_element_add_pad (plugin->visualizer, gst_ghost_pad_new ("src", pad));
	gst_object_unref (pad);

	/* set up visualizer */
	if (plugin->active) {
		if (vis_override) {
			vis_element_name = g_strdup (vis_override);
		} else {
			vis_element_name = eel_gconf_get_string (CONF_VIS_ELEMENT);
		}

		if (vis_element_name == NULL) {
			vis_element_name = g_strdup (DEFAULT_VIS_ELEMENT);
		}
		rb_debug ("creating new visualizer: %s", vis_element_name);

		vis_plugin = gst_element_factory_make (vis_element_name, NULL);
		gst_bin_add (GST_BIN (plugin->visualizer), vis_plugin);
		g_free (vis_element_name);

	} else {
		vis_plugin = g_object_new (rb_fake_vis_get_type (), NULL);
		gst_bin_add (GST_BIN (plugin->visualizer), vis_plugin);
	}

	pad = gst_element_get_pad (vis_plugin, "sink");
	gst_element_add_pad (plugin->visualizer, gst_ghost_pad_new ("sink", pad));
	gst_object_unref (pad);

	gst_element_link (vis_plugin, capsfilter);
	fixate_vis_caps (plugin, vis_plugin, capsfilter, quality);

	g_object_ref (plugin->visualizer);

	g_object_set (plugin->playbin, "vis-plugin", plugin->visualizer, NULL);
}

static void
actually_hide_controls (RBVisualizerPlugin *plugin)
{
	rb_debug ("hiding controls");
	switch (plugin->mode) {
	case FULLSCREEN:
		/* grab focus for the output window.  this should
		 * allow the user to just hit escape at any time
		 * to exit fullscreen mode.
		 */
		gtk_widget_grab_focus (plugin->vis_widget);
		/* fall through */
	case EMBEDDED:
		gtk_widget_hide (plugin->control_widget);
		gtk_widget_hide (plugin->play_control_widget);
		plugin->controls_shown = FALSE;
		break;
	case REMOTE_WINDOW:
		/* always keep controls shown */
		break;
	}
}

static gboolean
hide_controls_cb (RBVisualizerPlugin *plugin)
{
	plugin->hide_controls_id = 0;
	actually_hide_controls (plugin);
	return FALSE;
}

static void
show_controls (RBVisualizerPlugin *plugin, gboolean play_controls_only)
{
	gboolean autohide = TRUE;

	if (plugin->control_widget == NULL ||
	    plugin->play_control_widget == NULL)
		return;

	/* display the controls, if they're not already visible */
	if (plugin->controls_shown == FALSE) {
		rb_debug ("showing controls");
		if (play_controls_only == FALSE) {
			gtk_widget_show (plugin->control_widget);
			if (plugin->screen_controls_shown) {
				gtk_widget_show (plugin->screen_label);
				gtk_widget_show (plugin->screen_combo);
			} else {
				gtk_widget_hide (plugin->screen_label);
				gtk_widget_hide (plugin->screen_combo);
			}
		}

		switch (plugin->mode) {
		case EMBEDDED:
			gtk_widget_hide (plugin->play_control_widget);

			gtk_widget_show (plugin->fullscreen_button);
			if (plugin->has_desktop_manager) {
				gtk_widget_hide (plugin->root_window_button);
			} else {
				gtk_widget_show (plugin->root_window_button);
			}
			gtk_widget_hide (plugin->leave_fullscreen_button);
			break;
		case FULLSCREEN:
			gtk_widget_show (plugin->play_control_widget);

			gtk_widget_hide (plugin->fullscreen_button);
			gtk_widget_hide (plugin->root_window_button);
			gtk_widget_show (plugin->leave_fullscreen_button);
			break;
		case REMOTE_WINDOW:
			gtk_widget_hide (plugin->play_control_widget);

			gtk_widget_hide (plugin->fullscreen_button);
			gtk_widget_hide (plugin->root_window_button);
			gtk_widget_show (plugin->leave_fullscreen_button);
			autohide = FALSE;
			break;
		}

		/* slight hack; unimportant */
		if (play_controls_only == FALSE)
			plugin->controls_shown = TRUE;
	}

	/* start timeout to hide them again */
	if (plugin->hide_controls_id)
		g_source_remove (plugin->hide_controls_id);

	if (autohide) {
		plugin->hide_controls_id = g_timeout_add (HIDE_CONTROLS_TIMEOUT,
							  (GSourceFunc) hide_controls_cb,
							  plugin);
	}
}


static gboolean
rb_visualizer_plugin_motion_notify_cb (GtkWidget *vis_widget,
				       GdkEventMotion *event,
				       RBVisualizerPlugin *plugin)
{
	show_controls (plugin, FALSE);
	return FALSE;
}

static gboolean
rb_visualizer_plugin_key_release_cb (GtkWidget *vis_widget,
				     GdkEventKey *event,
				     RBVisualizerPlugin *plugin)
{
	GtkAction *action;

	if (event->keyval != GDK_Escape)
		return FALSE;

	switch (plugin->mode) {
	case EMBEDDED:
		/* stop visualization? */
		disable_visualization (plugin);
		action = gtk_action_group_get_action (plugin->action_group, "ToggleVisualizer");
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);
		update_visualizer (plugin, NULL, -1);
		break;
	case FULLSCREEN:
		/* leave fullscreen */
		update_window (plugin, EMBEDDED, -1, -1);
		enable_visualization (plugin);
		break;
	case REMOTE_WINDOW:
		/* ??? .. can this even happen? */
		break;
	}

	return FALSE;
}

static void
rb_visualizer_plugin_window_id_notify_cb (GObject *vis_widget,
					  GParamSpec *pspec,
					  RBVisualizerPlugin *plugin)
{
	XID window;
	g_object_get (plugin->vis_widget, "window-xid", &window, NULL);
	gst_x_overlay_set_xwindow_id (plugin->xoverlay, window);
}

static GdkScreen *
get_screen (RBVisualizerPlugin *plugin, int screen)
{
	GdkDisplay *display;
	GdkScreen *gdk_screen;

	display = gdk_display_get_default ();
	if (screen == -1 || screen >= gdk_display_get_n_screens (display)) {
		/* use current screen */
		GtkWindow *w;

		g_object_get (plugin->shell, "window", &w, NULL);
		gdk_screen = gtk_window_get_screen (w);
		g_object_unref (w);
	} else {
		gdk_screen = gdk_display_get_screen (display, screen);
	}

	return gdk_screen;
}

static void
update_window (RBVisualizerPlugin *plugin, VisualizerMode mode, int screen, int monitor)
{
	gboolean need_vis_widget;

	/* remove the visualizer container from whatever it's currently sitting in */
	if (plugin->vis_box == NULL) {
		plugin->vis_box = gtk_vbox_new (FALSE, 0);
		g_object_ref (plugin->vis_box);

		/* hrm, don't entirely like this */
		create_controls (plugin);
		gtk_box_pack_start (GTK_BOX (plugin->vis_box), plugin->play_control_widget, FALSE, FALSE, 6);
		gtk_box_pack_end (GTK_BOX (plugin->vis_box), plugin->control_widget, FALSE, FALSE, 6);
	} else {
		switch (plugin->mode) {
		case EMBEDDED:
			gtk_container_remove (GTK_CONTAINER (plugin->vis_shell),
					      plugin->vis_box);
			break;
		case FULLSCREEN:
			gtk_container_remove (GTK_CONTAINER (plugin->vis_window),
					      plugin->vis_box);
			break;
		case REMOTE_WINDOW:
			/* would be nice to force the window to redraw itself here.. */
			rb_shell_remove_widget (plugin->shell,
						plugin->vis_box,
						RB_SHELL_UI_LOCATION_MAIN_BOTTOM);
			break;
		}
	}

	/* clean up the old output window */
	if (plugin->vis_widget != NULL) {
		rb_debug ("destroying old output window");
		gtk_widget_hide (plugin->vis_widget);
		gtk_widget_destroy (plugin->vis_widget);
		g_object_unref (plugin->vis_widget);
		plugin->vis_widget = NULL;
	}

	plugin->mode = mode;
	switch (plugin->mode) {
	case EMBEDDED:
	case FULLSCREEN:
		need_vis_widget = TRUE;
		break;
	case REMOTE_WINDOW:
		need_vis_widget = FALSE;
		break;
	default:
		g_assert_not_reached ();
		break;
	}

	if (need_vis_widget) {
		plugin->vis_widget = GTK_WIDGET (g_object_new (RB_TYPE_VIS_WIDGET, NULL));
		g_object_ref (plugin->vis_widget);

		g_signal_connect_object (plugin->vis_widget,
					 "motion-notify-event",
					 G_CALLBACK (rb_visualizer_plugin_motion_notify_cb),
					 plugin, 0);
		g_signal_connect_object (plugin->vis_widget,
					 "key-release-event",
					 G_CALLBACK (rb_visualizer_plugin_key_release_cb),
					 plugin, 0);
		g_signal_connect_object (plugin->vis_widget,
					 "notify::window-xid",
					 G_CALLBACK (rb_visualizer_plugin_window_id_notify_cb),
					 plugin, 0);
		gtk_box_pack_start (GTK_BOX (plugin->vis_box), plugin->vis_widget, TRUE, TRUE, 0 /* 6? */);
	}


	/* insert the output window + controls into the correct container */
	switch (plugin->mode) {
	case EMBEDDED:
		gtk_box_pack_start (GTK_BOX (plugin->vis_shell), plugin->vis_box, TRUE, TRUE, 0);
		break;
	case FULLSCREEN:
	{
		GdkScreen *gdk_screen;

		gdk_screen = get_screen (plugin, screen);
		gtk_window_set_screen (GTK_WINDOW (plugin->vis_window), gdk_screen);

		if (monitor != -1 || monitor >= gdk_screen_get_n_monitors (gdk_screen)) {
			GdkRectangle rect;

			gdk_screen_get_monitor_geometry (gdk_screen, monitor, &rect);
			gtk_window_move (GTK_WINDOW (plugin->vis_window), rect.x, rect.y);
			gtk_window_resize (GTK_WINDOW (plugin->vis_window), rect.width, rect.height);
		}

		gtk_container_add (GTK_CONTAINER (plugin->vis_window), plugin->vis_box);
		break;
	}
	case REMOTE_WINDOW:
		if (plugin->remote_window == 0) {
			GdkScreen *gdk_screen;
			GdkWindow *root_window;

			/* this is probably going to look crap on multi-monitor screens */
			gdk_screen = get_screen (plugin, screen);
			root_window = gdk_screen_get_root_window (gdk_screen);
			plugin->remote_window = GDK_WINDOW_XWINDOW (root_window);
			rb_debug ("got root window id %lu", plugin->remote_window);
		}

		if (plugin->xoverlay) {
			gst_x_overlay_set_xwindow_id (plugin->xoverlay, plugin->remote_window);
		}

		rb_shell_add_widget (plugin->shell,
				     plugin->vis_box,
				     RB_SHELL_UI_LOCATION_MAIN_BOTTOM);
		rb_shell_notebook_set_page (plugin->shell, NULL);
		break;
	}

	/* update controls */
	plugin->controls_shown = FALSE;
	show_controls (plugin, FALSE);
}

static void
mutate_playbin (RBPlayer *player, GstElement *playbin, RBVisualizerPlugin *plugin)
{
	GstElement *current_vis_plugin;
	GstElement *current_video_sink;
	GstBus *bus;

	if (playbin == plugin->playbin)
		return;

	rb_debug ("mutating playbin");

	/* check no one has already set the playbin properties we're interested in */
	g_object_get (G_OBJECT (playbin),
		      "vis-plugin", &current_vis_plugin,
		      "video-sink", &current_video_sink,
		      NULL);

	/* ignore fakesinks */
	if (current_video_sink != NULL) {
		const char *factoryname;
		GstElementFactory *factory;

		factory = gst_element_get_factory (current_video_sink);
		factoryname = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (factory));
		if (strcmp (factoryname, "fakesink") == 0) {
			g_object_unref (current_video_sink);
			current_video_sink = NULL;
		}
	}

	if ((current_vis_plugin != plugin->visualizer) || (current_video_sink != plugin->video_sink)) {
		g_warning ("sink and/or vis plugin already set on playbin");
		if (current_vis_plugin)
			g_object_unref (current_vis_plugin);
		if (current_video_sink)
			g_object_unref (current_video_sink);
		return;
	}

	/* detach from old playbin (this should never really happen) */
	if (plugin->playbin) {
		if (plugin->bus_sync_id) {
			g_signal_handler_disconnect (plugin->playbin, plugin->bus_sync_id);
			plugin->bus_sync_id = 0;
		}

		g_object_unref (plugin->playbin);
	}

	/* attach to new playbin */
	plugin->playbin = g_object_ref (playbin);
	bus = gst_element_get_bus (playbin);
	gst_bus_set_sync_handler (bus,
				  gst_bus_sync_signal_handler,
				  plugin);
	plugin->bus_sync_id = g_signal_connect (bus,
						"sync-message::element",
						G_CALLBACK (bus_sync_message_cb),
						plugin);

	plugin->video_sink = gst_element_factory_make ("gconfvideosink", "videosink");
	gst_element_set_state (plugin->video_sink, GST_STATE_READY);
	find_xoverlay (plugin);
	g_object_set (playbin, "video-sink", plugin->video_sink, NULL);

	update_visualizer (plugin, NULL, -1);
}

static void
playbin_notify_cb (GObject *object, GParamSpec *arg, RBVisualizerPlugin *pi)
{
	GstElement *playbin;

	g_object_get (object, "playbin", &playbin, NULL);
	if (playbin) {
		mutate_playbin (RB_PLAYER (object), playbin, pi);
		g_object_unref (playbin);
	}
}

/* next two functions stolen directly from totem */
static gboolean
totem_display_is_local (void)
{
	const char *name, *work;
	int display, screen;
	gboolean has_hostname;

	name = gdk_display_get_name (gdk_display_get_default ());
	if (name == NULL)
		return TRUE;

	work = strstr (name, ":");
	if (work == NULL)
		return TRUE;

	has_hostname = (work - name) > 0;

	/* Get to the character after the colon */
	work++;
	if (work == NULL)
		return TRUE;

	if (sscanf (work, "%d.%d", &display, &screen) != 2)
		return TRUE;

	if (has_hostname == FALSE)
		return TRUE;

	if (display < 10)
		return TRUE;

	return FALSE;
}

static gboolean
confirm_visualization (void)
{
        GtkWidget *dialog;
        int answer;

	if (totem_display_is_local ())
		return TRUE;

        dialog =
                gtk_message_dialog_new (NULL,
                                GTK_DIALOG_MODAL,
                                GTK_MESSAGE_ERROR,
                                GTK_BUTTONS_YES_NO,
                                _("Enable visual effects?"));
        gtk_message_dialog_format_secondary_text (GTK_MESSAGE_DIALOG (dialog),
                                                  _("It seems you are running Rhythmbox remotely.\n"
                                                    "Are you sure you want to enable the visual "
                                                    "effects?"));
        gtk_container_set_border_width (GTK_CONTAINER (dialog), 5);
        gtk_dialog_set_default_response (GTK_DIALOG (dialog),
                        GTK_RESPONSE_NO);
        answer = gtk_dialog_run (GTK_DIALOG (dialog));
        gtk_widget_destroy (dialog);

        return (answer == GTK_RESPONSE_YES ? TRUE : FALSE);
}

static void
enable_visualization (RBVisualizerPlugin *pi)
{
	rb_debug ("enabling visualization");

	pi->active = TRUE;
	switch (pi->mode) {
	case EMBEDDED:
		gtk_widget_show_all (pi->vis_shell);
		gtk_widget_hide (pi->vis_window);
		rb_shell_notebook_set_page (pi->shell, pi->vis_shell);
		break;
	case FULLSCREEN:
		gtk_widget_hide (pi->vis_shell);
		gtk_widget_show_all (pi->vis_window);
		gtk_window_fullscreen (GTK_WINDOW (pi->vis_window));
		break;
	case REMOTE_WINDOW:
		gtk_widget_show (pi->vis_box);
		break;
	}

	actually_hide_controls (pi);
}

static gboolean
disable_visualization (RBVisualizerPlugin *pi)
{
	rb_debug ("disabling visualization");

	switch (pi->mode) {
	case EMBEDDED:
		gtk_widget_hide_all (pi->vis_box);
		rb_shell_notebook_set_page (pi->shell, NULL);
		break;
	case FULLSCREEN:
		gtk_window_unfullscreen (GTK_WINDOW (pi->vis_window));
		gtk_widget_hide_all (pi->vis_window);
		break;
	case REMOTE_WINDOW:
		gtk_widget_hide (pi->vis_box);
		break;
	}
	pi->active = FALSE;
	return FALSE;
}

static void
rb_visualizer_plugin_cmd_toggle (GtkAction *action, RBVisualizerPlugin *pi)
{
	gboolean enabled;
	rb_debug ("visualization toggled");

	enabled = gtk_toggle_action_get_active (GTK_TOGGLE_ACTION (action));
	if (enabled) {
		/* if on a remote display, ask for confirmation first */
		if (confirm_visualization () == FALSE) {
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action),
						      FALSE);
			return;
		}
		/* if playing something, enable visualization now, otherwise,
		 * wait until we start playing.
		 */
		if (rb_player_opened (pi->player)) {
			enable_visualization (pi);
			update_visualizer (pi, NULL, -1);
		} else {
			pi->enable_deferred = TRUE;
		}
	} else {
		disable_visualization (pi);
		update_visualizer (pi, NULL, -1);
	}
}

static void
rb_visualizer_plugin_source_selected_cb (GObject *shell,
					 GParamSpec *arg,
					 RBVisualizerPlugin *plugin)
{
	GtkAction *action;

	switch (plugin->mode) {
	case EMBEDDED:
		if (plugin->active) {
			/* disable visualization */
			disable_visualization (plugin);
			action = gtk_action_group_get_action (plugin->action_group, "ToggleVisualizer");
			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);
			update_visualizer (plugin, NULL, -1);
		}
		break;
	case FULLSCREEN:
	case REMOTE_WINDOW:
		break;
	}

}

static void
rb_visualizer_plugin_window_title_change_cb (RBShellPlayer *player,
					     const char *title,
					     RBVisualizerPlugin *plugin)
{
	char *markup;

	markup = g_markup_printf_escaped ("<big><b>%s</b></big>", title);
	gtk_label_set_markup (GTK_LABEL (plugin->song_info_label), markup);
	g_free (markup);

	show_controls (plugin, TRUE);
}

static void
rb_visualizer_plugin_song_change_cb (RBShellPlayer *player,
				     RhythmDBEntry *entry,
				     RBVisualizerPlugin *plugin)
{
	GtkAction *action;
	action = gtk_action_group_get_action (plugin->action_group, "ToggleVisualizer");

	if (entry) {
		if (plugin->enable_deferred) {
			enable_visualization (plugin);
			update_visualizer (plugin, NULL, -1);
			plugin->enable_deferred = FALSE;

			gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
		}

	} else if (plugin->active) {
		/* disable, and re-enable when we start playing something */
		disable_visualization (plugin);
		update_visualizer (plugin, NULL, -1);
		plugin->enable_deferred = TRUE;
		gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), TRUE);
	}
}

static void
rb_visualizer_plugin_playing_changed_cb (RBShellPlayer *player,
					 gboolean playing,
					 RBVisualizerPlugin *plugin)
{
	plugin->syncing_play = TRUE;
	gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (plugin->play_button), playing);
	plugin->syncing_play = FALSE;

	show_controls (plugin, TRUE);
}

static void
rb_visualizer_plugin_shell_visibility_changed_cb (RBShell *shell,
						  gboolean visible,
						  RBVisualizerPlugin *plugin)
{
	if (plugin->active == FALSE)
		return;

	switch (plugin->mode) {
	case EMBEDDED:
		/* disable visualization when hiding the window */
		if (visible) {
			rb_debug ("re-enabling visualization");
			enable_visualization (plugin);
			update_visualizer (plugin, NULL, -1);
		} else {
			rb_debug ("disabling visualization until window is visible again");
			disable_visualization (plugin);
			update_visualizer (plugin, NULL, -1);
			plugin->active = TRUE;
		}
		break;
	case FULLSCREEN:
	case REMOTE_WINDOW:
		return;
	}
}

static void
impl_activate (RBPlugin *plugin,
	       RBShell *shell)
{
	gboolean connected = FALSE;
	RBVisualizerPlugin *pi = RB_VISUALIZER_PLUGIN (plugin);
	GtkUIManager *uim = NULL;
	GtkAction *action;
	char *ui_file;

	rb_fake_vis_get_type ();

	pi->shell = shell;

	/* find the player backend and connect to its pipeline mutation signal */
	g_object_get (shell, "shell-player", &pi->shell_player, NULL);
	if (pi->shell_player == NULL) {
		rb_debug ("couldn't find shell player object..");
		return;
	}

	g_object_get (pi->shell_player, "player", &pi->player, NULL);
	if (pi->player && g_object_class_find_property (G_OBJECT_GET_CLASS (pi->player), "playbin")) {
		GstElement *playbin;

		pi->playbin_notify_id =
			g_signal_connect_object (pi->player,
						 "notify::playbin",
						 (GCallback) playbin_notify_cb,
						 plugin,
						 0);

		g_object_get (G_OBJECT (pi->player), "playbin", &playbin, NULL);
		if (playbin) {
			mutate_playbin (pi->player, playbin, pi);
		}

		connected = TRUE;
	} else {
		g_warning ("no player backend exists or wrong type?");
		g_object_unref (pi->player);
		pi->player = NULL;
	}

	if (!connected)
		return;

	rb_debug ("connected to playbin mutation signal");

	/* create action group */
	pi->action_group = gtk_action_group_new ("VisualizerActions");
	gtk_action_group_set_translation_domain (pi->action_group, GETTEXT_PACKAGE);
	gtk_action_group_add_toggle_actions (pi->action_group,
					     rb_visualizer_plugin_toggle_actions,
					     G_N_ELEMENTS (rb_visualizer_plugin_toggle_actions),
					     pi);

	g_object_get (shell, "ui-manager", &uim, NULL);
	gtk_ui_manager_insert_action_group (uim, pi->action_group, 0);

	ui_file = rb_plugin_find_file (plugin, "visualizer-ui.xml");
	pi->merge_id = gtk_ui_manager_add_ui_from_file (uim,
							ui_file,
							NULL);
	g_free (ui_file);

	g_object_unref (uim);

	if (pi->vis_shell == NULL) {
		pi->vis_shell = gtk_vbox_new (FALSE, 0);
		rb_shell_add_widget (pi->shell, pi->vis_shell, RB_SHELL_UI_LOCATION_MAIN_NOTEBOOK);
	}

	if (pi->vis_window == NULL) {
		pi->vis_window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_skip_taskbar_hint (GTK_WINDOW (pi->vis_window), TRUE);
		g_object_ref (pi->vis_window);
	}

	/* real output window */
	update_window (pi, EMBEDDED, -1, -1);

	/* fake output window */
	if (pi->fake_window == NULL) {
		GdkWindowAttr attributes;
		gint attributes_mask;
		attributes.window_type = GDK_WINDOW_CHILD;
		attributes.x = 0;
		attributes.y = 0;
		attributes.width = 60;
		attributes.height = 60;
		attributes.wclass = GDK_INPUT_OUTPUT;
		attributes.event_mask = gtk_widget_get_events (pi->vis_widget);
		attributes.event_mask |= GDK_EXPOSURE_MASK;
		attributes_mask = GDK_WA_X | GDK_WA_Y;
		pi->fake_window = gdk_window_new (NULL, &attributes, attributes_mask);
	}

	/* could be smarter - check per-screen when the selected screen changes;
	 * but I think this will be good enough anyway.
	 */
	pi->has_desktop_manager = check_desktop_manager (pi, -1);

	action = gtk_action_group_get_action (pi->action_group, "ToggleVisualizer");
	gtk_toggle_action_set_active (GTK_TOGGLE_ACTION (action), FALSE);

	pi->selected_source_notify_id =
		g_signal_connect_object (pi->shell,
					 "notify::selected-source",
					 G_CALLBACK (rb_visualizer_plugin_source_selected_cb),
					 pi, 0);
	pi->shell_visibility_change_id =
		g_signal_connect_object (pi->shell,
					 "visibility-changed",
					 G_CALLBACK (rb_visualizer_plugin_shell_visibility_changed_cb),
					 pi, 0);
	pi->playing_song_changed_id =
		g_signal_connect_object (pi->shell_player,
					 "playing-song-changed",
					 G_CALLBACK (rb_visualizer_plugin_song_change_cb),
					 pi, 0);
	pi->playing_changed_id =
		g_signal_connect_object (pi->shell_player,
					 "playing-changed",
					 G_CALLBACK (rb_visualizer_plugin_playing_changed_cb),
					 pi, 0);
	pi->window_title_change_id =
		g_signal_connect_object (pi->shell_player,
					 "window-title-changed",
					 G_CALLBACK (rb_visualizer_plugin_window_title_change_cb),
					 pi, 0);

#ifdef WITH_DBUS
	/* add dbus interface */
	if (pi->dbus_interface_registered == FALSE) {
		DBusGConnection *conn;
		GError *error = NULL;

		conn = dbus_g_bus_get (DBUS_BUS_SESSION, &error);
		if (conn != NULL) {
			dbus_g_object_type_install_info (RB_TYPE_VISUALIZER_PLUGIN,
							 &dbus_glib_rb_visualizer_object_info);
			dbus_g_connection_register_g_object (conn,
							     VISUALIZER_DBUS_PATH,
							     G_OBJECT (plugin));
			pi->dbus_interface_registered = TRUE;
		}
	}
#endif
	pi->plugin_enabled = TRUE;
}

static void
impl_deactivate	(RBPlugin *plugin,
		 RBShell *shell)
{
	RBVisualizerPlugin *pi = RB_VISUALIZER_PLUGIN (plugin);
	GtkUIManager *uim;

	disable_visualization (pi);
	update_visualizer (pi, NULL, -1);

	/* remove ui */
	g_object_get (G_OBJECT (shell), "ui-manager", &uim, NULL);

	gtk_ui_manager_remove_ui (uim, pi->merge_id);
	pi->merge_id = 0;

	gtk_ui_manager_remove_action_group (uim, pi->action_group);

	/* can't remove the dbus interface.  it only goes away when the
	 * plugin object does, which is when the process exits.
	 */

	g_object_unref (uim);

	/* disconnect signal handlers */
	if (pi->playbin_notify_id != 0) {
		g_signal_handler_disconnect (pi->player, pi->playbin_notify_id);
		pi->playbin_notify_id = 0;
	}

	if (pi->selected_source_notify_id != 0) {
		g_signal_handler_disconnect (pi->shell, pi->selected_source_notify_id);
		pi->selected_source_notify_id = 0;
	}
	if (pi->shell_visibility_change_id != 0) {
		g_signal_handler_disconnect (pi->shell, pi->shell_visibility_change_id);
		pi->shell_visibility_change_id = 0;
	}
	if (pi->playing_song_changed_id != 0) {
		g_signal_handler_disconnect (pi->shell_player, pi->playing_song_changed_id);
		pi->playing_song_changed_id = 0;
	}
	if (pi->playing_changed_id != 0) {
		g_signal_handler_disconnect (pi->shell_player, pi->playing_changed_id);
		pi->playing_changed_id = 0;
	}
	if (pi->window_title_change_id != 0) {
		g_signal_handler_disconnect (pi->shell_player, pi->window_title_change_id);
		pi->window_title_change_id = 0;
	}

	if (pi->player != NULL) {
		g_object_unref (pi->player);
		pi->player = NULL;
	}

	if (pi->shell_player != NULL) {
		g_object_unref (pi->shell_player);
		pi->shell_player = NULL;
	}

	if (pi->shell != NULL) {
		pi->shell = NULL;
	}

	pi->plugin_enabled = FALSE;
}

/* play controls (prev|play|next + song info (+ maybe seek bar) */

static void
previous_clicked_cb (GtkButton *button, RBVisualizerPlugin *plugin)
{
	show_controls (plugin, TRUE);
	rb_shell_player_do_previous (plugin->shell_player, NULL);
}

static void
play_toggled_cb (GtkToggleButton *button, RBVisualizerPlugin *plugin)
{
	if (plugin->syncing_play)
		return;

	show_controls (plugin, TRUE);
	rb_shell_player_playpause (plugin->shell_player, FALSE, NULL);
}

static void
next_clicked_cb (GtkButton *button, RBVisualizerPlugin *plugin)
{
	show_controls (plugin, TRUE);
	rb_shell_player_do_next (plugin->shell_player, NULL);
}

/* visualization configuration stuff */

static gboolean
vis_plugin_filter (GstPluginFeature *feature, gpointer data)
{
	GstElementFactory *f;

	/* skip our fake visualizer */
	if (strcmp (gst_plugin_feature_get_name (feature), "rbfakevis") == 0)
		return FALSE;

	if  (!GST_IS_ELEMENT_FACTORY (feature))
		return FALSE;
	f = GST_ELEMENT_FACTORY (feature);

	return (g_strrstr (gst_element_factory_get_klass (f), "Visualization") != NULL);
}

static GList *
get_vis_plugin_list (RBVisualizerPlugin *pi)
{
	GList *features;
	GList *plugin_info = NULL;
	GList *t;

	if (pi->vis_plugin_list)
		return pi->vis_plugin_list;

	rb_debug ("building vis plugin list");
	features = gst_registry_feature_filter (gst_registry_get_default (),
						vis_plugin_filter,
						FALSE, NULL);
	for (t = features; t != NULL; t = t->next) {
		GstElementFactory *f;
		VisPluginInfo *plugin;

		f = GST_ELEMENT_FACTORY (t->data);

		plugin = g_new0 (VisPluginInfo, 1);
		plugin->displayname = gst_element_factory_get_longname (f);
		plugin->name = gst_plugin_feature_get_name (GST_PLUGIN_FEATURE (f));
		rb_debug ("adding visualizer element: %s (%s)", plugin->displayname, plugin->name);

		plugin_info = g_list_prepend (plugin_info, plugin);
	}
	plugin_info = g_list_reverse (plugin_info);

	pi->vis_plugin_list = plugin_info;
	return plugin_info;
}

static void
element_list_cell_data (GtkCellLayout *layout,
			GtkCellRenderer *cell,
			GtkTreeModel *model,
			GtkTreeIter *iter,
			gpointer whatever)
{
	VisPluginInfo *vpi;

	gtk_tree_model_get (model, iter, 0, &vpi, -1);
	g_object_set (G_OBJECT (cell), "text", vpi->displayname, NULL);
}


static void
quality_list_cell_data (GtkCellLayout *layout,
			GtkCellRenderer *cell,
			GtkTreeModel *model,
			GtkTreeIter *iter,
			gpointer whatever)
{
	VisualizerQuality *quality;

	gtk_tree_model_get (model, iter, 0, &quality, -1);
	g_object_set (G_OBJECT (cell), "text", gettext (quality->name), NULL);
}

static void
screen_list_cell_data (GtkCellLayout *layout,
		       GtkCellRenderer *cell,
		       GtkTreeModel *model,
		       GtkTreeIter *iter,
		       gpointer whatever)
{
	int screen, monitor;
	char *t;

	gtk_tree_model_get (model, iter, 0, &screen, 1, &monitor, -1);
	t = g_strdup_printf ("%d.%d", screen, monitor);
	rb_debug ("displaying %s (%p)", t, iter);
	g_object_set (G_OBJECT (cell), "text", t, NULL);
	g_free (t);
}

static void
populate_combo_boxes (RBVisualizerPlugin *pi)
{
	int i;
	int count, index;
	int screen;
	int active;
	GList *l;
	GtkListStore *model;
	GtkCellRenderer *renderer;
	int quality;
	char *element;
	int num_screens;

	/* visualizer element selection */
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (pi->element_combo), renderer, TRUE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (pi->element_combo),
					    renderer,
					    (GtkCellLayoutDataFunc) element_list_cell_data,
					    NULL, NULL);

	model = gtk_list_store_new (1, G_TYPE_POINTER);
	l = get_vis_plugin_list (pi);
	active = 0;
	element = eel_gconf_get_string (CONF_VIS_ELEMENT);
	if (element == NULL)
		element = g_strdup (DEFAULT_VIS_ELEMENT);

	for (; l != NULL; l = l->next) {
		GtkTreeIter iter;

		VisPluginInfo *vpi = (VisPluginInfo *)l->data;
		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, vpi, -1);

		if (strcmp (element, vpi->name) == 0) {
			active = gtk_tree_model_iter_n_children (GTK_TREE_MODEL (model), NULL) - 1;
		}
	}
	gtk_combo_box_set_model (GTK_COMBO_BOX (pi->element_combo), GTK_TREE_MODEL (model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (pi->element_combo), active);
	g_free (element);

	/* quality selection */
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (pi->quality_combo), renderer, TRUE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (pi->quality_combo),
					    renderer,
					    (GtkCellLayoutDataFunc) quality_list_cell_data,
					    NULL, NULL);

	model = gtk_list_store_new (1, G_TYPE_POINTER);
	for (i = 0; i < G_N_ELEMENTS (vis_quality); i++) {
		GtkTreeIter iter;

		gtk_list_store_append (model, &iter);
		gtk_list_store_set (model, &iter, 0, &vis_quality[i], -1);
	}
	gtk_combo_box_set_model (GTK_COMBO_BOX (pi->quality_combo), GTK_TREE_MODEL (model));

	quality = eel_gconf_get_integer (CONF_VIS_QUALITY);
	if (quality < 0 || quality >= G_N_ELEMENTS (vis_quality))
		quality = DEFAULT_VIS_QUALITY;
	gtk_combo_box_set_active (GTK_COMBO_BOX (pi->quality_combo), quality);

	/* screen selection */
	num_screens = gdk_display_get_n_screens (gdk_display_get_default ());


	rb_debug ("populating screen selection combo box with %d screens", num_screens);
	renderer = gtk_cell_renderer_text_new ();
	gtk_cell_layout_pack_start (GTK_CELL_LAYOUT (pi->screen_combo), renderer, TRUE);
	gtk_cell_layout_set_cell_data_func (GTK_CELL_LAYOUT (pi->screen_combo),
					    renderer,
					    (GtkCellLayoutDataFunc) screen_list_cell_data,
					    NULL, NULL);

	screen = eel_gconf_get_integer (CONF_VIS_SCREEN);
	if (screen < 0 || screen >= num_screens)
		screen = 0;

	model = gtk_list_store_new (2, G_TYPE_INT, G_TYPE_INT);
	count = 0;
	index = 0;
	for (i = 0; i < num_screens; i++) {
		int num_monitors;
		int monitor;
		int j;

		num_monitors = gdk_screen_get_n_monitors (gdk_display_get_screen (gdk_display_get_default (), i));
		rb_debug ("populating screen selection combo box with %d monitors from screen %d", num_monitors, i);

		for (j = 0; j < num_monitors; j++) {
			GtkTreeIter iter;

			gtk_list_store_append (model, &iter);
			gtk_list_store_set (model, &iter, 0, i, 1, j, -1);
			rb_debug ("appending <%d,%d> to store", i, j);
		}

		if (i == screen) {
			monitor = eel_gconf_get_integer (CONF_VIS_MONITOR);
			if (monitor < 0 || monitor >= num_monitors)
				monitor = 0;

			index = count + monitor;
			rb_debug ("current output is on %d.%d, id %d", screen, monitor, index);
		}

		count += num_monitors;
	}

	gtk_combo_box_set_model (GTK_COMBO_BOX (pi->screen_combo), GTK_TREE_MODEL (model));
	gtk_combo_box_set_active (GTK_COMBO_BOX (pi->screen_combo), index);

	if (num_screens > 1 || gdk_screen_get_n_monitors (gdk_display_get_screen (gdk_display_get_default (), 0)) > 1) {
		pi->screen_controls_shown = TRUE;
	}

}

static void
element_combo_changed_cb (GtkComboBox *combo, RBVisualizerPlugin *pi)
{
	int index;
	VisPluginInfo *vpi;
	char *old_element;
	GList *plugins = get_vis_plugin_list (pi);

	index = gtk_combo_box_get_active (combo);
	vpi = g_list_nth_data (plugins, index);
	if (vpi == NULL) {
		rb_debug ("unknown vis element selected?");
		return;
	}

	old_element = eel_gconf_get_string (CONF_VIS_ELEMENT);
	if (old_element == NULL || strcmp (old_element, vpi->name)) {
		rb_debug ("vis element changed: %s", vpi->name);
		eel_gconf_set_string (CONF_VIS_ELEMENT, vpi->name);

		update_visualizer (pi, vpi->name, -1);
	}
	g_free (old_element);
}

static void
quality_combo_changed_cb (GtkComboBox *combo, RBVisualizerPlugin *pi)
{
	int index;

	index = gtk_combo_box_get_active (combo);
	if (index < 0 || index > G_N_ELEMENTS (vis_quality)) {
		rb_debug ("unknown vis quality selected?");
		index = DEFAULT_VIS_QUALITY;
	}

	eel_gconf_set_integer (CONF_VIS_QUALITY, index);

	update_visualizer (pi, NULL, index);
}

static void
screen_changed_cb (GtkComboBox *combo, RBVisualizerPlugin *pi)
{
	GtkTreeIter iter;
	int screen;
	int monitor;

	gtk_combo_box_get_active_iter (combo, &iter);
	gtk_tree_model_get (gtk_combo_box_get_model (combo), &iter, 0, &screen, 1, &monitor, -1);

	if (screen < 0 || screen >= gdk_display_get_n_screens (gdk_display_get_default ())) {
		rb_debug ("unknown screen %d selected?", screen);
		screen = 0;
		monitor = 0;
	} else if (monitor < 0 || monitor >= gdk_screen_get_n_monitors (gdk_display_get_screen (gdk_display_get_default (), screen))) {
		rb_debug ("unknown monitor %d (of screen %d) selected?", monitor, screen);
		monitor = 0;
	} else {
		rb_debug ("selecting screen %d.%d", screen, monitor);
	}

	eel_gconf_set_integer (CONF_VIS_SCREEN, screen);
	eel_gconf_set_integer (CONF_VIS_MONITOR, monitor);

	update_window (pi, pi->mode, screen, monitor);
}

static void
fullscreen_clicked_cb (GtkButton *button, RBVisualizerPlugin *pi)
{
	update_window (pi, FULLSCREEN, eel_gconf_get_integer (CONF_VIS_SCREEN), eel_gconf_get_integer (CONF_VIS_MONITOR));
	enable_visualization (pi);
}

static void
leave_fullscreen_clicked_cb (GtkButton *button, RBVisualizerPlugin *pi)
{
	update_window (pi, EMBEDDED, -1, -1);
	enable_visualization (pi);
}

static void
root_window_clicked_cb (GtkButton *button, RBVisualizerPlugin *pi)
{
	update_window (pi, REMOTE_WINDOW, eel_gconf_get_integer (CONF_VIS_SCREEN), eel_gconf_get_integer (CONF_VIS_MONITOR));
	enable_visualization (pi);
}

static void
create_controls (RBVisualizerPlugin *plugin)
{
	GtkWidget *widget;
	GladeXML *xml;
	char *gladefile;

	/* load glade stuff */
	gladefile = rb_plugin_find_file (RB_PLUGIN (plugin), "visualizer-controls.glade");
	if (gladefile == NULL) {
		return;
	}
	xml = glade_xml_new (gladefile,
			     "visualizer_controls",
			     NULL);

	plugin->control_widget = glade_xml_get_widget (xml, "visualizer_controls");
	plugin->element_combo = glade_xml_get_widget (xml, "element");
	plugin->quality_combo = glade_xml_get_widget (xml, "quality");

	plugin->screen_label = glade_xml_get_widget (xml, "screen_label");
	plugin->screen_combo = glade_xml_get_widget (xml, "screen");

	plugin->fullscreen_button = glade_xml_get_widget (xml, "fullscreen");
	plugin->leave_fullscreen_button = glade_xml_get_widget (xml, "leave_fullscreen");
	plugin->root_window_button = glade_xml_get_widget (xml, "root_window");

	populate_combo_boxes (plugin);

	g_signal_connect_object (plugin->element_combo, "changed",
				 G_CALLBACK (element_combo_changed_cb), plugin,
				 0);
	g_signal_connect_object (plugin->quality_combo, "changed",
				 G_CALLBACK (quality_combo_changed_cb), plugin,
				 0);
	g_signal_connect_object (plugin->screen_combo, "changed",
				 G_CALLBACK (screen_changed_cb), plugin,
				 0);
	g_signal_connect_object (plugin->fullscreen_button, "clicked",
				 G_CALLBACK (fullscreen_clicked_cb), plugin,
				 0);
	g_signal_connect_object (plugin->leave_fullscreen_button, "clicked",
				 G_CALLBACK (leave_fullscreen_clicked_cb), plugin,
				 0);
	g_signal_connect_object (plugin->root_window_button, "clicked",
				 G_CALLBACK (root_window_clicked_cb), plugin,
				 0);

	g_object_ref (plugin->control_widget);
	g_object_unref (xml);

	xml = glade_xml_new (gladefile,
			     "play_controls",
			     NULL);
	plugin->play_control_widget = glade_xml_get_widget (xml, "play_controls");
	plugin->song_info_label = glade_xml_get_widget (xml, "song_info");

	plugin->play_button = glade_xml_get_widget (xml, "play");
	g_signal_connect_object (plugin->play_button, "toggled",
				 G_CALLBACK (play_toggled_cb), plugin,
				 0);

	widget = glade_xml_get_widget (xml, "previous");
	g_signal_connect_object (widget, "clicked",
				 G_CALLBACK (previous_clicked_cb), plugin,
				 0);
	widget = glade_xml_get_widget (xml, "next");
	g_signal_connect_object (widget, "clicked",
				 G_CALLBACK (next_clicked_cb), plugin,
				 0);

	g_object_ref (plugin->play_control_widget);

	g_object_unref (xml);
	g_free (gladefile);
}


static void
rb_visualizer_plugin_class_init (RBVisualizerPluginClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBPluginClass *plugin_class = RB_PLUGIN_CLASS (klass);

	object_class->dispose = rb_visualizer_plugin_dispose;
	object_class->finalize = rb_visualizer_plugin_finalize;

	plugin_class->activate = impl_activate;
	plugin_class->deactivate = impl_deactivate;
}

#ifdef WITH_DBUS

gboolean
rb_visualizer_start_remote (RBVisualizerPlugin *plugin, unsigned long window_id, GError **error)
{
	/* don't do anything is plugin is disabled */
	if (plugin->plugin_enabled == FALSE)
		return TRUE;

	plugin->remote_window = window_id;
	update_window (plugin, REMOTE_WINDOW, -1, -1);
	return TRUE;
}

gboolean
rb_visualizer_stop_remote (RBVisualizerPlugin *plugin, GError **error)
{
	/* don't do anything is plugin is disabled */
	if (plugin->plugin_enabled == FALSE)
		return TRUE;

	plugin->remote_window = 0;
	update_window (plugin, EMBEDDED, -1, -1);
	return TRUE;
}

#endif

