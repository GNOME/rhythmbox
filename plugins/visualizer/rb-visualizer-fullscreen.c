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

#include <glib/gi18n.h>
#include <clutter-gtk/clutter-gtk.h>
#include <mx/mx.h>

#include "rb-visualizer-fullscreen.h"

#include <shell/rb-shell-player.h>
#include <rhythmdb/rhythmdb.h>
#include <lib/rb-file-helpers.h>
#include <lib/rb-util.h>
#include <lib/rb-debug.h>
#include <metadata/rb-ext-db.h>

#define MAX_IMAGE_HEIGHT		128		/* should be style-controlled, but it's tricky */
#define FULLSCREEN_BORDER_WIDTH		32		/* this should be style-controlled too */

#define TRACK_INFO_DATA		"rb-track-info-actor"
#define CONTROLS_DATA		"rb-controls-actor"

static MxStyle *style = NULL;

void
rb_visualizer_fullscreen_load_style (GObject *plugin)
{
	char *file;

	if (style == NULL) {
		style = mx_style_new ();

		file = rb_find_plugin_data_file (plugin, "visualizer.css");
		if (file != NULL) {
			mx_style_load_from_file (style, file, NULL);
			g_free (file);
		}
	}
}

/* cover art display */

/* use a 'missing image' image instead? */
static void
set_blank_image (MxFrame *frame)
{
	ClutterActor *blank;
	ClutterColor nothing = { 0, 0, 0, 0 };

	blank = clutter_rectangle_new_with_color (&nothing);
	clutter_actor_set_height (blank, MAX_IMAGE_HEIGHT);
	clutter_actor_set_width (blank, MAX_IMAGE_HEIGHT);
	mx_bin_set_child (MX_BIN (frame), blank);
}

static void
art_cb (RBExtDBKey *key, const char *filename, GValue *data, MxFrame *frame)
{
	ClutterActor *image;
	GdkPixbuf *pixbuf;

	if (data == NULL || G_VALUE_HOLDS (data, GDK_TYPE_PIXBUF) == FALSE) {
		return;
	}

	clutter_threads_enter ();

	image = gtk_clutter_texture_new ();
	pixbuf = GDK_PIXBUF (g_value_get_object (data));
	gtk_clutter_texture_set_from_pixbuf (GTK_CLUTTER_TEXTURE (image), pixbuf, NULL);
	if (clutter_actor_get_height (image) > MAX_IMAGE_HEIGHT) {
		clutter_actor_set_height (image, MAX_IMAGE_HEIGHT);
		clutter_texture_set_keep_aspect_ratio (CLUTTER_TEXTURE (image), TRUE);
	}
	if (clutter_actor_get_width (image) > MAX_IMAGE_HEIGHT) {
		clutter_actor_set_width (image, MAX_IMAGE_HEIGHT);
	}
	mx_bin_set_child (MX_BIN (frame), image);
	clutter_actor_show_all (CLUTTER_ACTOR (frame));

	clutter_threads_leave ();
}

static void
cover_art_entry_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, MxFrame *frame)
{
	RBExtDBKey *key;
	RBExtDB *art_store;

	art_store = rb_ext_db_new ("album-art");

	clutter_threads_enter ();
	set_blank_image (frame);
	clutter_actor_show_all (CLUTTER_ACTOR (frame));
	clutter_threads_leave ();

	key = rhythmdb_entry_create_ext_db_key (entry, RHYTHMDB_PROP_ALBUM);
	rb_ext_db_request (art_store, key, (RBExtDBRequestCallback) art_cb, g_object_ref (frame), g_object_unref);
	rb_ext_db_key_free (key);

	g_object_unref (art_store);
}

/* track info display */

static void
get_artist_album_templates (const char *artist,
			    const char *album,
			    const char **artist_template,
			    const char **album_template)
{
	PangoDirection tag_dir;
	PangoDirection template_dir;

	/* Translators: by Artist */
	*artist_template = _("by <i>%s</i>");
	/* Translators: from Album */
	*album_template = _("from <i>%s</i>");

	/* find the direction (left-to-right or right-to-left) of the
	 * track's tags and the localized templates
	 */
	if (artist != NULL && artist[0] != '\0') {
		tag_dir = pango_find_base_dir (artist, -1);
		template_dir = pango_find_base_dir (*artist_template, -1);
	} else if (album != NULL && album[0] != '\0') {
		tag_dir = pango_find_base_dir (album, -1);
		template_dir = pango_find_base_dir (*album_template, -1);
	} else {
		return;
	}

	/* if the track's tags and the localized templates have a different
	 * direction, switch to direction-neutral templates in order to improve
	 * display.
	 * text can have a neutral direction, this condition only applies when
	 * both directions are defined and they are conflicting.
	 * https://bugzilla.gnome.org/show_bug.cgi?id=609767
	 */
	if (((tag_dir == PANGO_DIRECTION_LTR) && (template_dir == PANGO_DIRECTION_RTL)) ||
	    ((tag_dir == PANGO_DIRECTION_RTL) && (template_dir == PANGO_DIRECTION_LTR))) {
		/* these strings should not be localized, they must be
		 * locale-neutral and direction-neutral
		 */
		*artist_template = "<i>%s</i>";
		*album_template = "/ <i>%s</i>";
	}
}

static void
str_append_printf_escaped (GString *str, const char *format, ...)
{
	va_list args;
	char *bit;

	va_start (args, format);
	bit = g_markup_vprintf_escaped (format, args);
	va_end (args);

	g_string_append (str, bit);
	g_free (bit);
}

static void
update_track_info (MxLabel *label, RhythmDB *db, RhythmDBEntry *entry, const char *streaming_title)
{
	const char *title;
	ClutterActor *text;
	GString *str;

	clutter_threads_enter ();
	text = mx_label_get_clutter_text (label);

	str = g_string_sized_new (100);
	if (entry == NULL) {
		g_string_append_printf (str, "<big>%s</big>", _("Not Playing"));
	} else {
		title = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_TITLE);

		if (streaming_title) {
			str_append_printf_escaped (str, "<big>%s</big>\n", streaming_title);
			str_append_printf_escaped (str, _("from <i>%s</i>"), title);
		} else {
			const char *artist_template = NULL;
			const char *album_template = NULL;
			const char *artist;
			const char *album;
			gboolean space = FALSE;

			artist = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST);
			album = rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM);
			get_artist_album_templates (artist, album, &artist_template, &album_template);

			str_append_printf_escaped (str, "<big>%s</big>\n", title);

			if (album != NULL && album[0] != '\0') {
				str_append_printf_escaped (str, album_template, album);
				space = TRUE;
			}
			if (artist != NULL && artist[0] != '\0') {
				if (space) {
					g_string_append_c (str, ' ');
				}
				str_append_printf_escaped (str, artist_template, artist);
			}
		}
	}

	/* tiny bit of extra padding */
	g_string_append (str, "  ");
	clutter_text_set_markup (CLUTTER_TEXT (text), str->str);
	clutter_text_set_ellipsize (CLUTTER_TEXT (text), PANGO_ELLIPSIZE_NONE);
	clutter_threads_leave ();
	g_string_free (str, TRUE);
}

static void
playing_song_changed_cb (RBShellPlayer *player, RhythmDBEntry *entry, ClutterActor *label)
{
	RhythmDB *db;

	g_object_get (player, "db", &db, NULL);
	update_track_info (MX_LABEL (label), db, entry, NULL);
	g_object_unref (db);
}

static void
entry_changed_cb (RhythmDB *db, RhythmDBEntry *entry, GValueArray *changes, ClutterActor *label)
{
	int i;
	/* somehow check entry == playing entry */

	for (i = 0; i < changes->n_values; i++) {
		GValue *v = g_value_array_get_nth (changes, i);
		RhythmDBEntryChange *change = g_value_get_boxed (v);
		switch (change->prop) {
		case RHYTHMDB_PROP_TITLE:
		case RHYTHMDB_PROP_ARTIST:
		case RHYTHMDB_PROP_ALBUM:
			update_track_info (MX_LABEL (label), db, entry, NULL);
			return;

		default:
			break;
		}
	}
}

static void
streaming_title_notify_cb (RhythmDB *db, RhythmDBEntry *entry, const char *field, GValue *metadata, ClutterActor *label)
{
	if (G_VALUE_HOLDS_STRING (metadata)) {
		update_track_info (MX_LABEL (label), db, entry, g_value_get_string (metadata));
	}
}


/* elapsed time / duration display */

static void
elapsed_changed_cb (RBShellPlayer *player, guint elapsed, ClutterActor *label)
{
	long duration;
	char *str;

	duration = rb_shell_player_get_playing_song_duration (player);
	str = rb_make_elapsed_time_string (elapsed, duration, FALSE);
	clutter_threads_enter ();

	mx_label_set_text (MX_LABEL (label), str);

	clutter_threads_leave ();

	g_free (str);
}


static ClutterActor *
create_track_info (RBShell *shell)
{
	RBShellPlayer *player;
	RhythmDB *db;
	ClutterActor *box;
	ClutterActor *box2;
	ClutterActor *widget;
	ClutterActor *frame;
	RhythmDBEntry *entry;
	GValue *value;
	guint elapsed;

	g_object_get (shell, "shell-player", &player, "db", &db, NULL);
	entry = rb_shell_player_get_playing_entry (player);

	box = mx_box_layout_new ();
	mx_box_layout_set_orientation (MX_BOX_LAYOUT (box), MX_ORIENTATION_HORIZONTAL);
	mx_box_layout_set_spacing (MX_BOX_LAYOUT (box), 16);
	mx_stylable_set_style_class (MX_STYLABLE (box), "TrackInfoBox");
	mx_stylable_set_style (MX_STYLABLE (box), style);

	/* XXX rtl? */

	/* image container */
	frame = mx_frame_new ();
	mx_stylable_set_style_class (MX_STYLABLE (frame), "TrackInfoImage");
	mx_stylable_set_style (MX_STYLABLE (frame), style);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box), frame, 0);
	clutter_container_child_set (CLUTTER_CONTAINER (box), frame,
				     "expand", FALSE,
				     NULL);

	g_signal_connect_object (player, "playing-song-changed", G_CALLBACK (cover_art_entry_changed_cb), frame, 0);
	cover_art_entry_changed_cb (player, entry, MX_FRAME (frame));

	box2 = mx_box_layout_new ();
	mx_box_layout_set_orientation (MX_BOX_LAYOUT (box2), MX_ORIENTATION_VERTICAL);
	mx_box_layout_set_spacing (MX_BOX_LAYOUT (box2), 16);
	mx_stylable_set_style (MX_STYLABLE (box2), style);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box), box2, 1);
	clutter_container_child_set (CLUTTER_CONTAINER (box), box2,
				     "expand", TRUE,
				     "x-fill", TRUE,
				     "y-fill", TRUE,
				     "y-align", MX_ALIGN_MIDDLE,
				     NULL);

	/* track info */
	widget = mx_label_new ();
	mx_stylable_set_style_class (MX_STYLABLE (widget), "TrackInfoText");
	mx_stylable_set_style (MX_STYLABLE (widget), style);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box2), widget, 1);
	clutter_container_child_set (CLUTTER_CONTAINER (box2), widget,
				     "expand", FALSE,
				     "x-fill", TRUE,
				     "y-fill", TRUE,
				     "y-align", MX_ALIGN_MIDDLE,
				     NULL);

	g_signal_connect_object (player, "playing-song-changed", G_CALLBACK (playing_song_changed_cb), widget, 0);
	g_signal_connect_object (db, "entry-changed", G_CALLBACK (entry_changed_cb), widget, 0);
	g_signal_connect_object (db, "entry-extra-metadata-notify::" RHYTHMDB_PROP_STREAM_SONG_TITLE, G_CALLBACK (streaming_title_notify_cb), widget, 0);

	value = rhythmdb_entry_request_extra_metadata (db, entry, RHYTHMDB_PROP_STREAM_SONG_TITLE);
	if (value != NULL) {
		update_track_info (MX_LABEL (widget), db, entry, g_value_get_string (value));
		g_value_unset (value);
		g_free (value);
	} else {
		update_track_info (MX_LABEL (widget), db, entry, NULL);
	}

	/* elapsed/duration */
	widget = mx_label_new ();
	mx_stylable_set_style_class (MX_STYLABLE (widget), "TrackTimeText");
	mx_stylable_set_style (MX_STYLABLE (widget), style);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box2), widget, 2);
	clutter_container_child_set (CLUTTER_CONTAINER (box2), widget,
				     "expand", FALSE,
				     "x-fill", TRUE,
				     "y-fill", TRUE,
				     "y-align", MX_ALIGN_MIDDLE,
				     NULL);

	g_signal_connect_object (player, "elapsed-changed", G_CALLBACK (elapsed_changed_cb), widget, 0);
	if (rb_shell_player_get_playing_time (player, &elapsed, NULL)) {
		elapsed_changed_cb (player, elapsed, widget);
	}

	rhythmdb_entry_unref (entry);
	g_object_unref (player);
	g_object_unref (db);
	return box;
}

static ClutterActor *
create_button (const char *button_style, const char *icon_style, const char *icon_name)
{
	ClutterActor *widget;
	ClutterActor *icon;

	icon = mx_icon_new ();
	mx_stylable_set_style_class (MX_STYLABLE (icon), icon_style);
	mx_stylable_set_style (MX_STYLABLE (icon), style);
	mx_icon_set_icon_name (MX_ICON (icon), icon_name);
	mx_icon_set_icon_size (MX_ICON (icon), 64);

	widget = mx_button_new ();
	mx_stylable_set_style_class (MX_STYLABLE (widget), button_style);
	mx_stylable_set_style (MX_STYLABLE (widget), style);
	mx_bin_set_child (MX_BIN (widget), icon);

	return widget;
}

static void
next_clicked_cb (MxButton *button, RBShellPlayer *player)
{
	rb_shell_player_do_next (player, NULL);
}

static void
prev_clicked_cb (MxButton *button, RBShellPlayer *player)
{
	rb_shell_player_do_previous (player, NULL);
}

static void
playpause_clicked_cb (MxButton *button, RBShellPlayer *player)
{
	rb_shell_player_playpause (player, FALSE, NULL);
}

static void
playing_changed_cb (RBShellPlayer *player, gboolean playing, MxButton *button)
{
	ClutterActor *child;

	clutter_threads_enter ();
	child = mx_bin_get_child (MX_BIN (button));
	if (playing) {
		mx_stylable_set_style_class (MX_STYLABLE (button), "PauseButton");
		mx_icon_set_icon_name (MX_ICON (child), "media-playback-pause");
	} else {
		mx_stylable_set_style_class (MX_STYLABLE (button), "PlayButton");
		mx_icon_set_icon_name (MX_ICON (child), "media-playback-start");
	}
	clutter_threads_leave ();

	/* stop button?  meh */
}

static ClutterActor *
create_controls (RBShell *shell)
{
	RBShellPlayer *player;
	ClutterActor *box;
	ClutterActor *button;
	int pos;
	gboolean playing;

	g_object_get (shell, "shell-player", &player, NULL);

	box = mx_box_layout_new ();
	mx_box_layout_set_orientation (MX_BOX_LAYOUT (box), MX_ORIENTATION_HORIZONTAL);
	mx_box_layout_set_spacing (MX_BOX_LAYOUT (box), 16);
	mx_stylable_set_style_class (MX_STYLABLE (box), "ControlsBox");
	mx_stylable_set_style (MX_STYLABLE (box), style);
	clutter_actor_set_reactive (box, TRUE);

	/* XXX rtl? */
	pos = 0;
	button = create_button ("PrevButton", "PrevButtonIcon", "media-skip-backward");
	g_signal_connect_object (button, "clicked", G_CALLBACK (prev_clicked_cb), player, 0);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box), button, pos++);

	button = create_button ("PlayPauseButton", "PlayPauseButtonIcon", "media-playback-start");
	g_signal_connect_object (button, "clicked", G_CALLBACK (playpause_clicked_cb), player, 0);
	g_signal_connect_object (player, "playing-changed", G_CALLBACK (playing_changed_cb), button, 0);
	g_object_get (player, "playing", &playing, NULL);
	playing_changed_cb (player, playing, MX_BUTTON (button));
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box), button, pos++);

	button = create_button ("NextButton", "NextButtonIcon", "media-skip-forward");
	g_signal_connect_object (button, "clicked", G_CALLBACK (next_clicked_cb), player, 0);
	mx_box_layout_add_actor (MX_BOX_LAYOUT (box), button, pos++);

	g_object_unref (player);
	return box;
}

static gboolean
hide_controls_cb (ClutterActor *controls)
{
	rb_debug ("controls pseudo class: %s", mx_stylable_get_style_pseudo_class (MX_STYLABLE (controls)));
	if (clutter_actor_has_pointer (controls) == FALSE) {
		g_object_set_data (G_OBJECT (controls), "hide-controls-id", NULL);

		clutter_actor_hide (controls);

		clutter_stage_hide_cursor (CLUTTER_STAGE (clutter_actor_get_stage (controls)));
	}
	return FALSE;
}

static void
start_hide_timer (ClutterActor *controls)
{
	guint hide_controls_id;

	hide_controls_id = g_timeout_add_seconds (5, (GSourceFunc) hide_controls_cb, controls);
	g_object_set_data (G_OBJECT (controls), "hide-controls-id", GUINT_TO_POINTER (hide_controls_id));
}

static void
stop_hide_timer (ClutterActor *controls)
{
	guint hide_controls_id;

	hide_controls_id = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (controls), "hide-controls-id"));
	if (hide_controls_id != 0) {
		g_source_remove (hide_controls_id);
	}
}

static gboolean
stage_motion_event_cb (ClutterActor *stage, ClutterEvent *event, ClutterActor *controls)
{
	if (g_object_get_data (G_OBJECT (controls), "cursor-in-controls") != NULL) {
		rb_debug ("bleep");
		return FALSE;
	}

	clutter_stage_show_cursor (CLUTTER_STAGE (stage));

	clutter_actor_show (controls);

	stop_hide_timer (controls);
	start_hide_timer (controls);

	return FALSE;
}

static gboolean
controls_enter_event_cb (ClutterActor *controls, ClutterEvent *event, gpointer data)
{
	rb_debug ("bloop");
	stop_hide_timer (controls);
	g_object_set_data (G_OBJECT (controls), "cursor-in-controls", GINT_TO_POINTER (1));
	return FALSE;
}

static gboolean
controls_leave_event_cb (ClutterActor *controls, ClutterEvent *event, gpointer data)
{
	rb_debug ("blip");
	start_hide_timer (controls);
	g_object_set_data (G_OBJECT (controls), "cursor-in-controls", NULL);
	return FALSE;
}

void
rb_visualizer_fullscreen_add_widgets (GtkWidget *window, ClutterActor *stage, RBShell *shell)
{
	ClutterActor *track_info;
	ClutterActor *controls;
	GdkScreen *screen;
	GdkRectangle geom;
	int x;
	int y;
	int monitor;

	clutter_threads_enter ();

	/* get geometry for the monitor we're going to appear on */
	screen = gtk_widget_get_screen (window);
	monitor = gdk_screen_get_monitor_at_window (screen, gtk_widget_get_window (window));
	gdk_screen_get_monitor_geometry (screen, monitor, &geom);

	/* create and place the track info display */
	track_info = create_track_info (shell);

	clutter_container_add_actor (CLUTTER_CONTAINER (stage), track_info);
	g_object_set_data (G_OBJECT (stage), TRACK_INFO_DATA, track_info);

	/* XXX rtl? */
	clutter_actor_set_position (track_info, FULLSCREEN_BORDER_WIDTH, FULLSCREEN_BORDER_WIDTH);

	/* create and place the playback controls */
	controls = create_controls (shell);
	clutter_container_add_actor (CLUTTER_CONTAINER (stage), controls);
	g_object_set_data (G_OBJECT (stage), CONTROLS_DATA, controls);

	/* put this bit somewhere near the bottom */
	/* XXX rtl */
	x = FULLSCREEN_BORDER_WIDTH;
	y = geom.height - (clutter_actor_get_height (controls) + FULLSCREEN_BORDER_WIDTH);
	clutter_actor_set_position (controls, x, y);

	/* hide mouse cursor when not moving, hide playback controls when mouse not moving
	 * and outside them
	 */
	g_signal_connect_object (stage, "motion-event", G_CALLBACK (stage_motion_event_cb), controls, 0);
	g_signal_connect (controls, "leave-event", G_CALLBACK (controls_leave_event_cb), NULL);
	g_signal_connect (controls, "enter-event", G_CALLBACK (controls_enter_event_cb), NULL);
	start_hide_timer (controls);

	clutter_threads_leave ();
}

void
rb_visualizer_fullscreen_remove_widgets (ClutterActor *stage)
{
	ClutterActor *track_info;
	ClutterActor *controls;

	clutter_threads_enter ();

	track_info = CLUTTER_ACTOR (g_object_steal_data (G_OBJECT (stage), TRACK_INFO_DATA));
	if (track_info != NULL) {
		clutter_container_remove_actor (CLUTTER_CONTAINER (stage), track_info);
	}

	controls = CLUTTER_ACTOR (g_object_steal_data (G_OBJECT (stage), CONTROLS_DATA));
	if (controls != NULL) {
		stop_hide_timer (controls);
		clutter_container_remove_actor (CLUTTER_CONTAINER (stage), controls);
	}

	clutter_threads_leave ();
}
