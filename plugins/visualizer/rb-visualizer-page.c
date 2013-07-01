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
 */

#include "config.h"

#include <string.h>
#include <glib.h>
#include <glib/gi18n.h>

#include "rb-visualizer-page.h"
#include "rb-visualizer-fullscreen.h"

#include <widgets/rb-dialog.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>


G_DEFINE_DYNAMIC_TYPE (RBVisualizerPage, rb_visualizer_page, RB_TYPE_DISPLAY_PAGE)

static GtkWidget *create_embed (RBVisualizerPage *page);

enum {
	PROP_0,
	PROP_SINK,
	PROP_FULLSCREEN_ACTION,
	PROP_POPUP
};

enum {
	START,
	STOP,
	FULLSCREEN,
	LAST_SIGNAL
};

static guint signals[LAST_SIGNAL] = {0,};

RBVisualizerPage *
rb_visualizer_page_new (GObject *plugin, RBShell *shell, GSimpleAction *fullscreen, GMenuModel *popup)
{
	GObject *page;
	page = g_object_new (RB_TYPE_VISUALIZER_PAGE,
			     "plugin", plugin,
			     "shell", shell,
			     "name", _("Visual Effects"),
			     "fullscreen-action", fullscreen,
			     "popup", popup,
			     NULL);
	rb_display_page_set_icon_name (RB_DISPLAY_PAGE (page), "video-display-symbolic");

	return RB_VISUALIZER_PAGE (page);
}

static void
set_action_state (RBVisualizerPage *page, gboolean active)
{
	page->setting_state = TRUE;
	g_simple_action_set_state (page->fullscreen_action, g_variant_new_boolean (active));
	page->setting_state = FALSE;
}

static void
start_fullscreen (RBVisualizerPage *page)
{
	if (page->fullscreen == NULL) {
		ClutterActor *stage;
		GtkWindow *main_window;
		RBShell *shell;
		int x, y;

		rb_debug ("starting fullscreen display");
		g_object_get (page, "shell", &shell, NULL);
		g_object_get (shell, "window", &main_window, NULL);

		page->fullscreen = gtk_window_new (GTK_WINDOW_TOPLEVEL);
		gtk_window_set_skip_taskbar_hint (GTK_WINDOW (page->fullscreen), TRUE);

		/* move the texture from the page embed to the new fullscreen embed */

		g_object_ref (page->texture);

		stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (page->embed));
		clutter_container_remove_actor (CLUTTER_CONTAINER (stage), page->texture);

		page->fullscreen_embed = create_embed (page);
		stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (page->fullscreen_embed));
		clutter_container_add (CLUTTER_CONTAINER (stage), page->texture, NULL);
		g_object_unref (page->texture);

		gtk_container_add (GTK_CONTAINER (page->fullscreen), page->fullscreen_embed);
		gtk_widget_show_all (GTK_WIDGET (page->fullscreen));

		gtk_window_get_position (main_window, &x, &y);
		gtk_window_move (GTK_WINDOW (page->fullscreen), x, y);

		gtk_window_fullscreen (GTK_WINDOW (page->fullscreen));
		gtk_window_set_transient_for (GTK_WINDOW (page->fullscreen), main_window);
		g_object_unref (main_window);

		rb_visualizer_fullscreen_add_widgets (page->fullscreen, stage, shell);
		g_object_unref (shell);
	}

	set_action_state (page, TRUE);
}

static void
stop_fullscreen (RBVisualizerPage *page)
{
	if (page->fullscreen != NULL) {
		ClutterActor *stage;

		rb_debug ("stopping fullscreen display");

		g_object_ref (page->texture);
		stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (page->fullscreen_embed));
		rb_visualizer_fullscreen_stop (stage);
		clutter_container_remove_actor (CLUTTER_CONTAINER (stage), page->texture);

		stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (page->embed));
		clutter_container_add (CLUTTER_CONTAINER (stage), page->texture, NULL);

		g_object_unref (page->texture);

		gtk_widget_destroy (GTK_WIDGET (page->fullscreen));
		page->fullscreen = NULL;

		page->fullscreen_embed = NULL;
	}

	set_action_state (page, FALSE);
}

static void
toggle_fullscreen (RBVisualizerPage *page)
{
	if (page->fullscreen != NULL) {
		stop_fullscreen (page);
	} else {
		start_fullscreen (page);
	}
}

static void
toggle_fullscreen_cb (GSimpleAction *action, GVariant *parameters, RBVisualizerPage *page)
{
	if (page->setting_state == FALSE) {
		toggle_fullscreen (page);
	}
}

static gboolean
stage_button_press_cb (ClutterActor *stage, ClutterEvent *event, RBVisualizerPage *page)
{
	if (event->button.button == 1 && event->button.click_count == 2) {
		clutter_threads_leave ();
		toggle_fullscreen (page);
		clutter_threads_enter ();
	} else if (event->button.button == 3) {
		GtkWidget *menu;

		menu = gtk_menu_new_from_model (page->popup);
		gtk_menu_popup (GTK_MENU (menu), NULL, NULL, NULL, NULL, 3, event->any.time);
	}

	return FALSE;
}

static gboolean
stage_key_release_cb (ClutterActor *stage, ClutterEvent *event, RBVisualizerPage *page)
{
	if (event->key.keyval == CLUTTER_KEY_Escape) {
		clutter_threads_leave ();
		stop_fullscreen (page);
		clutter_threads_enter ();
	}
	return FALSE;
}

static void
resize_sink_texture (ClutterActor *stage, ClutterActorBox *box, ClutterAllocationFlags flags, ClutterActor *texture)
{
	clutter_actor_set_size (texture, box->x2 - box->x1, box->y2 - box->y1);
}


static GtkWidget *
create_embed (RBVisualizerPage *page)
{
	ClutterActor *stage;
	GtkWidget *embed;

	embed = gtk_clutter_embed_new ();

	stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (embed));
	g_signal_connect_object (stage, "allocation-changed", G_CALLBACK (resize_sink_texture), page->texture, 0);
	g_signal_connect_object (stage, "button-press-event", G_CALLBACK (stage_button_press_cb), page, 0);
	g_signal_connect_object (stage, "key-release-event", G_CALLBACK (stage_key_release_cb), page, 0);

	return embed;
}

static void
impl_selected (RBDisplayPage *bpage)
{
	RBVisualizerPage *page = RB_VISUALIZER_PAGE (bpage);

	RB_DISPLAY_PAGE_CLASS (rb_visualizer_page_parent_class)->selected (bpage);

	if (page->embed == NULL) {
		ClutterActor *stage;

		page->embed = create_embed (page);

		stage = gtk_clutter_embed_get_stage (GTK_CLUTTER_EMBED (page->embed));

		clutter_container_add (CLUTTER_CONTAINER (stage), page->texture, NULL);

		gtk_box_pack_start (GTK_BOX (page), page->embed, TRUE, TRUE, 0);
		gtk_widget_show_all (GTK_WIDGET (page));
	}

	g_signal_emit (page, signals[START], 0);
}

static void
impl_deselected (RBDisplayPage *bpage)
{
	RBVisualizerPage *page = RB_VISUALIZER_PAGE (bpage);

	RB_DISPLAY_PAGE_CLASS (rb_visualizer_page_parent_class)->deselected (bpage);

	if (page->fullscreen == NULL) {
		g_signal_emit (page, signals[STOP], 0);
	} else {
		/* might as well leave it running.. */
	}
}

static void
impl_get_property (GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	RBVisualizerPage *page = RB_VISUALIZER_PAGE (object);

	switch (prop_id) {
	case PROP_SINK:
		g_value_set_object (value, page->sink);
		break;
	case PROP_POPUP:
		g_value_set_object (value, page->popup);
		break;
	case PROP_FULLSCREEN_ACTION:
		g_value_set_object (value, page->fullscreen_action);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_set_property (GObject *object, guint prop_id, const GValue *value, GParamSpec *pspec)
{
	RBVisualizerPage *page = RB_VISUALIZER_PAGE (object);

	switch (prop_id) {
	case PROP_POPUP:
		page->popup = g_value_get_object (value);
		break;
	case PROP_FULLSCREEN_ACTION:
		page->fullscreen_action = g_value_get_object (value);
		break;
	default:
		G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
		break;
	}
}

static void
impl_dispose (GObject *object)
{
	RBVisualizerPage *page = RB_VISUALIZER_PAGE (object);

	if (page->embed != NULL) {
		gtk_container_remove (GTK_CONTAINER (page), page->embed);
		page->embed = NULL;
	}
	if (page->sink != NULL) {
		g_object_unref (page->sink);
		page->sink = NULL;
	}
	if (page->popup != NULL) {
		g_object_unref (page->popup);
		page->popup = NULL;
	}

	G_OBJECT_CLASS (rb_visualizer_page_parent_class)->dispose (object);
}

static void
impl_constructed (GObject *object)
{
	RBVisualizerPage *page;
	ClutterInitError err;
	GstElement *videoconvert;
	GstElement *realsink;
	GstCaps *caps;
	GstPad *pad;

	RB_CHAIN_GOBJECT_METHOD (rb_visualizer_page_parent_class, constructed, object);
	page = RB_VISUALIZER_PAGE (object);

	err = gtk_clutter_init (NULL, NULL);
	if (err != CLUTTER_INIT_SUCCESS) {
		/* maybe do something more sensible here.  not sure if there are any user-recoverable
		 * conditions that would cause clutter init to fail, though, so it may not be worth it.
		 * as it is, we just won't add the page to the page tree.
		 */
		g_warning ("Unable to display visual effects due to Clutter init failure");
		return;
	}

	page->texture = clutter_texture_new ();
	clutter_texture_set_sync_size (CLUTTER_TEXTURE (page->texture), TRUE);
	clutter_texture_set_keep_aspect_ratio (CLUTTER_TEXTURE (page->texture), TRUE);

	page->sink = gst_bin_new (NULL);
	g_object_ref (page->sink);

	/* actual sink */
	realsink = gst_element_factory_make ("cluttersink", NULL);
	g_object_set (realsink, "texture", page->texture, NULL);

	videoconvert = gst_element_factory_make ("videoconvert", NULL);

	/* force rgb format (without this we end up using ayuv)  -
	 * XXX check this in 0.11 */
	caps = gst_caps_from_string ("video/x-raw,format=(string)RGB");

	gst_bin_add_many (GST_BIN (page->sink),
				 videoconvert, realsink, NULL);

	gst_element_link_filtered(videoconvert, realsink, caps);
	gst_caps_unref(caps);

	pad = gst_element_get_static_pad (videoconvert, "sink");
	gst_element_add_pad (page->sink, gst_ghost_pad_new ("sink", pad));
	gst_object_unref (pad);

	g_signal_connect (page->fullscreen_action, "activate", G_CALLBACK (toggle_fullscreen_cb), page);
}

static void
rb_visualizer_page_init (RBVisualizerPage *page)
{
}

static void
rb_visualizer_page_class_init (RBVisualizerPageClass *klass)
{
	GObjectClass *object_class = G_OBJECT_CLASS (klass);
	RBDisplayPageClass *page_class = RB_DISPLAY_PAGE_CLASS (klass);

	object_class->constructed = impl_constructed;
	object_class->get_property = impl_get_property;
	object_class->set_property = impl_set_property;
	object_class->dispose = impl_dispose;

	page_class->selected = impl_selected;
	page_class->deselected = impl_deselected;

	g_object_class_install_property (object_class,
					 PROP_SINK,
					 g_param_spec_object ("sink",
							      "sink",
							      "gstreamer sink element",
							      GST_TYPE_ELEMENT,
							      G_PARAM_READABLE));
	g_object_class_install_property (object_class,
					 PROP_POPUP,
					 g_param_spec_object ("popup",
							      "popup",
							      "popup menu",
							      G_TYPE_MENU_MODEL,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));
	g_object_class_install_property (object_class,
					 PROP_FULLSCREEN_ACTION,
					 g_param_spec_object ("fullscreen-action",
							      "fullscreen action",
							      "fullscreen action",
							      G_TYPE_SIMPLE_ACTION,
							      G_PARAM_READWRITE | G_PARAM_CONSTRUCT_ONLY));

	signals[START] = g_signal_new ("start",
				       RB_TYPE_VISUALIZER_PAGE,
				       G_SIGNAL_RUN_LAST,
				       0,
				       NULL, NULL,
				       g_cclosure_marshal_VOID__VOID,
				       G_TYPE_NONE,
				       0);
	signals[STOP] = g_signal_new ("stop",
				      RB_TYPE_VISUALIZER_PAGE,
				      G_SIGNAL_RUN_LAST,
				      0,
				      NULL, NULL,
				      g_cclosure_marshal_VOID__VOID,
				      G_TYPE_NONE,
				      0);
	signals[FULLSCREEN] = g_signal_new_class_handler ("toggle-fullscreen",
							  RB_TYPE_VISUALIZER_PAGE,
							  G_SIGNAL_RUN_LAST,
							  G_CALLBACK (toggle_fullscreen),
							  NULL, NULL,
							  g_cclosure_marshal_VOID__VOID,
							  G_TYPE_NONE,
							  0);
}

static void
rb_visualizer_page_class_finalize (RBVisualizerPageClass *klass)
{
}

void
_rb_visualizer_page_register_type (GTypeModule *module)
{
	rb_visualizer_page_register_type (module);
}
