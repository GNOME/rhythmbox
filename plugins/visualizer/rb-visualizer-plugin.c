/*
 * Copyright (C) 2010 Jonathan Matthew <jonathan@d14n.org>
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

#include <glib/gi18n-lib.h>
#include <gmodule.h>
#include <gtk/gtk.h>
#include <glib.h>
#include <glib-object.h>
#include <libpeas/peas.h>

#include <plugins/rb-plugin-macros.h>
#include <shell/rb-shell-player.h>
#include <sources/rb-display-page.h>
#include <sources/rb-display-page-group.h>
#include <sources/rb-display-page-model.h>
#include <backends/rb-player.h>
#include <backends/rb-player-gst-tee.h>
#include <lib/rb-debug.h>

#include "rb-visualizer-page.h"
#include "rb-visualizer-fullscreen.h"
#include "rb-visualizer-menu.h"

#define RB_TYPE_VISUALIZER_PLUGIN		(rb_visualizer_plugin_get_type ())
#define RB_VISUALIZER_PLUGIN(o)			(G_TYPE_CHECK_INSTANCE_CAST ((o), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPlugin))
#define RB_VISUALIZER_PLUGIN_CLASS(k)		(G_TYPE_CHECK_CLASS_CAST((k), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPluginClass))
#define RB_IS_VISUALIZER_PLUGIN(o)		(G_TYPE_CHECK_INSTANCE_TYPE ((o), RB_TYPE_VISUALIZER_PLUGIN))
#define RB_IS_VISUALIZER_PLUGIN_CLASS(k)	(G_TYPE_CHECK_CLASS_TYPE ((k), RB_TYPE_VISUALIZER_PLUGIN))
#define RB_VISUALIZER_PLUGIN_GET_CLASS(o)	(G_TYPE_INSTANCE_GET_CLASS ((o), RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPluginClass))

/* playbin2 flag(s) */
#define PLAYBIN2_FLAG_VIS	0x08

typedef struct
{
	PeasExtensionBase parent;

	RBShellPlayer *shell_player;
	RBPlayer *player;

	/* pipeline stuff */
	GstElement *visualizer;
	GstElement *sink;

	GstElement *identity;
	GstElement *capsfilter;
	GstElement *vis_plugin;

	GstElement *playbin;
	gulong playbin_notify_id;

	/* ui */
	RBVisualizerPage *page;

	GSettings *settings;
} RBVisualizerPlugin;

typedef struct
{
	PeasExtensionBaseClass parent_class;
} RBVisualizerPluginClass;


G_MODULE_EXPORT void peas_register_types (PeasObjectModule *module);

RB_DEFINE_PLUGIN(RB_TYPE_VISUALIZER_PLUGIN, RBVisualizerPlugin, rb_visualizer_plugin,)

static void
fixate_vis_caps (RBVisualizerPlugin *plugin)
{
	GstPad *pad;
	GstCaps *caps = NULL;
	const GstCaps *template_caps;

	pad = gst_element_get_static_pad (plugin->vis_plugin, "src");
	template_caps = gst_pad_get_pad_template_caps (pad);
	gst_object_unref (pad);

	if (template_caps == NULL) {
		rb_debug ("vis element has no template caps?");
		return;
	}

	caps = gst_caps_copy (template_caps);

	if (gst_caps_is_fixed (caps) == FALSE) {
		guint i;
		char *dbg;
		const VisualizerQuality *q = &rb_visualizer_quality[g_settings_get_enum (plugin->settings, "quality")];

		rb_debug ("fixating caps towards %dx%d, %d/%d", q->width, q->height, q->fps_n, q->fps_d);
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

		g_object_set (plugin->capsfilter, "caps", caps, NULL);
	} else {
		char *dbg = gst_caps_to_string (caps);
		rb_debug ("vis element caps already fixed: %s", dbg);
		g_free (dbg);
	}

	gst_caps_unref (caps);
}

static void
mutate_playbin (RBVisualizerPlugin *plugin, GstElement *playbin)
{
	GstElement *current_vis_plugin;
	GstElement *current_video_sink;
	int playbin_flags;

	if (playbin == plugin->playbin)
		return;

	rb_debug ("mutating playbin");

	/* check no one has already set the playbin properties we're interested in */
	g_object_get (playbin,
		      "vis-plugin", &current_vis_plugin,
		      "video-sink", &current_video_sink,
		      "flags", &playbin_flags,
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

	if ((current_vis_plugin != NULL) || (current_video_sink != NULL)) {
		g_warning ("sink and/or vis plugin already set on playbin");
		if (current_vis_plugin)
			g_object_unref (current_vis_plugin);
		if (current_video_sink)
			g_object_unref (current_video_sink);
		return;
	}

	/* detach from old playbin (this should never really happen) */
	if (plugin->playbin) {
		g_object_unref (plugin->playbin);
	}

	/* attach to new playbin */
	plugin->playbin = g_object_ref (playbin);
	g_object_set (plugin->playbin, "video-sink", plugin->sink, NULL);

	/* start visualizer if it's supposed to be running */
	if (plugin->visualizer != NULL) {
		playbin_flags |= PLAYBIN2_FLAG_VIS;
		g_object_set (plugin->playbin,
			      "flags", playbin_flags,
			      "vis-plugin", plugin->visualizer,
			      NULL);
	}
}

static void
playbin_notify_cb (GObject *object, GParamSpec *arg, RBVisualizerPlugin *pi)
{
	GstElement *playbin;

	g_object_get (object, "playbin", &playbin, NULL);
	if (playbin) {
		mutate_playbin (pi, playbin);
		g_object_unref (playbin);
	}
}


static void
update_visualizer (RBVisualizerPlugin *plugin)
{
	if (plugin->visualizer == NULL) {
		return;
	}

	/* pad blocking and other such nonsense, i guess */
}

static void
start_visualizer_cb (RBVisualizerPage *page, RBVisualizerPlugin *plugin)
{
	GstPad *pad;
	char *plugin_name;

	if (plugin->visualizer) {
		g_object_unref (plugin->visualizer);
		plugin->visualizer = NULL;
		plugin->identity = NULL;
		plugin->capsfilter = NULL;
		plugin->vis_plugin = NULL;
	}
	plugin->visualizer = gst_bin_new (NULL);

	/* create common bits of visualizer bin: identity ! <effect> ! capsfilter */
	plugin->identity = gst_element_factory_make ("identity", NULL);
	plugin->capsfilter = gst_element_factory_make ("capsfilter", NULL);

	plugin_name = g_settings_get_string (plugin->settings, "vis-plugin");
	if (plugin_name != NULL) {
		plugin->vis_plugin = gst_element_factory_make (plugin_name, NULL);
		if (plugin->vis_plugin == NULL) {
			g_warning ("Configured visualizer plugin %s not available", plugin_name);
		}
		g_free (plugin_name);
	}
	if (plugin->vis_plugin == NULL) {
		plugin->vis_plugin = gst_element_factory_make ("goom", NULL);
		if (plugin->vis_plugin == NULL) {
			g_warning ("Fallback visualizer plugin (goom) not available");
			return;
		}
	}

	/* set up capsfilter */
	gst_bin_add_many (GST_BIN (plugin->visualizer), plugin->identity, plugin->vis_plugin, plugin->capsfilter, NULL);

	pad = gst_element_get_static_pad (plugin->identity, "sink");
	gst_element_add_pad (plugin->visualizer, gst_ghost_pad_new ("sink", pad));
	gst_object_unref (pad);

	/* XXX check errors etc. */
	if (gst_element_link_many (plugin->identity, plugin->vis_plugin, plugin->capsfilter, NULL) == FALSE) {
		g_warning ("couldn't link visualizer bin elements");
		return;
	}
	fixate_vis_caps (plugin);

	g_object_ref (plugin->visualizer);

	if (plugin->playbin_notify_id) {
		GstPad *pad;
		int playbin_flags;

		pad = gst_element_get_static_pad (plugin->capsfilter, "src");
		gst_element_add_pad (plugin->visualizer, gst_ghost_pad_new ("src", pad));
		gst_object_unref (pad);

		g_object_get (plugin->playbin, "flags", &playbin_flags, NULL);
		if (plugin->playbin != NULL) {
			playbin_flags |= PLAYBIN2_FLAG_VIS;
			rb_debug ("enabling vis; new playbin2 flags %x", playbin_flags);
			g_object_set (plugin->playbin,
				      "vis-plugin", plugin->visualizer,
				      "flags", playbin_flags,
				      NULL);
		} else {
			rb_debug ("playback hasn't started yet");
		}
	} else {
		GstElement *colorspace;
		GstElement *queue;

		colorspace = gst_element_factory_make ("ffmpegcolorspace", NULL);
		queue = gst_element_factory_make ("queue", NULL);

		g_object_set (queue, "max-size-buffers", 3, "max-size-bytes", 0, "max-size-time", (gint64) 0, NULL);

		gst_bin_add_many (GST_BIN (plugin->visualizer), queue, colorspace, plugin->sink, NULL);
		gst_element_link_many (plugin->capsfilter, queue, colorspace, plugin->sink, NULL);

		rb_debug ("adding visualizer bin to the pipeline");
		rb_player_gst_tee_add_tee (RB_PLAYER_GST_TEE (plugin->player),
					   plugin->visualizer);
	}
}

static void
stop_visualizer_cb (RBVisualizerPage *page, RBVisualizerPlugin *plugin)
{
	if (plugin->visualizer == NULL) {
		return;
	}

	if (plugin->playbin_notify_id) {
		int playbin_flags;

		g_object_get (plugin->playbin, "flags", &playbin_flags, NULL);
		playbin_flags &= ~PLAYBIN2_FLAG_VIS;
		rb_debug ("disabling vis; new playbin2 flags %d", playbin_flags);
		g_object_set (plugin->playbin,
			      "flags", playbin_flags,
			      "vis-plugin", NULL,
			      NULL);
	} else {
		rb_debug ("removing visualizer bin from pipeline");
		rb_player_gst_tee_remove_tee (RB_PLAYER_GST_TEE (plugin->player),
					      plugin->visualizer);
	}

	if (plugin->visualizer) {
		g_object_unref (plugin->visualizer);
		plugin->visualizer = NULL;
	}
}

static void
settings_changed_cb (GSettings *settings, const char *key, RBVisualizerPlugin *plugin)
{
	update_visualizer (plugin);
}

static void
playing_song_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, RBVisualizerPlugin *plugin)
{
	g_object_set (plugin->page, "visibility", (entry != NULL), NULL);
}

static void
impl_activate (PeasActivatable *activatable)
{
	RBVisualizerPlugin *pi = RB_VISUALIZER_PLUGIN (activatable);
	RBDisplayPageGroup *page_group;
	RhythmDBEntry *entry;
	GtkToggleAction *fullscreen;
	GtkWidget *menu;
	RBShell *shell;

	g_object_get (pi, "object", &shell, NULL);

	pi->settings = g_settings_new ("org.gnome.rhythmbox.plugins.visualizer");
	g_signal_connect_object (pi->settings, "changed", G_CALLBACK (settings_changed_cb), pi, 0);

	/* create UI actions and menus and stuff */
	fullscreen = gtk_toggle_action_new ("VisualizerFullscreen",
					    _("Fullscreen"),
					    _("Toggle fullscreen visual effects"),
					    GTK_STOCK_FULLSCREEN);
	menu = rb_visualizer_create_popup_menu (fullscreen);
	g_object_ref_sink (menu);

	/* create visualizer page */
	pi->page = rb_visualizer_page_new (G_OBJECT (pi), shell, fullscreen, menu);
	g_signal_connect_object (pi->page, "start", G_CALLBACK (start_visualizer_cb), pi, 0);
	g_signal_connect_object (pi->page, "stop", G_CALLBACK (stop_visualizer_cb), pi, 0);

	/* don't do anything if we couldn't create a video sink (clutter is broken, etc.) */
	g_object_get (pi->page, "sink", &pi->sink, NULL);
	if (pi->sink == NULL) {
		g_object_unref (shell);
		return;
	}

	/* prepare style stuff for fullscreen display */
	rb_visualizer_fullscreen_load_style (G_OBJECT (pi));

	/* add the visualizer page to the UI */
	page_group = rb_display_page_group_get_by_id ("display");
	if (page_group == NULL) {
		page_group = rb_display_page_group_new (G_OBJECT (shell),
							"display",
							_("Display"),
							RB_DISPLAY_PAGE_GROUP_CATEGORY_TOOLS);
		rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (page_group), NULL);
	}
	g_object_set (pi->page, "visibility", FALSE, NULL);

	rb_shell_append_display_page (shell, RB_DISPLAY_PAGE (pi->page), RB_DISPLAY_PAGE (page_group));

	/* get player objects */
	g_object_get (shell, "shell-player", &pi->shell_player, NULL);
	g_object_get (pi->shell_player, "player", &pi->player, NULL);

	/* only show the page in the page tree when playing something */
	g_signal_connect_object (pi->shell_player, "playing-song-changed", G_CALLBACK (playing_song_changed_cb), pi, 0);
	entry = rb_shell_player_get_playing_entry (pi->shell_player);
	playing_song_changed_cb (pi->shell_player, entry, pi);
	if (entry != NULL) {
		rhythmdb_entry_unref (entry);
	}

	/* figure out how to insert the visualizer into the playback pipeline */
	if (g_object_class_find_property (G_OBJECT_GET_CLASS (pi->player), "playbin")) {

		rb_debug ("using playbin-based visualization");
		pi->playbin_notify_id = g_signal_connect_object (pi->player,
								 "notify::playbin",
								 G_CALLBACK (playbin_notify_cb),
								 pi,
								 0);
		g_object_get (pi->player, "playbin", &pi->playbin, NULL);
		if (pi->playbin != NULL) {
			mutate_playbin (pi, pi->playbin);
		}
	} else if (RB_IS_PLAYER_GST_TEE (pi->player)) {
		rb_debug ("using tee-based visualization");
	} else {
		g_warning ("unknown player backend type");
		g_object_unref (pi->player);
		pi->player = NULL;
	}

	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *activatable)
{
	RBVisualizerPlugin *pi = RB_VISUALIZER_PLUGIN (activatable);

	if (pi->page != NULL) {
		stop_visualizer_cb (pi->page, pi);

		rb_display_page_delete_thyself (RB_DISPLAY_PAGE (pi->page));
		pi->page = NULL;
	}

	if (pi->sink != NULL) {
		g_object_unref (pi->sink);
		pi->sink = NULL;
	}

	if (pi->settings != NULL) {
		g_object_unref (pi->settings);
		pi->settings = NULL;
	}
}

static void
rb_visualizer_plugin_init (RBVisualizerPlugin *plugin)
{
	rb_debug ("RBVisualizerPlugin initialising");

	/* for uninstalled builds, add plugins/visualizer/icons as an icon search path */
#ifdef USE_UNINSTALLED_DIRS
	gtk_icon_theme_append_search_path (gtk_icon_theme_get_default (),
					   PLUGIN_SRC_DIR G_DIR_SEPARATOR_S "icons");
#endif
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_visualizer_plugin_register_type (G_TYPE_MODULE (module));
	_rb_visualizer_page_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_VISUALIZER_PLUGIN);
}
