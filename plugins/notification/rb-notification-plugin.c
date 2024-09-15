/*
 * rb-notification-plugin.c
 *
 *  Copyright (C) 2010  Jonathan Matthew  <jonathan@d14n.org>
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
#include <glib.h>
#include <glib-object.h>
#include <pango/pango-bidi-type.h>
#include <libnotify/notify.h>

#include "rb-util.h"
#include "rb-plugin-macros.h"
#include "rb-debug.h"
#include "rb-shell.h"
#include "rb-shell-player.h"
#include "rb-stock-icons.h"
#include "rb-ext-db.h"

#define PLAYING_ENTRY_NOTIFY_TIME	4

#define RB_TYPE_NOTIFICATION_PLUGIN		(rb_notification_plugin_get_type ())
G_DECLARE_FINAL_TYPE (RBNotificationPlugin, rb_notification_plugin, RB, NOTIFICATION_PLUGIN, PeasExtensionBase)

struct _RBNotificationPlugin
{
	PeasExtensionBase parent;

	/* current playing data */
	char *current_title;
	char *current_album_and_artist;	/* from _album_ by _artist_ */

	gchar *notify_art_path;
	RBExtDBKey *notify_art_key;
	gboolean is_flatpak;
	NotifyNotification *notification;
	NotifyNotification *misc_notification;
	gboolean notify_supports_actions;
	gboolean notify_supports_icon_buttons;
	gboolean notify_supports_persistence;

	RBShellPlayer *shell_player;
	RhythmDB *db;
	RBExtDB *art_store;
};

struct _RBNotificationPluginClass
{
	PeasExtensionBaseClass parent_class;
};

G_MODULE_EXPORT void peas_register_types (PeasObjectModule  *module);

RB_DEFINE_PLUGIN(RB_TYPE_NOTIFICATION_PLUGIN, RBNotificationPlugin, rb_notification_plugin,)

static gchar *
markup_escape (const char *text)
{
	return (text == NULL) ? NULL : g_markup_escape_text (text, -1);
}

static void
notification_closed_cb (NotifyNotification *notification,
			RBNotificationPlugin *plugin)
{
	rb_debug ("notification closed");
}

static void
notification_next_cb (NotifyNotification *notification,
		      const char *action,
		      RBNotificationPlugin *plugin)
{
	rb_debug ("notification action: %s", action);
	rb_shell_player_do_next (plugin->shell_player, NULL);
}

static void
notification_playpause_cb (NotifyNotification *notification,
			   const char *action,
			   RBNotificationPlugin *plugin)
{
	rb_debug ("notification action: %s", action);
	rb_shell_player_playpause (plugin->shell_player, NULL);
}

static void
notification_previous_cb (NotifyNotification *notification,
			  const char *action,
			  RBNotificationPlugin *plugin)
{
	rb_debug ("notification action: %s", action);
	rb_shell_player_do_previous (plugin->shell_player, NULL);
}

static void
do_notify (RBNotificationPlugin *plugin,
	   guint timeout,
	   const char *primary,
	   const char *secondary,
	   const char *image_uri,
	   gboolean playback)
{
	GError *error = NULL;
	NotifyNotification *notification;

	if (notify_is_initted () == FALSE) {
		GList *caps;

		if (notify_init ("Rhythmbox") == FALSE) {
			g_warning ("libnotify initialization failed");
			return;
		}

		/* ask the notification server if it supports actions */
		caps = notify_get_server_caps ();
		if (g_list_find_custom (caps, "actions", (GCompareFunc)g_strcmp0) != NULL) {
			rb_debug ("notification server supports actions");
			plugin->notify_supports_actions = TRUE;

			if (g_list_find_custom (caps, "action-icons", (GCompareFunc)g_strcmp0) != NULL) {
				rb_debug ("notifiction server supports icon buttons");
				plugin->notify_supports_icon_buttons = TRUE;
			}
		} else {
			rb_debug ("notification server does not support actions");
		}
		if (g_list_find_custom (caps, "persistence", (GCompareFunc)g_strcmp0) != NULL) {
			rb_debug ("notification server supports persistence");
			plugin->notify_supports_persistence = TRUE;
		} else {
			rb_debug ("notification server does not support persistence");
		}

		rb_list_deep_free (caps);
	}

	if (primary == NULL)
		primary = "";

	if (secondary == NULL)
		secondary = "";

	if (playback) {
		notification = plugin->notification;
	} else {
		notification = plugin->misc_notification;
	}

	if (notification == NULL) {
		notification = notify_notification_new (primary, secondary, RB_APP_ICON);

		g_signal_connect_object (notification,
					 "closed",
					 G_CALLBACK (notification_closed_cb),
					 plugin, 0);
		if (playback) {
			plugin->notification = notification;
		} else {
			plugin->misc_notification = notification;
		}
	} else {
		notify_notification_clear_hints (notification);
		notify_notification_update (notification, primary, secondary, RB_APP_ICON);
	}

	notify_notification_set_timeout (notification, timeout);

	if (image_uri != NULL) {
		notify_notification_clear_hints (notification);
		notify_notification_set_hint (notification,
					      "image_path",
					      g_variant_new_string (image_uri));
	}

        if (!plugin->is_flatpak && playback)
        	notify_notification_set_category (notification, "x-gnome.music");
        notify_notification_set_hint (notification, "desktop-entry",
                                      g_variant_new_string ("org.gnome.Rhythmbox3"));

	notify_notification_clear_actions (notification);
	if (playback && plugin->notify_supports_actions) {
		gboolean rtl;
		const char *play_icon;

		rtl = (gtk_widget_get_default_direction () == GTK_TEXT_DIR_RTL);
		play_icon = rtl ? "media-playback-start-rtl" : "media-playback-start";

		if (plugin->notify_supports_icon_buttons) {
			gboolean playing = FALSE;
			rb_shell_player_get_playing (plugin->shell_player, &playing, NULL);

			notify_notification_add_action (notification,
							rtl ? "media-skip-backward-rtl" : "media-skip-backward",
							_("Previous"),
							(NotifyActionCallback) notification_previous_cb,
							plugin,
							NULL);
			notify_notification_add_action (notification,
							playing ? "media-playback-pause" : play_icon,
							playing ? _("Pause") : _("Play"),
							(NotifyActionCallback) notification_playpause_cb,
							plugin,
							NULL);
			notify_notification_set_hint (notification, "action-icons", g_variant_new_boolean (TRUE));
		}

		notify_notification_add_action (notification,
						rtl ? "media-skip-forward-rtl" : "media-skip-forward",
						_("Next"),
						(NotifyActionCallback) notification_next_cb,
						plugin,
						NULL);
	}

	if (plugin->notify_supports_persistence) {
		const char *hint;

		if (playback) {
			hint = "resident";
		} else {
			hint = "transient";
		}
		notify_notification_set_hint (notification, hint, g_variant_new_boolean (TRUE));
	}

	notify_notification_set_hint (notification, "suppress-sound", g_variant_new_boolean (TRUE));

	if (notify_notification_show (notification, &error) == FALSE) {
		g_warning ("Failed to send notification (%s): %s", primary, error->message);
		g_error_free (error);
	}
}

static void
notify_playing_entry (RBNotificationPlugin *plugin, gboolean requested)
{
	do_notify (plugin,
		   PLAYING_ENTRY_NOTIFY_TIME * 1000,
		   plugin->current_title,
		   plugin->current_album_and_artist,
		   plugin->notify_art_path,
		   TRUE);
}

static void
notify_custom (RBNotificationPlugin *plugin,
	       guint timeout,
	       const char *primary,
	       const char *secondary,
	       const char *image_uri,
	       gboolean requested)
{
	do_notify (plugin, timeout, primary, secondary, image_uri, FALSE);
}

static void
cleanup_notification (RBNotificationPlugin *plugin)
{
	if (plugin->notification != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->notification,
						      G_CALLBACK (notification_closed_cb),
						      plugin);
		notify_notification_close (plugin->notification, NULL);
		plugin->notification = NULL;
	}
	if (plugin->misc_notification != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->misc_notification,
						      G_CALLBACK (notification_closed_cb),
						      plugin);
		notify_notification_close (plugin->misc_notification, NULL);
		plugin->misc_notification = NULL;
	}
}

static void
shell_notify_playing_cb (RBShell *shell, gboolean requested, RBNotificationPlugin *plugin)
{
	notify_playing_entry (plugin, requested);
}

static void
shell_notify_custom_cb (RBShell *shell,
			guint timeout,
			const char *primary,
			const char *secondary,
			const char *image_uri,
			gboolean requested,
			RBNotificationPlugin *plugin)
{
	notify_custom (plugin, timeout, primary, secondary, image_uri, requested);
}

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
art_cb (RBExtDBKey *key, RBExtDBKey *store_key, const char *filename, GValue *data, RBNotificationPlugin *plugin)
{
	RhythmDBEntry *entry;

	entry = rb_shell_player_get_playing_entry (plugin->shell_player);
	if (entry == NULL) {
		return;
	}

	if (rhythmdb_entry_matches_ext_db_key (plugin->db, entry, store_key)) {
		guint elapsed = 0;

		plugin->notify_art_path = g_strdup (filename);

		rb_shell_player_get_playing_time (plugin->shell_player, &elapsed, NULL);
		if (elapsed < PLAYING_ENTRY_NOTIFY_TIME) {
			notify_playing_entry (plugin, FALSE);
		}

		if (plugin->notify_art_key != NULL)
			rb_ext_db_key_free (plugin->notify_art_key);
		plugin->notify_art_key = rb_ext_db_key_copy (store_key);
	}

	rhythmdb_entry_unref (entry);
}

static void
update_current_playing_data (RBNotificationPlugin *plugin, RhythmDBEntry *entry)
{
	GValue *value;
	const char *stream_title = NULL;
	char *artist = NULL;
	char *album = NULL;
	char *title = NULL;
	GString *secondary;
	RBExtDBKey *key;

	const char *artist_template = NULL;
	const char *album_template = NULL;

	g_free (plugin->current_title);
	g_free (plugin->current_album_and_artist);
	plugin->current_title = NULL;
	plugin->current_album_and_artist = NULL;

	if (entry == NULL) {
		plugin->current_title = g_strdup (_("Not Playing"));
		plugin->current_album_and_artist = g_strdup ("");
		g_free (plugin->notify_art_path);
		plugin->notify_art_path = NULL;
		return;
	}

	secondary = g_string_sized_new (100);

	if (plugin->notify_art_key == NULL ||
	    (rhythmdb_entry_matches_ext_db_key (plugin->db, entry, plugin->notify_art_key) == FALSE)) {
		if (plugin->notify_art_key)
			rb_ext_db_key_free (plugin->notify_art_key);
		plugin->notify_art_key = NULL;
		g_free (plugin->notify_art_path);
		plugin->notify_art_path = NULL;

		/* request album art */
		key = rhythmdb_entry_create_ext_db_key (entry, RHYTHMDB_PROP_ALBUM);
		rb_ext_db_request (plugin->art_store,
				   key,
				   (RBExtDBRequestCallback) art_cb,
				   g_object_ref (plugin),
				   g_object_unref);
		rb_ext_db_key_free (key);
	}

	/* get artist, preferring streaming song details */
	value = rhythmdb_entry_request_extra_metadata (plugin->db,
						       entry,
						       RHYTHMDB_PROP_STREAM_SONG_ARTIST);
	if (value != NULL) {
		artist = markup_escape (g_value_get_string (value));
		g_value_unset (value);
		g_free (value);
	} else {
		artist = markup_escape (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ARTIST));
	}

	/* get album, preferring streaming song details */
	value = rhythmdb_entry_request_extra_metadata (plugin->db,
						       entry,
						       RHYTHMDB_PROP_STREAM_SONG_ALBUM);
	if (value != NULL) {
		album = markup_escape (g_value_get_string (value));
		g_value_unset (value);
		g_free (value);
	} else {
		album = markup_escape (rhythmdb_entry_get_string (entry, RHYTHMDB_PROP_ALBUM));
	}

	get_artist_album_templates (artist, album, &artist_template, &album_template);

	if (artist != NULL && artist[0] != '\0') {
		g_string_append_printf (secondary, artist_template, artist);
	}
	g_free (artist);

	if (album != NULL && album[0] != '\0') {
		if (secondary->len != 0)
			g_string_append_c (secondary, ' ');

		g_string_append_printf (secondary, album_template, album);
	}
	g_free (album);

	/* get title and possibly stream name.
	 * if we have a streaming song title, the entry's title
	 * property is the stream name.
	 */
	value = rhythmdb_entry_request_extra_metadata (plugin->db,
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

	if (title == NULL) {
		/* Translators: unknown track title */
		title = g_strdup (_("Unknown"));
	}

	plugin->current_title = title;
	plugin->current_album_and_artist = g_string_free (secondary, FALSE);
}

static void
playing_entry_changed_cb (RBShellPlayer *player,
			  RhythmDBEntry *entry,
			  RBNotificationPlugin *plugin)
{
	if (entry == NULL) {
		cleanup_notification (plugin);
	} else {
		update_current_playing_data (plugin, entry);
		notify_playing_entry (plugin, FALSE);
	}
}

static void
playing_changed_cb (RBShellPlayer *player,
		    gboolean       playing,
		    RBNotificationPlugin *plugin)
{
	/* only notify while not playing if there are actions in an existing notification to update */
	if (playing || (plugin->notify_supports_icon_buttons && plugin->notification != NULL)) {
		notify_playing_entry (plugin, FALSE);
	}
}

static gboolean
is_playing_entry (RBNotificationPlugin *plugin, RhythmDBEntry *entry)
{
	RhythmDBEntry *playing;

	playing = rb_shell_player_get_playing_entry (plugin->shell_player);
	if (playing == NULL) {
		return FALSE;
	}

	rhythmdb_entry_unref (playing);
	return (entry == playing);
}

static void
db_stream_metadata_cb (RhythmDB *db,
		       RhythmDBEntry *entry,
		       const char *field,
		       GValue *metadata,
		       RBNotificationPlugin *plugin)
{
	if (is_playing_entry (plugin, entry) == FALSE)
		return;

	update_current_playing_data (plugin, entry);
}

/* plugin infrastructure */

static void
impl_activate (PeasActivatable *bplugin)
{
	RBNotificationPlugin *plugin;
	RBShell *shell;

	rb_debug ("activating notification plugin");

	plugin = RB_NOTIFICATION_PLUGIN (bplugin);
	g_object_get (plugin, "object", &shell, NULL);
	g_object_get (shell,
		      "shell-player", &plugin->shell_player,
		      "db", &plugin->db,
		      NULL);

	/* connect various things */
	g_signal_connect_object (shell, "notify-playing-entry", G_CALLBACK (shell_notify_playing_cb), plugin, 0);
	g_signal_connect_object (shell, "notify-custom", G_CALLBACK (shell_notify_custom_cb), plugin, 0);

	g_signal_connect_object (plugin->shell_player, "playing-song-changed", G_CALLBACK (playing_entry_changed_cb), plugin, 0);
	g_signal_connect_object (plugin->shell_player, "playing-changed", G_CALLBACK (playing_changed_cb), plugin, 0);

	g_signal_connect_object (plugin->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_TITLE,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);
	g_signal_connect_object (plugin->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_ARTIST,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);
	g_signal_connect_object (plugin->db, "entry_extra_metadata_notify::" RHYTHMDB_PROP_STREAM_SONG_ALBUM,
				 G_CALLBACK (db_stream_metadata_cb), plugin, 0);

	plugin->art_store = rb_ext_db_new ("album-art");

	/* hook into shell preferences so we can poke stuff into the general prefs page? */

	g_object_unref (shell);
}

static void
impl_deactivate	(PeasActivatable *bplugin)
{
	RBNotificationPlugin *plugin;
	RBShell *shell;

	plugin = RB_NOTIFICATION_PLUGIN (bplugin);

	g_object_get (plugin, "object", &shell, NULL);

	cleanup_notification (plugin);

	/* disconnect signal handlers used to update the icon */
	if (plugin->shell_player != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->shell_player, playing_entry_changed_cb, plugin);

		g_object_unref (plugin->shell_player);
		plugin->shell_player = NULL;
	}

	if (plugin->db != NULL) {
		g_signal_handlers_disconnect_by_func (plugin->db, db_stream_metadata_cb, plugin);

		g_object_unref (plugin->db);
		plugin->db = NULL;
	}

	g_signal_handlers_disconnect_by_func (shell, shell_notify_playing_cb, plugin);
	g_signal_handlers_disconnect_by_func (shell, shell_notify_custom_cb, plugin);

	if (plugin->art_store) {
		rb_ext_db_cancel_requests (plugin->art_store, (RBExtDBRequestCallback) art_cb, plugin);
		g_clear_object (&plugin->art_store);
	}

	/* forget what's playing */
	if (plugin->notify_art_key)
		rb_ext_db_key_free (plugin->notify_art_key);
	g_free (plugin->current_title);
	g_free (plugin->current_album_and_artist);
	g_free (plugin->notify_art_path);
	plugin->notify_art_key = NULL;
	plugin->current_title = NULL;
	plugin->current_album_and_artist = NULL;
	plugin->notify_art_path = NULL;

	g_object_unref (shell);
}

static void
rb_notification_plugin_init (RBNotificationPlugin *bplugin)
{
	RBNotificationPlugin *plugin;

	plugin = RB_NOTIFICATION_PLUGIN (bplugin);
	plugin->is_flatpak = g_file_test ("/.flatpak-info", G_FILE_TEST_EXISTS);
}

G_MODULE_EXPORT void
peas_register_types (PeasObjectModule *module)
{
	rb_notification_plugin_register_type (G_TYPE_MODULE (module));
	peas_object_module_register_extension_type (module,
						    PEAS_TYPE_ACTIVATABLE,
						    RB_TYPE_NOTIFICATION_PLUGIN);
}
